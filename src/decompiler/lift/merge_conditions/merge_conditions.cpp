#include "decompiler/lift/merge_conditions/merge_conditions.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

int32_t decompiler::explicit_test ( builder& b, int32_t e, int64_t v ) {

	if ( e < 0 )
		return e;

	expr node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::LAND || node.kind == op::LOR ) {

		int32_t left = explicit_test ( b, node.a, v );
		int32_t right = explicit_test ( b, node.b, v );

		if ( left == node.a && right == node.b )
			return e;

		return b.make ( node.kind, node.size, left, right );

	}

	if ( node.kind == op::LNOT && bare_var ( b, node.a, v ) ) {

		uint8_t size = b.exprs [ static_cast < size_t > ( node.a ) ].size;

		return b.binary ( op::EQ, node.a, b.constant ( 0, size ), 1 );

	}

	if ( bare_var ( b, e, v ) )
		return b.binary ( op::NE, e, b.constant ( 0, node.size ), 1 );

	return e;

}
int32_t decompiler::comma_test ( builder& b, int32_t e, int64_t v, int32_t target, int32_t source ) {

	if ( e < 0 )
		return e;

	expr node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::LAND || node.kind == op::LOR ) {

		int32_t left = comma_test ( b, node.a, v, target, source );
		int32_t right = comma_test ( b, node.b, v, target, source );

		if ( left == node.a && right == node.b )
			return e;

		return b.make ( node.kind, node.size, left, right );

	}

	if ( !b.references ( e, static_cast < int32_t > ( v ), -1 ) )
		return e;

	int32_t made = b.make ( op::SET, 1, target, source, 1 );

	b.exprs [ static_cast < size_t > ( made ) ].c = e;

	return made;

}
bool decompiler::tests_directly ( const builder& b, int32_t e, int64_t v ) {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	switch ( node.kind ) {

		case op::LAND:
		case op::LOR:
			return tests_directly ( b, node.a, v ) || tests_directly ( b, node.b, v );

		case op::LNOT:
			return bare_var ( b, node.a, v ) || tests_directly ( b, node.a, v );

		case op::EQ:
		case op::NE:
		case op::LT:
		case op::LE:
		case op::GT:
		case op::GE:
			return bare_var ( b, node.a, v ) || bare_var ( b, node.b, v );

		default:
			return bare_var ( b, e, v );

	}

}
void decompiler::builder::merge_conditions ( ) {

	sink_into_tests ( );
	chain_switches ( );

	int32_t total = code.count ( );

	if ( total != graph.blocks.count ( ) || total < 3 )
		return;

	base::list < int32_t > preds;

	auto count_preds = [ & ] ( ) {

		preds.clear ( );
		preds.fill ( static_cast < size_t > ( total ), 0 );

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to >= 0 && link.to < total )
				preds [ static_cast < size_t > ( link.to ) ]++;

		}

	};

	auto empty = [ & ] ( int32_t block ) {

		const block_code& item = code [ static_cast < size_t > ( block ) ];

		for ( int32_t s = item.first; s < item.first + item.count && s < statements.count ( ); s++ ) {

			if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
				return false;

		}

		return true;

	};

	auto rewire = [ & ] ( int32_t a, int32_t gone, int32_t taken, int32_t other ) {

		int32_t kept = 0;

		for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

			const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];

			if ( link.from == a || link.from == gone )
				continue;

			graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

		}

		while ( graph.edges.count ( ) > kept )
			graph.edges.pop ( );

		analysis::edge yes;

		yes.from = a;
		yes.to = taken;
		yes.kind = analysis::edge_kind::TAKEN;
		graph.edges.push ( yes );

		analysis::edge no;

		no.from = a;
		no.to = other;
		no.kind = analysis::edge_kind::NOT_TAKEN;
		graph.edges.push ( no );

		block_code& dead = code [ static_cast < size_t > ( gone ) ];

		dead.reachable = false;
		dead.end = term::STOP;
		dead.taken = -1;
		dead.other = -1;

	};

	for ( int32_t round = 0; round < 64; round++ ) {

		bool changed = false;

		count_preds ( );

		for ( int32_t a = 0; a < total; a++ ) {

			block_code& head = code [ static_cast < size_t > ( a ) ];

			if ( head.end != term::BRANCH || head.condition < 0 || !head.reachable || head.taken == head.other )
				continue;

			for ( int32_t side = 0; side < 2 && !changed; side++ ) {

				int32_t inner = side == 0 ? head.other : head.taken;

				if ( inner <= 0 || inner >= total || inner == a || preds [ static_cast < size_t > ( inner ) ] != 1 )
					continue;

				block_code next = code [ static_cast < size_t > ( inner ) ];

				if ( next.end != term::BRANCH || next.condition < 0 || next.taken == next.other || next.taken == inner || next.other == inner )
					continue;

				int32_t assigned = -1;

				if ( !empty ( inner ) ) {

					if ( !assign_in_tests )
						continue;

					for ( int32_t s = next.first; s < next.first + next.count && s < statements.count ( ); s++ ) {

						if ( statements [ static_cast < size_t > ( s ) ].kind == stmt::NOP )
							continue;

						assigned = assigned < 0 ? s : -2;

					}

					if ( assigned < 0 )
						continue;

					const statement& line = statements [ static_cast < size_t > ( assigned ) ];

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
						continue;

					int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;
					int32_t joined = side == 0 ? head.taken : head.other;
					int32_t rest = next.taken == joined ? next.other : ( next.other == joined ? next.taken : -1 );

					if ( rest < 0 || rest >= total || rest <= inner || joined <= inner )
						continue;

					if ( code [ static_cast < size_t > ( joined ) ].end == term::RETURN && empty ( joined ) )
						continue;

					const block_code& after = code [ static_cast < size_t > ( rest ) ];

					if ( after.end == term::FALL && ( after.taken == joined || after.other == joined ) )
						continue;

					if ( after.end == term::BRANCH && ( after.taken == joined || after.other == joined ) )
						continue;

					if ( exprs [ static_cast < size_t > ( line.right ) ].kind != op::CALL ) {

						base::list < uint8_t > seen;
						base::list < int32_t > queue;
						bool reaches = false;

						seen.fill ( static_cast < size_t > ( total ), 0 );
						queue.push ( rest );
						seen [ static_cast < size_t > ( rest ) ] = 1;

						for ( int32_t q = 0; q < queue.count ( ) && !reaches; q++ ) {

							const block_code& at = code [ static_cast < size_t > ( queue [ static_cast < size_t > ( q ) ] ) ];
							int32_t next_blocks [ 2 ] = { at.taken, at.other };

							for ( int32_t n : next_blocks ) {

								if ( n < 0 || n >= total || seen [ static_cast < size_t > ( n ) ] || n == a )
									continue;

								if ( n == joined ) {

									reaches = true;

									break;

								}

								seen [ static_cast < size_t > ( n ) ] = 1;
								queue.push ( n );

							}

						}

						if ( reaches )
							continue;

					}
					base::list < int32_t > seen;

					seen.fill ( vars.size ( ), 0 );
					count_vars ( *this, next.condition, seen, 0 );

					if ( v < 0 || v >= seen.count ( ) || seen [ static_cast < size_t > ( v ) ] < 1 || ( seen [ static_cast < size_t > ( v ) ] != 1 && !comma_uses ) )
						continue;

					bool direct = seen [ static_cast < size_t > ( v ) ] == 1 && tests_directly ( *this, next.condition, v );

					if ( !direct && !comma_uses )
						continue;

					if ( !vars [ static_cast < size_t > ( v ) ].born )
						vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

					if ( !direct )
						next.condition = comma_test ( *this, next.condition, v, line.left, line.right );
					else if ( exprs [ static_cast < size_t > ( line.right ) ].kind == op::CALL )
						next.condition = comma_test ( *this, explicit_test ( *this, next.condition, v ), v, line.left, line.right );
					else {

						int32_t setter = make ( op::SET, exprs [ static_cast < size_t > ( line.left ) ].size, line.left, line.right );

						next.condition = substitute ( *this, explicit_test ( *this, next.condition, v ), v, setter, 0 );

					}

				}

				int32_t shared = side == 0 ? head.taken : head.other;
				int32_t condition = -1;
				int32_t taken = -1;
				int32_t other = -1;

				if ( side == 0 ) {

					if ( next.taken == shared ) {

						condition = binary ( op::LOR, head.condition, next.condition, 1 );
						taken = shared;
						other = next.other;

					} else if ( next.other == shared ) {

						condition = binary ( op::LOR, head.condition, negate ( next.condition ), 1 );
						taken = shared;
						other = next.taken;

					}

				} else {

					if ( next.other == shared ) {

						condition = binary ( op::LAND, head.condition, next.condition, 1 );
						taken = next.taken;
						other = shared;

					} else if ( next.taken == shared ) {

						condition = binary ( op::LAND, head.condition, negate ( next.condition ), 1 );
						taken = next.other;
						other = shared;

					}

				}

				if ( condition < 0 || taken == other )
					continue;

				head.condition = condition;
				head.taken = taken;
				head.other = other;
				head.last = next.last;

				if ( assigned >= 0 )
					statements [ static_cast < size_t > ( assigned ) ].kind = stmt::NOP;

				rewire ( a, inner, taken, other );
				changed = true;

			}

			if ( changed )
				break;

		}

		if ( !changed )
			break;

	}

	reindex_edges ( );

}