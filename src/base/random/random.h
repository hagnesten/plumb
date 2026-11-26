#pragma once

#include "base/types/types.h"

namespace base {

	class random {

	public:

		void seed ( uint64_t value );

		uint64_t next ( );

		float unit ( );

	private:

		uint64_t state = 0x9E3779B97F4A7C15ull;

	};

}