#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"

namespace decompiler {

	using x86::mnemonic;

	bool nonvolatile ( int32_t family, bool long_mode );

}