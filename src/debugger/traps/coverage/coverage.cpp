#include "debugger/traps/coverage/coverage.h"
#include "debugger/core/state/state.h"
#include "debugger/target/memory/memory.h"
#include <windows.h>

int32_t debugger::find_cover ( uint64_t address ) {

	int32_t low = 0;
	int32_t high = cover.count ( ) - 1;

	while ( low <= high ) {

		int32_t middle = ( low + high ) / 2;
		uint64_t value = cover [ static_cast < size_t > ( middle ) ].address;

		if ( value == address )
			return middle;

		if ( value < address )
			low = middle + 1;
		else
			high = middle - 1;

	}

	return -1;

}

void debugger::record_cover ( cover_point& item ) {

	item.armed = false;

	AcquireSRWLockExclusive ( &lock );
	cover_hits.push ( item.address - image_base );
	ReleaseSRWLockExclusive ( &lock );

}

void debugger::arm_coverage ( ) {

	base::list < uint64_t > copy;

	AcquireSRWLockShared ( &lock );

	for ( uint64_t rva : cover_wanted )
		copy.push ( rva );

	ReleaseSRWLockShared ( &lock );

	cover.clear ( );

	if ( copy.empty ( ) )
		return;

	uint64_t low = image_base + copy [ 0 ];
	uint64_t high = image_base + copy.back ( ) + 1;
	DWORD old = 0;
	void* region = reinterpret_cast < void* > ( low );
	SIZE_T span = static_cast < SIZE_T > ( high - low );

	VirtualProtectEx ( process, region, span, PAGE_EXECUTE_READWRITE, &old );

	for ( uint64_t rva : copy ) {

		cover_point& item = cover.emplace ( );
		uint8_t trap = 0xCC;
		SIZE_T done = 0;

		item.address = image_base + rva;

		if ( read_memory ( item.address, &item.original, 1 ) && WriteProcessMemory ( process, reinterpret_cast < void* > ( item.address ), &trap, 1, &done ) && done == 1 )
			item.armed = true;

	}

	VirtualProtectEx ( process, region, span, old, &old );
	FlushInstructionCache ( process, region, span );

}

void debugger::set_coverage ( const uint64_t* rvas, int32_t count ) {

	AcquireSRWLockExclusive ( &lock );
	cover_wanted.clear ( );
	cover_hits.clear ( );

	for ( int32_t i = 0; i < count; i++ )
		cover_wanted.push ( rvas [ i ] );

	ReleaseSRWLockExclusive ( &lock );

}

int32_t debugger::take_coverage ( base::list < uint64_t >& out ) {

	AcquireSRWLockExclusive ( &lock );

	int32_t count = cover_hits.count ( );

	for ( uint64_t rva : cover_hits )
		out.push ( rva );

	cover_hits.clear ( );
	ReleaseSRWLockExclusive ( &lock );

	return count;

}