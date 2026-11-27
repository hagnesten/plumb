#pragma once

#include "base/types/types.h"

namespace debugger {

	struct thread_entry {

		unsigned long id = 0;
		void* handle = nullptr;

	};

	extern const wchar_t* const names64 [ ];

	extern const wchar_t* const names32 [ ];

	const wchar_t* register_name ( int32_t index, bool wide );

	void* thread_handle ( unsigned long id );

	bool get_registers ( void* thread, uint64_t* values );

	void adjust ( void* thread, bool set_pc, uint64_t pc, bool trap );

}