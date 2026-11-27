#pragma once

#include "base/types/types.h"

namespace host {

	void run_loop ( );

	void check_device ( );

	unsigned long wait_for_frame ( );

	unsigned long wait_idle ( bool visible );

	bool pump_messages ( );

	bool frame_tick ( bool need, unsigned long result );

}