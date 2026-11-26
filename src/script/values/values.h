#pragma once

#include "base/types/types.h"

namespace script {

	enum class kind : uint8_t {

		NONE,
		NUMBER,
		TEXT,
		LIST,

	};

	struct value {

		kind type = kind::NONE;
		int64_t number = 0;
		int32_t ref = -1;

	};

}