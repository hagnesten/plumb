#pragma once

#include "base/list/list.h"
#include "base/text/text.h"

struct _OUTPUT_DEBUG_STRING_INFO;
struct _RTL_SRWLOCK;

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

	enum class purpose : int32_t {

		USER,
		ENTRY,
		STEP_OVER,
		RUN_TO,
		STEP_OUT,

	};

	struct breakpoint {

		uint64_t address = 0;
		uint8_t original = 0;
		bool armed = false;
		bool temporary = false;
		purpose use = purpose::USER;

	};

	struct cover_point {

		uint64_t address = 0;
		uint8_t original = 0;
		bool armed = false;

	};

	struct condition_entry {

		uint64_t rva = 0;
		base::text text;

	};

	struct thread_entry {

		unsigned long id = 0;
		void* handle = nullptr;

	};

	extern _RTL_SRWLOCK lock;

	extern void* wake;

	extern void* worker;

	extern snapshot shared;

	extern volatile long pending;

	extern volatile long wanted_serial;

	extern volatile long hardware_serial;

	extern base::list < uint64_t > wanted;

	extern base::list < base::text > messages;

	extern base::list < condition_entry > conditions;

	extern base::list < uint64_t > cover_wanted;

	extern base::list < uint64_t > cover_hits;

	extern base::list < cover_point > cover;

	extern base::list < module_info > loaded;

	extern hardware_point hardware_wanted [ 4 ];

	extern wchar_t target [ 600 ];

	extern wchar_t command_tail [ 1000 ];

	extern wchar_t module_name [ 260 ];

	extern uint32_t attach_id;

	extern uint64_t entry;

	extern uint64_t run_target;

	extern bool wide_target;

	extern void* process;

	extern unsigned long process_id;

	extern base::list < thread_entry > threads;

	extern base::list < breakpoint > points;

	extern hardware_point hardware [ 4 ];

	extern uint64_t image_base;

	extern uint64_t reinsert;

	extern bool stepping;

	extern bool stepping_out;

	extern bool out_finishing;

	extern bool out_ignore_step;

	extern bool attach_announced;

	extern int64_t out_steps;

	extern bool entry_reached;

	extern bool resume_flag;

	extern long synced_serial;

	extern long hardware_synced;

	extern const wchar_t* const names64 [ ];

	extern const wchar_t* const names32 [ ];

	void log_text ( const wchar_t* text );

	void append_number ( wchar_t* note, int32_t& at, uint64_t value, bool hex );

	void append_text ( wchar_t* note, int32_t& at, const wchar_t* text );

	void* thread_handle ( unsigned long id );

	bool read_memory ( uint64_t address, void* out, size_t size );

	bool write_memory ( uint64_t address, const void* data, size_t size );

	bool write_byte ( uint64_t address, uint8_t value );

	void path_of ( void* file, wchar_t* out, int32_t room );

	const wchar_t* leaf_of ( const wchar_t* path );

	uint64_t image_size ( uint64_t base );

	void add_module ( uint64_t base, void* file );

	int32_t find_cover ( uint64_t address );

	void record_cover ( cover_point& item );

	void arm ( breakpoint& item );

	void arm_coverage ( );

	bool get_registers ( void* thread, uint64_t* values );

	bool condition_holds ( uint64_t address, void* thread );

	bool shared_trap ( const breakpoint& item );

	void disarm ( breakpoint& item );

	int32_t find_point ( uint64_t address );

	void sync_points ( );

	void adjust ( void* thread, bool set_pc, uint64_t pc, bool trap );

	void debug_registers ( void* thread, bool clear_status );

	int32_t hardware_hit ( void* thread );

	void sync_hardware ( );

	void publish ( status state, void* thread, unsigned long thread_id, const wchar_t* note );

	void read_debug_string ( const _OUTPUT_DEBUG_STRING_INFO& info );

	void module_ready ( uint64_t base );

	bool matches_module ( const wchar_t* path );

	bool step_out_next ( void* thread, bool& armed_call );

	void release_traps ( );

	void finish ( const wchar_t* note );

	unsigned long __stdcall run ( void* );

	bool launch ( uint32_t id, const wchar_t* path, const wchar_t* arguments, const wchar_t* module, uint64_t entry_rva, bool wide );

	bool parse_value ( const wchar_t*& at, uint64_t& value );

}
