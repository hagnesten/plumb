#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	using x86::mnemonic;

	int32_t call_arguments ( builder& b, const x86::instruction& ins );

	bool cookie_check ( const base::list < x86::instruction >& list, int32_t index );

	void annotate ( builder& b, const base::list < x86::instruction >& list, base::list < uint32_t >& uses, base::list < uint32_t >& defs );

}