#include "debugger/events/lifetime/lifetime.h"
#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include <windows.h>

void debugger::process_created ( const DEBUG_EVENT& event ) {

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

}

void debugger::thread_created ( const DEBUG_EVENT& event ) {

	thread_entry& item = threads.emplace ( );

	item.id = event.dwThreadId;
	item.handle = event.u.CreateThread.hThread;
	debug_registers ( item.handle, false );

}

void debugger::thread_exited ( unsigned long id ) {

	for ( int32_t i = 0; i < threads.count ( ); i++ ) {

		if ( threads [ static_cast < size_t > ( i ) ].id == id ) {

			threads.remove_at ( static_cast < size_t > ( i ) );

			return;

		}

	}

}

void debugger::dll_loaded ( const LOAD_DLL_DEBUG_INFO& info ) {

	uint64_t base = reinterpret_cast < uint64_t > ( info.lpBaseOfDll );

	add_module ( base, info.hFile );

	AcquireSRWLockShared ( &lock );

	bool wanted_one = !loaded.empty ( ) && matches_module ( loaded.back ( ).path );

	ReleaseSRWLockShared ( &lock );

	if ( info.hFile )
		CloseHandle ( info.hFile );

	if ( wanted_one && !image_base )
		module_ready ( base );

}

void debugger::dll_unloaded ( const UNLOAD_DLL_DEBUG_INFO& info ) {

	uint64_t base = reinterpret_cast < uint64_t > ( info.lpBaseOfDll );

	AcquireSRWLockExclusive ( &lock );

	for ( int32_t i = 0; i < loaded.count ( ); i++ ) {

		if ( loaded [ static_cast < size_t > ( i ) ].base == base ) {

			loaded.remove_at ( static_cast < size_t > ( i ) );

			break;

		}

	}

	ReleaseSRWLockExclusive ( &lock );

	if ( base != image_base )
		return;

	points.clear ( );
	cover.clear ( );
	image_base = 0;
	log_text ( L"The module being debugged was unloaded" );

}