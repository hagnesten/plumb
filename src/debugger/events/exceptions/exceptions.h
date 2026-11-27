#pragma once

#include "debugger/events/loop/loop.h"
#include "debugger/traps/breakpoints/breakpoints.h"

struct _DEBUG_EVENT;

namespace debugger {

	void exception_raised ( const _DEBUG_EVENT& event, event_outcome& outcome );

	bool breakpoint_hit ( uint64_t at, void* thread, wchar_t* note );

	bool point_reached ( int32_t hit, uint64_t at, void* thread, wchar_t* note );

	bool temporary_reached ( int32_t hit, uint64_t at, purpose use, void* thread, wchar_t* note );

	bool single_step ( void* thread, wchar_t* note );

	bool watch_hit ( int32_t watched, void* thread, wchar_t* note );

	bool step_out_step ( void* thread, wchar_t* note );

	bool fault ( unsigned long code, bool first_chance, wchar_t* note );

}