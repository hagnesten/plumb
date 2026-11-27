#include "debugger/traps/hardware/hardware.h"
#include "debugger/core/state/state.h"
#include <windows.h>

const wchar_t* const debugger::watch_names [ 3 ] = { L"execute", L"write", L"access" };

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

void debugger::set_hardware ( const hardware_point* list, int32_t count ) {

	AcquireSRWLockExclusive ( &lock );

	for ( int32_t i = 0; i < 4; i++ )
		hardware_wanted [ i ] = i < count ? list [ i ] : hardware_point ( );

	ReleaseSRWLockExclusive ( &lock );
	InterlockedIncrement ( &hardware_serial );

	if ( wake )
		SetEvent ( wake );

}