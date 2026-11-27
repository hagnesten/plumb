#pragma once

#include "base/types/types.h"

namespace host {

	void measure_refresh ( );

	int64_t clock_now ( );

	unsigned long frame_wait ( );

	void schedule_frame ( );

}