#include "debugger/debugger.h"
#include "base/text/text.h"
#include "base/strings/strings.h"
#include "x86/instruction/instruction.h"
#include "x86/decoder/decoder.h"
#include <windows.h>
#include <tlhelp32.h>

SRWLOCK debugger::lock = SRWLOCK_INIT;

HANDLE debugger::wake = nullptr;

HANDLE debugger::worker = nullptr;

debugger::snapshot debugger::shared;

volatile long debugger::pending = 0;

volatile long debugger::wanted_serial = 0;

volatile long debugger::hardware_serial = 0;

base::list < uint64_t > debugger::wanted;

base::list < base::text > debugger::messages;

base::list < debugger::condition_entry > debugger::conditions;

base::list < uint64_t > debugger::cover_wanted;

base::list < uint64_t > debugger::cover_hits;

base::list < debugger::cover_point > debugger::cover;

base::list < debugger::module_info > debugger::loaded;

debugger::hardware_point debugger::hardware_wanted [ 4 ];

wchar_t debugger::target [ 600 ];

wchar_t debugger::command_tail [ 1000 ];

wchar_t debugger::module_name [ 260 ];

uint32_t debugger::attach_id = 0;

uint64_t debugger::entry = 0;

uint64_t debugger::run_target = 0;

bool debugger::wide_target = true;

HANDLE debugger::process = nullptr;

DWORD debugger::process_id = 0;

base::list < debugger::thread_entry > debugger::threads;

base::list < debugger::breakpoint > debugger::points;

debugger::hardware_point debugger::hardware [ 4 ];

uint64_t debugger::image_base = 0;

uint64_t debugger::reinsert = 0;

bool debugger::stepping = false;

bool debugger::stepping_out = false;

bool debugger::out_finishing = false;

bool debugger::out_ignore_step = false;

bool debugger::attach_announced = false;

int64_t debugger::out_steps = 0;

bool debugger::entry_reached = false;

bool debugger::resume_flag = false;

long debugger::synced_serial = -1;

long debugger::hardware_synced = -1;

const wchar_t* const debugger::names64 [ 18 ] = { L"RAX", L"RCX", L"RDX", L"RBX", L"RSP", L"RBP", L"RSI", L"RDI", L"R8", L"R9", L"R10", L"R11", L"R12", L"R13", L"R14", L"R15", L"RIP", L"RFLAGS" };

const wchar_t* const debugger::names32 [ 18 ] = { L"EAX", L"ECX", L"EDX", L"EBX", L"ESP", L"EBP", L"ESI", L"EDI", L"", L"", L"", L"", L"", L"", L"", L"", L"EIP", L"EFLAGS" };

const wchar_t* debugger::register_name ( int32_t index, bool wide ) {

	if ( index < 0 || index >= 18 )
		return L"";

	return wide ? names64 [ index ] : names32 [ index ];

}

void debugger::log_text ( const wchar_t* text ) {

	AcquireSRWLockExclusive ( &lock );

	if ( messages.count ( ) < 500 )
		messages.push ( base::text ( text ) );

	ReleaseSRWLockExclusive ( &lock );

}

void debugger::append_number ( wchar_t* note, int32_t& at, uint64_t value, bool hex ) {

	static const wchar_t digits [ ] = L"0123456789ABCDEF";
	wchar_t reverse [ 24 ];
	int32_t n = 0;
	uint64_t radix = hex ? 16 : 10;

	do {

		reverse [ n++ ] = digits [ value % radix ];
		value /= radix;

	} while ( value && n < 23 );

	while ( n && at < 190 )
		note [ at++ ] = reverse [ --n ];

	note [ at ] = 0;

}

void debugger::append_text ( wchar_t* note, int32_t& at, const wchar_t* text ) {

	for ( ; *text && at < 190; text++ )
		note [ at++ ] = *text;

	note [ at ] = 0;

}

HANDLE debugger::thread_handle ( DWORD id ) {

	for ( const thread_entry& item : threads ) {

		if ( item.id == id )
			return item.handle;

	}

	return nullptr;

}

bool debugger::read_memory ( uint64_t address, void* out, size_t size ) {

	SIZE_T done = 0;

	return ReadProcessMemory ( process, reinterpret_cast < const void* > ( address ), out, size, &done ) && done == size;

}

bool debugger::write_memory ( uint64_t address, const void* data, size_t size ) {

	SIZE_T done = 0;
	DWORD old = 0;
	void* at = reinterpret_cast < void* > ( address );

	VirtualProtectEx ( process, at, size, PAGE_EXECUTE_READWRITE, &old );

	bool ok = WriteProcessMemory ( process, at, data, size, &done ) && done == size;

	VirtualProtectEx ( process, at, size, old, &old );
	FlushInstructionCache ( process, at, size );

	return ok;

}

bool debugger::write_byte ( uint64_t address, uint8_t value ) {

	return write_memory ( address, &value, 1 );

}

void debugger::path_of ( HANDLE file, wchar_t* out, int32_t room ) {

	out [ 0 ] = 0;

	if ( !file )
		return;

	wchar_t full [ 600 ];
	DWORD length = GetFinalPathNameByHandleW ( file, full, 600, FILE_NAME_NORMALIZED );

	if ( !length || length >= 600 )
		return;

	const wchar_t* from = full;

	if ( full [ 0 ] == L'\\' && full [ 1 ] == L'\\' && full [ 2 ] == L'?' && full [ 3 ] == L'\\' )
		from = full + 4;

	int32_t k = 0;

	for ( ; from [ k ] && k < room - 1; k++ )
		out [ k ] = from [ k ];

	out [ k ] = 0;

}

const wchar_t* debugger::leaf_of ( const wchar_t* path ) {

	const wchar_t* leaf = path;

	for ( const wchar_t* at = path; *at; at++ ) {

		if ( *at == L'\\' || *at == L'/' )
			leaf = at + 1;

	}

	return leaf;

}

