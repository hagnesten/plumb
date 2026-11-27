#pragma once

#include "decompiler/builder/builder.h"

namespace decompiler {

	bool emit ( builder& b );

	void append_result ( result& target, const result& source );

}