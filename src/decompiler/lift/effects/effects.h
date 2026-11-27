#pragma once

#include "base/types/types.h"
#include "x86/instruction/instruction.h"

namespace decompiler {

	void effects ( const x86::instruction& ins, bool long_mode, uint32_t& use, uint32_t& def, bool partial = true );

	using x86::mnemonic;

	int32_t slot_of ( uint8_t reg );

	bool same_registers ( const x86::instruction& ins );

}