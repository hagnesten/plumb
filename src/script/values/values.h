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

	struct rope_node {

		int32_t left = -1;
		int32_t right = -1;
		int32_t length = 0;

	};

}