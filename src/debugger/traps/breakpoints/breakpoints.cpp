#include "debugger/traps/breakpoints/breakpoints.h"
#include "debugger/core/state/state.h"
#include "debugger/target/memory/memory.h"
#include <windows.h>

void debugger::arm ( breakpoint& item ) {

	if ( item.armed )
		return;

	int32_t shared_point = find_cover ( item.address );

	if ( shared_point >= 0 && cover [ static_cast < size_t > ( shared_point ) ].armed ) {

		item.original = cover [ static_cast < size_t > ( shared_point ) ].original;
		record_cover ( cover [ static_cast < size_t > ( shared_point ) ] );
		item.armed = true;

		return;

	}

	for ( const breakpoint& other : points ) {

		if ( &other != &item && other.armed && other.address == item.address ) {

			item.original = other.original;
			item.armed = true;

			return;

		}

	}

	if ( read_memory ( item.address, &item.original, 1 ) && write_byte ( item.address, 0xCC ) )
		item.armed = true;

}

void debugger::arm_temporary ( uint64_t address, purpose use ) {

	breakpoint& item = points.emplace ( );

	item.address = address;
	item.temporary = true;
	item.use = use;
	arm ( item );

}

bool debugger::shared_trap ( const breakpoint& item ) {

	for ( const breakpoint& other : points ) {

		if ( &other != &item && other.armed && other.address == item.address )
			return true;

	}

	return false;

}

void debugger::disarm ( breakpoint& item ) {

	if ( !item.armed )
		return;

	if ( !shared_trap ( item ) )
		write_byte ( item.address, item.original );

	item.armed = false;

}

int32_t debugger::find_point ( uint64_t address ) {

	int32_t found = -1;

	for ( int32_t i = 0; i < points.count ( ); i++ ) {

		const breakpoint& item = points [ static_cast < size_t > ( i ) ];

		if ( item.address == address && ( found < 0 || ( item.armed && !points [ static_cast < size_t > ( found ) ].armed ) || ( item.temporary && !points [ static_cast < size_t > ( found ) ].temporary && item.armed ) ) )
			found = i;

	}

	return found;

}

void debugger::sync_points ( ) {

	long serial = __atomic_load_n ( &wanted_serial, __ATOMIC_ACQUIRE );

	if ( serial == synced_serial || !image_base )
		return;

	synced_serial = serial;

	base::list < uint64_t > copy;

	AcquireSRWLockShared ( &lock );

	for ( uint64_t rva : wanted )
		copy.push ( image_base + rva );

	ReleaseSRWLockShared ( &lock );

	for ( int32_t i = points.count ( ) - 1; i >= 0; i-- ) {

		breakpoint& item = points [ static_cast < size_t > ( i ) ];
		bool keep = item.temporary;

		for ( uint64_t address : copy ) {

			if ( address == item.address )
				keep = true;

		}

		if ( !keep ) {

			if ( reinsert == item.address )
				reinsert = 0;

			disarm ( item );
			points.remove_at ( static_cast < size_t > ( i ) );

		}

	}

	for ( uint64_t address : copy ) {

		bool present = false;

		for ( const breakpoint& item : points )
			present = present || ( item.address == address && !item.temporary );

		if ( present )
			continue;

		breakpoint& item = points.emplace ( );

		item.address = address;

		if ( address != reinsert )
			arm ( item );

	}

}

void debugger::release_traps ( ) {

	for ( breakpoint& item : points )
		disarm ( item );

	for ( cover_point& item : cover ) {

		if ( item.armed ) {

			write_byte ( item.address, item.original );
			item.armed = false;

		}

	}

	for ( int32_t i = 0; i < 4; i++ )
		hardware [ i ].used = false;

	for ( const thread_entry& item : threads )
		debug_registers ( item.handle, true );

}

void debugger::set_breakpoints ( const uint64_t* rvas, int32_t count ) {

	AcquireSRWLockExclusive ( &lock );
	wanted.clear ( );

	for ( int32_t i = 0; i < count; i++ )
		wanted.push ( rvas [ i ] );

	ReleaseSRWLockExclusive ( &lock );
	InterlockedIncrement ( &wanted_serial );

	if ( wake )
		SetEvent ( wake );

}