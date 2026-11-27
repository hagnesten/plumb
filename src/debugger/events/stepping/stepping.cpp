#include "debugger/events/stepping/stepping.h"
#include "debugger/core/state/state.h"
#include "debugger/events/publish/publish.h"
#include "debugger/target/memory/memory.h"
#include "x86/decoder/decoder.h"
#include <windows.h>

bool debugger::wait_for_order ( HANDLE thread, DWORD thread_id, const wchar_t* note ) {

	publish ( status::PAUSED, thread, thread_id, note );

	while ( true ) {

		WaitForSingleObject ( wake, 100 );
		sync_points ( );
		sync_hardware ( );

		long value = InterlockedExchange ( &pending, 0 );

		if ( value == static_cast < long > ( order::NONE ) || value == static_cast < long > ( order::PAUSE ) )
			continue;

		if ( value == static_cast < long > ( order::STOP ) ) {

			TerminateProcess ( process, 1 );

			return false;

		}

		if ( value == static_cast < long > ( order::DETACH ) )
			return true;

		resume ( thread, static_cast < order > ( value ) );

		return false;

	}

}

void debugger::resume ( HANDLE thread, order value ) {

	bool trap = reinsert != 0;

	if ( value == order::STEP_INTO ) {

		trap = true;
		stepping = true;

	} else if ( value == order::STEP_OVER )
		trap = step_over ( thread, trap );
	else if ( value == order::STEP_OUT )
		trap = step_out ( thread, trap );
	else if ( value == order::RUN_TO && run_target )
		arm_temporary ( run_target, purpose::RUN_TO );

	if ( trap )
		adjust ( thread, false, 0, true );
	else if ( resume_flag )
		adjust ( thread, false, 0, false );

	publish ( status::RUNNING, nullptr, 0, L"Running" );

}

bool debugger::step_over ( HANDLE thread, bool trap ) {

	uint64_t registers [ 18 ] = { };
	uint8_t bytes [ 16 ] = { };
	x86::instruction ins;

	get_registers ( thread, registers );

	if ( read_memory ( registers [ 16 ], bytes, 16 ) && x86::decode ( bytes, 16, registers [ 16 ], wide_target, ins ) && ins.control == x86::flow::CALL ) {

		arm_temporary ( registers [ 16 ] + ins.length, purpose::STEP_OVER );

		return trap;

	}

	stepping = true;

	return true;

}

bool debugger::step_out ( HANDLE thread, bool trap ) {

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

	return trap;

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
		arm_temporary ( registers [ 16 ] + ins.length, purpose::STEP_OUT );

		return true;

	}

	if ( ins.control == x86::flow::RETURN )
		out_finishing = true;

	adjust ( thread, false, 0, true );

	return true;

}