#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"

namespace decompiler {

	extern bool lone_return_folds;

	void sweep_unreachable ( builder& b );

}