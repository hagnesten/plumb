#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	int32_t explicit_test ( builder& b, int32_t e, int64_t v );
	int32_t comma_test ( builder& b, int32_t e, int64_t v, int32_t target, int32_t source );
	bool tests_directly ( const builder& b, int32_t e, int64_t v );

}