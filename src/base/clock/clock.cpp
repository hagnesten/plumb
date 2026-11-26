#include "base/clock/clock.h"
#include <windows.h>

double base::seconds ( ) {

	static int64_t frequency = 0;
	LARGE_INTEGER now;

	if ( !frequency ) {

		LARGE_INTEGER value;

		QueryPerformanceFrequency ( &value );
		frequency = value.QuadPart;

	}

	QueryPerformanceCounter ( &now );

	return static_cast < double > ( now.QuadPart ) / static_cast < double > ( frequency );

}

uint64_t base::ticks ( ) {

	LARGE_INTEGER now;

	QueryPerformanceCounter ( &now );

	return static_cast < uint64_t > ( now.QuadPart );

}

void base::pause ( int32_t milliseconds ) {

	Sleep ( static_cast < DWORD > ( milliseconds ) );

}