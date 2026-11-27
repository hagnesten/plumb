#pragma once

#include "decompiler/output/output.h"

namespace decompiler {

	enum class line_shape : uint8_t {

		BLANK,
		STATEMENT,
		CONTROL,
		OPEN,
		CLOSE,
		ELSE,
		LOOP_TAIL,
		CASE,
		LABEL,
		RETURN,
		CONTINUATION,

	};

	line_shape shape_of ( const result& code, const line& item, int32_t& column );

	bool wants_gap ( line_shape shape, int32_t column, line_shape before, int32_t before_column );

	void space_lines ( result& code );

}