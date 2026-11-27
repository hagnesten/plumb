#include "decompiler/lift/stack/stack.h"
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

void decompiler::builder::merge_stack_slots ( ) {

	if ( !merge_slots )
		return;

	int32_t total = vars.count ( );
	base::list < int32_t > owner;

	owner.fill ( static_cast < size_t > ( total ), -1 );

	for ( int32_t w = 0; w < total; w++ ) {

		const variable& part = vars [ static_cast < size_t > ( w ) ];

		if ( part.kind != var_kind::STACK || part.size == 0 || part.size > 8 )
			continue;

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& whole = vars [ static_cast < size_t > ( v ) ];

			if ( v == w || whole.kind != var_kind::STACK || whole.size != 16 )
				continue;

			int64_t k = part.offset - whole.offset;

			if ( k > 0 && k + part.size <= 16 && k % part.size == 0 ) {

				owner [ static_cast < size_t > ( w ) ] = v;

				break;

			}

		}

	}

	int32_t count = exprs.count ( );

	for ( int32_t i = 0; i < count; i++ ) {

		const expr node = exprs [ static_cast < size_t > ( i ) ];

		if ( ( node.kind != op::VAR && !( node.kind == op::ADDRESS && node.a < 0 ) ) || node.value < 0 || node.value >= total )
			continue;

		int32_t v = owner [ static_cast < size_t > ( node.value ) ];

		if ( v < 0 )
			continue;

		int64_t k = vars [ static_cast < size_t > ( node.value ) ].offset - vars [ static_cast < size_t > ( v ) ].offset;
		int32_t base = make ( op::ADDRESS, 8, -1, -1, v, POINTER );
		int32_t step = make ( op::CONST, 8, -1, -1, k, 0 );

		if ( node.kind == op::VAR ) {

			int32_t where = make ( op::ADD, 8, base, step, 0, POINTER );
			expr& target = exprs [ static_cast < size_t > ( i ) ];

			target.kind = op::LOAD;
			target.a = where;
			target.b = -1;
			target.value = 0;

		} else {

			expr& target = exprs [ static_cast < size_t > ( i ) ];

			target.kind = op::ADD;
			target.a = base;
			target.b = step;
			target.value = 0;
			target.bits = POINTER;

		}

	}

	for ( statement& line : statements ) {

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::LOAD )
			line.kind = stmt::STORE;

	}

}

int32_t decompiler::builder::stack_var ( int64_t offset, uint8_t size ) {

	for ( int32_t i = 0; i < vars.count ( ); i++ ) {

		variable& item = vars [ static_cast < size_t > ( i ) ];

		if ( item.kind == var_kind::STACK && item.offset == offset ) {

			if ( size > item.size )
				item.size = size;

			return i;

		}

	}

	if ( long_mode && offset >= 8 && offset <= 32 && ( offset & 7 ) == 0 ) {

		static const int32_t homes [ 4 ] = { 1, 2, 8, 9 };
		int32_t position = static_cast < int32_t > ( offset / 8 ) - 1;
		int32_t family = homes [ position ];

		if ( !( home_local & ( 1u << position ) ) && ( entry_live & ( ( 1u << family ) | ( 1u << ( 16 + position ) ) ) ) )
			return register_var ( ( entry_live & ( 1u << family ) ) ? family : 16 + position );

	}

	variable& item = vars.emplace ( );

	item.kind = var_kind::STACK;
	item.offset = offset;
	item.size = size;

	if ( ( long_mode && offset >= 0x28 ) || ( !long_mode && offset >= 4 ) ) {

		item.kind = var_kind::PARAMETER;
		item.position = long_mode ? static_cast < int32_t > ( ( offset - 0x28 ) / 8 ) + 4 : static_cast < int32_t > ( ( offset - 4 ) / 4 );
		params = base::max ( params, item.position + 1 );

	}

	return vars.count ( ) - 1;

}