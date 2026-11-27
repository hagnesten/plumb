#include "debugger/events/exceptions/exceptions.h"
#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include "debugger/events/stepping/stepping.h"
#include "debugger/target/memory/memory.h"
#include <windows.h>

void debugger::exception_raised ( const DEBUG_EVENT& event, event_outcome& outcome ) {

	const EXCEPTION_RECORD& record = event.u.Exception.ExceptionRecord;
	uint64_t at = reinterpret_cast < uint64_t > ( record.ExceptionAddress );
	DWORD code = record.ExceptionCode;
	HANDLE thread = thread_handle ( event.dwThreadId );
	wchar_t note [ 200 ] = { };
	bool pause = false;

	if ( code == EXCEPTION_BREAKPOINT || code == 0x4000001F )
		pause = breakpoint_hit ( at, thread, note );
	else if ( code == EXCEPTION_SINGLE_STEP || code == 0x4000001E )
		pause = single_step ( thread, note );
	else {

		pause = fault ( code, event.u.Exception.dwFirstChance != 0, note );
		outcome.reply = DBG_EXCEPTION_NOT_HANDLED;

	}

	if ( pause )
		outcome.detach = wait_for_order ( thread, event.dwThreadId, note );

}

bool debugger::breakpoint_hit ( uint64_t at, HANDLE thread, wchar_t* note ) {

	int32_t hit = find_point ( at );
	int32_t covered = hit < 0 ? find_cover ( at ) : -1;

	if ( covered >= 0 && cover [ static_cast < size_t > ( covered ) ].armed ) {

		write_byte ( at, cover [ static_cast < size_t > ( covered ) ].original );
		record_cover ( cover [ static_cast < size_t > ( covered ) ] );
		adjust ( thread, true, at, false );

		return false;

	}

	if ( hit >= 0 && points [ static_cast < size_t > ( hit ) ].armed && !points [ static_cast < size_t > ( hit ) ].temporary && !condition_holds ( at, thread ) ) {

		disarm ( points [ static_cast < size_t > ( hit ) ] );
		adjust ( thread, true, at, true );
		reinsert = at;

		return false;

	}

	if ( hit >= 0 && points [ static_cast < size_t > ( hit ) ].armed )
		return point_reached ( hit, at, thread, note );

	if ( !entry_reached )
		return false;

	lstrcpyW ( note, attach_id && !attach_announced ? L"Attached" : L"Paused" );
	attach_announced = true;

	return true;

}

bool debugger::point_reached ( int32_t hit, uint64_t at, HANDLE thread, wchar_t* note ) {

	breakpoint& item = points [ static_cast < size_t > ( hit ) ];
	purpose use = item.use;

	adjust ( thread, true, at, false );

	if ( item.temporary )
		return temporary_reached ( hit, at, use, thread, note );

	disarm ( item );
	reinsert = at;
	stepping_out = false;
	lstrcpyW ( note, L"Breakpoint hit" );

	return true;

}

bool debugger::temporary_reached ( int32_t hit, uint64_t at, purpose use, HANDLE thread, wchar_t* note ) {

	bool user_here = false;

	disarm ( points [ static_cast < size_t > ( hit ) ] );
	points.remove_at ( static_cast < size_t > ( hit ) );

	for ( breakpoint& other : points ) {

		if ( other.address != at )
			continue;

		user_here = user_here || !other.temporary;
		disarm ( other );

	}

	if ( user_here )
		reinsert = at;

	if ( use == purpose::STEP_OUT && stepping_out ) {

		bool armed_call = false;

		if ( step_out_next ( thread, armed_call ) )
			return false;

		stepping_out = false;
		lstrcpyW ( note, L"Stopped stepping out" );

		return true;

	}

	if ( use == purpose::ENTRY && !entry_reached ) {

		entry_reached = true;
		lstrcpyW ( note, L"Stopped at the entry point" );

	} else if ( use == purpose::RUN_TO )
		lstrcpyW ( note, L"Reached the cursor" );
	else
		lstrcpyW ( note, L"Stepped over the call" );

	return true;

}

bool debugger::single_step ( HANDLE thread, wchar_t* note ) {

	int32_t watched = hardware_hit ( thread );

	if ( reinsert ) {

		int32_t again = find_point ( reinsert );

		if ( again >= 0 )
			arm ( points [ static_cast < size_t > ( again ) ] );

		reinsert = 0;

	}

	if ( watched >= 0 )
		return watch_hit ( watched, thread, note );

	if ( stepping_out )
		return step_out_step ( thread, note );

	if ( !stepping )
		return false;

	stepping = false;
	lstrcpyW ( note, L"Stepped" );

	return true;

}

bool debugger::watch_hit ( int32_t watched, HANDLE thread, wchar_t* note ) {

	int32_t write_at = 0;

	debug_registers ( thread, true );
	stepping = false;
	stepping_out = false;
	resume_flag = hardware [ watched ].kind == watch_kind::EXECUTE;
	append_text ( note, write_at, L"Hardware breakpoint " );
	append_number ( note, write_at, static_cast < uint64_t > ( watched + 1 ), false );
	append_text ( note, write_at, L" (" );
	append_text ( note, write_at, watch_names [ static_cast < int32_t > ( hardware [ watched ].kind ) ] );
	append_text ( note, write_at, L")" );

	return true;

}

bool debugger::step_out_step ( HANDLE thread, wchar_t* note ) {

	bool armed_call = false;

	if ( out_ignore_step ) {

		out_ignore_step = false;

		return false;

	}

	if ( out_finishing ) {

		stepping_out = false;
		out_finishing = false;
		lstrcpyW ( note, L"Stepped out" );

		return true;

	}

	if ( step_out_next ( thread, armed_call ) )
		return false;

	stepping_out = false;
	lstrcpyW ( note, L"Stopped stepping out" );

	return true;

}

bool debugger::fault ( DWORD code, bool first_chance, wchar_t* note ) {

	lstrcpyW ( note, first_chance ? L"Exception 0x00000000, first chance" : L"Exception 0x00000000, second chance" );

	for ( int32_t i = 0; i < 8; i++ )
		note [ 12 + i ] = L"0123456789ABCDEF" [ ( code >> ( 28 - i * 4 ) ) & 15 ];

	stepping_out = false;
	log_text ( note );

	return true;

}