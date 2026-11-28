#include "decompiler/lift/compares/compares.h"

void decompiler::builder::order_compares ( ) {

	int32_t total = exprs.count ( );

	for ( int32_t i = 0; i < total; i++ ) {

		expr& node = exprs [ static_cast < size_t > ( i ) ];
		op flipped = node.kind;

		switch ( node.kind ) {

			case op::LT: flipped = op::GT; break;
			case op::GT: flipped = op::LT; break;
			case op::LE: flipped = op::GE; break;
			case op::GE: flipped = op::LE; break;
			case op::EQ: break;
			case op::NE: break;
			default: continue;

		}

		if ( node.a < 0 || node.b < 0 )
			continue;

		const expr& left = exprs [ static_cast < size_t > ( node.a ) ];
		const expr& right = exprs [ static_cast < size_t > ( node.b ) ];

		if ( left.kind != op::CONST || right.kind == op::CONST || ( left.bits & POINTER ) )
			continue;

		int32_t swap = node.a;

		node.a = node.b;
		node.b = swap;
		node.kind = flipped;

	}

}
