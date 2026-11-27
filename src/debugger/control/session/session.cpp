#include "debugger/control/session/session.h"
#include "debugger/core/state/state.h"
#include "debugger/events/loop/loop.h"
#include <windows.h>

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