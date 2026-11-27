#include "host/pacing/pacing.h"
#include "host/state/state.h"
#include "base/utility/utility.h"
#include <windows.h>
#include <dwmapi.h>
#include <timeapi.h>

void host::measure_refresh ( ) {

	MONITORINFOEXW info = { };
	DEVMODEW mode = { };

	info.cbSize = sizeof ( info );
	mode.dmSize = sizeof ( mode );

	if ( GetMonitorInfoW ( MonitorFromWindow ( main_window, MONITOR_DEFAULTTONEAREST ), &info ) && EnumDisplaySettingsW ( info.szDevice, ENUM_CURRENT_SETTINGS, &mode ) && mode.dmDisplayFrequency > 1 )
		refresh_rate = static_cast < int32_t > ( mode.dmDisplayFrequency );

}

int64_t host::clock_now ( ) {

	LARGE_INTEGER value;

	QueryPerformanceCounter ( &value );

	return value.QuadPart;

}

DWORD host::frame_wait ( ) {

	if ( !frame_armed )
		return 250;

	LARGE_INTEGER frequency;

	QueryPerformanceFrequency ( &frequency );

	int64_t left = frame_due - clock_now ( );

	if ( left <= 0 )
		return 0;

	return static_cast < DWORD > ( base::min ( left * 1000 / frequency.QuadPart + 1, static_cast < int64_t > ( 250 ) ) );

}

void host::schedule_frame ( ) {

	LARGE_INTEGER frequency;
	DWM_TIMING_INFO timing = { };
	int64_t now = clock_now ( );

	QueryPerformanceFrequency ( &frequency );
	timing.cbSize = sizeof ( timing );

	int64_t period = frequency.QuadPart / base::max ( refresh_rate, 30 );
	int64_t vblank = now;

	if ( SUCCEEDED ( DwmGetCompositionTimingInfo ( nullptr, &timing ) ) && timing.qpcRefreshPeriod > 0 ) {

		period = static_cast < int64_t > ( timing.qpcRefreshPeriod );
		vblank = static_cast < int64_t > ( timing.qpcVBlank );

	}

	int64_t earliest = base::max ( now + period / 20, frame_last + period - period / 2 );
	int64_t target = vblank;

	if ( target < earliest )
		target += ( ( earliest - target ) / period + 1 ) * period;

	if ( !fine_timer ) {

		timeBeginPeriod ( 1 );
		fine_timer = true;

	}

	if ( frame_armed && frame_due <= target + period / 4 )
		return;

	LARGE_INTEGER due;

	due.QuadPart = -base::max ( static_cast < int64_t > ( static_cast < double > ( target - now ) * 10000000.0 / static_cast < double > ( frequency.QuadPart ) ), static_cast < int64_t > ( 1 ) );
	SetWaitableTimer ( frame_timer, &due, 0, nullptr, nullptr, FALSE );
	frame_armed = true;
	frame_due = target;

}