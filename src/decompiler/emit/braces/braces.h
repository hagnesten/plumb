#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"

namespace decompiler {

	bool line_starts ( const result& r, const line& item, const wchar_t* text );

	bool line_is ( const result& r, const line& item, const wchar_t* text );

	bool control_head ( const result& r, const line& item );

	bool simple_line ( const result& r, const line& item );

	void drop_braces ( result& r );

}