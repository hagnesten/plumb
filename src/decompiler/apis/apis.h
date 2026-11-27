#pragma once

#include "base/types/types.h"

namespace decompiler {

	const wchar_t* api_prototype ( const wchar_t* name );

	struct known_entry {

		const wchar_t* name;
		const wchar_t* params [ 4 ];
		const wchar_t* types [ 4 ];

	};

	const known_entry* known_function ( const wchar_t* function );

	const wchar_t* known_parameter ( const wchar_t* function, int32_t position );

	const wchar_t* known_parameter_type ( const wchar_t* function, int32_t position );

	int32_t import_arguments ( const wchar_t* name );

	extern const wchar_t* const api_table [ ];

	bool matches ( const wchar_t* prototype, const wchar_t* name, int32_t length );

}