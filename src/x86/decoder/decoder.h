#pragma once

#include "base/types/types.h"
#include "x86/instruction/instruction.h"

namespace x86 {

	bool decode ( const uint8_t* code, size_t available, uint64_t address, bool long_mode, instruction& out );

	void prepare ( );

}