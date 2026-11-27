#include "host/window/window.h"
#include "host/state/state.h"
#include "host/dpi/dpi.h"
#include "host/pacing/pacing.h"
#include "host/painting/painting.h"
#include "base/utility/utility.h"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

void host::apply_dark_frame ( ) {

	BOOL dark = program && !program->light ? TRUE : FALSE;

	DwmSetWindowAttribute ( main_window, 20, &dark, sizeof ( dark ) );

}

void host::sync_size ( ) {

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

LRESULT host::cursor_for ( ui::pointer kind ) {

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

LRESULT __stdcall host::procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

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

			if ( DragQueryFileW ( drop, 0, path, 1024 ) ) {

				int32_t length = 0;

				while ( path [ length ] )
					length++;

				bool script_file = length > 4 && path [ length - 4 ] == L'.' && ( path [ length - 3 ] | 32 ) == L'p' && ( path [ length - 2 ] | 32 ) == L's' && ( path [ length - 1 ] | 32 ) == L'c';

				if ( script_file )
					program->run_script_file ( path, nullptr );
				else
					program->open_file ( path );

			}

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

bool host::system_light ( ) {

	DWORD value = 0;
	DWORD size = sizeof ( value );

	if ( RegGetValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size ) != ERROR_SUCCESS )
		return false;

	return value != 0;

}