uint64_t debugger::image_size ( uint64_t base ) {

	uint8_t header [ 0x400 ] = { };

	if ( !read_memory ( base, header, sizeof ( header ) ) )
		return 0;

	uint32_t pe = *reinterpret_cast < const uint32_t* > ( header + 0x3C );

	if ( pe + 0x58 > sizeof ( header ) )
		return 0;

	return *reinterpret_cast < const uint32_t* > ( header + pe + 0x50 );

}

void debugger::add_module ( uint64_t base, HANDLE file ) {

	module_info item;

	item.base = base;
	item.size = image_size ( base );
	path_of ( file, item.path, 300 );

	AcquireSRWLockExclusive ( &lock );
	loaded.push ( item );
	ReleaseSRWLockExclusive ( &lock );

}

int32_t debugger::find_cover ( uint64_t address ) {

	int32_t low = 0;
	int32_t high = cover.count ( ) - 1;

	while ( low <= high ) {

		int32_t middle = ( low + high ) / 2;
		uint64_t value = cover [ static_cast < size_t > ( middle ) ].address;

		if ( value == address )
			return middle;

		if ( value < address )
			low = middle + 1;
		else
			high = middle - 1;

	}

	return -1;

}

void debugger::record_cover ( cover_point& item ) {

	item.armed = false;

	AcquireSRWLockExclusive ( &lock );
	cover_hits.push ( item.address - image_base );
	ReleaseSRWLockExclusive ( &lock );

}

void debugger::arm ( breakpoint& item ) {

	if ( item.armed )
		return;

	int32_t shared_point = find_cover ( item.address );

	if ( shared_point >= 0 && cover [ static_cast < size_t > ( shared_point ) ].armed ) {

		item.original = cover [ static_cast < size_t > ( shared_point ) ].original;
		record_cover ( cover [ static_cast < size_t > ( shared_point ) ] );
		item.armed = true;

		return;

	}

	for ( const breakpoint& other : points ) {

		if ( &other != &item && other.armed && other.address == item.address ) {

			item.original = other.original;
			item.armed = true;

			return;

		}

	}

	if ( read_memory ( item.address, &item.original, 1 ) && write_byte ( item.address, 0xCC ) )
		item.armed = true;

}

void debugger::arm_coverage ( ) {

	base::list < uint64_t > copy;

	AcquireSRWLockShared ( &lock );

	for ( uint64_t rva : cover_wanted )
		copy.push ( rva );

	ReleaseSRWLockShared ( &lock );

	cover.clear ( );

	if ( copy.empty ( ) )
		return;

	uint64_t low = image_base + copy [ 0 ];
	uint64_t high = image_base + copy.back ( ) + 1;
	DWORD old = 0;
	void* region = reinterpret_cast < void* > ( low );
	SIZE_T span = static_cast < SIZE_T > ( high - low );

	VirtualProtectEx ( process, region, span, PAGE_EXECUTE_READWRITE, &old );

	for ( uint64_t rva : copy ) {

		cover_point& item = cover.emplace ( );
		uint8_t trap = 0xCC;
		SIZE_T done = 0;

		item.address = image_base + rva;

		if ( read_memory ( item.address, &item.original, 1 ) && WriteProcessMemory ( process, reinterpret_cast < void* > ( item.address ), &trap, 1, &done ) && done == 1 )
			item.armed = true;

	}

	VirtualProtectEx ( process, region, span, old, &old );
	FlushInstructionCache ( process, region, span );

}

bool debugger::condition_holds ( uint64_t address, HANDLE thread ) {

	base::text text;

	AcquireSRWLockShared ( &lock );

	for ( const condition_entry& item : conditions ) {

		if ( image_base + item.rva == address )
			text.set ( item.text.get ( ) );

	}

	ReleaseSRWLockShared ( &lock );

	if ( text.empty ( ) )
		return true;

	uint64_t registers [ 18 ] = { };
	bool valid = false;

	get_registers ( thread, registers );

	bool result = test_condition ( text.get ( ), registers, wide_target, valid );

	return !valid || result;

}

bool debugger::shared_trap ( const breakpoint& item ) {

	for ( const breakpoint& other : points ) {

		if ( &other != &item && other.armed && other.address == item.address )
			return true;

	}

	return false;

}

void debugger::disarm ( breakpoint& item ) {

	if ( !item.armed )
		return;

	if ( !shared_trap ( item ) )
		write_byte ( item.address, item.original );

	item.armed = false;

}

int32_t debugger::find_point ( uint64_t address ) {

	int32_t found = -1;

	for ( int32_t i = 0; i < points.count ( ); i++ ) {

		const breakpoint& item = points [ static_cast < size_t > ( i ) ];

		if ( item.address == address && ( found < 0 || ( item.armed && !points [ static_cast < size_t > ( found ) ].armed ) || ( item.temporary && !points [ static_cast < size_t > ( found ) ].temporary && item.armed ) ) )
			found = i;

	}

	return found;

}

void debugger::sync_points ( ) {

	long serial = __atomic_load_n ( &wanted_serial, __ATOMIC_ACQUIRE );

	if ( serial == synced_serial || !image_base )
		return;

	synced_serial = serial;

	base::list < uint64_t > copy;

	AcquireSRWLockShared ( &lock );

	for ( uint64_t rva : wanted )
		copy.push ( image_base + rva );

	ReleaseSRWLockShared ( &lock );

	for ( int32_t i = points.count ( ) - 1; i >= 0; i-- ) {

		breakpoint& item = points [ static_cast < size_t > ( i ) ];
		bool keep = item.temporary;

		for ( uint64_t address : copy ) {

			if ( address == item.address )
				keep = true;

		}

		if ( !keep ) {

			if ( reinsert == item.address )
				reinsert = 0;

			disarm ( item );
			points.remove_at ( static_cast < size_t > ( i ) );

		}

	}

	for ( uint64_t address : copy ) {

		bool present = false;

		for ( const breakpoint& item : points )
			present = present || ( item.address == address && !item.temporary );

		if ( present )
			continue;

		breakpoint& item = points.emplace ( );

		item.address = address;

		if ( address != reinsert )
			arm ( item );

	}

}

