#pragma once

#include "base/types/types.h"

struct _OUTPUT_DEBUG_STRING_INFO;

namespace debugger {

	void log_text ( const wchar_t* text );

	void append_number ( wchar_t* note, int32_t& at, uint64_t value, bool hex );

	void append_text ( wchar_t* note, int32_t& at, const wchar_t* text );

	void read_debug_string ( const _OUTPUT_DEBUG_STRING_INFO& info );

	bool take_log ( wchar_t* out, int32_t room );

}