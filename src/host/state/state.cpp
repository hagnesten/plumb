#include "host/state/state.h"
#include <windows.h>

ui::app* host::program = nullptr;

render::gpu* host::device = nullptr;

render::output host::main_output;

HWND host::main_window = nullptr;

bool host::tracking = false;

bool host::frame_tracking = false;

bool host::device_lost = false;

double host::device_retry = 0.0;

bool host::software_only = false;

int32_t host::refresh_rate = 60;

HANDLE host::frame_timer = nullptr;

bool host::frame_armed = false;

int64_t host::frame_due = 0;

int64_t host::frame_last = 0;

bool host::fine_timer = false;