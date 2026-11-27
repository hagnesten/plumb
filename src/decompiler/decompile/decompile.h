#pragma once

#include "analysis/database/database.h"
#include "decompiler/output/output.h"
#include "decompiler/namer/namer.h"

namespace decompiler {

	bool decompile ( const analysis::database& db, uint64_t function, namer& names, result& out );

}