#include "decompiler/lift/constant_branches/constant_branches.h"
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