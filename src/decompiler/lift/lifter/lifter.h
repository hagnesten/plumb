#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	using x86::mnemonic;

	int32_t call_arguments ( builder& b, const x86::instruction& ins );

	bool opaque_thunk ( const analysis::database& db, uint64_t address );

	bool cookie_check ( const base::list < x86::instruction >& list, int32_t index );

	void annotate ( builder& b, const base::list < x86::instruction >& list, base::list < uint32_t >& uses, base::list < uint32_t >& defs );

	uint32_t useful_entry ( const builder& b, const base::list < base::list < x86::instruction > >& listings, const base::list < base::list < uint32_t > >& uses, const base::list < base::list < uint32_t > >& defs );

}