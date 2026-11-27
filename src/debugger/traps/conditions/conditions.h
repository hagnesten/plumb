#pragma once

#include "base/text/text.h"

namespace debugger {

	struct condition_entry {

		uint64_t rva = 0;
		base::text text;

	};

	bool condition_holds ( uint64_t address, void* thread );

	void set_condition ( uint64_t rva, const wchar_t* text );

	bool parse_value ( const wchar_t*& at, uint64_t& value );

	bool test_condition ( const wchar_t* text, const uint64_t* registers, bool wide, bool& valid );

}