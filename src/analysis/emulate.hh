#pragma once

#include "analysis/database.hh"

namespace analysis {

	struct emulated_string {

		uint64_t address = 0;
		uint64_t writer = 0;
		base::text text;
		bool wide = false;

	};

	struct emulated_call {

		uint64_t site = 0;
		uint64_t target = 0;
		uint64_t arguments [ 4 ] = { };

	};

	struct emulation {

		uint64_t registers [ 16 ] = { };
		uint64_t steps = 0;
		uint64_t stopped_at = 0;
		bool returned = false;
		base::text reason;
		base::list < emulated_string > strings;
		base::list < emulated_call > calls;

	};

	bool emulate ( const database& db, uint64_t start, const uint64_t* seed, const bool* seeded, uint64_t step_limit, emulation& out );

}
