#include "decompiler/lift/constants/constants.h"
#include "decompiler/lift/returns/returns.h"

bool decompiler::constant_value ( const builder& b, int32_t e, int64_t& value ) {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
	uint64_t mask = node.size >= 8 ? ~0ull : ( 1ull << ( node.size * 8 ) ) - 1;

	if ( node.bits & FLOATING )
		return false;

	if ( node.kind == op::CONST ) {

		value = static_cast < int64_t > ( static_cast < uint64_t > ( node.value ) & mask );

		return true;

	}

	if ( node.kind == op::CAST && constant_value ( b, node.a, value ) ) {

		value = static_cast < int64_t > ( static_cast < uint64_t > ( value ) & mask );

		return true;

	}

	return false;

}

bool decompiler::constant_truth ( const builder& b, int32_t e, bool& truth ) {

	int64_t left = 0;
	int64_t right = 0;

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( constant_value ( b, e, left ) ) {

		truth = left != 0;

		return true;

	}

	if ( node.kind == op::LNOT && constant_truth ( b, node.a, truth ) ) {

		truth = !truth;

		return true;

	}

	if ( ( node.kind == op::EQ || node.kind == op::NE ) && constant_value ( b, node.a, left ) && constant_value ( b, node.b, right ) ) {

		truth = ( left == right ) == ( node.kind == op::EQ );

		return true;

	}

	return false;

}

void decompiler::fold_constant_branches ( builder& b ) {

	int32_t total = b.code.count ( );
	bool changed = false;

	if ( total != b.graph.blocks.count ( ) )
		return;

	for ( int32_t k = 0; k < total; k++ ) {

		block_code& head = b.code [ static_cast < size_t > ( k ) ];
		bool truth = false;

		if ( !head.reachable || head.end != term::BRANCH || head.taken == head.other || !constant_truth ( b, head.condition, truth ) )
			continue;

		int32_t live = truth ? head.taken : head.other;
		int32_t dead = truth ? head.other : head.taken;
		int32_t kept = 0;

		for ( int32_t e = 0; e < b.graph.edges.count ( ); e++ ) {

			analysis::edge link = b.graph.edges [ static_cast < size_t > ( e ) ];

			if ( link.from == k && link.to == dead )
				continue;

			if ( link.from == k )
				link.kind = analysis::edge_kind::FALL;

			b.graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

		}

		while ( b.graph.edges.count ( ) > kept )
			b.graph.edges.pop ( );

		head.end = term::FALL;
		head.taken = live;
		head.other = -1;
		head.condition = -1;
		changed = true;

	}

	if ( !changed )
		return;

	b.reindex_edges ( );
	sweep_unreachable ( b );

}

bool decompiler::plain_constant ( const builder& b, int32_t e, int64_t& value ) {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::CONST || ( node.bits & ( POINTER | FLOATING ) ) )
		return false;

	value = node.value;

	return true;

}

int32_t decompiler::folded_constant ( builder& b, int64_t value, const expr& node ) {

	uint64_t mask = node.size >= 8 ? ~0ull : ( 1ull << ( node.size * 8 ) ) - 1;
	int64_t shown = ( node.bits & SIGNED ) ? truncate ( value, node.size ) : static_cast < int64_t > ( static_cast < uint64_t > ( value ) & mask );
	int32_t made = b.constant ( shown, node.size );

	b.exprs [ static_cast < size_t > ( made ) ].bits |= node.bits & ( SIGNED | UNSIGNED );

	return made;

}

int32_t decompiler::fold_tree ( builder& b, int32_t e, int32_t depth ) {

	if ( e < 0 || depth > 64 )
		return e;

	const expr node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::CONST || node.kind == op::REAL || node.kind == op::VAR || node.kind == op::TEXT || node.kind == op::ADDRESS || node.kind == op::GLOBAL )
		return e;

	bool called = node.kind == op::CALL || node.kind == op::INTRINSIC;

	for ( int32_t i = 0; called && i < node.arg_count; i++ ) {

		size_t at = static_cast < size_t > ( node.first_arg + i );

		b.args [ at ] = fold_tree ( b, b.args [ at ], depth + 1 );

	}

	int32_t a = fold_tree ( b, node.a, depth + 1 );
	int32_t c = called ? node.b : fold_tree ( b, node.b, depth + 1 );
	int32_t t = node.kind == op::TERNARY ? fold_tree ( b, node.c, depth + 1 ) : node.c;
	int64_t x = 0;
	int64_t y = 0;

	if ( !( node.bits & FLOATING ) ) {

		if ( node.kind == op::CAST && plain_constant ( b, a, x ) )
			return b.cast ( a, node.size, node.bits );

		if ( ( node.kind == op::NOT || node.kind == op::NEG ) && plain_constant ( b, a, x ) )
			return folded_constant ( b, node.kind == op::NOT ? ~x : -x, node );

		if ( plain_constant ( b, a, x ) && plain_constant ( b, c, y ) ) {

			switch ( node.kind ) {

				case op::ADD: return folded_constant ( b, x + y, node );
				case op::SUB: return folded_constant ( b, x - y, node );
				case op::MUL: return folded_constant ( b, x * y, node );
				case op::AND: return folded_constant ( b, x & y, node );
				case op::OR: return folded_constant ( b, x | y, node );
				case op::XOR: return folded_constant ( b, x ^ y, node );
				case op::SHL: if ( y >= 0 && y < 64 ) return folded_constant ( b, x << y, node ); break;
				default: break;

			}

		}

	}

	if ( a == node.a && c == node.b && t == node.c )
		return e;

	expr copy = node;

	copy.a = a;
	copy.b = c;
	copy.c = t;
	b.exprs.push ( copy );

	return b.exprs.count ( ) - 1;

}

void decompiler::fold_constant_trees ( builder& b ) {

	for ( int32_t s = 0; s < b.statements.count ( ); s++ ) {

		statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		line.left = fold_tree ( b, line.left, 0 );
		line.right = fold_tree ( b, line.right, 0 );

	}

	for ( block_code& block : b.code ) {

		block.condition = fold_tree ( b, block.condition, 0 );
		block.selector = fold_tree ( b, block.selector, 0 );
		block.returned = fold_tree ( b, block.returned, 0 );

	}

}