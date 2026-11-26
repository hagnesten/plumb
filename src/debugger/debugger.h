#pragma once

#include "base/list/list.h"

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

	enum class watch_kind : int32_t {

		EXECUTE,
		WRITE,
		ACCESS,

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

	struct module_info {

		uint64_t base = 0;
		uint64_t size = 0;
		wchar_t path [ 300 ] = { };

	};

	struct hardware_point {

		uint64_t rva = 0;
		watch_kind kind = watch_kind::EXECUTE;
		int32_t length = 1;
		bool used = false;

	};

	struct process_entry {

		uint32_t id = 0;
		wchar_t name [ 260 ] = { };

	};

	bool start ( const wchar_t* path, const wchar_t* arguments, const wchar_t* module, uint64_t entry_rva, bool wide );

	bool attach ( uint32_t process_id, const wchar_t* module, uint64_t entry_rva, bool wide );

	void send ( order value );

	void run_to ( uint64_t rva );

	void set_breakpoints ( const uint64_t* rvas, int32_t count );

	void set_hardware ( const hardware_point* points, int32_t count );

	snapshot read ( );

	bool active ( );

	bool take_log ( wchar_t* out, int32_t room );

	void set_condition ( uint64_t rva, const wchar_t* text );

	void set_coverage ( const uint64_t* rvas, int32_t count );

	int32_t take_coverage ( base::list < uint64_t >& out );

	bool peek ( uint64_t address, void* out, size_t size );

	bool poke ( uint64_t address, const void* data, size_t size );

	int32_t modules ( base::list < module_info >& out );

	int32_t processes ( base::list < process_entry >& out );

	bool test_condition ( const wchar_t* text, const uint64_t* registers, bool wide, bool& valid );

	const wchar_t* register_name ( int32_t index, bool wide );

}
