#pragma once

#include "base/list/list.h"

namespace debugger {

	struct cover_point {

		uint64_t address = 0;
		uint8_t original = 0;
		bool armed = false;

	};

	int32_t find_cover ( uint64_t address );

	void record_cover ( cover_point& item );

	void arm_coverage ( );

	void set_coverage ( const uint64_t* rvas, int32_t count );

	int32_t take_coverage ( base::list < uint64_t >& out );

}