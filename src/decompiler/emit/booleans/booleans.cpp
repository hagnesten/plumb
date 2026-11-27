#include "decompiler/emit/booleans/booleans.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

void decompiler::boolean_vars ( builder& b, base::list < uint8_t >& boolean ) {

	int32_t total = b.vars.count ( );
	base::list < uint8_t > state;

	boolean.clear ( );
	boolean.fill ( static_cast < size_t > ( total ), 0 );
	state.fill ( static_cast < size_t > ( total ), 0 );

	if ( !detect_bools )
		return;

	auto self_truth = [ & ] ( int32_t e, bool& compared ) -> int32_t {

		if ( e < 0 )
			return 0;

		op kind = b.exprs [ static_cast < size_t > ( e ) ].kind;

		if ( ( kind >= op::EQ && kind <= op::LOR ) || kind == op::LNOT ) {

			compared = true;

			return 1;

		}

		return 0;

	};

	auto truth = [ & ] ( int32_t e, bool& compared ) -> int32_t {

		if ( e < 0 )
			return 0;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		switch ( node.kind ) {

			case op::EQ:
			case op::NE:
			case op::LT:
			case op::LE:
			case op::GT:
			case op::GE:
			case op::LNOT:
			case op::LAND:
			case op::LOR:
				compared = true;
				return 1;

			case op::CONST:
				return node.value == 0 || node.value == 1 ? 1 : 0;

			case op::VAR:
				return node.value >= 0 && node.value < total ? 2 : 0;

			case op::AND:
			case op::OR:
			case op::XOR: {

				bool left = false;
				bool right = false;
				int32_t x = self_truth ( node.a, left );
				int32_t y = self_truth ( node.b, right );

				if ( x == 1 && y == 1 && left && right ) {

					compared = true;

					return 1;

				}

				return 0;

			}

			case op::CAST:
				return node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind >= op::EQ && b.exprs [ static_cast < size_t > ( node.a ) ].kind <= op::LOR ? ( compared = true, 1 ) : 0;

			default:
				return 0;

		}

	};

	base::list < int32_t > copy_from;
	base::list < int32_t > copy_to;

	for ( const block_code& block : b.code ) {

		if ( !block.reachable )
			continue;

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& item = b.statements [ static_cast < size_t > ( s ) ];

			if ( item.kind != stmt::ASSIGN || item.left < 0 || b.exprs [ static_cast < size_t > ( item.left ) ].kind != op::VAR )
				continue;

			int64_t v = b.exprs [ static_cast < size_t > ( item.left ) ].value;

			if ( v < 0 || v >= total )
				continue;

			bool compared = false;
			int32_t kind = truth ( item.right, compared );

			if ( kind == 0 )
				state [ static_cast < size_t > ( v ) ] |= 4;
			else if ( kind == 2 ) {

				copy_from.push ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.right ) ].value ) );
				copy_to.push ( static_cast < int32_t > ( v ) );

			}

			if ( compared )
				state [ static_cast < size_t > ( v ) ] |= 1;

		}

	}

	for ( int32_t v = 0; v < total; v++ ) {

		const variable& item = b.vars [ static_cast < size_t > ( v ) ];

		if ( item.flags || item.kind != var_kind::REGISTER || ( item.bits & FLOATING ) || ( v < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( v ) ].empty ( ) ) || item.cookie || item.family < 0 )
			state [ static_cast < size_t > ( v ) ] |= 4;

	}

	for ( int32_t round = 0; round < 8; round++ ) {

		bool changed = false;

		for ( int32_t i = 0; i < copy_from.count ( ); i++ ) {

			int32_t from = copy_from [ static_cast < size_t > ( i ) ];
			int32_t to = copy_to [ static_cast < size_t > ( i ) ];
			bool from_bool = !b.vars [ static_cast < size_t > ( from ) ].flags ? ( state [ static_cast < size_t > ( from ) ] & 4 ) == 0 : true;

			if ( !from_bool && !( state [ static_cast < size_t > ( to ) ] & 4 ) ) {

				state [ static_cast < size_t > ( to ) ] |= 4;
				changed = true;

			}

			if ( from_bool && b.vars [ static_cast < size_t > ( from ) ].flags && !( state [ static_cast < size_t > ( to ) ] & 1 ) ) {

				state [ static_cast < size_t > ( to ) ] |= 1;
				changed = true;

			}

		}

		if ( !changed )
			break;

	}

	for ( int32_t v = 0; v < total; v++ ) {

		boolean [ static_cast < size_t > ( v ) ] = state [ static_cast < size_t > ( v ) ] == 1 ? 1 : 0;

		if ( boolean [ static_cast < size_t > ( v ) ] )
			b.vars [ static_cast < size_t > ( v ) ].size = 1;

	}

}