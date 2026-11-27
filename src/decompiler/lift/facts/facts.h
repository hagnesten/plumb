#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"

namespace decompiler {

	struct fact {

		int32_t var = -1;
		uint8_t width = 8;
		int64_t value = 0;

	};

	bool subject_of ( const builder& b, int32_t e, int32_t& var, uint8_t& width );

	bool fact_for ( const builder& b, int32_t condition, bool taken, fact& out );

	int64_t mask_to_size ( int64_t value, uint8_t size );

	bool low_bits_only ( op kind );

	struct fact_rewriter {

		builder& b;
		fact known;
		int32_t changed = 0;

		int32_t constant ( int64_t value, uint8_t size, uint8_t bits );

		int32_t fold ( int32_t e );

		int32_t rewrite ( int32_t e, uint8_t limit, int32_t depth );

	};

}