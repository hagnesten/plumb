#pragma once

#include "debugger/control/session/session.h"

namespace debugger {

	bool wait_for_order ( void* thread, unsigned long thread_id, const wchar_t* note );

	void resume ( void* thread, order value );

	bool step_over ( void* thread, bool trap );

	bool step_out ( void* thread, bool trap );

	bool step_out_next ( void* thread, bool& armed_call );

}