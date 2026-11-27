#include "host/painting/painting.h"
#include "host/state/state.h"
#include "host/window/window.h"
#include "base/clock/clock.h"
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

void host::render_main ( ) {

	if ( device_lost )
		return;

	program->frame ( base::seconds ( ) );
	device->present ( main_output, device->chrome );

	ui::request asked = program->take_request ( );

	switch ( asked ) {

		case ui::request::MINIMIZE:
			ShowWindow ( main_window, SW_MINIMIZE );
			break;

		case ui::request::MAXIMIZE:
			ShowWindow ( main_window, IsZoomed ( main_window ) ? SW_RESTORE : SW_MAXIMIZE );
			break;

		case ui::request::CLOSE:
			PostMessageW ( main_window, WM_CLOSE, 0, 0 );
			break;

		case ui::request::OPEN:
		case ui::request::COMPARE:
		case ui::request::SCRIPT: {

			IFileOpenDialog* dialog = nullptr;

			if ( SUCCEEDED ( CoCreateInstance ( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &dialog ) ) ) ) {

				COMDLG_FILTERSPEC kinds [ 4 ] = { { L"Executables and libraries", L"*.exe;*.dll;*.sys;*.ocx;*.cpl;*.scr;*.efi" }, { L"Plumb projects", L"*.plumb" }, { L"Object and data files", L"*.obj;*.bin;*.dat;*.rom" }, { L"All files", L"*.*" } };
				COMDLG_FILTERSPEC scripts [ 2 ] = { { L"Plumb scripts", L"*.psc" }, { L"All files", L"*.*" } };
				IShellItem* picked = nullptr;

				if ( asked == ui::request::SCRIPT )
					dialog->SetFileTypes ( 2, scripts );
				else
					dialog->SetFileTypes ( 4, kinds );

				dialog->SetTitle ( asked == ui::request::COMPARE ? L"Compare with another build" : ( asked == ui::request::SCRIPT ? L"Run a Plumb script" : L"Open a binary" ) );

				if ( SUCCEEDED ( dialog->Show ( main_window ) ) && SUCCEEDED ( dialog->GetResult ( &picked ) ) ) {

					wchar_t* path = nullptr;

					if ( SUCCEEDED ( picked->GetDisplayName ( SIGDN_FILESYSPATH, &path ) ) ) {

						if ( asked == ui::request::COMPARE )
							program->compare_with ( path );
						else if ( asked == ui::request::SCRIPT )
							program->run_script_file ( path, nullptr );
						else
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

void host::recover_device ( ) {

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