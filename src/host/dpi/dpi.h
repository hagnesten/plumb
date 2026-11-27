#pragma once

#include "base/types/types.h"

struct DPI_AWARENESS_CONTEXT__;
struct HINSTANCE__;
struct HMONITOR__;
struct HWND__;

namespace host {

	typedef unsigned int ( __stdcall* window_dpi_call ) ( HWND__* );

	typedef unsigned int ( __stdcall* system_dpi_call ) ( );

	typedef int ( __stdcall* metric_dpi_call ) ( int, unsigned int );

	typedef int ( __stdcall* context_call ) ( DPI_AWARENESS_CONTEXT__* );

	typedef long ( __stdcall* awareness_call ) ( int );

	typedef long ( __stdcall* monitor_dpi_call ) ( HMONITOR__*, int, unsigned int*, unsigned int* );

	extern window_dpi_call dpi_of_window;

	extern system_dpi_call dpi_of_system;

	extern metric_dpi_call metric_of_dpi;

	extern monitor_dpi_call dpi_of_monitor;

	void* find_export ( HINSTANCE__* module, const char* name );

	void load_dpi ( );

	unsigned int system_dpi ( );

	unsigned int window_dpi ( HWND__* window );

	int32_t scaled_metric ( int32_t index, unsigned int dpi );

	int32_t frame_thickness ( HWND__* window );

}