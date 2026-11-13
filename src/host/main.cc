#include "ui/app.hh"
#include "base/files.hh"
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <shobjidl.h>
#include <shellapi.h>

#ifdef PLUMB_DEBUG

bool debug_main ( );

#endif

static ui::app* program = nullptr;
static render::gpu* device = nullptr;
static render::output main_output;
static HWND main_window = nullptr;
static bool tracking = false;
static bool frame_tracking = false;
static bool device_lost = false;
static double device_retry = 0.0;
static bool software_only = false;
static int32_t refresh_rate = 60;
static HANDLE frame_timer = nullptr;
static bool frame_armed = false;
static int64_t frame_due = 0;
static int64_t frame_last = 0;
static bool fine_timer = false;
static constexpr UINT_PTR resize_timer = 7;

typedef UINT ( __stdcall* window_dpi_call ) ( HWND );
typedef UINT ( __stdcall* system_dpi_call ) ( );
typedef int ( __stdcall* metric_dpi_call ) ( int, UINT );
typedef BOOL ( __stdcall* context_call ) ( DPI_AWARENESS_CONTEXT );
typedef HRESULT ( __stdcall* awareness_call ) ( int );
typedef HRESULT ( __stdcall* monitor_dpi_call ) ( HMONITOR, int, UINT*, UINT* );

static window_dpi_call dpi_of_window = nullptr;
static system_dpi_call dpi_of_system = nullptr;
static metric_dpi_call metric_of_dpi = nullptr;
static monitor_dpi_call dpi_of_monitor = nullptr;

template < typename call >
static call find_call ( HMODULE module, const char* name ) {

	return module ? reinterpret_cast < call > ( reinterpret_cast < void* > ( GetProcAddress ( module, name ) ) ) : nullptr;

}

static void load_dpi ( ) {

	HMODULE user = GetModuleHandleW ( L"user32.dll" );
	HMODULE shell = LoadLibraryExW ( L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
	context_call context = find_call < context_call > ( user, "SetProcessDpiAwarenessContext" );
	awareness_call awareness = find_call < awareness_call > ( shell, "SetProcessDpiAwareness" );

	dpi_of_window = find_call < window_dpi_call > ( user, "GetDpiForWindow" );
	dpi_of_system = find_call < system_dpi_call > ( user, "GetDpiForSystem" );
	metric_of_dpi = find_call < metric_dpi_call > ( user, "GetSystemMetricsForDpi" );
	dpi_of_monitor = find_call < monitor_dpi_call > ( shell, "GetDpiForMonitor" );

	if ( context && context ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ) )
		return;

	if ( awareness && SUCCEEDED ( awareness ( 2 ) ) )
		return;

	SetProcessDPIAware ( );

}

static UINT system_dpi ( ) {

	if ( dpi_of_system )
		return dpi_of_system ( );

	HDC screen = GetDC ( nullptr );
	int32_t dpi = screen ? GetDeviceCaps ( screen, LOGPIXELSX ) : 96;

	if ( screen )
		ReleaseDC ( nullptr, screen );

	return dpi > 0 ? static_cast < UINT > ( dpi ) : 96;

}

static UINT window_dpi ( HWND window ) {

	UINT dpi = dpi_of_window ? dpi_of_window ( window ) : 0;
	UINT x = 0;
	UINT y = 0;

	if ( !dpi && dpi_of_monitor && SUCCEEDED ( dpi_of_monitor ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), 0, &x, &y ) ) )
		dpi = x;

	return dpi ? dpi : system_dpi ( );

}

static int32_t scaled_metric ( int32_t index, UINT dpi ) {

	if ( metric_of_dpi )
		return metric_of_dpi ( index, dpi );

	return MulDiv ( GetSystemMetrics ( index ), static_cast < int32_t > ( dpi ), static_cast < int32_t > ( system_dpi ( ) ) );

}