bool debugger::get_registers ( HANDLE thread, uint64_t* values ) {

	if ( wide_target ) {

		CONTEXT context = { };

		context.ContextFlags = CONTEXT_FULL;

		if ( !GetThreadContext ( thread, &context ) )
			return false;

		const uint64_t list [ 18 ] = { context.Rax, context.Rcx, context.Rdx, context.Rbx, context.Rsp, context.Rbp, context.Rsi, context.Rdi, context.R8, context.R9, context.R10, context.R11, context.R12, context.R13, context.R14, context.R15, context.Rip, context.EFlags };

		for ( int32_t i = 0; i < 18; i++ )
			values [ i ] = list [ i ];

		return true;

	}

	WOW64_CONTEXT context = { };

	context.ContextFlags = WOW64_CONTEXT_FULL;

	if ( !Wow64GetThreadContext ( thread, &context ) )
		return false;

	const uint64_t list [ 18 ] = { context.Eax, context.Ecx, context.Edx, context.Ebx, context.Esp, context.Ebp, context.Esi, context.Edi, 0, 0, 0, 0, 0, 0, 0, 0, context.Eip, context.EFlags };

	for ( int32_t i = 0; i < 18; i++ )
		values [ i ] = list [ i ];

	return true;

}

void debugger::adjust ( HANDLE thread, bool set_pc, uint64_t pc, bool trap ) {

	if ( wide_target ) {

		CONTEXT context = { };

		context.ContextFlags = CONTEXT_FULL;

		if ( !GetThreadContext ( thread, &context ) )
			return;

		if ( set_pc )
			context.Rip = pc;

		if ( trap )
			context.EFlags |= 0x100;

		if ( resume_flag )
			context.EFlags |= 0x10000;

		SetThreadContext ( thread, &context );

		return;

	}

	WOW64_CONTEXT context = { };

	context.ContextFlags = WOW64_CONTEXT_FULL;

	if ( !Wow64GetThreadContext ( thread, &context ) )
		return;

	if ( set_pc )
		context.Eip = static_cast < DWORD > ( pc );

	if ( trap )
		context.EFlags |= 0x100;

	if ( resume_flag )
		context.EFlags |= 0x10000;

	Wow64SetThreadContext ( thread, &context );

}

void debugger::debug_registers ( HANDLE thread, bool clear_status ) {

	uint64_t addresses [ 4 ] = { };
	uint64_t control = 0;

	for ( int32_t i = 0; i < 4; i++ ) {

		const hardware_point& item = hardware [ i ];

		if ( !item.used || !image_base )
			continue;

		uint64_t rw = item.kind == watch_kind::EXECUTE ? 0 : ( item.kind == watch_kind::WRITE ? 1 : 3 );
		uint64_t length = item.kind == watch_kind::EXECUTE ? 0 : ( item.length >= 8 ? 2 : ( item.length >= 4 ? 3 : ( item.length >= 2 ? 1 : 0 ) ) );

		addresses [ i ] = image_base + item.rva;
		control |= 1ull << ( i * 2 );
		control |= rw << ( 16 + i * 4 );
		control |= length << ( 18 + i * 4 );

	}

	if ( wide_target ) {

		CONTEXT context = { };

		context.ContextFlags = CONTEXT_DEBUG_REGISTERS;

		if ( !GetThreadContext ( thread, &context ) )
			return;

		context.Dr0 = addresses [ 0 ];
		context.Dr1 = addresses [ 1 ];
		context.Dr2 = addresses [ 2 ];
		context.Dr3 = addresses [ 3 ];
		context.Dr7 = control;

		if ( clear_status )
			context.Dr6 = 0;

		SetThreadContext ( thread, &context );

		return;

	}

	WOW64_CONTEXT context = { };

	context.ContextFlags = WOW64_CONTEXT_DEBUG_REGISTERS;

	if ( !Wow64GetThreadContext ( thread, &context ) )
		return;

	context.Dr0 = static_cast < DWORD > ( addresses [ 0 ] );
	context.Dr1 = static_cast < DWORD > ( addresses [ 1 ] );
	context.Dr2 = static_cast < DWORD > ( addresses [ 2 ] );
	context.Dr3 = static_cast < DWORD > ( addresses [ 3 ] );
	context.Dr7 = static_cast < DWORD > ( control );

	if ( clear_status )
		context.Dr6 = 0;

	Wow64SetThreadContext ( thread, &context );

}

int32_t debugger::hardware_hit ( HANDLE thread ) {

	uint64_t status_bits = 0;

	if ( wide_target ) {

		CONTEXT context = { };

		context.ContextFlags = CONTEXT_DEBUG_REGISTERS;

		if ( GetThreadContext ( thread, &context ) )
			status_bits = context.Dr6;

	} else {

		WOW64_CONTEXT context = { };

		context.ContextFlags = WOW64_CONTEXT_DEBUG_REGISTERS;

		if ( Wow64GetThreadContext ( thread, &context ) )
			status_bits = context.Dr6;

	}

	for ( int32_t i = 0; i < 4; i++ ) {

		if ( ( status_bits & ( 1ull << i ) ) && hardware [ i ].used )
			return i;

	}

	return -1;

}

void debugger::sync_hardware ( ) {

	long serial = __atomic_load_n ( &hardware_serial, __ATOMIC_ACQUIRE );

	if ( serial == hardware_synced || !process )
		return;

	hardware_synced = serial;

	AcquireSRWLockShared ( &lock );

	for ( int32_t i = 0; i < 4; i++ )
		hardware [ i ] = hardware_wanted [ i ];

	ReleaseSRWLockShared ( &lock );

	for ( const thread_entry& item : threads )
		debug_registers ( item.handle, false );

}

