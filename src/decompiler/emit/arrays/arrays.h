#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	void stack_arrays ( builder& b, base::list < int32_t >& array_of, base::list < int32_t >& array_step, base::list < int32_t >& array_count );

	void stack_memsets ( builder& b, const base::list < int32_t >& array_of );

}