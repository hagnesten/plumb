#include "debugger/events/publish/publish.h"
#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include "debugger/target/memory/memory.h"
#include <windows.h>

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