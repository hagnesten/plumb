#pragma once

#include "ui/core/app/app.h"

struct HWND__;

namespace host {

	constexpr unsigned long long resize_timer = 7;

	extern ui::app* program;

	extern render::gpu* device;

	extern render::output main_output;

	extern HWND__* main_window;

	extern bool tracking;

	extern bool frame_tracking;

	extern bool device_lost;

	extern double device_retry;

	extern bool software_only;

	extern int32_t refresh_rate;

	extern void* frame_timer;

	extern bool frame_armed;

	extern int64_t frame_due;

	extern int64_t frame_last;

	extern bool fine_timer;

}