void debugger::publish ( status state, HANDLE thread, DWORD thread_id, const wchar_t* note ) {

	snapshot value;

	AcquireSRWLockShared ( &lock );
	value = shared;
	ReleaseSRWLockShared ( &lock );

	value.state = state;
	value.image_base = image_base;
	value.wide = wide_target;
	value.attached = attach_id != 0;
	value.process_id = process_id;
	value.thread_id = thread_id;
	value.stack_count = 0;

	if ( thread && get_registers ( thread, value.registers ) ) {

		value.pc = value.registers [ 16 ];

		if ( state == status::PAUSED ) {

			if ( wide_target ) {

				if ( read_memory ( value.registers [ 4 ], value.stack, sizeof ( value.stack ) ) )
					value.stack_count = 64;

			} else {

				uint32_t narrow [ 64 ] = { };

				if ( read_memory ( value.registers [ 4 ], narrow, sizeof ( narrow ) ) ) {

					for ( int32_t i = 0; i < 64; i++ )
						value.stack [ i ] = narrow [ i ];

					value.stack_count = 64;

				}

			}

		}

	}

	int32_t i = 0;

	for ( ; note && note [ i ] && i < 199; i++ )
		value.note [ i ] = note [ i ];

	value.note [ i ] = 0;

	AcquireSRWLockExclusive ( &lock );
	value.serial = shared.serial + 1;
	shared = value;
	ReleaseSRWLockExclusive ( &lock );

}

void debugger::read_debug_string ( const OUTPUT_DEBUG_STRING_INFO& info ) {

	uint32_t length = base::min ( static_cast < uint32_t > ( info.nDebugStringLength ), 400u );
	wchar_t text [ 420 ] = L"debug: ";
	int32_t at = 7;

	if ( info.fUnicode ) {

		wchar_t buffer [ 401 ] = { };

		read_memory ( reinterpret_cast < uint64_t > ( info.lpDebugStringData ), buffer, length * 2 );

		for ( uint32_t i = 0; i < length && buffer [ i ]; i++ )
			text [ at++ ] = buffer [ i ] == L'\n' || buffer [ i ] == L'\r' ? L' ' : buffer [ i ];

	} else {

		char buffer [ 401 ] = { };

		read_memory ( reinterpret_cast < uint64_t > ( info.lpDebugStringData ), buffer, length );

		for ( uint32_t i = 0; i < length && buffer [ i ]; i++ )
			text [ at++ ] = buffer [ i ] == '\n' || buffer [ i ] == '\r' ? L' ' : static_cast < wchar_t > ( static_cast < uint8_t > ( buffer [ i ] ) );

	}

	while ( at > 7 && text [ at - 1 ] == L' ' )
		at--;

	text [ at ] = 0;

	if ( at > 7 )
		log_text ( text );

}

void debugger::module_ready ( uint64_t base ) {

	image_base = base;

	if ( !attach_id && entry ) {

		breakpoint& start_point = points.emplace ( );

		start_point.address = image_base + entry;
		start_point.temporary = true;
		start_point.use = purpose::ENTRY;
		arm ( start_point );

	}

	arm_coverage ( );
	synced_serial = -1;
	hardware_synced = -1;
	sync_points ( );
	sync_hardware ( );

	wchar_t note [ 200 ] = { };
	int32_t at = 0;

	append_text ( note, at, module_name [ 0 ] ? module_name : leaf_of ( target ) );
	append_text ( note, at, L" is loaded at 0x" );
	append_number ( note, at, base, true );
	log_text ( note );

}

bool debugger::matches_module ( const wchar_t* path ) {

	return module_name [ 0 ] && path [ 0 ] && base::compare_folded ( leaf_of ( path ), module_name ) == 0;

}

bool debugger::step_out_next ( HANDLE thread, bool& armed_call ) {

	uint64_t registers [ 18 ] = { };
	uint8_t bytes [ 16 ] = { };
	x86::instruction ins;

	armed_call = false;

	if ( ++out_steps > 2000000 || !get_registers ( thread, registers ) || !read_memory ( registers [ 16 ], bytes, 16 ) || !x86::decode ( bytes, 16, registers [ 16 ], wide_target, ins ) )
		return false;

	if ( ins.control == x86::flow::CALL ) {

		armed_call = true;

		breakpoint& item = points.emplace ( );

		item.address = registers [ 16 ] + ins.length;
		item.temporary = true;
		item.use = purpose::STEP_OUT;
		arm ( item );

		return true;

	}

	if ( ins.control == x86::flow::RETURN )
		out_finishing = true;

	adjust ( thread, false, 0, true );

	return true;

}

void debugger::release_traps ( ) {

	for ( breakpoint& item : points )
		disarm ( item );

	for ( cover_point& item : cover ) {

		if ( item.armed ) {

			write_byte ( item.address, item.original );
			item.armed = false;

		}

	}

	for ( int32_t i = 0; i < 4; i++ )
		hardware [ i ].used = false;

	for ( const thread_entry& item : threads )
		debug_registers ( item.handle, true );

}

void debugger::finish ( const wchar_t* note ) {

	publish ( status::EXITED, nullptr, 0, note );
	log_text ( note );

	AcquireSRWLockExclusive ( &lock );
	loaded.clear ( );
	ReleaseSRWLockExclusive ( &lock );

	threads.clear ( );
	points.clear ( );
	cover.clear ( );
	process = nullptr;
	process_id = 0;
	image_base = 0;

}

