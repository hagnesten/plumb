#pragma once

#include "base/base.hh"

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

	};

	struct snapshot {

		status state = status::IDLE;
		uint64_t image_base = 0;
		uint64_t pc = 0;
		uint64_t registers [ 18 ] = { };
		bool wide = true;
		uint32_t exit_code = 0;
		uint32_t serial = 0;
		uint32_t process_id = 0;
		wchar_t note [ 200 ] = { };
		uint64_t stack [ 64 ] = { };
		int32_t stack_count = 0;

	};

	bool start ( const wchar_t* path, uint64_t entry_rva, bool wide );

	void send ( order value );

	void set_breakpoints ( const uint64_t* rvas, int32_t count );

	snapshot read ( );

	bool active ( );

	bool take_log ( wchar_t* out, int32_t room );

	void set_condition ( uint64_t rva, const wchar_t* text );

	void set_coverage ( const uint64_t* rvas, int32_t count );

	int32_t take_coverage ( base::list < uint64_t >& out );

	bool peek ( uint64_t address, void* out, size_t size );

	bool test_condition ( const wchar_t* text, const uint64_t* registers, bool wide, bool& valid );

	const wchar_t* register_name ( int32_t index, bool wide );

}