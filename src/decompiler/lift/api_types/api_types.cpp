#include "decompiler/lift/api_types/api_types.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

bool decompiler::builder::api_result_type ( ) {

	if ( has_prototype || returns_float || result_width || !names )
		return false;

	base::text chosen;
	uint8_t chosen_size = 0;
	bool any = false;

	auto call_type = [ & ] ( int32_t e ) -> bool {

		const expr& call = exprs [ static_cast < size_t > ( e ) ];
		wchar_t callee [ 200 ];

		if ( call.kind != op::CALL || !call.value || !names->name ( static_cast < uint64_t > ( call.value ), true, callee, 200 ) )
			return false;

		const wchar_t* api = api_prototype ( callee );
		c_prototype shape;
		base::writer problem;

		if ( !api || !parse_prototype ( api, long_mode, shape, problem ) || shape.result_type.empty || shape.result_type.size == 0 || ( shape.result_type.bits & FLOATING ) )
			return false;

		if ( !any ) {

			chosen.set ( shape.result );
			chosen_size = shape.result_type.size;
			any = true;

			return true;

		}

		return base::same_text ( chosen.get ( ), shape.result );

	};

	for ( const block_code& item : code ) {

		if ( !item.reachable || ( item.end != term::RETURN && item.end != term::TAIL ) || item.returned < 0 )
			continue;

		int32_t e = strip_casts ( *this, item.returned );
		const expr& back = exprs [ static_cast < size_t > ( e ) ];

		if ( back.kind == op::CONST )
			continue;

		if ( back.kind == op::CALL ) {

			if ( !call_type ( e ) )
				return false;

			continue;

		}

		if ( back.kind != op::VAR )
			return false;

		bool assigned = false;

		for ( const statement& line : statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != back.value )
				continue;

			int32_t source = strip_casts ( *this, line.right );

			if ( exprs [ static_cast < size_t > ( source ) ].kind == op::CONST )
				continue;

			if ( !call_type ( source ) )
				return false;

			assigned = true;

		}

		if ( !assigned )
			return false;

	}

	if ( !any )
		return false;

	result_text = chosen;

	if ( chosen_size <= 4 ) {

		return_size = chosen_size;

		return true;

	}

	return false;

}