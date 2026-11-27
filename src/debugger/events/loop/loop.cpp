#include "debugger/events/loop/loop.h"
#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include "debugger/events/begin/begin.h"
#include "debugger/events/exceptions/exceptions.h"
#include "debugger/events/lifetime/lifetime.h"
#include "debugger/events/publish/publish.h"
#include <windows.h>

unsigned long __stdcall debugger::run ( void* ) {

	if ( !begin_target ( ) )
		return 0;

	event_outcome outcome;

	while ( !outcome.exited ) {

		DEBUG_EVENT event = { };

		if ( !WaitForDebugEvent ( &event, 40 ) ) {

			if ( !idle_order ( ) )
				return 0;

			continue;

		}

		outcome.reply = DBG_CONTINUE;
		resume_flag = false;
		handle_event ( event, outcome );

		if ( !outcome.exited )
			sync_hardware ( );

		if ( outcome.detach ) {

			release_traps ( );
			ContinueDebugEvent ( event.dwProcessId, event.dwThreadId, outcome.reply );
			end_detached ( );

			return 0;

		}

		ContinueDebugEvent ( event.dwProcessId, event.dwThreadId, outcome.reply );

	}

	report_exit ( outcome.exit_code );

	return 0;

}

bool debugger::idle_order ( ) {

	long value = InterlockedExchange ( &pending, 0 );

	if ( value == static_cast < long > ( order::PAUSE ) && process )
		DebugBreakProcess ( process );
	else if ( value == static_cast < long > ( order::STOP ) && process )
		TerminateProcess ( process, 1 );
	else if ( value == static_cast < long > ( order::DETACH ) && process ) {

		release_traps ( );
		end_detached ( );

		return false;

	}

	sync_points ( );

	return true;

}

void debugger::end_detached ( ) {

	DebugActiveProcessStop ( process_id );
	finish ( L"Detached from the process" );

}

void debugger::handle_event ( const DEBUG_EVENT& event, event_outcome& outcome ) {

	switch ( event.dwDebugEventCode ) {

		case CREATE_PROCESS_DEBUG_EVENT:
			process_created ( event );
			break;

		case CREATE_THREAD_DEBUG_EVENT:
			thread_created ( event );
			break;

		case EXIT_THREAD_DEBUG_EVENT:
			thread_exited ( event.dwThreadId );
			break;

		case LOAD_DLL_DEBUG_EVENT:
			dll_loaded ( event.u.LoadDll );
			break;

		case UNLOAD_DLL_DEBUG_EVENT:
			dll_unloaded ( event.u.UnloadDll );
			break;

		case OUTPUT_DEBUG_STRING_EVENT:
			read_debug_string ( event.u.DebugString );
			break;

		case EXCEPTION_DEBUG_EVENT:
			exception_raised ( event, outcome );
			break;

		case EXIT_PROCESS_DEBUG_EVENT:
			outcome.exit_code = event.u.ExitProcess.dwExitCode;
			outcome.exited = true;
			break;

		default:
			break;

	}

}

void debugger::report_exit ( unsigned long exit_code ) {

	wchar_t note [ 80 ] = { };
	int32_t at = 0;

	append_text ( note, at, L"Process exited with code " );
	append_number ( note, at, exit_code, false );

	AcquireSRWLockExclusive ( &lock );
	shared.exit_code = exit_code;
	ReleaseSRWLockExclusive ( &lock );

	finish ( note );

}