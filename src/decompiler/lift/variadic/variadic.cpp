#include "decompiler/lift/variadic/variadic.h"
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

void decompiler::builder::detect_variadic ( ) {

	if ( !find_variadic || !long_mode || has_prototype || params < 2 )
		return;

	int32_t chosen = -1;
	int32_t slot = -1;

	for ( int32_t position = 1; position < 4 && chosen < 0; position++ ) {

		for ( int32_t v = 0; v < vars.count ( ); v++ ) {

			const variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( item.kind != var_kind::STACK || item.offset != 8 * ( position + 1 ) )
				continue;

			bool taken = false;

			for ( const expr& node : exprs ) {

				if ( node.kind == op::ADDRESS && node.a < 0 && node.value == v )
					taken = true;

			}

			bool spilled = false;

			for ( const statement& line : statements ) {

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
					continue;

				const expr& left = exprs [ static_cast < size_t > ( line.left ) ];
				const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

				if ( left.kind == op::VAR && left.value == v && right.kind == op::VAR && right.value >= 0 && right.value < vars.count ( ) && vars [ static_cast < size_t > ( right.value ) ].kind == var_kind::PARAMETER && vars [ static_cast < size_t > ( right.value ) ].position == position )
					spilled = true;

			}

			if ( taken && spilled ) {

				chosen = position;
				slot = v;

			}

		}

	}

	if ( chosen < 0 )
		return;

	for ( statement& line : statements ) {

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
			continue;

		const expr& left = exprs [ static_cast < size_t > ( line.left ) ];
		const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

		if ( left.kind != op::VAR || right.kind != op::VAR || right.value < 0 || right.value >= vars.count ( ) || left.value < 0 || left.value >= vars.count ( ) )
			continue;

		const variable& home = vars [ static_cast < size_t > ( left.value ) ];
		const variable& from = vars [ static_cast < size_t > ( right.value ) ];

		if ( home.kind == var_kind::STACK && from.kind == var_kind::PARAMETER && from.position >= chosen && home.offset == 8 * ( from.position + 1 ) )
			line.kind = stmt::NOP;

	}

	for ( int32_t v = 0; v < vars.count ( ); v++ ) {

		variable& item = vars [ static_cast < size_t > ( v ) ];

		if ( item.kind == var_kind::PARAMETER && item.position >= chosen )
			item.kind = var_kind::REGISTER;

	}

	variadic_from = chosen;
	va_var = slot;
	params = chosen;

}