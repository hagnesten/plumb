#pragma once

#include "base/types/types.h"

namespace debugger {

	bool begin_target ( );

	bool attach_target ( );

	bool create_target ( );

	int32_t build_command ( wchar_t* command, wchar_t* folder );

}