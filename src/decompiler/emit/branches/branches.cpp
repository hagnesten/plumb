#include "decompiler/emit/branches/branches.h"
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

int32_t decompiler::emitter::sinkable_step ( int32_t latch, int32_t test ) {

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
	int32_t last = -1;

	for ( int32_t s = tail.first; s < tail.first + tail.count; s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
			last = s;

	}

	for ( int32_t s = tail.first; s < tail.first + tail.count; s++ ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || s == last )
			continue;

		const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];
		int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value );

		if ( !( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == var && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST ) )
			continue;

		if ( !b.references ( test, var, -1 ) )
			continue;

		bool used = false;
		bool only_steps = true;

		for ( int32_t k = s + 1; k < tail.first + tail.count && !used; k++ ) {

			const statement& later = b.statements [ static_cast < size_t > ( k ) ];

			if ( later.kind == stmt::NOP || later.kind == stmt::ASM ) {

				used = used || later.kind == stmt::ASM;

				continue;

			}

			used = b.references ( later.right, var, -1 ) || ( later.left >= 0 && b.references ( later.left, var, -1 ) );

			bool step = later.kind == stmt::ASSIGN && later.left >= 0 && later.right >= 0 && b.exprs [ static_cast < size_t > ( later.left ) ].kind == op::VAR;

			if ( step ) {

				const expr& next = b.exprs [ static_cast < size_t > ( later.right ) ];

				step = ( next.kind == op::ADD || next.kind == op::SUB ) && next.a >= 0 && b.exprs [ static_cast < size_t > ( next.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( next.a ) ].value == b.exprs [ static_cast < size_t > ( later.left ) ].value && next.b >= 0 && b.exprs [ static_cast < size_t > ( next.b ) ].kind == op::CONST;

			}

			only_steps = only_steps && step;

		}

		if ( keep_step_order && only_steps )
			continue;

		if ( !used )
			return s;

	}

	return -1;

}
int32_t decompiler::emitter::single_assign ( int32_t block, int32_t follow ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	if ( item.end != term::FALL || item.taken != follow || pred [ static_cast < size_t > ( block ) ].count ( ) != 1 || wanted_label [ static_cast < size_t > ( block ) ] || header [ static_cast < size_t > ( block ) ] || emitted [ static_cast < size_t > ( block ) ] )
		return -1;

	int32_t found = -1;

	for ( int32_t s = item.first; s < item.first + item.count; s++ ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP )
			continue;

		if ( found >= 0 || line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || ( s < hidden.count ( ) && hidden [ static_cast < size_t > ( s ) ] ) )
			return -1;

		found = s;

	}

	return found;

}
int32_t decompiler::emitter::counted_step ( int32_t latch, int32_t test ) {

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

	for ( int32_t s = tail.first + tail.count - 1; s >= tail.first; s-- ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP )
			continue;

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
			return -1;

		const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];
		int64_t var = b.exprs [ static_cast < size_t > ( line.left ) ].value;

		if ( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == var && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST && b.references ( test, static_cast < int32_t > ( var ), -1 ) )
			return s;

		return -1;

	}

	return -1;

}
bool decompiler::emitter::guarded_entry ( int32_t start, int64_t init, op kind, int32_t bound ) {

	if ( !guarded_loops || start < 0 )
		return false;

	int32_t pre = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

		if ( loop_body [ static_cast < size_t > ( start ) ] [ static_cast < size_t > ( p ) ] )
			continue;

		pre = pre < 0 ? p : -2;

	}

	if ( pre < 0 )
		return false;

	int32_t gate = pre;
	int32_t into = start;

	if ( b.code [ static_cast < size_t > ( pre ) ].end != term::BRANCH ) {

		if ( pred [ static_cast < size_t > ( pre ) ].count ( ) != 1 )
			return false;

		gate = pred [ static_cast < size_t > ( pre ) ] [ 0 ];
		into = pre;

	}

	const block_code& g = b.code [ static_cast < size_t > ( gate ) ];

	if ( g.end != term::BRANCH || g.condition < 0 || ( g.taken != into && g.other != into ) || g.taken == g.other )
		return false;

	int32_t condition = g.taken == into ? g.condition : b.negate ( g.condition );
	const expr& c = b.exprs [ static_cast < size_t > ( condition ) ];

	if ( c.a < 0 || c.b < 0 )
		return false;

	int32_t target = strip ( bound );

	if ( target < 0 || b.exprs [ static_cast < size_t > ( target ) ].kind != op::VAR )
		return false;

	for ( int32_t s = b.code [ static_cast < size_t > ( pre ) ].first; s < b.code [ static_cast < size_t > ( pre ) ].first + b.code [ static_cast < size_t > ( pre ) ].count && pre != gate; s++ ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && target >= 0 && b.exprs [ static_cast < size_t > ( target ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == b.exprs [ static_cast < size_t > ( target ) ].value && b.exprs [ static_cast < size_t > ( strip ( line.right ) ) ].kind == op::VAR )
			target = strip ( line.right );

	}

	int32_t left = strip ( c.a );
	int32_t right = strip ( c.b );
	op relation = c.kind;
	int64_t k = 0;

	if ( right >= 0 && b.exprs [ static_cast < size_t > ( right ) ].kind == op::CONST && left >= 0 && ( b.same ( left, target ) || b.same ( left, strip ( bound ) ) ) )
		k = b.exprs [ static_cast < size_t > ( right ) ].value;
	else if ( left >= 0 && b.exprs [ static_cast < size_t > ( left ) ].kind == op::CONST && right >= 0 && ( b.same ( right, target ) || b.same ( right, strip ( bound ) ) ) ) {

		k = b.exprs [ static_cast < size_t > ( left ) ].value;

		switch ( relation ) {

			case op::LT: relation = op::GT; break;
			case op::LE: relation = op::GE; break;
			case op::GT: relation = op::LT; break;
			case op::GE: relation = op::LE; break;
			default: break;

		}

	} else
		return false;

	bool above = ( relation == op::GT && k >= init ) || ( relation == op::GE && k > init );

	switch ( kind ) {

		case op::NE:
			return above || ( relation == op::NE && k == init ) || ( relation == op::LT && k <= init );

		case op::LT:
			return above;

		case op::LE:
			return above || ( relation == op::GE && k >= init );

		default:
			return false;

	}

}
bool decompiler::emitter::entry_true ( int32_t test, int32_t step, int32_t start ) {

	const statement& grow = b.statements [ static_cast < size_t > ( step ) ];
	int32_t found = -1;
	int32_t line = -1;

	if ( !find_init ( b.exprs [ static_cast < size_t > ( grow.left ) ].value, found, line ) )
		return false;

	const statement& seed = b.statements [ static_cast < size_t > ( found ) ];

	if ( seed.left < 0 || seed.right < 0 || b.exprs [ static_cast < size_t > ( seed.left ) ].value != b.exprs [ static_cast < size_t > ( grow.left ) ].value || b.exprs [ static_cast < size_t > ( seed.right ) ].kind != op::CONST )
		return false;

	const expr& t = b.exprs [ static_cast < size_t > ( test ) ];

	if ( t.a < 0 || t.b < 0 )
		return false;

	int32_t subject = strip ( t.a );
	int32_t other_side = t.b;
	op test_kind = t.kind;

	if ( b.exprs [ static_cast < size_t > ( subject ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( subject ) ].value != b.exprs [ static_cast < size_t > ( seed.left ) ].value ) {

		int32_t flipped = strip ( t.b );

		if ( flipped < 0 || b.exprs [ static_cast < size_t > ( flipped ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( flipped ) ].value != b.exprs [ static_cast < size_t > ( seed.left ) ].value )
			return false;

		subject = flipped;
		other_side = t.a;

		switch ( test_kind ) {

			case op::LT: test_kind = op::GT; break;
			case op::LE: test_kind = op::GE; break;
			case op::GT: test_kind = op::LT; break;
			case op::GE: test_kind = op::LE; break;
			default: break;

		}

	}

	if ( b.exprs [ static_cast < size_t > ( other_side ) ].kind != op::CONST )
		return guarded_entry ( start, b.exprs [ static_cast < size_t > ( seed.right ) ].value, test_kind, other_side );

	if ( other_side != t.b )
		return false;

	int64_t x = b.exprs [ static_cast < size_t > ( seed.right ) ].value;
	int64_t y = b.exprs [ static_cast < size_t > ( t.b ) ].value;
	bool is_unsigned = ( t.bits & UNSIGNED ) != 0;
	uint64_t ux = static_cast < uint64_t > ( x );
	uint64_t uy = static_cast < uint64_t > ( y );

	switch ( t.kind ) {

		case op::EQ: return x == y;
		case op::NE: return x != y;
		case op::LT: return is_unsigned ? ux < uy : x < y;
		case op::LE: return is_unsigned ? ux <= uy : x <= y;
		case op::GT: return is_unsigned ? ux > uy : x > y;
		case op::GE: return is_unsigned ? ux >= uy : x >= y;
		default: return false;

	}

}
int32_t decompiler::emitter::rotate ( int32_t block, int32_t entry, int32_t leave, int32_t guard_test, const frame& context ) {

	int32_t pre = -1;
	int32_t head = entry;

	if ( !header [ static_cast < size_t > ( head ) ] ) {

		const block_code& passage = b.code [ static_cast < size_t > ( entry ) ];

		if ( passage.end != term::FALL || pred [ static_cast < size_t > ( entry ) ].count ( ) != 1 || passage.taken < 0 || !header [ static_cast < size_t > ( passage.taken ) ] )
			return -2;

		pre = entry;
		head = passage.taken;

	}

	if ( emitted [ static_cast < size_t > ( head ) ] )
		return -2;

	const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( head ) ];
	int32_t latch = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( head ) ] ) {

		if ( !members.empty ( ) && members [ static_cast < size_t > ( p ) ] )
			latch = latch < 0 ? p : -2;
		else if ( p != ( pre >= 0 ? pre : block ) )
			return -2;

	}

	if ( latch < 0 )
		return -2;

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

	if ( tail.end != term::BRANCH || tail.condition < 0 )
		return -2;

	bool back_taken = tail.taken == head;

	if ( ( back_taken ? tail.other : tail.taken ) != leave || ( back_taken ? tail.taken : tail.other ) != head )
		return -2;

	int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
	bool matched = equivalent ( guard_test, test, -1, -1, 0 );
	int32_t seed_statement = -1;

	if ( strict_rotation ) {

		if ( !matched || ( pre >= 0 && !empty_block ( pre ) ) )
			return -2;

		const block_code& guard = b.code [ static_cast < size_t > ( block ) ];

		for ( int32_t s = guard.first; s < guard.first + guard.count; s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.references ( guard_test, static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ), -1 ) && b.exprs [ static_cast < size_t > ( strip ( line.right ) ) ].kind != op::LOAD && b.exprs [ static_cast < size_t > ( strip ( line.right ) ) ].kind != op::CALL )
				return -2;

		}

	}

	const int32_t sources [ 2 ] = { pre, block };

	for ( int32_t source : sources ) {

		if ( matched || source < 0 )
			continue;

		const block_code& owner = b.code [ static_cast < size_t > ( source ) ];

		for ( int32_t s = owner.first + owner.count - 1; s >= owner.first && !matched; s-- ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value );

			if ( equivalent ( guard_test, test, var, line.right, 0 ) ) {

				matched = true;
				seed_statement = s;

			}

		}

	}

	if ( !matched )
		return -2;

	( void ) seed_statement;

	if ( pre >= 0 )
		body ( pre );

	int32_t step = -1;

	for ( int32_t s = tail.first + tail.count - 1; s >= tail.first; s-- ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP )
			continue;

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

			const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];

			if ( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == b.exprs [ static_cast < size_t > ( line.left ) ].value && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST && b.references ( test, static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ), -1 ) )
				step = s;
			else if ( strict_rotation && b.references ( test, static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ), -1 ) && grow.kind != op::CALL )
				step = s;

		}

		break;

	}

	frame inner;

	inner.loop = head;
	inner.exit = leave;

	label ( head );

	if ( step >= 0 && open_for ( tail.last, test, false, step, false ) )
		hidden [ static_cast < size_t > ( step ) ] = 1;
	else
		open_brace ( tail.last, L"while", test, false );

	emitted [ static_cast < size_t > ( latch ) ] = 1;

	if ( latch != head ) {

		region ( head, latch, inner );

		if ( !empty_block ( latch ) )
			body ( latch );

	} else
		body ( head );

	emitted [ static_cast < size_t > ( latch ) ] = 1;
	close_brace ( );
	ended = false;

	( void ) context;

	return leave;

}
bool decompiler::emitter::deferred ( int32_t target, int32_t from ) {

	if ( target < 0 || target >= count || emitted [ static_cast < size_t > ( target ) ] || pred [ static_cast < size_t > ( target ) ].count ( ) < 2 || header [ static_cast < size_t > ( target ) ] )
		return false;

	uint64_t start = b.graph.blocks [ static_cast < size_t > ( target ) ].start;

	for ( int32_t p : pred [ static_cast < size_t > ( target ) ] ) {

		if ( p != from && !emitted [ static_cast < size_t > ( p ) ] && ( b.graph.blocks [ static_cast < size_t > ( p ) ].end == start || ( defer_later && b.graph.blocks [ static_cast < size_t > ( p ) ].start < start ) ) && b.code [ static_cast < size_t > ( p ) ].reachable && !dominates ( target, p ) )
			return true;

	}

	return false;

}
bool decompiler::emitter::branch_fed ( int32_t block ) const {

	for ( int32_t p : pred [ static_cast < size_t > ( block ) ] ) {

		if ( b.code [ static_cast < size_t > ( p ) ].end == term::BRANCH )
			return true;

	}

	return false;

}
bool decompiler::emitter::negative_test ( int32_t condition ) const {

	const expr& test = b.exprs [ static_cast < size_t > ( condition ) ];

	if ( test.kind == op::LNOT )
		return true;

	if ( ( test.kind != op::EQ && test.kind != op::NE ) || test.b < 0 || ( test.bits & FLOATING ) )
		return false;

	const expr& right = b.exprs [ static_cast < size_t > ( test.b ) ];
	bool zero = right.kind == op::CONST && right.value == 0;

	if ( !fall_then_signs )
		return test.kind == op::EQ;

	if ( unequal_falls && test.kind == op::NE && right.kind != op::CONST )
		return true;

	if ( equal_falls && test.kind == op::EQ && right.kind != op::CONST )
		return true;

	return test.kind == op::EQ ? zero : ( right.kind == op::CONST && !zero );

}
int32_t decompiler::emitter::region_size ( int32_t start, int32_t stop ) {

	base::list < uint8_t > seen;
	base::list < int32_t > work;

	seen.fill ( static_cast < size_t > ( count ), 0 );
	work.push ( start );
	seen [ static_cast < size_t > ( start ) ] = 1;

	int32_t total = 0;

	while ( work.count ( ) ) {

		int32_t at = work [ static_cast < size_t > ( work.count ( ) - 1 ) ];

		work.pop ( );
		total++;

		const block_code& item = b.code [ static_cast < size_t > ( at ) ];
		int32_t nexts [ 2 ] = { item.taken, item.end == term::BRANCH ? item.other : -1 };

		for ( int32_t next : nexts ) {

			if ( next < 0 || next >= count || next == stop || seen [ static_cast < size_t > ( next ) ] )
				continue;

			seen [ static_cast < size_t > ( next ) ] = 1;
			work.push ( next );

		}

		if ( item.end == term::SWITCH && at < succ.count ( ) ) {

			for ( int32_t next : succ [ static_cast < size_t > ( at ) ] ) {

				if ( next < 0 || next >= count || next == stop || seen [ static_cast < size_t > ( next ) ] )
					continue;

				seen [ static_cast < size_t > ( next ) ] = 1;
				work.push ( next );

			}

		}

	}

	return total;

}
int32_t decompiler::emitter::branch ( int32_t block, int32_t stop, const frame& context, bool chained ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];
	int32_t taken = item.taken;
	int32_t other = item.other;
	int32_t follow = follow_of ( block, stop, context );
	bool taken_jump = taken == context.loop || taken == context.exit;
	bool other_jump = other == context.loop || other == context.exit;

	if ( shared_returns && follow < 0 && context.loop < 0 && taken >= 0 && other >= 0 && taken < count && other < count ) {

		int32_t last = -1;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( b.code [ static_cast < size_t > ( i ) ].reachable && ( last < 0 || b.graph.blocks [ static_cast < size_t > ( i ) ].start > b.graph.blocks [ static_cast < size_t > ( last ) ].start ) )
				last = i;

		}

		auto same_value = [ & ] ( int32_t x ) {

			const block_code& tail = b.code [ static_cast < size_t > ( x ) ];

			if ( tail.returned < 0 )
				return true;

			const expr& back = b.exprs [ static_cast < size_t > ( tail.returned ) ];

			if ( back.kind == op::CONST )
				return true;

			if ( back.kind != op::VAR )
				return false;

			for ( int32_t p : pred [ static_cast < size_t > ( x ) ] ) {

				const block_code& from = b.code [ static_cast < size_t > ( p ) ];

				for ( int32_t s = from.first; s < from.first + from.count && s < b.statements.count ( ); s++ ) {

					const statement& line = b.statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == back.value )
						return false;

				}

			}

			return true;

		};

		auto falls_in = [ & ] ( int32_t x ) {

			for ( int32_t p : pred [ static_cast < size_t > ( x ) ] ) {

				if ( b.graph.blocks [ static_cast < size_t > ( p ) ].end == b.graph.blocks [ static_cast < size_t > ( x ) ].start && b.code [ static_cast < size_t > ( p ) ].end != term::BRANCH )
					return true;

			}

			return false;

		};

		auto sets_return = [ & ] ( int32_t x, int32_t end_block ) {

			const block_code& tail = b.code [ static_cast < size_t > ( end_block ) ];

			if ( tail.returned < 0 )
				return true;

			const expr& back = b.exprs [ static_cast < size_t > ( tail.returned ) ];

			if ( back.kind == op::CONST )
				return true;

			if ( back.kind != op::VAR )
				return false;

			const block_code& mine = b.code [ static_cast < size_t > ( x ) ];

			for ( int32_t s = mine.first; s < mine.first + mine.count && s < b.statements.count ( ); s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == back.value && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.right ) ].kind == op::CONST )
					return true;

			}

			return false;

		};

		auto shared = [ & ] ( int32_t x ) {

			if ( emitted [ static_cast < size_t > ( x ) ] || pred [ static_cast < size_t > ( x ) ].count ( ) < 2 || !small_block ( x ) || !falls_in ( x ) || header [ static_cast < size_t > ( x ) ] )
				return false;

			if ( exit_block ( x ) )
				return same_value ( x ) && ( x == last || sets_return ( x, x ) );

			const block_code& step = b.code [ static_cast < size_t > ( x ) ];

			if ( step.end != term::FALL || step.taken < 0 || step.taken >= count || !exit_block ( step.taken ) || !empty_block ( step.taken ) )
				return false;

			return sets_return ( x, step.taken );

		};

		if ( shared ( taken ) && !exit_block ( other ) )
			follow = taken;
		else if ( shared ( other ) && !exit_block ( taken ) )
			follow = other;

	}

	if ( void_skips && !b.returns_value && follow < 0 && context.loop < 0 && taken >= 0 && other >= 0 && taken < count && other < count && taken != other ) {

		auto bare_return = [ & ] ( int32_t x ) {

			return b.code [ static_cast < size_t > ( x ) ].end == term::RETURN && empty_block ( x ) && !emitted [ static_cast < size_t > ( x ) ] && !wanted_label [ static_cast < size_t > ( x ) ];

		};

		bool taken_bare = bare_return ( taken );
		bool other_bare = bare_return ( other );

		if ( taken_bare && !other_bare )
			follow = taken;
		else if ( other_bare && !taken_bare )
			follow = other;

	}

	if ( context.loop >= 0 && stop == context.loop && ( taken == context.loop ) != ( other == context.loop ) ) {

		int32_t away = taken == context.loop ? other : taken;
		const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( context.loop ) ];
		bool outside = away == context.exit || emitted [ static_cast < size_t > ( away ) ] || ( !members.empty ( ) && !members [ static_cast < size_t > ( away ) ] );

		if ( outside ) {

			open_brace ( item.last, chained ? L"else if" : L"if", item.condition, taken == context.loop );

			if ( !special ( away, -2, context, item.last ) ) {

				if ( emitted [ static_cast < size_t > ( away ) ] )
					go_to ( away, item.last );
				else
					region ( away, context.loop, context );

			}

			close_brace ( );

			return -1;

		}

	}

	if ( taken_jump && !other_jump ) {

		open_brace ( item.last, chained ? L"else if" : L"if", item.condition, false );
		special ( taken, -2, context, item.last );
		close_brace ( );

		return other;

	}

	if ( other_jump && !taken_jump ) {

		open_brace ( item.last, chained ? L"else if" : L"if", item.condition, true );
		special ( other, -2, context, item.last );
		close_brace ( );

		return taken;

	}

	bool negated = false;
	int32_t then_part = taken;
	int32_t else_part = other;

	auto leads = [ & ] ( int32_t x, int32_t target ) {

		for ( int32_t hops = 0; hops < 8 && x >= 0 && x < count; hops++ ) {

			if ( x == target )
				return true;

			const block_code& step = b.code [ static_cast < size_t > ( x ) ];

			if ( !empty_block ( x ) || step.end != term::FALL || header [ static_cast < size_t > ( x ) ] || wanted_label [ static_cast < size_t > ( x ) ] )
				return false;

			x = step.taken;

		}

		return x == target;

	};

	if ( empty_then && then_part != follow && else_part != follow && stop >= 0 && ( leads ( then_part, stop ) || ( follow >= 0 && leads ( then_part, follow ) ) ) && !leads ( else_part, stop ) ) {

		then_part = other;
		else_part = taken;
		negated = true;

		if ( follow < 0 || leads ( else_part, stop ) )
			follow = stop;

		if ( leads ( else_part, follow ) )
			else_part = follow;

	} else if ( then_part == follow || ( cut_stops && follow >= 0 && then_part != else_part && leads ( then_part, follow ) && !leads ( else_part, follow ) ) ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( fall_then && follow >= 0 && else_part != follow && other >= 0 && taken >= 0 && item.condition >= 0 && negative_test ( item.condition ) && !( cut_stops && leads ( other, follow ) && !leads ( taken, follow ) ) && ( !fall_then_simple || ( value_falls && b.exprs [ static_cast < size_t > ( item.condition ) ].kind == op::NE && b.exprs [ static_cast < size_t > ( item.condition ) ].b >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( item.condition ) ].b ) ].kind != op::CONST ) || ( b.code [ static_cast < size_t > ( other ) ].end == term::FALL && b.code [ static_cast < size_t > ( other ) ].taken == follow && b.code [ static_cast < size_t > ( taken ) ].end == term::FALL && b.code [ static_cast < size_t > ( taken ) ].taken == follow ) ) ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( big_fall_then && follow >= 0 && else_part != follow && other >= 0 && taken >= 0 && other != follow && item.condition >= 0 && negative_test ( item.condition ) && b.code [ static_cast < size_t > ( taken ) ].end == term::FALL && b.code [ static_cast < size_t > ( taken ) ].taken == follow && small_block ( taken ) && pred [ static_cast < size_t > ( taken ) ].count ( ) == 1 && pred [ static_cast < size_t > ( other ) ].count ( ) == 1 && b.code [ static_cast < size_t > ( other ) ].end == term::BRANCH ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( fall_returns_first && follow < 0 && other >= 0 && taken >= 0 && item.condition >= 0 && b.exprs [ static_cast < size_t > ( item.condition ) ].kind == op::LOR && else_returns && !chained && empty_block ( taken ) && empty_block ( other ) && !branch_fed ( block ) && b.code [ static_cast < size_t > ( other ) ].end == term::RETURN && b.code [ static_cast < size_t > ( taken ) ].end == term::RETURN && pred [ static_cast < size_t > ( other ) ].count ( ) == 1 && pred [ static_cast < size_t > ( taken ) ].count ( ) == 1 ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( fall_returns_first && follow < 0 && other >= 0 && taken >= 0 && item.condition >= 0 && negative_test ( item.condition ) && ( b.exprs [ static_cast < size_t > ( item.condition ) ].kind == op::NE || ( b.code [ static_cast < size_t > ( taken ) ].returned >= 0 && b.exprs [ static_cast < size_t > ( b.code [ static_cast < size_t > ( taken ) ].returned ) ].kind != op::CONST ) || ( else_returns && !chained && empty_block ( taken ) && empty_block ( other ) && !branch_fed ( block ) ) ) && b.code [ static_cast < size_t > ( other ) ].end == term::RETURN && b.code [ static_cast < size_t > ( taken ) ].end == term::RETURN && pred [ static_cast < size_t > ( other ) ].count ( ) == 1 && pred [ static_cast < size_t > ( taken ) ].count ( ) == 1 ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( short_fall_returns && follow < 0 && !chained && other >= 0 && taken >= 0 && other != taken && item.condition >= 0 && b.code [ static_cast < size_t > ( other ) ].end == term::RETURN && empty_block ( other ) && pred [ static_cast < size_t > ( other ) ].count ( ) == 1 && !empty_block ( taken ) && ( b.code [ static_cast < size_t > ( taken ) ].end == term::RETURN || b.code [ static_cast < size_t > ( taken ) ].end == term::TAIL ) && !emitted [ static_cast < size_t > ( other ) ] && !emitted [ static_cast < size_t > ( taken ) ] ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( ending_fall_first && follow < 0 && !chained && other >= 0 && taken >= 0 && other != taken && item.condition >= 0 && negative_test ( item.condition ) && !emitted [ static_cast < size_t > ( other ) ] && !emitted [ static_cast < size_t > ( taken ) ] && !exit_block ( taken ) && b.code [ static_cast < size_t > ( taken ) ].end != term::RETURN ) {

		then_part = other;
		else_part = taken;
		negated = true;

	}

	if ( diamond_ternary && !no_ternary && follow >= 0 && then_part >= 0 && else_part >= 0 && then_part != follow && else_part != follow ) {

		int32_t then_line = single_assign ( then_part, follow );
		int32_t else_line = single_assign ( else_part, follow );

		if ( then_line >= 0 && else_line >= 0 ) {

			const statement& yes = b.statements [ static_cast < size_t > ( then_line ) ];
			const statement& no = b.statements [ static_cast < size_t > ( else_line ) ];
			const expr& yes_left = b.exprs [ static_cast < size_t > ( yes.left ) ];
			const expr& no_left = b.exprs [ static_cast < size_t > ( no.left ) ];

			if ( yes_left.kind == op::VAR && no_left.kind == op::VAR && yes_left.value == no_left.value ) {

				int32_t test = negated ? b.negate ( item.condition ) : item.condition;
				int32_t select = b.make ( op::TERNARY, yes_left.size, test, yes.right );

				b.exprs [ static_cast < size_t > ( select ) ].c = no.right;
				b.deepen ( select );
				label ( then_part );
				label ( else_part );
				emitted [ static_cast < size_t > ( then_part ) ] = 1;
				emitted [ static_cast < size_t > ( else_part ) ] = 1;

				if ( !dry ) {

					out.begin ( yes.address );
					value ( yes.left, 0 );
					out.put ( L" = ", syntax::OPERATOR );
					value ( select, 2 );
					out.put ( L";" );
					out.finish ( );

				}

				ended = false;

				return follow;

			}

		}

	}

	if ( follow < 0 && then_part >= 0 && else_part >= 0 && !chained ) {

		for ( int32_t side = 0; side < 2; side++ ) {

			int32_t bare = side == 0 ? else_part : then_part;
			int32_t work = side == 0 ? then_part : else_part;
			const block_code& empty_ret = b.code [ static_cast < size_t > ( bare ) ];
			const block_code& full_ret = b.code [ static_cast < size_t > ( work ) ];

			if ( empty_ret.end != term::RETURN || full_ret.end != term::RETURN || !empty_block ( bare ) || empty_block ( work ) || emitted [ static_cast < size_t > ( work ) ] || emitted [ static_cast < size_t > ( bare ) ] || pred [ static_cast < size_t > ( work ) ].count ( ) != 1 )
				continue;

			bool alike = ( empty_ret.returned < 0 && full_ret.returned < 0 ) || ( empty_ret.returned >= 0 && full_ret.returned >= 0 && b.same ( empty_ret.returned, full_ret.returned ) );

			if ( !alike )
				continue;

			bool toward = side == 0 ? negated : !negated;

			open_brace ( item.last, L"if", item.condition, toward );
			body ( work );
			close_brace ( );

			return bare;

		}

	}

	if ( rotate_guards && !chained && then_part >= 0 && else_part >= 0 && then_part != follow && else_part != context.loop && else_part != context.exit ) {

		int32_t after = rotate ( block, then_part, else_part, negated ? b.negate ( item.condition ) : item.condition, context );

		if ( after != -2 )
			return after;

	}

	bool skip_else = false;

	if ( early_returns && ( follow < 0 || ( joined_returns && else_part != follow && then_part != follow ) ) && !chained && then_part >= 0 && else_part >= 0 && then_part != else_part && b.code [ static_cast < size_t > ( else_part ) ].end == term::RETURN && empty_block ( else_part ) && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && b.code [ static_cast < size_t > ( then_part ) ].end != term::RETURN && b.code [ static_cast < size_t > ( then_part ) ].end != term::STOP && !exit_block ( then_part ) && !( b.code [ static_cast < size_t > ( else_part ) ].returned >= 0 && b.exprs [ static_cast < size_t > ( b.code [ static_cast < size_t > ( else_part ) ].returned ) ].kind == op::VAR ) && !emitted [ static_cast < size_t > ( else_part ) ] && then_part != context.loop && then_part != context.exit && else_part != context.loop && else_part != context.exit ) {

		int32_t held = then_part;

		then_part = else_part;
		else_part = held;
		negated = !negated;

	}

	if ( guard_returns && !chained && follow >= 0 && else_part == follow && then_part >= 0 && then_part != follow && context.loop < 0 && b.code [ static_cast < size_t > ( follow ) ].end == term::RETURN && empty_block ( follow ) && !emitted [ static_cast < size_t > ( then_part ) ] && region_size ( then_part, follow ) >= guard_return_blocks ) {

		open_brace ( item.last, L"if", item.condition, !negated );
		ended = false;
		return_line ( b.code [ static_cast < size_t > ( follow ) ] );
		out.indent--;
		brace ( L"}" );
		ended = false;

		if ( !special ( then_part, -2, context, item.last ) )
			region ( then_part, follow, context );

		return follow;

	}

	if ( follow < 0 && context.loop < 0 && silent_return ( then_part ) && !silent_return ( else_part ) && else_part >= 0 ) {

		int32_t held = then_part;

		then_part = else_part;
		else_part = held;
		negated = !negated;

	}

	if ( else_part != follow && follow < 0 && context.loop < 0 && silent_return ( else_part ) ) {

		skip_else = true;
		emitted [ static_cast < size_t > ( else_part ) ] = 1;

	}

	open_brace ( item.last, chained ? L"else if" : L"if", item.condition, negated );

	ended = false;

	if ( !special ( then_part, -2, context, item.last ) ) {

		if ( defer_joins && deferred ( then_part, block ) )
			go_to ( then_part, item.last );
		else
			region ( then_part, follow, context );

	}

	bool then_ends = ended;

	out.indent--;

	if ( skip_else ) {

		brace ( L"}" );
		ended = false;

	} else if ( then_ends && else_part != follow && else_part >= 0 && !( chain_rest && chained && chain_falls ) && !( else_returns && !chained && follow < 0 && then_part >= 0 && b.code [ static_cast < size_t > ( then_part ) ].end == term::RETURN && b.code [ static_cast < size_t > ( else_part ) ].end == term::RETURN && pred [ static_cast < size_t > ( then_part ) ].count ( ) == 1 && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && !emitted [ static_cast < size_t > ( else_part ) ] && empty_block ( then_part ) && empty_block ( else_part ) && !branch_fed ( block ) ) ) {

		brace ( L"}" );
		ended = false;

		if ( !special ( else_part, -2, context, item.last ) )
			region ( else_part, follow, context );

	} else if ( else_part != follow && else_part >= 0 ) {

		bool chain = !emitted [ static_cast < size_t > ( else_part ) ] && b.code [ static_cast < size_t > ( else_part ) ].end == term::BRANCH && empty_block ( else_part ) && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && !header [ static_cast < size_t > ( else_part ) ] && follow_of ( else_part, stop, context ) == follow && !wanted_label [ static_cast < size_t > ( else_part ) ];

		if ( chain ) {

			brace ( L"}" );
			body ( else_part );

			bool saved_falls = chain_falls;

			chain_falls = ( chained && chain_falls ) || !then_ends;

			int32_t next = branch ( else_part, stop, context, true );
			bool chain_ends = ended;

			chain_falls = saved_falls;

			if ( chain_rest && next >= 0 && next < count && next != follow && !emitted [ static_cast < size_t > ( next ) ] ) {

				else_line ( );
				brace ( L"{" );
				out.indent++;
				ended = false;

				if ( !special ( next, -2, context, item.last ) )
					region ( next, follow, context );

				chain_ends = ended;
				close_brace ( );

			}

			ended = then_ends && chain_ends;

		} else {

			brace ( L"}" );
			else_line ( );
			brace ( L"{" );
			out.indent++;
			ended = false;

			if ( !special ( else_part, -2, context, item.last ) )
				region ( else_part, follow, context );

			bool else_ends = ended;

			close_brace ( );
			ended = then_ends && else_ends;

		}

	} else {

		brace ( L"}" );
		ended = false;

	}

	return follow;

}