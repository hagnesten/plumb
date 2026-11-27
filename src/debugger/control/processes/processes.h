#pragma once

#include "base/list/list.h"

namespace debugger {

	struct process_entry {

		uint32_t id = 0;
		wchar_t name [ 260 ] = { };

	};

	int32_t processes ( base::list < process_entry >& out );

}