#pragma once

#include "debugger/control/session/session.h"
#include "debugger/target/modules/modules.h"
#include "debugger/target/registers/registers.h"
#include "debugger/traps/breakpoints/breakpoints.h"
#include "debugger/traps/conditions/conditions.h"
#include "debugger/traps/coverage/coverage.h"
#include "debugger/traps/hardware/hardware.h"
#include "base/list/list.h"
#include "base/text/text.h"

struct _RTL_SRWLOCK;

namespace debugger {

	extern _RTL_SRWLOCK lock;

	extern void* wake;

	extern void* worker;

	extern snapshot shared;

	extern volatile long pending;

	extern volatile long wanted_serial;

	extern volatile long hardware_serial;

	extern base::list < uint64_t > wanted;

	extern base::list < base::text > messages;

	extern base::list < condition_entry > conditions;

	extern base::list < uint64_t > cover_wanted;

	extern base::list < uint64_t > cover_hits;

	extern base::list < cover_point > cover;

	extern base::list < module_info > loaded;

	extern hardware_point hardware_wanted [ 4 ];

	extern wchar_t target [ 600 ];

	extern wchar_t command_tail [ 1000 ];

	extern wchar_t module_name [ 260 ];

	extern uint32_t attach_id;

	extern uint64_t entry;

	extern uint64_t run_target;

	extern bool wide_target;

	extern void* process;

	extern unsigned long process_id;

	extern base::list < thread_entry > threads;

	extern base::list < breakpoint > points;

	extern hardware_point hardware [ 4 ];

	extern uint64_t image_base;

	extern uint64_t reinsert;

	extern bool stepping;

	extern bool stepping_out;

	extern bool out_finishing;

	extern bool out_ignore_step;

	extern bool attach_announced;

	extern int64_t out_steps;

	extern bool entry_reached;

	extern bool resume_flag;

	extern long synced_serial;

	extern long hardware_synced;

}