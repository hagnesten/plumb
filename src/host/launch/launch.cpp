#include "host/launch/launch.h"
#include "host/headless/capture.h"
#include "host/state/state.h"
#include "host/dpi/dpi.h"
#include "host/pacing/pacing.h"
#include "host/painting/painting.h"
#include "host/window/window.h"
#include "base/utility/utility.h"
#include "base/strings/strings.h"
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>

void host::begin ( ) {

	load_dpi ( );
	CoInitializeEx ( nullptr, COINIT_APARTMENTTHREADED );

}

bool host::run_debug_modes ( ) {

#ifdef PLUMB_DEBUG

	return debug_main ( );

#else

	return false;

#endif

}

host::launch_options host::read_options ( ) {

	launch_options options;
	int32_t count = 0;

	options.arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );

	for ( int32_t i = 1; options.arguments && i < count; i++ ) {

		const wchar_t* item = options.arguments [ i ];

		if ( base::same_text ( item, L"--software" ) )
			software_only = true;
		else if ( base::same_text ( item, L"--mcp" ) )
			options.serve = true;
		else if ( item [ 0 ] != L'-' )
			options.open_path = item;

	}

	return options;

}

void host::release_options ( launch_options& options ) {

	if ( options.arguments )
		LocalFree ( options.arguments );

	options.arguments = nullptr;
	options.open_path = nullptr;

}

int32_t host::serve_mcp ( launch_options& options ) {

	ui::app* server = new ui::app ( );

	server->setup ( nullptr, nullptr );

	if ( options.open_path )
		server->open_file ( options.open_path );

	release_options ( options );

	int32_t code = server->serve_mcp ( );

	CoUninitialize ( );

	return code;

}

bool host::start_window ( launch_options& options ) {

	if ( !start_renderer ( ) )
		return false;

	register_window_class ( );
	create_program ( );

	if ( !create_main_window ( ) )
		return false;

	style_main_window ( );

	if ( !program->setup ( device, main_window ) ) {

		fail ( L"Plumb could not set up its interface." );

		return false;

	}

	apply_dark_frame ( );
	restore_placement ( );
	SetWindowPos ( main_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

	if ( !attach_surface ( ) )
		return false;

	start_frame_timer ( );
	measure_refresh ( );
	sync_size ( );

	if ( options.open_path )
		program->open_file ( options.open_path );

	release_options ( options );
	show_main_window ( );

	return true;

}

bool host::start_renderer ( ) {

	device = new render::gpu ( );

	if ( device->setup ( software_only ) )
		return true;

	fail ( L"Plumb could not start its renderer." );

	return false;

}

void host::register_window_class ( ) {

	HINSTANCE instance = GetModuleHandleW ( nullptr );
	WNDCLASSEXW type = { };

	type.cbSize = sizeof ( type );
	type.lpfnWndProc = &procedure;
	type.hInstance = instance;
	type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
	type.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
	type.hIconSm = static_cast < HICON > ( LoadImageW ( instance, MAKEINTRESOURCEW ( 1 ), IMAGE_ICON, GetSystemMetrics ( SM_CXSMICON ), GetSystemMetrics ( SM_CYSMICON ), 0 ) );
	type.lpszClassName = L"plumb";
	type.style = CS_DBLCLKS;
	RegisterClassExW ( &type );

}

void host::create_program ( ) {

	program = new ui::app ( );
	program->light = system_light ( );
	program->load_settings ( );

}

bool host::create_main_window ( ) {

	float scale = static_cast < float > ( system_dpi ( ) ) / 96.0f;
	RECT work = { };

	SystemParametersInfoW ( SPI_GETWORKAREA, 0, &work, 0 );

	int32_t frame = scaled_metric ( SM_CXFRAME, system_dpi ( ) ) + scaled_metric ( SM_CXPADDEDBORDER, system_dpi ( ) );
	int32_t client_w = base::min ( static_cast < int32_t > ( 1440.0f * scale ), static_cast < int32_t > ( work.right - work.left ) - 40 );
	int32_t client_h = base::min ( static_cast < int32_t > ( 900.0f * scale ), static_cast < int32_t > ( work.bottom - work.top ) - 40 );
	int32_t left = work.left + ( work.right - work.left - client_w ) / 2 - frame;
	int32_t top = work.top + ( work.bottom - work.top - client_h ) / 2;

	main_window = CreateWindowExW ( WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, L"plumb", L"Plumb", WS_OVERLAPPEDWINDOW, left, top, client_w + frame * 2, client_h + frame, nullptr, nullptr, GetModuleHandleW ( nullptr ), nullptr );

	return main_window != nullptr;

}

void host::style_main_window ( ) {

	MARGINS margins = { 0, 0, 1, 0 };
	int32_t corners = 2;

	DwmExtendFrameIntoClientArea ( main_window, &margins );
	DwmSetWindowAttribute ( main_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof ( corners ) );
	DragAcceptFiles ( main_window, TRUE );

}

void host::restore_placement ( ) {

	if ( !program->placement_valid )
		return;

	RECT saved = { program->placement [ 0 ], program->placement [ 1 ], program->placement [ 0 ] + program->placement [ 2 ], program->placement [ 1 ] + program->placement [ 3 ] };

	if ( !MonitorFromRect ( &saved, MONITOR_DEFAULTTONULL ) )
		return;

	WINDOWPLACEMENT place = { };

	place.length = sizeof ( place );
	place.showCmd = SW_HIDE;
	place.rcNormalPosition = saved;
	SetWindowPlacement ( main_window, &place );

}

bool host::attach_surface ( ) {

	RECT client = { };

	GetClientRect ( main_window, &client );

	if ( device->attach ( main_output, main_window, client.right, client.bottom ) )
		return true;

	fail ( L"Plumb could not create its window surface." );

	return false;

}

void host::start_frame_timer ( ) {

	frame_timer = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );

	if ( !frame_timer )
		frame_timer = CreateWaitableTimerW ( nullptr, FALSE, nullptr );

}

void host::show_main_window ( ) {

	ShowWindow ( main_window, program->placement_valid && program->placement_maximized ? SW_SHOWMAXIMIZED : SW_SHOW );
	sync_size ( );
	render_main ( );

}

void host::fail ( const wchar_t* message ) {

	MessageBoxW ( nullptr, message, L"Plumb", MB_ICONERROR );

}

int32_t host::end ( int32_t code ) {

	if ( program )
		program->shutdown ( );

	CoUninitialize ( );

	return code;

}