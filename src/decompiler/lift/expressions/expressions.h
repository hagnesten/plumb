#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	void count_vars ( const builder& b, int32_t e, base::list < int32_t >& uses, int32_t depth );
	int32_t substitute ( builder& b, int32_t e, int64_t var, int32_t value, int32_t depth );
	bool has_call ( const builder& b, int32_t e, int32_t depth );
	bool register_only ( const builder& b, int32_t e, int32_t depth );
	int32_t occurrences ( const builder& b, int32_t e, int64_t var, int32_t depth );
	bool mentions ( const builder& b, int32_t e, int64_t var, int32_t depth );
	int32_t strip_casts ( const builder& b, int32_t e );
	bool bare_var ( const builder& b, int32_t e, int64_t v );

}