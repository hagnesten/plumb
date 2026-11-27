#pragma once

#include "ui/core/app/app.h"

struct HWND__;

namespace host {

	void apply_dark_frame ( );

	void sync_size ( );

	long long cursor_for ( ui::pointer kind );

	long long __stdcall procedure ( HWND__* window, unsigned int message, unsigned long long wide, long long extra );

	bool system_light ( );

}