#pragma once

#include "base/types/types.h"

namespace decompiler {

	const wchar_t* api_prototype ( const wchar_t* name );

	int32_t import_arguments ( const wchar_t* name );

	extern const wchar_t* const api_table [ ];

	bool matches ( const wchar_t* prototype, const wchar_t* name, int32_t length );

}