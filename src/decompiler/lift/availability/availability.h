#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	bool writes_between ( const builder& b, int32_t block, uint32_t mask, uint64_t from, uint64_t to );
	bool holds_through ( const builder& b, uint32_t mask, int32_t from_block, uint64_t from, int32_t to_block, uint64_t to );

}