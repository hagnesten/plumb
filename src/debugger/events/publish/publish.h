#pragma once

#include "debugger/control/session/session.h"

namespace debugger {

	void publish ( status state, void* thread, unsigned long thread_id, const wchar_t* note );

	void finish ( const wchar_t* note );

}