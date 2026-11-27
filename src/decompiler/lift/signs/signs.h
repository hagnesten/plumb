#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	void note_signs ( const builder& b, int32_t e, base::list < uint8_t >& marks, int32_t depth );

}