static int32_t frame_thickness ( HWND window ) {

	UINT dpi = window_dpi ( window );

	return scaled_metric ( SM_CYFRAME, dpi ) + scaled_metric ( SM_CXPADDEDBORDER, dpi );

}

static void apply_dark_frame ( ) {

	BOOL dark = program && !program->light ? TRUE : FALSE;

	DwmSetWindowAttribute ( main_window, 20, &dark, sizeof ( dark ) );

}

static void sync_size ( ) {

	RECT client = { };

	GetClientRect ( main_window, &client );

	int32_t width = client.right - client.left;
	int32_t height = client.bottom - client.top;

	if ( width <= 0 || height <= 0 )
		return;

	if ( !device_lost )
		device->resize ( main_output, width, height );

	program->resize ( width, height, static_cast < float > ( window_dpi ( main_window ) ) / 96.0f );
	program->set_maximized ( IsZoomed ( main_window ) != FALSE );

}

static void measure_refresh ( ) {

	MONITORINFOEXW info = { };
	DEVMODEW mode = { };

	info.cbSize = sizeof ( info );
	mode.dmSize = sizeof ( mode );

	if ( GetMonitorInfoW ( MonitorFromWindow ( main_window, MONITOR_DEFAULTTONEAREST ), &info ) && EnumDisplaySettingsW ( info.szDevice, ENUM_CURRENT_SETTINGS, &mode ) && mode.dmDisplayFrequency > 1 )
		refresh_rate = static_cast < int32_t > ( mode.dmDisplayFrequency );

}

static int64_t clock_now ( ) {

	LARGE_INTEGER value;

	QueryPerformanceCounter ( &value );

	return value.QuadPart;

}

static DWORD frame_wait ( ) {

	if ( !frame_armed )
		return 250;

	LARGE_INTEGER frequency;

	QueryPerformanceFrequency ( &frequency );

	int64_t left = frame_due - clock_now ( );

	if ( left <= 0 )
		return 0;

	return static_cast < DWORD > ( base::min ( left * 1000 / frequency.QuadPart + 1, static_cast < int64_t > ( 250 ) ) );

}

static void schedule_frame ( ) {

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

static void render_main ( ) {

	if ( device_lost )
		return;

	program->frame ( base::seconds ( ) );
	device->present ( main_output, device->chrome );

	switch ( program->take_request ( ) ) {

		case ui::request::MINIMIZE:
			ShowWindow ( main_window, SW_MINIMIZE );
			break;

		case ui::request::MAXIMIZE:
			ShowWindow ( main_window, IsZoomed ( main_window ) ? SW_RESTORE : SW_MAXIMIZE );
			break;

		case ui::request::CLOSE:
			PostMessageW ( main_window, WM_CLOSE, 0, 0 );
			break;

		case ui::request::OPEN: {

			IFileOpenDialog* dialog = nullptr;

			if ( SUCCEEDED ( CoCreateInstance ( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &dialog ) ) ) ) {

				COMDLG_FILTERSPEC kinds [ 4 ] = { { L"Executables and libraries", L"*.exe;*.dll;*.sys;*.ocx;*.cpl;*.scr;*.efi" }, { L"Plumb projects", L"*.plumb" }, { L"Object and data files", L"*.obj;*.bin;*.dat;*.rom" }, { L"All files", L"*.*" } };
				IShellItem* picked = nullptr;

				dialog->SetFileTypes ( 4, kinds );
				dialog->SetTitle ( L"Open a binary" );

				if ( SUCCEEDED ( dialog->Show ( main_window ) ) && SUCCEEDED ( dialog->GetResult ( &picked ) ) ) {

					wchar_t* path = nullptr;

					if ( SUCCEEDED ( picked->GetDisplayName ( SIGDN_FILESYSPATH, &path ) ) ) {

						program->open_file ( path );
						CoTaskMemFree ( path );

					}

					picked->Release ( );

				}

				dialog->Release ( );

			}

			break;

		}

		default:
			break;

	}

	apply_dark_frame ( );

}

