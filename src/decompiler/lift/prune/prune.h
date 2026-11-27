#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	using x86::mnemonic;

	void count_reads ( const builder& b, int32_t e, base::list < int32_t >& reads );

	bool pure_intrinsic ( int32_t id );

	bool has_effects ( const builder& b, int32_t e );

	void release_reads ( const builder& b, int32_t e, base::list < int32_t >& pending );

}