unsigned long __stdcall debugger::run ( void* ) {

	if ( attach_id ) {

		if ( !DebugActiveProcess ( attach_id ) ) {

			wchar_t note [ 200 ] = { };
			int32_t at = 0;

			append_text ( note, at, L"Couldn't attach to process " );
			append_number ( note, at, attach_id, false );
			append_text ( note, at, L" (Windows error " );
			append_number ( note, at, GetLastError ( ), false );
			append_text ( note, at, L")" );
			publish ( status::FAILED, nullptr, 0, note );

			return 0;

		}

		DebugSetProcessKillOnExit ( FALSE );
		publish ( status::RUNNING, nullptr, 0, L"Attached" );

	} else {

		STARTUPINFOW startup = { };
		PROCESS_INFORMATION info = { };
		wchar_t command [ 1700 ];
		wchar_t folder [ 600 ];
		int32_t length = 0;
		int32_t slash = 0;

		command [ length++ ] = L'"';

		for ( int32_t i = 0; target [ i ] && length < 690; i++ ) {

			command [ length++ ] = target [ i ];
			folder [ i ] = target [ i ];

			if ( target [ i ] == L'\\' || target [ i ] == L'/' )
				slash = i;

		}

		command [ length++ ] = L'"';

		if ( command_tail [ 0 ] ) {

			command [ length++ ] = L' ';

			for ( int32_t i = 0; command_tail [ i ] && length < 1690; i++ )
				command [ length++ ] = command_tail [ i ];

		}

		command [ length ] = 0;
		folder [ slash ] = 0;
		startup.cb = sizeof ( startup );

		if ( !CreateProcessW ( target, command, nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, nullptr, slash ? folder : nullptr, &startup, &info ) ) {

			wchar_t note [ 200 ] = { };
			int32_t at = 0;

			append_text ( note, at, L"Couldn't start the process (Windows error " );
			append_number ( note, at, GetLastError ( ), false );
			append_text ( note, at, L")" );
			publish ( status::FAILED, nullptr, 0, note );

			return 0;

		}

		CloseHandle ( info.hThread );
		CloseHandle ( info.hProcess );
		publish ( status::RUNNING, nullptr, 0, L"Process started" );

	}

	bool alive = true;
	bool detach = false;
	DWORD exit_code = 0;

	while ( alive ) {

		DEBUG_EVENT event = { };

		if ( !WaitForDebugEvent ( &event, 40 ) ) {

			long value = InterlockedExchange ( &pending, 0 );

			if ( value == static_cast < long > ( order::PAUSE ) && process )
				DebugBreakProcess ( process );
			else if ( value == static_cast < long > ( order::STOP ) && process )
				TerminateProcess ( process, 1 );
			else if ( value == static_cast < long > ( order::DETACH ) && process ) {

				release_traps ( );
				DebugActiveProcessStop ( process_id );
				finish ( L"Detached from the process" );

				return 0;

			}

			sync_points ( );

			continue;

		}

		DWORD reply = DBG_CONTINUE;
		bool pause = false;
		wchar_t note [ 200 ] = { };

		resume_flag = false;

		switch ( event.dwDebugEventCode ) {

			case CREATE_PROCESS_DEBUG_EVENT: {

				thread_entry& item = threads.emplace ( );

				process = event.u.CreateProcessInfo.hProcess;
				process_id = event.dwProcessId;
				item.id = event.dwThreadId;
				item.handle = event.u.CreateProcessInfo.hThread;

				uint64_t base = reinterpret_cast < uint64_t > ( event.u.CreateProcessInfo.lpBaseOfImage );

				add_module ( base, event.u.CreateProcessInfo.hFile );

				if ( event.u.CreateProcessInfo.hFile )
					CloseHandle ( event.u.CreateProcessInfo.hFile );

				if ( !module_name [ 0 ] )
					module_ready ( base );

				break;

			}

			case CREATE_THREAD_DEBUG_EVENT: {

				thread_entry& item = threads.emplace ( );

				item.id = event.dwThreadId;
				item.handle = event.u.CreateThread.hThread;
				debug_registers ( item.handle, false );

				break;

			}

			case EXIT_THREAD_DEBUG_EVENT:

				for ( int32_t i = 0; i < threads.count ( ); i++ ) {

					if ( threads [ static_cast < size_t > ( i ) ].id == event.dwThreadId ) {

						threads.remove_at ( static_cast < size_t > ( i ) );

						break;

					}

				}

				break;

			case LOAD_DLL_DEBUG_EVENT: {

				uint64_t base = reinterpret_cast < uint64_t > ( event.u.LoadDll.lpBaseOfDll );

				add_module ( base, event.u.LoadDll.hFile );

				AcquireSRWLockShared ( &lock );

				bool wanted_one = !loaded.empty ( ) && matches_module ( loaded.back ( ).path );

				ReleaseSRWLockShared ( &lock );

				if ( event.u.LoadDll.hFile )
					CloseHandle ( event.u.LoadDll.hFile );

				if ( wanted_one && !image_base )
					module_ready ( base );

				break;

			}

			case UNLOAD_DLL_DEBUG_EVENT: {

				uint64_t base = reinterpret_cast < uint64_t > ( event.u.UnloadDll.lpBaseOfDll );

				AcquireSRWLockExclusive ( &lock );

				for ( int32_t i = 0; i < loaded.count ( ); i++ ) {

					if ( loaded [ static_cast < size_t > ( i ) ].base == base ) {

						loaded.remove_at ( static_cast < size_t > ( i ) );

						break;

					}

				}

				ReleaseSRWLockExclusive ( &lock );

				if ( base == image_base ) {

					points.clear ( );
					cover.clear ( );
					image_base = 0;
					log_text ( L"The module being debugged was unloaded" );

				}

				break;

			}

			case OUTPUT_DEBUG_STRING_EVENT:
				read_debug_string ( event.u.DebugString );
				break;

			case EXCEPTION_DEBUG_EVENT: {

				const EXCEPTION_RECORD& record = event.u.Exception.ExceptionRecord;
				uint64_t at = reinterpret_cast < uint64_t > ( record.ExceptionAddress );
				DWORD code = record.ExceptionCode;
				HANDLE thread = thread_handle ( event.dwThreadId );

				if ( code == EXCEPTION_BREAKPOINT || code == 0x4000001F ) {

					int32_t hit = find_point ( at );
					int32_t covered = hit < 0 ? find_cover ( at ) : -1;

					if ( covered >= 0 && cover [ static_cast < size_t > ( covered ) ].armed ) {

						write_byte ( at, cover [ static_cast < size_t > ( covered ) ].original );
						record_cover ( cover [ static_cast < size_t > ( covered ) ] );
						adjust ( thread, true, at, false );

					} else if ( hit >= 0 && points [ static_cast < size_t > ( hit ) ].armed && !points [ static_cast < size_t > ( hit ) ].temporary && !condition_holds ( at, thread ) ) {

						disarm ( points [ static_cast < size_t > ( hit ) ] );
						adjust ( thread, true, at, true );
						reinsert = at;

					} else if ( hit >= 0 && points [ static_cast < size_t > ( hit ) ].armed ) {

						breakpoint& item = points [ static_cast < size_t > ( hit ) ];
						purpose use = item.use;

						adjust ( thread, true, at, false );

						if ( item.temporary ) {

							bool user_here = false;

							disarm ( item );
							points.remove_at ( static_cast < size_t > ( hit ) );

							for ( breakpoint& other : points ) {

								if ( other.address != at )
									continue;

								user_here = user_here || !other.temporary;
								disarm ( other );

							}

							if ( user_here )
								reinsert = at;

							if ( use == purpose::STEP_OUT && stepping_out ) {

								bool armed_call = false;

								if ( !step_out_next ( thread, armed_call ) ) {

									stepping_out = false;
									pause = true;
									lstrcpyW ( note, L"Stopped stepping out" );

								}

							} else {

								pause = true;

								if ( use == purpose::ENTRY && !entry_reached ) {

									entry_reached = true;
									lstrcpyW ( note, L"Stopped at the entry point" );

								} else if ( use == purpose::RUN_TO )
									lstrcpyW ( note, L"Reached the cursor" );
								else
									lstrcpyW ( note, L"Stepped over the call" );

							}

						} else {

							disarm ( item );
							reinsert = at;
							stepping_out = false;
							lstrcpyW ( note, L"Breakpoint hit" );
							pause = true;

						}

					} else if ( entry_reached ) {

						pause = true;
						lstrcpyW ( note, attach_id && !attach_announced ? L"Attached" : L"Paused" );
						attach_announced = true;

					}

				} else if ( code == EXCEPTION_SINGLE_STEP || code == 0x4000001E ) {

					int32_t watched = hardware_hit ( thread );

					if ( reinsert ) {

						int32_t again = find_point ( reinsert );

						if ( again >= 0 )
							arm ( points [ static_cast < size_t > ( again ) ] );

						reinsert = 0;

					}

					if ( watched >= 0 ) {

						static const wchar_t* const kinds [ 3 ] = { L"execute", L"write", L"access" };
						int32_t write_at = 0;

						debug_registers ( thread, true );
						stepping = false;
						stepping_out = false;
						pause = true;
						resume_flag = hardware [ watched ].kind == watch_kind::EXECUTE;
						append_text ( note, write_at, L"Hardware breakpoint " );
						append_number ( note, write_at, static_cast < uint64_t > ( watched + 1 ), false );
						append_text ( note, write_at, L" (" );
						append_text ( note, write_at, kinds [ static_cast < int32_t > ( hardware [ watched ].kind ) ] );
						append_text ( note, write_at, L")" );

					} else if ( stepping_out ) {

						bool armed_call = false;

						if ( out_ignore_step )
							out_ignore_step = false;
						else if ( out_finishing ) {

							stepping_out = false;
							out_finishing = false;
							pause = true;
							lstrcpyW ( note, L"Stepped out" );

						} else if ( !step_out_next ( thread, armed_call ) ) {

							stepping_out = false;
							pause = true;
							lstrcpyW ( note, L"Stopped stepping out" );

						}

					} else if ( stepping ) {

						stepping = false;
						pause = true;
						lstrcpyW ( note, L"Stepped" );

					}

				} else {

					static const wchar_t digits [ ] = L"0123456789ABCDEF";
					wchar_t* write = note;

					lstrcpyW ( note, event.u.Exception.dwFirstChance ? L"Exception 0x00000000, first chance" : L"Exception 0x00000000, second chance" );

					for ( int32_t i = 0; i < 8; i++ )
						write [ 12 + i ] = digits [ ( code >> ( 28 - i * 4 ) ) & 15 ];

					pause = true;
					stepping_out = false;
					reply = DBG_EXCEPTION_NOT_HANDLED;
					log_text ( note );

				}

				if ( pause ) {

					publish ( status::PAUSED, thread, event.dwThreadId, note );

					while ( true ) {

						WaitForSingleObject ( wake, 100 );
						sync_points ( );
						sync_hardware ( );

						long value = InterlockedExchange ( &pending, 0 );

						if ( value == static_cast < long > ( order::NONE ) || value == static_cast < long > ( order::PAUSE ) )
							continue;

						if ( value == static_cast < long > ( order::STOP ) ) {

							TerminateProcess ( process, 1 );

							break;

						}

						if ( value == static_cast < long > ( order::DETACH ) ) {

							detach = true;

							break;

						}

						bool trap = reinsert != 0;

						if ( value == static_cast < long > ( order::STEP_INTO ) ) {

							trap = true;
							stepping = true;

						} else if ( value == static_cast < long > ( order::STEP_OVER ) ) {

							uint64_t registers [ 18 ] = { };
							uint8_t bytes [ 16 ] = { };
							x86::instruction ins;

							get_registers ( thread, registers );

							if ( read_memory ( registers [ 16 ], bytes, 16 ) && x86::decode ( bytes, 16, registers [ 16 ], wide_target, ins ) && ins.control == x86::flow::CALL ) {

								breakpoint& item = points.emplace ( );

								item.address = registers [ 16 ] + ins.length;
								item.temporary = true;
								item.use = purpose::STEP_OVER;
								arm ( item );

							} else {

								trap = true;
								stepping = true;

							}

						} else if ( value == static_cast < long > ( order::STEP_OUT ) ) {

							bool armed_call = false;

							stepping_out = true;
							out_finishing = false;
							out_ignore_step = false;
							out_steps = 0;

							if ( !step_out_next ( thread, armed_call ) )
								stepping_out = false;

							if ( stepping_out && armed_call && reinsert )
								out_ignore_step = true;
							else if ( stepping_out )
								trap = false;

						} else if ( value == static_cast < long > ( order::RUN_TO ) && run_target ) {

							breakpoint& item = points.emplace ( );

							item.address = run_target;
							item.temporary = true;
							item.use = purpose::RUN_TO;
							arm ( item );

						}

						if ( trap )
							adjust ( thread, false, 0, true );
						else if ( resume_flag )
							adjust ( thread, false, 0, false );

						publish ( status::RUNNING, nullptr, 0, L"Running" );

						break;

					}

				}

				break;

			}

			case EXIT_PROCESS_DEBUG_EVENT:
				exit_code = event.u.ExitProcess.dwExitCode;
				alive = false;
				break;

			default:
				break;

		}

		if ( alive )
			sync_hardware ( );

		if ( detach ) {

			release_traps ( );
			ContinueDebugEvent ( event.dwProcessId, event.dwThreadId, reply );
			DebugActiveProcessStop ( process_id );
			finish ( L"Detached from the process" );

			return 0;

		}

		ContinueDebugEvent ( event.dwProcessId, event.dwThreadId, reply );

	}

	wchar_t note [ 80 ] = { };
	int32_t at = 0;

	append_text ( note, at, L"Process exited with code " );
	append_number ( note, at, exit_code, false );

	AcquireSRWLockExclusive ( &lock );
	shared.exit_code = exit_code;
	ReleaseSRWLockExclusive ( &lock );

	finish ( note );

	return 0;

}

