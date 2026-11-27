#include "host/dpi/dpi.h"
#include <windows.h>

host::window_dpi_call host::dpi_of_window = nullptr;

host::system_dpi_call host::dpi_of_system = nullptr;

host::metric_dpi_call host::metric_of_dpi = nullptr;

host::monitor_dpi_call host::dpi_of_monitor = nullptr;

void* host::find_export ( HMODULE module, const char* name ) {

	return module ? reinterpret_cast < void* > ( GetProcAddress ( module, name ) ) : nullptr;

}

void host::load_dpi ( ) {

	HMODULE user = GetModuleHandleW ( L"user32.dll" );
	HMODULE shell = LoadLibraryExW ( L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
	context_call context = reinterpret_cast < context_call > ( find_export ( user, "SetProcessDpiAwarenessContext" ) );
	awareness_call awareness = reinterpret_cast < awareness_call > ( find_export ( shell, "SetProcessDpiAwareness" ) );

	dpi_of_window = reinterpret_cast < window_dpi_call > ( find_export ( user, "GetDpiForWindow" ) );
	dpi_of_system = reinterpret_cast < system_dpi_call > ( find_export ( user, "GetDpiForSystem" ) );
	metric_of_dpi = reinterpret_cast < metric_dpi_call > ( find_export ( user, "GetSystemMetricsForDpi" ) );
	dpi_of_monitor = reinterpret_cast < monitor_dpi_call > ( find_export ( shell, "GetDpiForMonitor" ) );

	if ( context && context ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ) )
		return;

	if ( awareness && SUCCEEDED ( awareness ( 2 ) ) )
		return;

	SetProcessDPIAware ( );

}

UINT host::system_dpi ( ) {

	if ( dpi_of_system )
		return dpi_of_system ( );

	HDC screen = GetDC ( nullptr );
	int32_t dpi = screen ? GetDeviceCaps ( screen, LOGPIXELSX ) : 96;

	if ( screen )
		ReleaseDC ( nullptr, screen );

	return dpi > 0 ? static_cast < UINT > ( dpi ) : 96;

}

UINT host::window_dpi ( HWND window ) {

	UINT dpi = dpi_of_window ? dpi_of_window ( window ) : 0;
	UINT x = 0;
	UINT y = 0;

	if ( !dpi && dpi_of_monitor && SUCCEEDED ( dpi_of_monitor ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), 0, &x, &y ) ) )
		dpi = x;

	return dpi ? dpi : system_dpi ( );

}

int32_t host::scaled_metric ( int32_t index, UINT dpi ) {

	if ( metric_of_dpi )
		return metric_of_dpi ( index, dpi );

	return MulDiv ( GetSystemMetrics ( index ), static_cast < int32_t > ( dpi ), static_cast < int32_t > ( system_dpi ( ) ) );

}

int32_t host::frame_thickness ( HWND window ) {

	UINT dpi = window_dpi ( window );

	return scaled_metric ( SM_CYFRAME, dpi ) + scaled_metric ( SM_CXPADDEDBORDER, dpi );

}