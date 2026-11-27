#pragma once

#include "ui/core/app/app.h"
#include <windows.h>

typedef UINT ( __stdcall* window_dpi_call ) ( HWND );
typedef UINT ( __stdcall* system_dpi_call ) ( );
typedef int ( __stdcall* metric_dpi_call ) ( int, UINT );
typedef BOOL ( __stdcall* context_call ) ( DPI_AWARENESS_CONTEXT );
typedef HRESULT ( __stdcall* awareness_call ) ( int );
typedef HRESULT ( __stdcall* monitor_dpi_call ) ( HMONITOR, int, UINT*, UINT* );