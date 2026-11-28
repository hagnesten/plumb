#include "decompiler/emit/loops/loops.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

int32_t decompiler::emitter::loop_step ( int32_t start, const base::list < uint8_t >& members, int32_t condition ) {

	int32_t latch = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

		if ( members [ static_cast < size_t > ( p ) ] )
			latch = latch < 0 ? p : -2;

	}

	if ( latch < 0 || latch == start )
		return -1;

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

	if ( tail.end != term::FALL || tail.taken != start )
		return -1;

	int32_t last = -1;

	for ( int32_t s = tail.first; s < tail.first + tail.count && s < b.statements.count ( ); s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
			last = s;

	}

	if ( last < 0 )
		return -1;

	const statement& line = b.statements [ static_cast < size_t > ( last ) ];

	if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
		return -1;

	const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];
	const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

	step_needs_init = false;

	if ( left.kind != op::VAR || ( right.bits & FLOATING ) )
		return -1;

	bool counted = right.kind == op::ADD && right.a >= 0 && right.b >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST;

	if ( !counted ) {

		if ( condition < 0 || !b.references ( line.right, static_cast < int32_t > ( left.value ), -1 ) )
			return -1;

		step_needs_init = true;

	}

	if ( condition >= 0 && !b.references ( condition, static_cast < int32_t > ( left.value ), -1 ) )
		return -1;

	return last;

}
bool decompiler::emitter::usable_step ( int32_t step ) const {

	if ( step < 0 )
		return false;

	for ( int32_t s = 0; s < fold_at.count ( ) && s < fold_inc.count ( ); s++ ) {

		if ( fold_at [ static_cast < size_t > ( s ) ] >= 0 && fold_inc [ static_cast < size_t > ( s ) ] == step )
			return false;

	}

	for ( int32_t k = 0; k < fold_cond.count ( ) && k < fold_cond_inc.count ( ); k++ ) {

		if ( fold_cond [ static_cast < size_t > ( k ) ] >= 0 && fold_cond_inc [ static_cast < size_t > ( k ) ] == step )
			return false;

	}

	return true;

}
int32_t decompiler::emitter::step_carrier ( int32_t step, int32_t latch ) const {

	if ( !step_carriers || step < 0 || latch < 0 || latch >= count )
		return -1;

	int32_t carrier = -1;

	for ( int32_t s = 0; s < fold_at.count ( ) && s < fold_inc.count ( ); s++ ) {

		if ( fold_at [ static_cast < size_t > ( s ) ] >= 0 && fold_inc [ static_cast < size_t > ( s ) ] == step )
			carrier = s;

	}

	if ( carrier < 0 || b.statements [ static_cast < size_t > ( carrier ) ].kind != stmt::STORE )
		return -1;

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
	int32_t last = -1;

	for ( int32_t s = tail.first; s < tail.first + tail.count && s < b.statements.count ( ); s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP && s != step )
			last = s;

	}

	return last == carrier ? carrier : -1;

}
bool decompiler::emitter::open_for ( uint64_t address, int32_t condition, bool negated, int32_t step, bool need_init, int32_t carrier ) {

	int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( b.statements [ static_cast < size_t > ( step ) ].left ) ].value );
	int32_t init = -1;
	int32_t init_line = -1;

	if ( !find_init ( var, init, init_line ) )
		init = -1;

	if ( need_init && ( dry || init < 0 ) )
		return false;

	name_loop_var ( var );

	if ( dry ) {

		out.indent++;

		return true;

	}

	if ( init >= 0 ) {

		if ( init_line == out.r->lines.count ( ) - 1 ) {

			const line held = out.r->lines.back ( );

			out.r->lines.pop ( );
			out.r->text.resize ( static_cast < size_t > ( held.text ) );
			out.r->spans.resize ( static_cast < size_t > ( held.first_span ) );

		} else
			out.r->lines.remove_at ( static_cast < size_t > ( init_line ) );

		last_assign = -1;
		last_assign_line = -1;
		recent_assign.clear ( );
		recent_line.clear ( );

	}

	hoisted.clear ( );
	hoisted_temp.clear ( );
	out.begin ( address );
	out.put ( L"for", syntax::CONTROL );
	paren_open ( );

	if ( init >= 0 )
		inline_statement ( init );

	out.put ( condition >= 0 ? L"; " : L"; ;" );

	if ( condition >= 0 ) {

		int32_t shown = negated ? b.negate ( condition ) : condition;
		const expr test = b.exprs [ static_cast < size_t > ( shown ) ];
		bool relation = test.kind >= op::EQ && test.kind <= op::GE;

		if ( relation && test.a >= 0 && test.b >= 0 && strip ( test.b ) >= 0 && b.exprs [ static_cast < size_t > ( strip ( test.b ) ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( strip ( test.b ) ) ].value == var && !( strip ( test.a ) >= 0 && b.exprs [ static_cast < size_t > ( strip ( test.a ) ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( strip ( test.a ) ) ].value == var ) ) {

			op turned = test.kind;

			switch ( test.kind ) {

				case op::LT: turned = op::GT; break;
				case op::LE: turned = op::GE; break;
				case op::GT: turned = op::LT; break;
				case op::GE: turned = op::LE; break;
				default: break;

			}

			shown = b.make ( turned, 1, test.b, test.a, 0, test.bits );

		}

		value ( shown, 0 );
		out.put ( L";" );

	}

	out.put ( L" " );
	inline_statement ( carrier >= 0 ? carrier : step );
	paren_close ( );
	out.finish ( );
	brace ( L"{" );
	out.indent++;

	return true;

}
int32_t decompiler::emitter::loop ( int32_t start, const frame& outer ) {

	const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( start ) ];
	int32_t exit = -1;

	for ( int32_t v = 0; v < count && exit < 0; v++ ) {

		if ( !members [ static_cast < size_t > ( v ) ] )
			continue;

		for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

			if ( !members [ static_cast < size_t > ( s ) ] ) {

				exit = s;

				break;

			}

		}

	}

	int32_t pd = ipdom [ static_cast < size_t > ( start ) ];

	if ( pd >= 0 && pd < count && !members [ static_cast < size_t > ( pd ) ] )
		exit = pd;

	if ( forced_exit >= 0 ) {

		exit = forced_exit;
		forced_exit = -1;

	}

	if ( return_exits && exit >= 0 && exit < count && exit_block ( exit ) && small_block ( exit ) ) {

		int32_t other = -1;

		for ( int32_t v = 0; v < count && other != -2; v++ ) {

			if ( !members [ static_cast < size_t > ( v ) ] )
				continue;

			for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

				if ( members [ static_cast < size_t > ( s ) ] || s == exit || exit_block ( s ) )
					continue;

				if ( other == -1 )
					other = s;
				else if ( other != s )
					other = -2;

			}

		}

		if ( other >= 0 )
			exit = other;

	}

	frame inner;

	inner.loop = start;
	inner.exit = exit;
	inner.outer = forced_outer;
	forced_outer = -1;

	const block_code& item = b.code [ static_cast < size_t > ( start ) ];

	if ( item.end == term::BRANCH && ( item.taken == start || item.other == start ) ) {

		bool back_taken = item.taken == start;
		int32_t leaving = back_taken ? item.other : item.taken;
		int32_t test = back_taken ? item.condition : b.negate ( item.condition );
		int32_t step = counted_step ( start, test );
		int32_t carrier = usable_step ( step ) ? -1 : step_carrier ( step, start );

		if ( ( usable_step ( step ) || carrier >= 0 ) && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true, carrier ) ) {

			hidden [ static_cast < size_t > ( step ) ] = 1;

			if ( carrier >= 0 ) {

				hidden [ static_cast < size_t > ( carrier ) ] = 1;
				carried.push ( carrier );

			}

			body ( start );
			close_brace ( );

			return leaving;

		}

		int32_t moved = sinkable_step ( start, test );

		open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

		int32_t open_line = !dry && out.r ? out.r->lines.count ( ) - 1 : -1;

		if ( moved >= 0 )
			hidden [ static_cast < size_t > ( moved ) ] = 1;

		body ( start );

		if ( moved >= 0 ) {

			hidden [ static_cast < size_t > ( moved ) ] = 0;
			statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

		}

		close_do ( open_line );
		while_tail ( item.last, test );

		return leaving;

	}

	int32_t latch = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

		if ( members [ static_cast < size_t > ( p ) ] )
			latch = latch < 0 ? p : -2;

	}

	if ( latch >= 0 && latch != start && b.code [ static_cast < size_t > ( latch ) ].end == term::BRANCH ) {

		const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
		bool back_taken = tail.taken == start;
		int32_t leaving = back_taken ? tail.other : tail.taken;

		if ( leaving == exit ) {

			int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
			int32_t step = counted_step ( latch, test );
			int32_t carrier = usable_step ( step ) ? -1 : step_carrier ( step, latch );

			if ( ( usable_step ( step ) || carrier >= 0 ) && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true, carrier ) ) {

				hidden [ static_cast < size_t > ( step ) ] = 1;

				if ( carrier >= 0 ) {

					hidden [ static_cast < size_t > ( carrier ) ] = 1;
					carried.push ( carrier );

				}

				emitted [ static_cast < size_t > ( latch ) ] = 1;

				if ( empty_block ( latch ) && latch_continues )
					inner.latch = latch;

				region ( start, latch, inner );

				if ( !empty_block ( latch ) )
					body ( latch );

				emitted [ static_cast < size_t > ( latch ) ] = 1;
				close_brace ( );

				return exit;

			}

			bool plain = empty_block ( latch );

			open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

			int32_t open_line = !dry && out.r ? out.r->lines.count ( ) - 1 : -1;

			emitted [ static_cast < size_t > ( latch ) ] = 1;

			if ( plain && latch_continues )
				inner.latch = latch;

			region ( start, latch, inner );

			if ( !plain ) {

				int32_t moved = sinkable_step ( latch, test );

				if ( moved >= 0 )
					hidden [ static_cast < size_t > ( moved ) ] = 1;

				body ( latch );

				if ( moved >= 0 ) {

					hidden [ static_cast < size_t > ( moved ) ] = 0;
					statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

				}

			}

			emitted [ static_cast < size_t > ( latch ) ] = 1;
			close_do ( open_line );
			while_tail ( tail.last, test );

			return exit;

		}

	}

	if ( nested_loops && latch == -2 && nest_depth < 3 ) {

		int32_t outer_latch = -1;

		for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

			if ( members [ static_cast < size_t > ( p ) ] && p != start && ( outer_latch < 0 || b.graph.blocks [ static_cast < size_t > ( p ) ].start > b.graph.blocks [ static_cast < size_t > ( outer_latch ) ].start ) )
				outer_latch = p;

		}

		const block_code& tail = b.code [ static_cast < size_t > ( outer_latch >= 0 ? outer_latch : start ) ];
		bool usable = outer_latch >= 0 && tail.end == term::BRANCH && ( tail.taken == start ) != ( tail.other == start ) && ( tail.taken == start ? tail.other : tail.taken ) == exit;

		base::list < uint8_t > inner_set;

		if ( usable ) {

			inner_set.fill ( static_cast < size_t > ( count ), 0 );

			base::list < int32_t > work;

			inner_set [ static_cast < size_t > ( start ) ] = 1;

			for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

				if ( members [ static_cast < size_t > ( p ) ] && p != outer_latch && !inner_set [ static_cast < size_t > ( p ) ] ) {

					inner_set [ static_cast < size_t > ( p ) ] = 1;
					work.push ( p );

				}

			}

			while ( !work.empty ( ) ) {

				int32_t v = work.back ( );

				work.pop ( );

				for ( int32_t p : pred [ static_cast < size_t > ( v ) ] ) {

					if ( members [ static_cast < size_t > ( p ) ] && !inner_set [ static_cast < size_t > ( p ) ] ) {

						inner_set [ static_cast < size_t > ( p ) ] = 1;
						work.push ( p );

					}

				}

			}

			if ( inner_set [ static_cast < size_t > ( outer_latch ) ] )
				usable = false;

		}

		int32_t resume = -1;

		if ( usable ) {

			for ( int32_t v = 0; v < count && resume != -2; v++ ) {

				if ( !inner_set [ static_cast < size_t > ( v ) ] )
					continue;

				for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

					if ( inner_set [ static_cast < size_t > ( s ) ] )
						continue;

					if ( !members [ static_cast < size_t > ( s ) ] ) {

						if ( !exit_block ( s ) && s != exit )
							resume = -2;

						continue;

					}

					if ( resume == -1 )
						resume = s;
					else if ( resume != s )
						resume = -2;

				}

			}

			usable = resume >= 0;

		}

		if ( usable ) {

			bool back_taken = tail.taken == start;
			int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
			base::list < uint8_t > saved = loop_body [ static_cast < size_t > ( start ) ];

			int32_t step = counted_step ( outer_latch, test );
			bool counted = usable_step ( step ) && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true );

			if ( counted )
				hidden [ static_cast < size_t > ( step ) ] = 1;
			else
				open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

			loop_body [ static_cast < size_t > ( start ) ] = inner_set;
			forced_exit = resume;
			forced_outer = exit;
			nest_depth++;

			int32_t after = loop ( start, inner );

			nest_depth--;
			forced_exit = -1;
			loop_body [ static_cast < size_t > ( start ) ] = saved;
			emitted [ static_cast < size_t > ( outer_latch ) ] = 1;

			if ( after >= 0 && after != outer_latch && !emitted [ static_cast < size_t > ( after ) ] )
				region ( after, outer_latch, inner );

			int32_t moved = counted ? -1 : sinkable_step ( outer_latch, test );

			if ( moved >= 0 )
				hidden [ static_cast < size_t > ( moved ) ] = 1;

			if ( !empty_block ( outer_latch ) )
				body ( outer_latch );

			if ( moved >= 0 ) {

				hidden [ static_cast < size_t > ( moved ) ] = 0;
				statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

			}

			close_brace ( );

			if ( !counted )
				while_tail ( tail.last, test );

			return exit;

		}

	}

	if ( item.end == term::BRANCH && empty_block ( start ) && ( ( item.taken == exit && members [ static_cast < size_t > ( item.other ) ] ) || ( item.other == exit && members [ static_cast < size_t > ( item.taken ) ] ) ) ) {

		bool stay_taken = item.other == exit;
		int32_t step = loop_step ( start, members, item.condition );

		label ( start );
		emitted [ static_cast < size_t > ( start ) ] = 1;

		if ( usable_step ( step ) && open_for ( item.last, item.condition, !stay_taken, step, step_needs_init ) )
			hidden [ static_cast < size_t > ( step ) ] = 1;
		else
			open_brace ( item.last, L"while", item.condition, !stay_taken );

		region ( stay_taken ? item.taken : item.other, start, inner );
		close_brace ( );

		return exit;

	}

	int32_t forever_step = loop_step ( start, members, -1 );

	if ( forever_step >= 0 && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, -1, false, forever_step, true ) )
		hidden [ static_cast < size_t > ( forever_step ) ] = 1;
	else
		open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"while", b.constant ( 1, 1 ), false );

	frame body_frame = inner;

	emitted [ static_cast < size_t > ( start ) ] = 0;
	region_header ( start, body_frame );
	close_brace ( );

	( void ) outer;

	return exit;

}