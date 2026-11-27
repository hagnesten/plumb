#pragma once

#include "decompiler/builder/builder.h"

namespace decompiler {

	bool constant_value ( const builder& b, int32_t e, int64_t& value );

	bool constant_truth ( const builder& b, int32_t e, bool& truth );

	void fold_constant_branches ( builder& b );

}