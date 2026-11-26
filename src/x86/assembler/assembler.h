#pragma once

#include "base/types/types.h"

namespace x86 {

	struct resolver {

		virtual bool lookup ( const wchar_t* name, uint64_t& value ) = 0;

	};

	int32_t assemble ( const wchar_t* text, uint64_t address, bool long_mode, resolver* names, uint8_t* out, int32_t room, wchar_t* problem, int32_t problem_room );

}