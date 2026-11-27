#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	int32_t zero_width ( const builder& b, int32_t e, const base::list < uint8_t >& known, int32_t depth );

	void narrow_values ( builder& b, base::list < uint8_t >& known );

}