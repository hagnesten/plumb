#include "debugger/debugger.hh"
#include "x86/x86.hh"
#include <windows.h>

namespace debugger {

	struct breakpoint {

		uint64_t address = 0;
		uint8_t original = 0;
		bool armed = false;
		bool temporary = false;

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

		DWORD id = 0;
		HANDLE handle = nullptr;

	};

	static SRWLOCK lock = SRWLOCK_INIT;
	static HANDLE wake = nullptr;
	static HANDLE worker = nullptr;
	static snapshot shared;
	static volatile long pending = 0;
	static volatile long wanted_serial = 0;
	static base::list < uint64_t > wanted;
	static base::list < base::text > messages;
	static base::list < condition_entry > conditions;
	static base::list < uint64_t > cover_wanted;
	static base::list < uint64_t > cover_hits;
	static base::list < cover_point > cover;
	static wchar_t target [ 600 ];
	static uint64_t entry = 0;
	static bool wide_target = true;

	static HANDLE process = nullptr;
	static base::list < thread_entry > threads;
	static base::list < breakpoint > points;
	static uint64_t image_base = 0;
	static uint64_t reinsert = 0;
	static bool stepping = false;
	static bool entry_reached = false;
	static long synced_serial = -1;

	static const wchar_t* const names64 [ 18 ] = { L"RAX", L"RCX", L"RDX", L"RBX", L"RSP", L"RBP", L"RSI", L"RDI", L"R8", L"R9", L"R10", L"R11", L"R12", L"R13", L"R14", L"R15", L"RIP", L"RFLAGS" };
	static const wchar_t* const names32 [ 18 ] = { L"EAX", L"ECX", L"EDX", L"EBX", L"ESP", L"EBP", L"ESI", L"EDI", L"", L"", L"", L"", L"", L"", L"", L"", L"EIP", L"EFLAGS" };

	const wchar_t* register_name ( int32_t index, bool wide ) {

		if ( index < 0 || index >= 18 )
			return L"";

		return wide ? names64 [ index ] : names32 [ index ];

	}

	static void log_text ( const wchar_t* text ) {

		AcquireSRWLockExclusive ( &lock );

		if ( messages.count ( ) < 500 )
			messages.push ( base::text ( text ) );

		ReleaseSRWLockExclusive ( &lock );

	}

	static HANDLE thread_handle ( DWORD id ) {

		for ( const thread_entry& item : threads ) {

			if ( item.id == id )
				return item.handle;

		}

		return nullptr;

	}

	static bool read_memory ( uint64_t address, void* out, size_t size ) {

		SIZE_T done = 0;

		return ReadProcessMemory ( process, reinterpret_cast < const void* > ( address ), out, size, &done ) && done == size;

	}

	static bool write_byte ( uint64_t address, uint8_t value ) {

		SIZE_T done = 0;
		DWORD old = 0;
		void* at = reinterpret_cast < void* > ( address );

		VirtualProtectEx ( process, at, 1, PAGE_EXECUTE_READWRITE, &old );

		bool ok = WriteProcessMemory ( process, at, &value, 1, &done ) && done == 1;

		VirtualProtectEx ( process, at, 1, old, &old );
		FlushInstructionCache ( process, at, 1 );

		return ok;

	}

