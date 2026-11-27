#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"

namespace decompiler {

	using x86::mnemonic;

	uint64_t high_product ( uint64_t x, uint64_t y );

	uint64_t magic_divisor ( uint64_t magic, int32_t shift );

}