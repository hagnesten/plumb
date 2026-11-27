#pragma once

#include "decompiler/builder/builder.h"

namespace decompiler {

	bool constant_value ( const builder& b, int32_t e, int64_t& value );

	bool constant_truth ( const builder& b, int32_t e, bool& truth );

	void fold_constant_branches ( builder& b );

	bool plain_constant ( const builder& b, int32_t e, int64_t& value );

	int32_t folded_constant ( builder& b, int64_t value, const expr& node );

	int32_t fold_tree ( builder& b, int32_t e, int32_t depth );

	void fold_constant_trees ( builder& b );

}