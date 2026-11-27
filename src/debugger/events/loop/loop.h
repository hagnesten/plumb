#pragma once

#include "base/types/types.h"

struct _DEBUG_EVENT;

namespace debugger {

	struct event_outcome {

		unsigned long reply = 0;
		unsigned long exit_code = 0;
		bool detach = false;
		bool exited = false;

	};

	unsigned long __stdcall run ( void* );

	bool idle_order ( );

	void end_detached ( );

	void handle_event ( const _DEBUG_EVENT& event, event_outcome& outcome );

	void report_exit ( unsigned long exit_code );

}