static void recover_device ( ) {

	RECT client = { };
	render::gpu* fresh = new render::gpu ( );

	device_retry = base::seconds ( ) + 1.0;
	device->release ( main_output );
	GetClientRect ( main_window, &client );

	if ( !fresh->setup ( software_only ) || !fresh->attach ( main_output, main_window, client.right, client.bottom ) ) {

		fresh->release ( main_output );
		fresh->shutdown ( );
		delete fresh;

		return;

	}

	program->rebuild_gpu ( fresh );
	device->shutdown ( );
	delete device;
	device = fresh;
	device_lost = false;
	sync_size ( );
	render_main ( );

}

static LRESULT cursor_for ( ui::pointer kind ) {

	LPCWSTR shape = IDC_ARROW;

	switch ( kind ) {

		case ui::pointer::TEXT:
			shape = IDC_IBEAM;
			break;

		case ui::pointer::HAND:
			shape = IDC_HAND;
			break;

		case ui::pointer::SIZE_WE:
			shape = IDC_SIZEWE;
			break;

		case ui::pointer::SIZE_NS:
			shape = IDC_SIZENS;
			break;

		default:
			break;

	}

	SetCursor ( LoadCursorW ( nullptr, shape ) );

	return TRUE;

}

static LRESULT __stdcall procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

	if ( message == WM_NCCALCSIZE && wide ) {

		NCCALCSIZE_PARAMS* sizes = reinterpret_cast < NCCALCSIZE_PARAMS* > ( extra );
		LONG top = sizes->rgrc [ 0 ].top;
		LRESULT result = DefWindowProcW ( window, message, wide, extra );

		sizes->rgrc [ 0 ].top = top + ( IsZoomed ( window ) ? frame_thickness ( window ) : 0 );

		return result;

	}

	if ( !program || window != main_window )
		return DefWindowProcW ( window, message, wide, extra );

	switch ( message ) {

		case WM_NCHITTEST: {

			LRESULT edge = DefWindowProcW ( window, message, wide, extra );

			if ( edge != HTCLIENT )
				return edge;

			POINT at = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };
			RECT client = { };

			ScreenToClient ( window, &at );
			GetClientRect ( window, &client );

			ui::hit kind = program->hit_test ( static_cast < float > ( at.x ), static_cast < float > ( at.y ) );
			int32_t frame = frame_thickness ( window );

			if ( kind == ui::hit::MAXIMIZE )
				return HTMAXBUTTON;

			if ( !IsZoomed ( window ) && at.y < frame ) {

				if ( at.x < frame * 2 )
					return HTTOPLEFT;

				if ( at.x > client.right - frame * 2 )
					return HTTOPRIGHT;

				return HTTOP;

			}

			return kind == ui::hit::CAPTION ? HTCAPTION : HTCLIENT;

		}

		case WM_NCMOUSEMOVE:

			if ( wide == HTMAXBUTTON ) {

				POINT at = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };

				if ( !frame_tracking ) {

					TRACKMOUSEEVENT track = { };

					track.cbSize = sizeof ( track );
					track.dwFlags = TME_LEAVE | TME_NONCLIENT;
					track.hwndTrack = window;
					TrackMouseEvent ( &track );
					frame_tracking = true;

				}

				ScreenToClient ( window, &at );
				program->mouse_move ( static_cast < float > ( at.x ), static_cast < float > ( at.y ) );

				return 0;

			}

			program->mouse_leave ( );

			break;

		case WM_NCMOUSELEAVE:
			frame_tracking = false;
			program->mouse_leave ( );
			break;

		case WM_NCLBUTTONDOWN:
		case WM_NCLBUTTONDBLCLK:

			if ( wide == HTMAXBUTTON ) {

				program->mouse_button ( 0, true );

				return 0;

			}

			break;

		case WM_NCLBUTTONUP:

			if ( wide == HTMAXBUTTON ) {

				program->mouse_button ( 0, false );

				return 0;

			}

			break;

		case WM_MOUSEMOVE:

			if ( !tracking ) {

				TRACKMOUSEEVENT track = { };

				track.cbSize = sizeof ( track );
				track.dwFlags = TME_LEAVE;
				track.hwndTrack = window;
				TrackMouseEvent ( &track );
				tracking = true;

			}

			program->mouse_move ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ), static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) );

			return 0;

		case WM_MOUSELEAVE:
			tracking = false;
			program->mouse_leave ( );
			return 0;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			SetCapture ( window );
			program->mouse_button ( 0, true );
			return 0;

		case WM_LBUTTONUP:
			ReleaseCapture ( );
			program->mouse_button ( 0, false );
			return 0;

		case WM_RBUTTONDOWN:
			program->mouse_button ( 1, true );
			return 0;

		case WM_MBUTTONDOWN:
			program->mouse_button ( 2, true );
			return 0;

		case WM_APP + 1:
			program->analysis_finished ( );
			schedule_frame ( );
			return 0;

		case WM_XBUTTONDOWN:
			program->mouse_button ( GET_XBUTTON_WPARAM ( wide ) == XBUTTON1 ? 3 : 4, true );
			return TRUE;

		case WM_MOUSEWHEEL:
			program->mouse_wheel ( static_cast < float > ( static_cast < int16_t > ( HIWORD ( wide ) ) ) / 120.0f, false );
			return 0;

		case WM_MOUSEHWHEEL:
			program->mouse_wheel ( static_cast < float > ( static_cast < int16_t > ( HIWORD ( wide ) ) ) / 120.0f, true );
			return 0;

		case WM_CHAR:
			program->character ( static_cast < wchar_t > ( wide ) );
			return 0;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:

			if ( wide == VK_F4 && ( GetKeyState ( VK_MENU ) & 0x8000 ) )
				break;

			program->key ( static_cast < uint32_t > ( wide ), ( GetKeyState ( VK_CONTROL ) & 0x8000 ) != 0, ( GetKeyState ( VK_SHIFT ) & 0x8000 ) != 0, ( GetKeyState ( VK_MENU ) & 0x8000 ) != 0 );

			return 0;

		case WM_SYSCHAR:
			return 0;

		case WM_DROPFILES: {

			HDROP drop = reinterpret_cast < HDROP > ( wide );
			wchar_t path [ 1024 ];

			if ( DragQueryFileW ( drop, 0, path, 1024 ) )
				program->open_file ( path );

			DragFinish ( drop );
			SetForegroundWindow ( window );
			render_main ( );

			return 0;

		}

		case WM_SIZE:

			if ( wide == SIZE_MINIMIZED )
				return 0;

			sync_size ( );
			render_main ( );

			return 0;

		case WM_DISPLAYCHANGE:
		case WM_WINDOWPOSCHANGED:
			measure_refresh ( );
			break;

		case WM_DPICHANGED: {

			RECT* suggested = reinterpret_cast < RECT* > ( extra );

			SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );

			return 0;

		}

		case WM_GETMINMAXINFO: {

			MINMAXINFO* info = reinterpret_cast < MINMAXINFO* > ( extra );
			float scale = static_cast < float > ( window_dpi ( window ) ) / 96.0f;
			MONITORINFO monitor = { };

			monitor.cbSize = sizeof ( monitor );
			info->ptMinTrackSize.x = static_cast < LONG > ( 900.0f * scale );
			info->ptMinTrackSize.y = static_cast < LONG > ( 560.0f * scale );

			if ( GetMonitorInfoW ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), &monitor ) ) {

				info->ptMinTrackSize.x = base::min ( info->ptMinTrackSize.x, monitor.rcWork.right - monitor.rcWork.left );
				info->ptMinTrackSize.y = base::min ( info->ptMinTrackSize.y, monitor.rcWork.bottom - monitor.rcWork.top );

			}

			return 0;

		}

		case WM_ACTIVATE:
			program->set_focus ( LOWORD ( wide ) != WA_INACTIVE );
			break;

		case WM_SETTINGCHANGE:
			render_main ( );
			break;

		case WM_CLOSE:

			if ( program && !program->can_close ( ) )
				return 0;

			DestroyWindow ( window );

			return 0;

		case WM_DESTROY: {

			WINDOWPLACEMENT place = { };

			place.length = sizeof ( place );

			if ( GetWindowPlacement ( window, &place ) ) {

				program->placement [ 0 ] = place.rcNormalPosition.left;
				program->placement [ 1 ] = place.rcNormalPosition.top;
				program->placement [ 2 ] = place.rcNormalPosition.right - place.rcNormalPosition.left;
				program->placement [ 3 ] = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
				program->placement_maximized = IsZoomed ( window ) != FALSE;
				program->placement_valid = true;

			}

			PostQuitMessage ( 0 );

			return 0;

		}

		case WM_ENTERSIZEMOVE:
			SetTimer ( window, resize_timer, 15, nullptr );
			break;

		case WM_EXITSIZEMOVE:
			KillTimer ( window, resize_timer );
			break;

		case WM_TIMER:

			if ( wide == resize_timer && program->busy ( ) )
				render_main ( );

			return 0;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
			ValidateRect ( window, nullptr );
			render_main ( );
			return 0;

		case WM_SETCURSOR:

			if ( LOWORD ( extra ) == HTCLIENT )
				return cursor_for ( program->cursor ( ) );

			break;

		default:
			break;

	}

	return DefWindowProcW ( window, message, wide, extra );

}