	static int32_t find_cover ( uint64_t address ) {

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

	static void record_cover ( cover_point& item ) {

		item.armed = false;

		AcquireSRWLockExclusive ( &lock );
		cover_hits.push ( item.address - image_base );
		ReleaseSRWLockExclusive ( &lock );

	}

	static void arm ( breakpoint& item ) {

		if ( item.armed )
			return;

		int32_t shared_point = find_cover ( item.address );

		if ( shared_point >= 0 && cover [ static_cast < size_t > ( shared_point ) ].armed ) {

			item.original = cover [ static_cast < size_t > ( shared_point ) ].original;
			record_cover ( cover [ static_cast < size_t > ( shared_point ) ] );
			item.armed = true;

			return;

		}

		if ( read_memory ( item.address, &item.original, 1 ) && write_byte ( item.address, 0xCC ) )
			item.armed = true;

	}

	static void arm_coverage ( ) {

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

	static bool get_registers ( HANDLE thread, uint64_t* values );

	static bool condition_holds ( uint64_t address, HANDLE thread ) {

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

	static void disarm ( breakpoint& item ) {

		if ( !item.armed )
			return;

		write_byte ( item.address, item.original );
		item.armed = false;

	}

	static int32_t find_point ( uint64_t address ) {

		for ( int32_t i = 0; i < points.count ( ); i++ ) {

			if ( points [ static_cast < size_t > ( i ) ].address == address )
				return i;

		}

		return -1;

	}

	static void sync_points ( ) {

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

			if ( find_point ( address ) >= 0 )
				continue;

			breakpoint& item = points.emplace ( );

			item.address = address;

			if ( address != reinsert )
				arm ( item );

		}

	}

	static bool get_registers ( HANDLE thread, uint64_t* values ) {

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

	static void adjust ( HANDLE thread, bool set_pc, uint64_t pc, bool trap ) {

		if ( wide_target ) {

			CONTEXT context = { };

			context.ContextFlags = CONTEXT_FULL;

			if ( !GetThreadContext ( thread, &context ) )
				return;

			if ( set_pc )
				context.Rip = pc;

			if ( trap )
				context.EFlags |= 0x100;

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

		Wow64SetThreadContext ( thread, &context );

	}

	static void publish ( status state, HANDLE thread, const wchar_t* note ) {

		snapshot value;

		AcquireSRWLockShared ( &lock );
		value = shared;
		ReleaseSRWLockShared ( &lock );

		value.state = state;
		value.image_base = image_base;
		value.wide = wide_target;

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

	static void read_debug_string ( const OUTPUT_DEBUG_STRING_INFO& info ) {

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

	static unsigned long __stdcall run ( void* ) {

		STARTUPINFOW startup = { };
		PROCESS_INFORMATION info = { };
		wchar_t command [ 700 ];
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
		command [ length ] = 0;
		folder [ slash ] = 0;
		startup.cb = sizeof ( startup );

		if ( !CreateProcessW ( target, command, nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, nullptr, slash ? folder : nullptr, &startup, &info ) ) {

			wchar_t note [ 120 ] = L"Couldn't start the process (Windows error ";
			int32_t at = 42;
			DWORD error = GetLastError ( );
			wchar_t digits [ 12 ];
			int32_t n = 0;

			do {

				digits [ n++ ] = static_cast < wchar_t > ( L'0' + error % 10 );
				error /= 10;

			} while ( error );

			while ( n )
				note [ at++ ] = digits [ --n ];

			note [ at++ ] = L')';
			note [ at ] = 0;
			publish ( status::FAILED, nullptr, note );

			return 0;

		}

		CloseHandle ( info.hThread );
		CloseHandle ( info.hProcess );
		publish ( status::RUNNING, nullptr, L"Process started" );

		bool alive = true;
		DWORD exit_code = 0;

		while ( alive ) {

			DEBUG_EVENT event = { };

			if ( !WaitForDebugEvent ( &event, 40 ) ) {

				long value = InterlockedExchange ( &pending, 0 );

				if ( value == static_cast < long > ( order::PAUSE ) && process )
					DebugBreakProcess ( process );
				else if ( value == static_cast < long > ( order::STOP ) && process )
					TerminateProcess ( process, 1 );

				sync_points ( );

				continue;

			}

			DWORD reply = DBG_CONTINUE;
			bool pause = false;
			wchar_t note [ 200 ] = { };

			switch ( event.dwDebugEventCode ) {

				case CREATE_PROCESS_DEBUG_EVENT: {

					thread_entry& item = threads.emplace ( );

					process = event.u.CreateProcessInfo.hProcess;
					image_base = reinterpret_cast < uint64_t > ( event.u.CreateProcessInfo.lpBaseOfImage );
					item.id = event.dwThreadId;
					item.handle = event.u.CreateProcessInfo.hThread;

					if ( event.u.CreateProcessInfo.hFile )
						CloseHandle ( event.u.CreateProcessInfo.hFile );

					breakpoint& start_point = points.emplace ( );

					start_point.address = image_base + entry;
					start_point.temporary = true;
					arm_coverage ( );
					arm ( start_point );
					synced_serial = -1;
					sync_points ( );

					break;

				}

				case CREATE_THREAD_DEBUG_EVENT: {

					thread_entry& item = threads.emplace ( );

					item.id = event.dwThreadId;
					item.handle = event.u.CreateThread.hThread;

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

				case LOAD_DLL_DEBUG_EVENT:

					if ( event.u.LoadDll.hFile )
						CloseHandle ( event.u.LoadDll.hFile );

					break;

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
							bool temporary = item.temporary;

							disarm ( item );
							adjust ( thread, true, at, false );

							if ( temporary ) {

								if ( item.address == image_base + entry && !entry_reached ) {

									entry_reached = true;
									lstrcpyW ( note, L"Stopped at the entry point" );

								} else
									lstrcpyW ( note, L"Stepped over the call" );

								points.remove_at ( static_cast < size_t > ( hit ) );

							} else {

								reinsert = at;
								lstrcpyW ( note, L"Breakpoint hit" );

							}

							pause = true;

						} else if ( entry_reached ) {

							pause = true;
							lstrcpyW ( note, L"Paused" );

						}

					} else if ( code == EXCEPTION_SINGLE_STEP || code == 0x4000001E ) {

						if ( reinsert ) {

							int32_t again = find_point ( reinsert );

							if ( again >= 0 )
								arm ( points [ static_cast < size_t > ( again ) ] );

							reinsert = 0;

						}

						if ( stepping ) {

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
						reply = DBG_EXCEPTION_NOT_HANDLED;
						log_text ( note );

					}

					if ( pause ) {

						publish ( status::PAUSED, thread, note );

						while ( true ) {

							WaitForSingleObject ( wake, 100 );
							sync_points ( );

							long value = InterlockedExchange ( &pending, 0 );

							if ( value == static_cast < long > ( order::NONE ) || value == static_cast < long > ( order::PAUSE ) )
								continue;

							if ( value == static_cast < long > ( order::STOP ) ) {

								TerminateProcess ( process, 1 );

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
									arm ( item );

								} else {

									trap = true;
									stepping = true;

								}

							}

							if ( trap )
								adjust ( thread, false, 0, true );

							publish ( status::RUNNING, nullptr, L"Running" );

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

			ContinueDebugEvent ( event.dwProcessId, event.dwThreadId, reply );

		}

		wchar_t note [ 80 ] = L"Process exited with code ";
		int32_t at = 25;
		wchar_t digits [ 12 ];
		int32_t n = 0;
		DWORD value = exit_code;

		do {

			digits [ n++ ] = static_cast < wchar_t > ( L'0' + value % 10 );
			value /= 10;

		} while ( value );

		while ( n )
			note [ at++ ] = digits [ --n ];

		note [ at ] = 0;

		AcquireSRWLockExclusive ( &lock );
		shared.exit_code = exit_code;
		ReleaseSRWLockExclusive ( &lock );

		publish ( status::EXITED, nullptr, note );
		log_text ( note );
		threads.clear ( );
		points.clear ( );
		cover.clear ( );
		process = nullptr;
		image_base = 0;

		return 0;

	}

	bool start ( const wchar_t* path, uint64_t entry_rva, bool wide ) {

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

		for ( ; path [ i ] && i < 599; i++ )
			target [ i ] = path [ i ];

		target [ i ] = 0;
		entry = entry_rva;
		wide_target = wide;
		reinsert = 0;
		stepping = false;
		entry_reached = false;
		synced_serial = -1;
		pending = 0;

		AcquireSRWLockExclusive ( &lock );

		uint32_t serial = shared.serial;

		shared = snapshot ( );
		shared.serial = serial + 1;
		shared.state = status::RUNNING;
		ReleaseSRWLockExclusive ( &lock );

		worker = CreateThread ( nullptr, 0, run, nullptr, 0, nullptr );

		return worker != nullptr;

	}

	void send ( order value ) {

		InterlockedExchange ( &pending, static_cast < long > ( value ) );

		if ( wake )
			SetEvent ( wake );

	}

	void set_breakpoints ( const uint64_t* rvas, int32_t count ) {

		AcquireSRWLockExclusive ( &lock );
		wanted.clear ( );

		for ( int32_t i = 0; i < count; i++ )
			wanted.push ( rvas [ i ] );

		ReleaseSRWLockExclusive ( &lock );
		InterlockedIncrement ( &wanted_serial );

		if ( wake )
			SetEvent ( wake );

	}

	void set_condition ( uint64_t rva, const wchar_t* text ) {

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

	void set_coverage ( const uint64_t* rvas, int32_t count ) {

		AcquireSRWLockExclusive ( &lock );
		cover_wanted.clear ( );
		cover_hits.clear ( );

		for ( int32_t i = 0; i < count; i++ )
			cover_wanted.push ( rvas [ i ] );

		ReleaseSRWLockExclusive ( &lock );

	}

	int32_t take_coverage ( base::list < uint64_t >& out ) {

		AcquireSRWLockExclusive ( &lock );

		int32_t count = cover_hits.count ( );

		for ( uint64_t rva : cover_hits )
			out.push ( rva );

		cover_hits.clear ( );
		ReleaseSRWLockExclusive ( &lock );

		return count;

	}

	bool peek ( uint64_t address, void* out, size_t size ) {

		if ( !process || read ( ).state != status::PAUSED )
			return false;

		return read_memory ( address, out, size );

	}

	static bool parse_value ( const wchar_t*& at, uint64_t& value ) {

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

	bool test_condition ( const wchar_t* text, const uint64_t* registers, bool wide, bool& valid ) {

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

	snapshot read ( ) {

		AcquireSRWLockShared ( &lock );

		snapshot value = shared;

		ReleaseSRWLockShared ( &lock );

		return value;

	}

	bool active ( ) {

		snapshot value = read ( );

		return value.state == status::RUNNING || value.state == status::PAUSED;

	}

	bool take_log ( wchar_t* out, int32_t room ) {

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

}