bool debugger::launch ( uint32_t id, const wchar_t* path, const wchar_t* arguments, const wchar_t* module, uint64_t entry_rva, bool wide ) {

	if ( active ( ) )
		return false;

	if ( worker ) {

		WaitForSingleObject ( worker, 2000 );
		CloseHandle ( worker );
		worker = nullptr;

	}

	if ( !wake )
		wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );

	int32_t i = 0;

	for ( ; path && path [ i ] && i < 599; i++ )
		target [ i ] = path [ i ];

	target [ i ] = 0;
	i = 0;

	for ( ; arguments && arguments [ i ] && i < 999; i++ )
		command_tail [ i ] = arguments [ i ];

	command_tail [ i ] = 0;
	i = 0;

	for ( ; module && module [ i ] && i < 259; i++ )
		module_name [ i ] = module [ i ];

	module_name [ i ] = 0;
	attach_id = id;
	entry = entry_rva;
	wide_target = wide;
	reinsert = 0;
	stepping = false;
	stepping_out = false;
	entry_reached = id != 0;
	attach_announced = false;
	out_ignore_step = false;
	synced_serial = -1;
	hardware_synced = -1;
	pending = 0;
	image_base = 0;

	AcquireSRWLockExclusive ( &lock );

	uint32_t serial = shared.serial;

	shared = snapshot ( );
	shared.serial = serial + 1;
	shared.state = status::RUNNING;
	loaded.clear ( );
	ReleaseSRWLockExclusive ( &lock );

	worker = CreateThread ( nullptr, 0, run, nullptr, 0, nullptr );

	return worker != nullptr;

}