static bool system_light ( ) {

	DWORD value = 0;
	DWORD size = sizeof ( value );

	if ( RegGetValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size ) != ERROR_SUCCESS )
		return false;

	return value != 0;

}

int program_main ( ) {

	load_dpi ( );
	CoInitializeEx ( nullptr, COINIT_APARTMENTTHREADED );

#ifdef PLUMB_DEBUG

	if ( debug_main ( ) ) {

		CoUninitialize ( );

		return 0;

	}

#endif

	int32_t argument_count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &argument_count );
	const wchar_t* open_path = nullptr;
	bool serve = false;

	for ( int32_t i = 1; arguments && i < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], L"--software" ) )
			software_only = true;
		else if ( base::same_text ( arguments [ i ], L"--mcp" ) )
			serve = true;
		else if ( arguments [ i ] [ 0 ] != L'-' )
			open_path = arguments [ i ];

	}

	if ( serve ) {

		ui::app* server = new ui::app ( );

		server->setup ( nullptr, nullptr );

		if ( open_path )
			server->open_file ( open_path );

		if ( arguments )
			LocalFree ( arguments );

		int32_t code = server->serve_mcp ( );

		CoUninitialize ( );

		return code;

	}

	device = new render::gpu ( );

	if ( !device->setup ( software_only ) ) {

		MessageBoxW ( nullptr, L"Plumb could not start its renderer.", L"Plumb", MB_ICONERROR );

		return 1;

	}

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

	program = new ui::app ( );
	program->light = system_light ( );
	program->load_settings ( );

	float scale = static_cast < float > ( system_dpi ( ) ) / 96.0f;
	RECT work = { };

	SystemParametersInfoW ( SPI_GETWORKAREA, 0, &work, 0 );

	int32_t frame = scaled_metric ( SM_CXFRAME, system_dpi ( ) ) + scaled_metric ( SM_CXPADDEDBORDER, system_dpi ( ) );
	int32_t client_w = base::min ( static_cast < int32_t > ( 1440.0f * scale ), static_cast < int32_t > ( work.right - work.left ) - 40 );
	int32_t client_h = base::min ( static_cast < int32_t > ( 900.0f * scale ), static_cast < int32_t > ( work.bottom - work.top ) - 40 );
	int32_t left = work.left + ( work.right - work.left - client_w ) / 2 - frame;
	int32_t top = work.top + ( work.bottom - work.top - client_h ) / 2;

	main_window = CreateWindowExW ( WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, L"plumb", L"Plumb", WS_OVERLAPPEDWINDOW, left, top, client_w + frame * 2, client_h + frame, nullptr, nullptr, instance, nullptr );

	if ( !main_window )
		return 1;

	MARGINS margins = { 0, 0, 1, 0 };
	int32_t corners = 2;

	DwmExtendFrameIntoClientArea ( main_window, &margins );
	DwmSetWindowAttribute ( main_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof ( corners ) );
	DragAcceptFiles ( main_window, TRUE );

	if ( !program->setup ( device, main_window ) ) {

		MessageBoxW ( nullptr, L"Plumb could not set up its interface.", L"Plumb", MB_ICONERROR );

		return 1;

	}

	apply_dark_frame ( );

	if ( program->placement_valid ) {

		RECT saved = { program->placement [ 0 ], program->placement [ 1 ], program->placement [ 0 ] + program->placement [ 2 ], program->placement [ 1 ] + program->placement [ 3 ] };

		if ( MonitorFromRect ( &saved, MONITOR_DEFAULTTONULL ) ) {

			WINDOWPLACEMENT place = { };

			place.length = sizeof ( place );
			place.showCmd = SW_HIDE;
			place.rcNormalPosition = saved;
			SetWindowPlacement ( main_window, &place );

		}

	}

	SetWindowPos ( main_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

	RECT client = { };

	GetClientRect ( main_window, &client );

	if ( !device->attach ( main_output, main_window, client.right, client.bottom ) ) {

		MessageBoxW ( nullptr, L"Plumb could not create its window surface.", L"Plumb", MB_ICONERROR );

		return 1;

	}

	frame_timer = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );

	if ( !frame_timer )
		frame_timer = CreateWaitableTimerW ( nullptr, FALSE, nullptr );

	measure_refresh ( );
	sync_size ( );

	if ( open_path )
		program->open_file ( open_path );

	if ( arguments )
		LocalFree ( arguments );

	ShowWindow ( main_window, program->placement_valid && program->placement_maximized ? SW_SHOWMAXIMIZED : SW_SHOW );
	sync_size ( );
	render_main ( );

	MSG message;
	bool running = true;

	while ( running ) {

		if ( !device_lost && !device->healthy ( ) )
			device_lost = true;

		if ( device_lost && base::seconds ( ) >= device_retry )
			recover_device ( );

		bool visible = !IsIconic ( main_window );
		bool need = visible && program->busy ( );
		DWORD result = WAIT_TIMEOUT;

		if ( need ) {

			schedule_frame ( );
			result = MsgWaitForMultipleObjectsEx ( 1, &frame_timer, frame_wait ( ), QS_ALLINPUT, MWMO_INPUTAVAILABLE );

		} else {

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

			result = MsgWaitForMultipleObjectsEx ( 0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE );

			if ( result == WAIT_TIMEOUT && visible )
				program->pending = base::max ( program->pending, 1 );

		}

		while ( PeekMessageW ( &message, nullptr, 0, 0, PM_REMOVE ) ) {

			if ( message.message == WM_QUIT ) {

				running = false;

				break;

			}

			TranslateMessage ( &message );
			DispatchMessageW ( &message );

		}

		if ( !running )
			break;

		bool late = frame_armed && clock_now ( ) >= frame_due;
		bool tick = !need || result == WAIT_OBJECT_0 || late || WaitForSingleObject ( frame_timer, 0 ) == WAIT_OBJECT_0;

		if ( need && tick ) {

			frame_last = clock_now ( );

			if ( frame_armed )
				CancelWaitableTimer ( frame_timer );

			frame_armed = false;

		}

		if ( visible && program->busy ( ) && tick )
			render_main ( );

	}

	program->shutdown ( );
	CoUninitialize ( );

	return 0;

}