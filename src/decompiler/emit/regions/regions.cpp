#include "decompiler/emit/regions/regions.h"
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

void decompiler::emitter::body ( int32_t block ) {

	ended = false;
	label ( block );
	emitted [ static_cast < size_t > ( block ) ] = 1;

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];
	int32_t direct = default_returns ? default_return ( block ) : -1;

	for ( int32_t i = 0; i < item.count; i++ ) {

		if ( item.first + i == direct ) {

			block_code shown = b.code [ static_cast < size_t > ( item.taken ) ];

			shown.returned = b.statements [ static_cast < size_t > ( direct ) ].right;

			const expr& loaded = b.exprs [ static_cast < size_t > ( shown.returned ) ];

			shown.last = b.statements [ static_cast < size_t > ( direct ) ].address;

			if ( loaded.kind == op::LOAD && loaded.size < 8 && !( loaded.bits & ( FLOATING | SIGNED ) ) && b.exprs [ static_cast < size_t > ( b.statements [ static_cast < size_t > ( direct ) ].left ) ].size == 8 ) {

				ended = true;

				if ( dry || dead_exit ( ) )
					continue;

				expr copy = loaded;

				out.begin ( shown.last );
				out.put ( L"return", syntax::CONTROL );
				out.put ( L" " );
				extended_load ( copy, 0, UNSIGNED );
				out.put ( L";" );
				out.finish ( );
				out.terminal = out.indent;

				continue;

			}

			return_line ( shown );

			continue;

		}

		statement_line ( b.statements [ static_cast < size_t > ( item.first + i ) ] );

	}

}
int32_t decompiler::emitter::default_return ( int32_t block ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	if ( !b.returns_value || item.end != term::FALL || item.taken < 0 || item.taken >= count || block >= pred.count ( ) )
		return -1;

	const block_code& target = b.code [ static_cast < size_t > ( item.taken ) ];

	if ( target.end != term::RETURN || target.returned < 0 || b.exprs [ static_cast < size_t > ( target.returned ) ].kind != op::VAR || !empty_block ( item.taken ) || pred [ static_cast < size_t > ( item.taken ) ].count ( ) < 2 )
		return -1;

	int64_t v = b.exprs [ static_cast < size_t > ( target.returned ) ].value;
	int32_t only = -1;

	for ( int32_t s = item.first; s < item.first + item.count; s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind == stmt::NOP )
			continue;

		if ( only >= 0 )
			return -1;

		only = s;

	}

	if ( only < 0 )
		return -1;

	const statement& line = b.statements [ static_cast < size_t > ( only ) ];

	if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( line.left ) ].value != v )
		return -1;

	op source = b.exprs [ static_cast < size_t > ( line.right ) ].kind;

	if ( source == op::CONST || source == op::VAR || ( source >= op::EQ && source <= op::GE ) || source == op::LAND || source == op::LOR || source == op::LNOT || b.result_width )
		return -1;

	if ( pred [ static_cast < size_t > ( block ) ].count ( ) != 1 || b.code [ static_cast < size_t > ( pred [ static_cast < size_t > ( block ) ] [ 0 ] ) ].end != term::BRANCH )
		return -1;

	int32_t constants = 0;

	for ( int32_t s = 0; s < b.statements.count ( ); s++ ) {

		const statement& prior = b.statements [ static_cast < size_t > ( s ) ];

		if ( s == only || prior.kind != stmt::ASSIGN || prior.left < 0 || b.exprs [ static_cast < size_t > ( prior.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( prior.left ) ].value != v )
			continue;

		if ( prior.right < 0 || b.exprs [ static_cast < size_t > ( prior.right ) ].kind != op::CONST )
			return -1;

		constants++;

	}

	return constants == 1 ? only : -1;

}
bool decompiler::emitter::empty_block ( int32_t block ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	for ( int32_t i = 0; i < item.count; i++ ) {

		if ( b.statements [ static_cast < size_t > ( item.first + i ) ].kind != stmt::NOP )
			return false;

	}

	return true;

}
void decompiler::emitter::open_brace ( uint64_t address, const wchar_t* keyword, int32_t condition, bool negated ) {

	if ( dry ) {

		out.indent++;

		return;

	}

	int32_t shown = condition >= 0 && negated ? b.negate ( condition ) : condition;

	untruncate = false;
	hoisted.clear ( );
	hoisted_temp.clear ( );

	if ( no_ternary && shown >= 0 && keyword [ 0 ] == L'i' && keyword [ 1 ] == L'f' && !keyword [ 2 ] )
		hoist ( shown, address );

	out.begin ( address );
	out.put ( keyword, syntax::CONTROL );

	if ( condition >= 0 ) {

		const expr& test = b.exprs [ static_cast < size_t > ( condition ) ];

		paren_open ( );

		if ( test.kind == op::CONST && !negated )
			out.put ( test.value ? L"1" : L"0", syntax::NUMBER );
		else {

			active_var = -1;

			if ( keyword [ 0 ] == L'i' || keyword [ 0 ] == L'e' ) {

				for ( int32_t k = 0; k < fold_cond.count ( ); k++ ) {

					if ( fold_cond [ static_cast < size_t > ( k ) ] == condition ) {

						active_var = fold_cond_var [ static_cast < size_t > ( k ) ];
						active_how = fold_cond_how [ static_cast < size_t > ( k ) ];
						active_slot = k;
						active_cond = true;
						active_unit = fold_cond_unit [ static_cast < size_t > ( k ) ];

					}

				}

			}

			truth = bit_tests;
			value ( shown, 0 );
			active_var = -1;

		}

		paren_close ( );

	}

	out.finish ( );
	brace ( L"{" );
	out.indent++;
	hoisted.clear ( );
	hoisted_temp.clear ( );

}
void decompiler::emitter::close_brace ( ) {

	ended = false;
	out.indent--;

	if ( dry )
		return;

	brace ( L"}" );

}
void decompiler::emitter::brace ( const wchar_t* text ) {

	if ( dry )
		return;

	out.begin ( 0 );
	out.put ( text );
	out.finish ( );

}
void decompiler::emitter::paren_open ( ) {

	bool keep = out.spaced;

	out.spaced = false;
	out.put ( L" ( " );
	out.spaced = keep;

}
void decompiler::emitter::paren_close ( ) {

	bool keep = out.spaced;

	out.spaced = false;
	out.put ( L" )" );
	out.spaced = keep;

}
void decompiler::emitter::else_line ( ) {

	if ( dry )
		return;

	out.begin ( 0 );
	out.put ( L"else", syntax::CONTROL );
	out.finish ( );

}
void decompiler::emitter::while_tail ( uint64_t address, int32_t condition ) {

	if ( dry )
		return;

	out.begin ( address );
	out.put ( L"while", syntax::CONTROL );
	paren_open ( );
	untruncate = false;
	truth = bit_tests;
	value ( condition, 0 );
	paren_close ( );
	out.put ( L";" );
	out.finish ( );

}
bool decompiler::emitter::dead_exit ( ) const {

	return out.terminal >= 0 && out.terminal == out.indent;

}
bool decompiler::emitter::in_loop ( int32_t loop, int32_t block ) {

	return loop >= 0 && loop_body [ static_cast < size_t > ( loop ) ] [ static_cast < size_t > ( block ) ];

}
bool decompiler::emitter::special ( int32_t target, int32_t stop, const frame& context, uint64_t address ) {

	if ( target == context.loop && context.loop >= 0 ) {

		if ( stop != target )
			jump_line ( L"continue", address );

		return true;

	}

	if ( target == context.exit && context.exit >= 0 ) {

		if ( stop != target )
			jump_line ( L"break", address );

		return true;

	}

	if ( target == context.latch && context.latch >= 0 ) {

		if ( stop != target )
			jump_line ( L"continue", address );

		return true;

	}

	if ( target == context.outer && context.outer >= 0 ) {

		if ( stop != target )
			go_to ( target, address );

		return true;

	}

	return false;

}
void decompiler::emitter::region ( int32_t block, int32_t stop, frame context ) {

	if ( ++depth > 400 ) {

		depth--;

		return;

	}

	while ( block >= 0 && block != stop ) {

		if ( emitted [ static_cast < size_t > ( block ) ] ) {

			if ( block == context.latch && context.latch >= 0 )
				jump_line ( L"continue", 0 );
			else
				go_to ( block, 0 );

			break;

		}

		if ( header [ static_cast < size_t > ( block ) ] && context.loop != block ) {

			block = loop ( block, context );

			continue;

		}

		body ( block );

		const block_code& item = b.code [ static_cast < size_t > ( block ) ];

		switch ( item.end ) {

			case term::RETURN:
			case term::TAIL:
				return_line ( item );
				block = -1;
				break;

			case term::STOP:
				block = -1;
				break;

			case term::FALL:

				if ( special ( item.taken, stop, context, item.last ) )
					block = -1;
				else if ( fall_returns && item.taken >= 0 && item.taken != stop && pred [ static_cast < size_t > ( item.taken ) ].count ( ) > 1 && b.code [ static_cast < size_t > ( item.taken ) ].end != term::RETURN && inline_return ( item.taken, item.last ) )
					block = -1;
				else
					block = item.taken;

				break;

			case term::BRANCH:
				block = branch ( block, stop, context, false );
				break;

			case term::SWITCH:
				block = choice ( block, stop, context );
				break;

			default:
				block = -1;
				break;

		}

	}

	depth--;

}
int32_t decompiler::emitter::follow_of ( int32_t block, int32_t stop, const frame& context ) {

	int32_t follow = ipdom [ static_cast < size_t > ( block ) ];

	if ( follow == count )
		follow = -1;

	if ( follow >= 0 && follow < cut.count ( ) && cut [ static_cast < size_t > ( follow ) ] && block < ipdom_cut.count ( ) ) {

		int32_t alt = ipdom_cut [ static_cast < size_t > ( block ) ];
		bool direct = false;

		for ( int32_t s : succ [ static_cast < size_t > ( block ) ] )
			direct = direct || cut [ static_cast < size_t > ( s ) ] || s == alt;

		if ( !direct && alt >= 0 && alt < count && alt != block && !emitted [ static_cast < size_t > ( alt ) ] )
			follow = alt;
		else if ( direct && stop >= 0 && stop < count && stop != block && !emitted [ static_cast < size_t > ( stop ) ] && cut_stops )
			follow = stop;

	}

	if ( loop_follows && context.loop >= 0 && ( follow < 0 || !in_loop ( context.loop, follow ) ) ) {

		int32_t local = loop_follow ( block, context.loop );

		if ( local >= 0 && !emitted [ static_cast < size_t > ( local ) ] )
			return local;

	}

	if ( context.loop >= 0 && follow >= 0 && !in_loop ( context.loop, follow ) )
		follow = exits_inline && stop == context.loop ? context.loop : context.exit;

	if ( follow < 0 )
		follow = stop;

	return follow;

}
int32_t decompiler::emitter::loop_follow ( int32_t block, int32_t loop ) {

	if ( block < 0 || block >= count || block >= succ.count ( ) )
		return -1;

	base::list < int32_t > hits;
	int32_t branches = 0;

	hits.fill ( static_cast < size_t > ( count ), 0 );

	for ( int32_t start : succ [ static_cast < size_t > ( block ) ] ) {

		if ( start < 0 || start >= count || start == loop || !in_loop ( loop, start ) )
			continue;

		base::list < uint8_t > seen;
		base::list < int32_t > work;

		seen.fill ( static_cast < size_t > ( count ), 0 );
		seen [ static_cast < size_t > ( start ) ] = 1;
		work.push ( start );

		for ( int32_t q = 0; q < work.count ( ); q++ ) {

			int32_t at = work [ static_cast < size_t > ( q ) ];

			hits [ static_cast < size_t > ( at ) ]++;

			for ( int32_t next : succ [ static_cast < size_t > ( at ) ] ) {

				if ( next < 0 || next >= count || next == loop || next == block || seen [ static_cast < size_t > ( next ) ] || !in_loop ( loop, next ) )
					continue;

				seen [ static_cast < size_t > ( next ) ] = 1;
				work.push ( next );

			}

		}

		branches++;

	}

	if ( branches < 2 )
		return -1;

	int32_t best = -1;

	for ( int32_t k = 0; k < count; k++ ) {

		if ( k == block || hits [ static_cast < size_t > ( k ) ] != branches )
			continue;

		if ( best < 0 || b.graph.blocks [ static_cast < size_t > ( k ) ].start < b.graph.blocks [ static_cast < size_t > ( best ) ].start )
			best = k;

	}

	return best;

}
bool decompiler::emitter::silent_return ( int32_t block ) {

	if ( block < 0 || block >= count )
		return false;

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	return item.end == term::RETURN && !b.returns_value && empty_block ( block ) && pred [ static_cast < size_t > ( block ) ].count ( ) == 1 && !emitted [ static_cast < size_t > ( block ) ];

}
void decompiler::emitter::region_header ( int32_t start, frame context ) {

	body ( start );

	const block_code& item = b.code [ static_cast < size_t > ( start ) ];
	int32_t next = -1;

	switch ( item.end ) {

		case term::RETURN:
		case term::TAIL:
			return_line ( item );
			break;

		case term::FALL:

			if ( !special ( item.taken, start, context, item.last ) )
				next = item.taken;

			break;

		case term::BRANCH:
			next = branch ( start, start, context, false );
			break;

		case term::SWITCH:
			next = choice ( start, start, context );
			break;

		default:
			break;

	}

	if ( next >= 0 && next != start )
		region ( next, start, context );

}
int32_t decompiler::emitter::choice ( int32_t block, int32_t stop, const frame& context ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];
	const analysis::block& node = b.graph.blocks [ static_cast < size_t > ( block ) ];
	int32_t follow = follow_of ( block, stop, context );

	if ( !dry ) {

		out.begin ( item.last );
		out.put ( L"switch", syntax::CONTROL );
		paren_open ( );
		value ( item.selector, 0 );
		paren_close ( );
		out.finish ( );

	}

	brace ( L"{" );
	out.indent++;

	base::list < int32_t > targets;

	for ( int32_t e = 0; e < node.out_count; e++ ) {

		const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];
		int32_t to = link.to;
		bool seen = false;

		if ( link.kind == analysis::edge_kind::JUMP && to == follow )
			continue;

		for ( int32_t t : targets ) {

			if ( t == to )
				seen = true;

		}

		if ( !seen )
			targets.push ( to );

	}

	frame inner = context;

	inner.exit = follow;

	bool selector_unsigned = false;
	bool selector_char = false;

	if ( case_styles && item.selector >= 0 ) {

		const expr& picked = b.exprs [ static_cast < size_t > ( item.selector ) ];

		if ( picked.kind == op::VAR && picked.value >= 0 && picked.value < b.vars.count ( ) ) {

			const variable& held = b.vars [ static_cast < size_t > ( picked.value ) ];

			selector_unsigned = ( held.bits & UNSIGNED ) != 0;
			selector_char = picked.size == 1;

			if ( !selector_char ) {

				int32_t widened = 0;
				int32_t others = 0;

				for ( const statement& line : b.statements ) {

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( line.left ) ].value != picked.value )
						continue;

					const expr& source = b.exprs [ static_cast < size_t > ( line.right ) ];
					bool byte_source = source.size == 1 || ( source.kind == op::CAST && source.a >= 0 && b.exprs [ static_cast < size_t > ( source.a ) ].size == 1 );

					if ( byte_source )
						widened++;
					else
						others++;

				}

				selector_char = widened > 0 && !others;

			}

		} else if ( picked.kind == op::LOAD && !( picked.bits & ( FLOATING | SIGNED ) ) ) {

			selector_unsigned = true;
			selector_char = picked.size == 1;

		}
		else if ( picked.kind == op::CAST ) {

			selector_unsigned = ( picked.bits & UNSIGNED ) != 0;
			selector_char = picked.a >= 0 && b.exprs [ static_cast < size_t > ( picked.a ) ].size == 1;

		}

	}

	int32_t default_to = -1;

	for ( int32_t e = 0; e < node.out_count; e++ ) {

		const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

		if ( link.kind == analysis::edge_kind::JUMP )
			default_to = link.to;

	}

	for ( int32_t target : targets ) {

		if ( default_cases && target == default_to ) {

			bool labeled = false;

			for ( int32_t e = 0; e < node.out_count; e++ ) {

				const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

				if ( link.to == target && link.kind == analysis::edge_kind::JUMP )
					labeled = true;

			}

			if ( !labeled || target == follow )
				continue;

		}

		if ( !dry ) {

			for ( int32_t e = 0; e < node.out_count; e++ ) {

				const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

				if ( link.to != target )
					continue;

				if ( default_cases && link.kind != analysis::edge_kind::JUMP && target == default_to )
					continue;

				out.begin ( b.graph.blocks [ static_cast < size_t > ( target ) ].start );

				if ( link.kind == analysis::edge_kind::JUMP )
					out.put ( L"default", syntax::CONTROL );
				else {

					out.put ( L"case", syntax::CONTROL );
					out.put ( L" " );

					if ( selector_char && link.value >= 0x20 && link.value < 0x7F ) {

						wchar_t quoted [ 5 ] = { L'\'', static_cast < wchar_t > ( link.value ), L'\'', 0, 0 };

						if ( link.value == L'\'' || link.value == L'\\' ) {

							quoted [ 1 ] = L'\\';
							quoted [ 2 ] = static_cast < wchar_t > ( link.value );
							quoted [ 3 ] = L'\'';

						}

						out.put ( quoted, syntax::NUMBER );

					} else if ( selector_unsigned )
						number ( link.value, 4, link.value >= 10, true );
					else
						number ( link.value, 4, false, false );

				}

				out.put ( L":" );
				out.finish ( );

			}

		}

		out.indent++;

		if ( target != follow && !emitted [ static_cast < size_t > ( target ) ] )
			region ( target, follow, inner );
		else if ( target != follow )
			go_to ( target, 0 );

		if ( out.terminal < 0 )
			jump_line ( L"break", 0 );

		out.indent--;

	}

	close_brace ( );

	return follow;

}