#include "decompiler/emit/flow/flow.h"
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

void decompiler::emitter::run ( ) {

	for ( int32_t c : carried ) {

		if ( c >= 0 && c < hidden.count ( ) )
			hidden [ static_cast < size_t > ( c ) ] = 0;

	}

	carried.clear ( );
	out.terminal = -1;
	emitted.fill ( static_cast < size_t > ( count ), 0 );
	labeled.fill ( static_cast < size_t > ( count ), 0 );

	if ( !dry )
		referenced.fill ( b.vars.size ( ), 0 );

	number_addresses.clear ( );
	number_values.clear ( );
	gotos = 0;
	depth = 0;
	next_temp = 1;

	frame top;

	region ( 0, -1, top );

	for ( int32_t i = 0; i < count; i++ ) {

		if ( !emitted [ static_cast < size_t > ( i ) ] && b.code [ static_cast < size_t > ( i ) ].reachable ) {

			wanted_label [ static_cast < size_t > ( i ) ] = 1;
			region ( i, -1, top );

		}

	}

}
void decompiler::emitter::analyze ( ) {

	count = b.graph.blocks.count ( );
	succ.resize ( static_cast < size_t > ( count ) );
	pred.resize ( static_cast < size_t > ( count ) );

	for ( const analysis::edge& link : b.graph.edges ) {

		bool seen = false;

		for ( int32_t s : succ [ static_cast < size_t > ( link.from ) ] ) {

			if ( s == link.to )
				seen = true;

		}

		if ( seen )
			continue;

		succ [ static_cast < size_t > ( link.from ) ].push ( link.to );
		pred [ static_cast < size_t > ( link.to ) ].push ( link.from );

	}

	base::list < int32_t > order;

	postorder ( count, 0, false, order );

	base::list < int32_t > rpo;

	for ( int32_t i = order.count ( ) - 1; i >= 0; i-- )
		rpo.push ( order [ static_cast < size_t > ( i ) ] );

	rpo_index.fill ( static_cast < size_t > ( count + 1 ), -1 );

	for ( int32_t i = 0; i < rpo.count ( ); i++ )
		rpo_index [ static_cast < size_t > ( rpo [ static_cast < size_t > ( i ) ] ) ] = i;

	dominate ( rpo, false, idom );

	base::list < int32_t > reverse_order;

	postorder ( count + 1, count, true, reverse_order );

	base::list < int32_t > reverse_rpo;

	for ( int32_t i = reverse_order.count ( ) - 1; i >= 0; i-- )
		reverse_rpo.push ( reverse_order [ static_cast < size_t > ( i ) ] );

	rpo_index.fill ( static_cast < size_t > ( count + 1 ), -1 );

	for ( int32_t i = 0; i < reverse_rpo.count ( ); i++ )
		rpo_index [ static_cast < size_t > ( reverse_rpo [ static_cast < size_t > ( i ) ] ) ] = i;

	dominate ( reverse_rpo, true, ipdom );

	header.fill ( static_cast < size_t > ( count ), 0 );
	loop_body.resize ( static_cast < size_t > ( count ) );

	for ( int32_t u = 0; u < count; u++ ) {

		for ( int32_t h : succ [ static_cast < size_t > ( u ) ] ) {

			if ( !dominates ( h, u ) )
				continue;

			header [ static_cast < size_t > ( h ) ] = 1;

			base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( h ) ];

			if ( members.empty ( ) )
				members.fill ( static_cast < size_t > ( count ), 0 );

			members [ static_cast < size_t > ( h ) ] = 1;

			base::list < int32_t > work;

			if ( !members [ static_cast < size_t > ( u ) ] ) {

				members [ static_cast < size_t > ( u ) ] = 1;
				work.push ( u );

			}

			while ( !work.empty ( ) ) {

				int32_t v = work.back ( );

				work.pop ( );

				for ( int32_t p : pred [ static_cast < size_t > ( v ) ] ) {

					if ( !members [ static_cast < size_t > ( p ) ] ) {

						members [ static_cast < size_t > ( p ) ] = 1;
						work.push ( p );

					}

				}

			}

		}

	}

	wanted_label.fill ( static_cast < size_t > ( count ), 0 );
	hidden.fill ( b.statements.size ( ), 0 );
	loop_letter.fill ( b.vars.size ( ), -1 );
	var_number.fill ( b.vars.size ( ), -1 );
	referenced.fill ( b.vars.size ( ), 0 );

}
bool decompiler::emitter::dominates ( int32_t a, int32_t v ) {

	int32_t steps = 0;

	while ( v >= 0 && steps++ < 100000 ) {

		if ( v == a )
			return true;

		int32_t up = idom [ static_cast < size_t > ( v ) ];

		if ( up == v )
			return false;

		v = up;

	}

	return false;

}
void decompiler::emitter::postorder ( int32_t total, int32_t root, bool reverse, base::list < int32_t >& order ) {

	base::list < uint8_t > state;
	base::list < int32_t > stack;
	base::list < int32_t > cursor;

	state.fill ( static_cast < size_t > ( total ), 0 );
	cursor.fill ( static_cast < size_t > ( total ), 0 );
	stack.push ( root );
	state [ static_cast < size_t > ( root ) ] = 1;

	while ( !stack.empty ( ) ) {

		int32_t v = stack.back ( );
		base::list < int32_t > next;

		neighbors ( v, reverse, next );

		int32_t& at = cursor [ static_cast < size_t > ( v ) ];

		if ( at < next.count ( ) ) {

			int32_t to = next [ static_cast < size_t > ( at++ ) ];

			if ( !state [ static_cast < size_t > ( to ) ] ) {

				state [ static_cast < size_t > ( to ) ] = 1;
				stack.push ( to );

			}

			continue;

		}

		order.push ( v );
		stack.pop ( );

	}

}
bool decompiler::emitter::small_block ( int32_t v ) {

	const block_code& item = b.code [ static_cast < size_t > ( v ) ];
	int32_t live = 0;

	for ( int32_t s = item.first; s < item.first + item.count && s < b.statements.count ( ); s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
			live++;

	}

	return live <= 1;

}
bool decompiler::emitter::exit_block ( int32_t v ) {

	term end = b.code [ static_cast < size_t > ( v ) ].end;

	return succ [ static_cast < size_t > ( v ) ].empty ( ) || end == term::RETURN || end == term::TAIL || end == term::STOP;

}
void decompiler::emitter::neighbors ( int32_t v, bool reverse, base::list < int32_t >& out_list ) {

	out_list.clear ( );

	if ( !reverse ) {

		if ( v < count )
			out_list = succ [ static_cast < size_t > ( v ) ];

		return;

	}

	if ( v == count ) {

		for ( int32_t i = 0; i < count; i++ ) {

			if ( exit_block ( i ) )
				out_list.push ( i );

		}

		return;

	}

	out_list = pred [ static_cast < size_t > ( v ) ];

}
void decompiler::emitter::incoming ( int32_t v, bool reverse, base::list < int32_t >& out_list ) {

	out_list.clear ( );

	if ( !reverse ) {

		out_list = pred [ static_cast < size_t > ( v ) ];

		return;

	}

	if ( v == count )
		return;

	out_list = succ [ static_cast < size_t > ( v ) ];

	if ( exit_block ( v ) )
		out_list.push ( count );

}
void decompiler::emitter::dominate ( const base::list < int32_t >& rpo, bool reverse, base::list < int32_t >& result_list ) {

	int32_t total = count + 1;
	int32_t root = rpo.empty ( ) ? 0 : rpo [ 0 ];

	result_list.fill ( static_cast < size_t > ( total ), -1 );
	result_list [ static_cast < size_t > ( root ) ] = root;

	bool changed = true;
	base::list < int32_t > preds;

	while ( changed ) {

		changed = false;

		for ( int32_t i = 1; i < rpo.count ( ); i++ ) {

			int32_t v = rpo [ static_cast < size_t > ( i ) ];
			int32_t chosen = -1;

			incoming ( v, reverse, preds );

			for ( int32_t p : preds ) {

				if ( result_list [ static_cast < size_t > ( p ) ] < 0 )
					continue;

				if ( chosen < 0 )
					chosen = p;
				else {

					int32_t x = p;
					int32_t y = chosen;

					while ( x != y ) {

						while ( rpo_index [ static_cast < size_t > ( x ) ] > rpo_index [ static_cast < size_t > ( y ) ] )
							x = result_list [ static_cast < size_t > ( x ) ];

						while ( rpo_index [ static_cast < size_t > ( y ) ] > rpo_index [ static_cast < size_t > ( x ) ] )
							y = result_list [ static_cast < size_t > ( y ) ];

					}

					chosen = x;

				}

			}

			if ( chosen >= 0 && result_list [ static_cast < size_t > ( v ) ] != chosen ) {

				result_list [ static_cast < size_t > ( v ) ] = chosen;
				changed = true;

			}

		}

	}

}