#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	int32_t zero_fold ( builder& b, int32_t e, int32_t depth );

}