bool debugger::start ( const wchar_t* path, const wchar_t* arguments, const wchar_t* module, uint64_t entry_rva, bool wide ) {

	return launch ( 0, path, arguments, module, entry_rva, wide );

}

bool debugger::attach ( uint32_t process_id_value, const wchar_t* module, uint64_t entry_rva, bool wide ) {

	return process_id_value && launch ( process_id_value, L"", L"", module, entry_rva, wide );

}

void debugger::send ( order value ) {

	InterlockedExchange ( &pending, static_cast < long > ( value ) );

	if ( wake )
		SetEvent ( wake );

}

void debugger::run_to ( uint64_t rva ) {

	snapshot now = read ( );

	run_target = now.image_base ? now.image_base + rva : 0;
	send ( order::RUN_TO );

}

void debugger::set_breakpoints ( const uint64_t* rvas, int32_t count ) {

	AcquireSRWLockExclusive ( &lock );
	wanted.clear ( );

	for ( int32_t i = 0; i < count; i++ )
		wanted.push ( rvas [ i ] );

	ReleaseSRWLockExclusive ( &lock );
	InterlockedIncrement ( &wanted_serial );

	if ( wake )
		SetEvent ( wake );

}

void debugger::set_hardware ( const hardware_point* list, int32_t count ) {

	AcquireSRWLockExclusive ( &lock );

	for ( int32_t i = 0; i < 4; i++ )
		hardware_wanted [ i ] = i < count ? list [ i ] : hardware_point ( );

	ReleaseSRWLockExclusive ( &lock );
	InterlockedIncrement ( &hardware_serial );

	if ( wake )
		SetEvent ( wake );

}

void debugger::set_condition ( uint64_t rva, const wchar_t* text ) {

	AcquireSRWLockExclusive ( &lock );

	for ( int32_t i = conditions.count ( ) - 1; i >= 0; i-- ) {

		if ( conditions [ static_cast < size_t > ( i ) ].rva == rva )
			conditions.remove_at ( static_cast < size_t > ( i ) );

	}

	if ( text && text [ 0 ] ) {

		condition_entry& item = conditions.emplace ( );

		item.rva = rva;
		item.text.set ( text );

	}

	ReleaseSRWLockExclusive ( &lock );

}

void debugger::set_coverage ( const uint64_t* rvas, int32_t count ) {

	AcquireSRWLockExclusive ( &lock );
	cover_wanted.clear ( );
	cover_hits.clear ( );

	for ( int32_t i = 0; i < count; i++ )
		cover_wanted.push ( rvas [ i ] );

	ReleaseSRWLockExclusive ( &lock );

}

int32_t debugger::take_coverage ( base::list < uint64_t >& out ) {

	AcquireSRWLockExclusive ( &lock );

	int32_t count = cover_hits.count ( );

	for ( uint64_t rva : cover_hits )
		out.push ( rva );

	cover_hits.clear ( );
	ReleaseSRWLockExclusive ( &lock );

	return count;

}

