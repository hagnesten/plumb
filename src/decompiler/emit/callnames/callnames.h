#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/text/text.h"
#include "base/list/list.h"

namespace decompiler {

	bool unique_name ( const base::list < base::text >& taken, const wchar_t* base_name, wchar_t* out );

	void name_from_calls ( builder& b, int32_t result_var );

}