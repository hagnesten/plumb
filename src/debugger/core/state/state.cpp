#include "debugger/core/state/state.h"
#include <windows.h>

SRWLOCK debugger::lock = SRWLOCK_INIT;

HANDLE debugger::wake = nullptr;

HANDLE debugger::worker = nullptr;

debugger::snapshot debugger::shared;

volatile long debugger::pending = 0;

volatile long debugger::wanted_serial = 0;

volatile long debugger::hardware_serial = 0;

base::list < uint64_t > debugger::wanted;

base::list < base::text > debugger::messages;

base::list < debugger::condition_entry > debugger::conditions;

base::list < uint64_t > debugger::cover_wanted;

base::list < uint64_t > debugger::cover_hits;

base::list < debugger::cover_point > debugger::cover;

base::list < debugger::module_info > debugger::loaded;

debugger::hardware_point debugger::hardware_wanted [ 4 ];

wchar_t debugger::target [ 600 ];

wchar_t debugger::command_tail [ 1000 ];

wchar_t debugger::module_name [ 260 ];

uint32_t debugger::attach_id = 0;

uint64_t debugger::entry = 0;

uint64_t debugger::run_target = 0;

bool debugger::wide_target = true;

HANDLE debugger::process = nullptr;

DWORD debugger::process_id = 0;

base::list < debugger::thread_entry > debugger::threads;

base::list < debugger::breakpoint > debugger::points;

debugger::hardware_point debugger::hardware [ 4 ];

uint64_t debugger::image_base = 0;

uint64_t debugger::reinsert = 0;

bool debugger::stepping = false;

bool debugger::stepping_out = false;

bool debugger::out_finishing = false;

bool debugger::out_ignore_step = false;

bool debugger::attach_announced = false;

int64_t debugger::out_steps = 0;

bool debugger::entry_reached = false;

bool debugger::resume_flag = false;

long debugger::synced_serial = -1;

long debugger::hardware_synced = -1;