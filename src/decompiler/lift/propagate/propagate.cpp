#include "decompiler/lift/propagate/propagate.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "decompiler/literals/literals.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"

bool decompiler::nonvolatile ( int32_t family, bool long_mode ) {

	if ( family == 3 || family == 5 || family == 6 || family == 7 )
		return true;

	return long_mode && ( ( family >= 12 && family < 16 ) || ( family >= 22 && family < 32 ) );

}

void decompiler::builder::propagate ( ) {

	for ( int32_t v = 0; v < vars.count ( ); v++ ) {

		const variable& item = vars [ static_cast < size_t > ( v ) ];

		if ( item.kind != var_kind::REGISTER || item.family < 0 || ( !nonvolatile ( item.family, long_mode ) && item.family < 16 ) )
			continue;

		int32_t count = 0;
		int32_t found = -1;

		for ( int32_t s = 0; s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == v ) {

				count++;
				found = s;

			}

		}

		if ( count != 1 || ( entry_live & ( 1u << item.family ) ) )
			continue;

		const expr value = exprs [ static_cast < size_t > ( statements [ static_cast < size_t > ( found ) ].right ) ];

		if ( value.kind != op::CONST )
			continue;

		statements [ static_cast < size_t > ( found ) ].kind = stmt::NOP;

		for ( int32_t i = 0; i < exprs.count ( ); i++ ) {

			expr& node = exprs [ static_cast < size_t > ( i ) ];

			if ( node.kind == op::VAR && node.value == v ) {

				node.kind = op::CONST;
				node.value = value.value;
				node.bits = value.bits;

			}

		}

	}

	int32_t total = exprs.count ( );

	for ( int32_t i = 0; i < total; i++ ) {

		expr node = exprs [ static_cast < size_t > ( i ) ];

		if ( node.kind != op::ADD || node.a < 0 || node.b < 0 )
			continue;

		const expr left = exprs [ static_cast < size_t > ( node.a ) ];
		const expr right = exprs [ static_cast < size_t > ( node.b ) ];

		if ( left.kind == op::CONST && right.kind == op::CONST ) {

			expr& target = exprs [ static_cast < size_t > ( i ) ];

			target.kind = op::CONST;
			target.value = left.value + right.value;
			target.bits = static_cast < uint8_t > ( ( left.bits | right.bits ) & POINTER );
			target.a = -1;
			target.b = -1;

		} else if ( right.kind == op::CONST && left.kind == op::ADD && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST ) {

			int64_t merged = exprs [ static_cast < size_t > ( left.b ) ].value + right.value;
			int32_t made = constant ( merged, node.size );

			exprs [ static_cast < size_t > ( i ) ].a = left.a;
			exprs [ static_cast < size_t > ( i ) ].b = made;

		} else if ( left.kind == op::ADD && left.a >= 0 && left.b >= 0 && exprs [ static_cast < size_t > ( left.a ) ].kind == op::CONST && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST ) {

			int32_t made = constant ( exprs [ static_cast < size_t > ( left.a ) ].value + exprs [ static_cast < size_t > ( left.b ) ].value, node.size );

			exprs [ static_cast < size_t > ( made ) ].bits |= POINTER;
			exprs [ static_cast < size_t > ( i ) ].a = node.b;
			exprs [ static_cast < size_t > ( i ) ].b = made;

		}

	}

	uint64_t low = file->low ( );
	uint64_t high = file->high ( );

	for ( expr& node : exprs ) {

		if ( node.kind != op::CONST || node.size < ( long_mode ? 8 : 4 ) )
			continue;

		uint64_t value = static_cast < uint64_t > ( node.value ) & ( long_mode ? ~0ull : 0xFFFFFFFFull );

		if ( value >= low && value < high && ( value == file->base || file->section_at ( value ) > 0 ) )
			node.bits |= POINTER;

	}

}