#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	struct counter_scan {

		int64_t var = -1;
		int32_t uses = 0;
		int32_t matched = 0;
		int32_t element = 0;
		bool bad = false;

	};

}
