#include "host/loop/loop.h"
#include "host/state/state.h"
#include "host/pacing/pacing.h"
#include "host/painting/painting.h"
#include "base/utility/utility.h"
#include "base/clock/clock.h"
#include <windows.h>
#include <timeapi.h>

void host::run_loop ( ) {

	while ( true ) {

		check_device ( );

		bool visible = !IsIconic ( main_window );
		bool need = visible && program->busy ( );
		DWORD result = need ? wait_for_frame ( ) : wait_idle ( visible );

		if ( !pump_messages ( ) )
			break;

		bool tick = frame_tick ( need, result );

		if ( visible && program->busy ( ) && tick )
			render_main ( );

	}

}

void host::check_device ( ) {

	if ( !device_lost && !device->healthy ( ) )
		device_lost = true;

	if ( device_lost && base::seconds ( ) >= device_retry )
		recover_device ( );

}

DWORD host::wait_for_frame ( ) {

	schedule_frame ( );

	return MsgWaitForMultipleObjectsEx ( 1, &frame_timer, frame_wait ( ), QS_ALLINPUT, MWMO_INPUTAVAILABLE );

}

DWORD host::wait_idle ( bool visible ) {

	if ( frame_armed ) {

		CancelWaitableTimer ( frame_timer );
		frame_armed = false;

	}

	if ( fine_timer ) {

		timeEndPeriod ( 1 );
		fine_timer = false;

	}

	double wake = program->wake_after ( );
	DWORD timeout = device_lost ? 250 : ( wake > 0.0 && visible ? static_cast < DWORD > ( wake * 1000.0 ) : INFINITE );
	DWORD result = MsgWaitForMultipleObjectsEx ( 0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE );

	if ( result == WAIT_TIMEOUT && visible )
		program->pending = base::max ( program->pending, 1 );

	return result;

}

bool host::pump_messages ( ) {

	MSG message;

	while ( PeekMessageW ( &message, nullptr, 0, 0, PM_REMOVE ) ) {

		if ( message.message == WM_QUIT )
			return false;

		TranslateMessage ( &message );
		DispatchMessageW ( &message );

	}

	return true;

}

bool host::frame_tick ( bool need, DWORD result ) {

	bool late = frame_armed && clock_now ( ) >= frame_due;
	bool tick = !need || result == WAIT_OBJECT_0 || late || WaitForSingleObject ( frame_timer, 0 ) == WAIT_OBJECT_0;

	if ( need && tick ) {

		frame_last = clock_now ( );

		if ( frame_armed )
			CancelWaitableTimer ( frame_timer );

		frame_armed = false;

	}

	return tick;

}