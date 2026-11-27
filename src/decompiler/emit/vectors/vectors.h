#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	bool text_has ( const wchar_t* text, const wchar_t* part );

	bool text_ends ( const wchar_t* text, const wchar_t* part );

	int32_t packed_flavor ( const wchar_t* name, bool result );

	void simd_vars ( builder& b, base::list < uint8_t >& vector, int32_t& si128 );

	void vector_vars ( builder& b, base::list < uint8_t >& vector );

}