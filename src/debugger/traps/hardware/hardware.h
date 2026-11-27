#pragma once

#include "base/types/types.h"

namespace debugger {

	enum class watch_kind : int32_t {

		EXECUTE,
		WRITE,
		ACCESS,

	};

	struct hardware_point {

		uint64_t rva = 0;
		watch_kind kind = watch_kind::EXECUTE;
		int32_t length = 1;
		bool used = false;

	};

	extern const wchar_t* const watch_names [ 3 ];

	void debug_registers ( void* thread, bool clear_status );

	int32_t hardware_hit ( void* thread );

	void sync_hardware ( );

	void set_hardware ( const hardware_point* points, int32_t count );

}