#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	bool global_slot_load ( const builder& b, int32_t e );

	bool quiet_statement ( const builder& b, const statement& line );

}
