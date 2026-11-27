#include "debugger/target/memory/memory.h"
#include "debugger/core/state/state.h"
#include <windows.h>

bool debugger::read_memory ( uint64_t address, void* out, size_t size ) {

	SIZE_T done = 0;

	return ReadProcessMemory ( process, reinterpret_cast < const void* > ( address ), out, size, &done ) && done == size;

}

bool debugger::write_memory ( uint64_t address, const void* data, size_t size ) {

	SIZE_T done = 0;
	DWORD old = 0;
	void* at = reinterpret_cast < void* > ( address );

	VirtualProtectEx ( process, at, size, PAGE_EXECUTE_READWRITE, &old );

	bool ok = WriteProcessMemory ( process, at, data, size, &done ) && done == size;

	VirtualProtectEx ( process, at, size, old, &old );
	FlushInstructionCache ( process, at, size );

	return ok;

}

bool debugger::write_byte ( uint64_t address, uint8_t value ) {

	return write_memory ( address, &value, 1 );

}

bool debugger::peek ( uint64_t address, void* out, size_t size ) {

	if ( !process || read ( ).state != status::PAUSED )
		return false;

	if ( !read_memory ( address, out, size ) )
		return false;

	uint8_t* bytes = static_cast < uint8_t* > ( out );

	for ( const breakpoint& item : points ) {

		if ( item.armed && item.address >= address && item.address < address + size )
			bytes [ item.address - address ] = item.original;

	}

	for ( const cover_point& item : cover ) {

		if ( item.armed && item.address >= address && item.address < address + size )
			bytes [ item.address - address ] = item.original;

	}

	return true;

}

bool debugger::poke ( uint64_t address, const void* data, size_t size ) {

	if ( !process || read ( ).state != status::PAUSED || !size )
		return false;

	const uint8_t* bytes = static_cast < const uint8_t* > ( data );

	for ( size_t k = 0; k < size; k++ ) {

		uint64_t at = address + k;
		bool trapped = false;

		for ( breakpoint& item : points ) {

			if ( item.armed && item.address == at ) {

				item.original = bytes [ k ];
				trapped = true;

			}

		}

		for ( cover_point& item : cover ) {

			if ( item.armed && item.address == at ) {

				item.original = bytes [ k ];
				trapped = true;

			}

		}

		if ( !trapped && !write_memory ( at, bytes + k, 1 ) )
			return false;

	}

	return true;

}