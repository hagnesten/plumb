#pragma once

#include "base/types/types.h"

namespace debugger {

	enum class purpose : int32_t {

		USER,
		ENTRY,
		STEP_OVER,
		RUN_TO,
		STEP_OUT,

	};

	struct breakpoint {

		uint64_t address = 0;
		uint8_t original = 0;
		bool armed = false;
		bool temporary = false;
		purpose use = purpose::USER;

	};

	void arm ( breakpoint& item );

	void arm_temporary ( uint64_t address, purpose use );

	bool shared_trap ( const breakpoint& item );

	void disarm ( breakpoint& item );

	int32_t find_point ( uint64_t address );

	void sync_points ( );

	void release_traps ( );

	void set_breakpoints ( const uint64_t* rvas, int32_t count );

}