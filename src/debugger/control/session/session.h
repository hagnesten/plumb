#pragma once

#include "base/types/types.h"

namespace debugger {

	enum class status : int32_t {

		IDLE,
		RUNNING,
		PAUSED,
		EXITED,
		FAILED,

	};

	enum class order : int32_t {

		NONE,
		CONTINUE,
		STEP_INTO,
		STEP_OVER,
		PAUSE,
		STOP,
		STEP_OUT,
		RUN_TO,
		DETACH,

	};

	struct snapshot {

		status state = status::IDLE;
		uint64_t image_base = 0;
		uint64_t pc = 0;
		uint64_t registers [ 18 ] = { };
		bool wide = true;
		bool attached = false;
		uint32_t exit_code = 0;
		uint32_t serial = 0;
		uint32_t process_id = 0;
		uint32_t thread_id = 0;
		wchar_t note [ 200 ] = { };
		uint64_t stack [ 64 ] = { };
		int32_t stack_count = 0;

	};

	bool launch ( uint32_t id, const wchar_t* path, const wchar_t* arguments, const wchar_t* module, uint64_t entry_rva, bool wide );

	bool start ( const wchar_t* path, const wchar_t* arguments, const wchar_t* module, uint64_t entry_rva, bool wide );

	bool attach ( uint32_t process_id, const wchar_t* module, uint64_t entry_rva, bool wide );

	void send ( order value );

	void run_to ( uint64_t rva );

	snapshot read ( );

	bool active ( );

}