bool debugger::peek ( uint64_t address, void* out, size_t size ) {

	if ( !process || read ( ).state != status::PAUSED )
		return false;

	if ( !read_memory ( address, out, size ) )
		return false;

	uint8_t* bytes = static_cast < uint8_t* > ( out );

	for ( const breakpoint& item : points ) {

		if ( item.armed && item.address >= address && item.address < address + size )
			bytes [ item.address - address ] = item.original;

	}

	for ( const cover_point& item : cover ) {

		if ( item.armed && item.address >= address && item.address < address + size )
			bytes [ item.address - address ] = item.original;

	}

	return true;

}

bool debugger::poke ( uint64_t address, const void* data, size_t size ) {

	if ( !process || read ( ).state != status::PAUSED || !size )
		return false;

	const uint8_t* bytes = static_cast < const uint8_t* > ( data );

	for ( size_t k = 0; k < size; k++ ) {

		uint64_t at = address + k;
		bool trapped = false;

		for ( breakpoint& item : points ) {

			if ( item.armed && item.address == at ) {

				item.original = bytes [ k ];
				trapped = true;

			}

		}

		for ( cover_point& item : cover ) {

			if ( item.armed && item.address == at ) {

				item.original = bytes [ k ];
				trapped = true;

			}

		}

		if ( !trapped && !write_memory ( at, bytes + k, 1 ) )
			return false;

	}

	return true;

}

int32_t debugger::modules ( base::list < module_info >& out ) {

	out.clear ( );

	AcquireSRWLockShared ( &lock );

	for ( const module_info& item : loaded )
		out.push ( item );

	ReleaseSRWLockShared ( &lock );

	return out.count ( );

}

int32_t debugger::processes ( base::list < process_entry >& out ) {

	out.clear ( );

	HANDLE snap = CreateToolhelp32Snapshot ( TH32CS_SNAPPROCESS, 0 );

	if ( snap == INVALID_HANDLE_VALUE )
		return 0;

	PROCESSENTRY32W item = { };

	item.dwSize = sizeof ( item );

	for ( BOOL more = Process32FirstW ( snap, &item ); more; more = Process32NextW ( snap, &item ) ) {

		process_entry& made = out.emplace ( );
		int32_t k = 0;

		made.id = item.th32ProcessID;

		for ( ; item.szExeFile [ k ] && k < 259; k++ )
			made.name [ k ] = item.szExeFile [ k ];

		made.name [ k ] = 0;

	}

	CloseHandle ( snap );

	return out.count ( );

}

bool debugger::parse_value ( const wchar_t*& at, uint64_t& value ) {

	bool hex = at [ 0 ] == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' );
	bool any = false;

	if ( hex )
		at += 2;

	value = 0;

	while ( *at ) {

		wchar_t c = *at;
		uint32_t digit = 0;

		if ( c >= L'0' && c <= L'9' )
			digit = static_cast < uint32_t > ( c - L'0' );
		else if ( hex && c >= L'a' && c <= L'f' )
			digit = static_cast < uint32_t > ( c - L'a' + 10 );
		else if ( hex && c >= L'A' && c <= L'F' )
			digit = static_cast < uint32_t > ( c - L'A' + 10 );
		else
			break;

		value = value * ( hex ? 16 : 10 ) + digit;
		any = true;
		at++;

	}

	return any;

}

bool debugger::test_condition ( const wchar_t* text, const uint64_t* registers, bool wide, bool& valid ) {

	const wchar_t* at = text;
	wchar_t name [ 16 ];
	int32_t length = 0;
	uint64_t left = 0;
	uint64_t right = 0;

	valid = false;

	while ( *at == L' ' )
		at++;

	while ( length < 15 && ( ( *at >= L'a' && *at <= L'z' ) || ( *at >= L'A' && *at <= L'Z' ) || ( *at >= L'0' && *at <= L'9' ) ) )
		name [ length++ ] = static_cast < wchar_t > ( *at >= L'a' && *at <= L'z' ? *at - 32 : *at ), at++;

	name [ length ] = 0;

	int32_t found = -1;

	for ( int32_t i = 0; i < 18; i++ ) {

		if ( base::same_text ( name, register_name ( i, wide ) ) )
			found = i;

	}

	if ( found < 0 )
		return false;

	left = registers [ found ];

	while ( *at == L' ' )
		at++;

	wchar_t op [ 3 ] = { };
	int32_t op_length = 0;

	while ( op_length < 2 && ( *at == L'=' || *at == L'!' || *at == L'<' || *at == L'>' ) )
		op [ op_length++ ] = *at++;

	while ( *at == L' ' )
		at++;

	if ( !op_length || !parse_value ( at, right ) )
		return false;

	valid = true;

	if ( base::same_text ( op, L"==" ) || base::same_text ( op, L"=" ) )
		return left == right;

	if ( base::same_text ( op, L"!=" ) )
		return left != right;

	if ( base::same_text ( op, L"<" ) )
		return left < right;

	if ( base::same_text ( op, L">" ) )
		return left > right;

	if ( base::same_text ( op, L"<=" ) )
		return left <= right;

	if ( base::same_text ( op, L">=" ) )
		return left >= right;

	valid = false;

	return false;

}

debugger::snapshot debugger::read ( ) {

	AcquireSRWLockShared ( &lock );

	snapshot value = shared;

	ReleaseSRWLockShared ( &lock );

	return value;

}

bool debugger::active ( ) {

	snapshot value = read ( );

	return value.state == status::RUNNING || value.state == status::PAUSED;

}

bool debugger::take_log ( wchar_t* out, int32_t room ) {

	AcquireSRWLockExclusive ( &lock );

	bool found = !messages.empty ( );

	if ( found ) {

		const wchar_t* text = messages [ 0 ].get ( );
		int32_t i = 0;

		for ( ; text [ i ] && i < room - 1; i++ )
			out [ i ] = text [ i ];

		out [ i ] = 0;
		messages.remove_at ( 0 );

	}

	ReleaseSRWLockExclusive ( &lock );

	return found;

}