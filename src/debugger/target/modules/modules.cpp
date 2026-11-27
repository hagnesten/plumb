#include "debugger/target/modules/modules.h"
#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include "debugger/target/memory/memory.h"
#include "base/strings/strings.h"
#include <windows.h>

void debugger::path_of ( HANDLE file, wchar_t* out, int32_t room ) {

	out [ 0 ] = 0;

	if ( !file )
		return;

	wchar_t full [ 600 ];
	DWORD length = GetFinalPathNameByHandleW ( file, full, 600, FILE_NAME_NORMALIZED );

	if ( !length || length >= 600 )
		return;

	const wchar_t* from = full;

	if ( full [ 0 ] == L'\\' && full [ 1 ] == L'\\' && full [ 2 ] == L'?' && full [ 3 ] == L'\\' )
		from = full + 4;

	int32_t k = 0;

	for ( ; from [ k ] && k < room - 1; k++ )
		out [ k ] = from [ k ];

	out [ k ] = 0;

}

const wchar_t* debugger::leaf_of ( const wchar_t* path ) {

	const wchar_t* leaf = path;

	for ( const wchar_t* at = path; *at; at++ ) {

		if ( *at == L'\\' || *at == L'/' )
			leaf = at + 1;

	}

	return leaf;

}

uint64_t debugger::image_size ( uint64_t base ) {

	uint8_t header [ 0x400 ] = { };

	if ( !read_memory ( base, header, sizeof ( header ) ) )
		return 0;

	uint32_t pe = *reinterpret_cast < const uint32_t* > ( header + 0x3C );

	if ( pe + 0x58 > sizeof ( header ) )
		return 0;

	return *reinterpret_cast < const uint32_t* > ( header + pe + 0x50 );

}

void debugger::add_module ( uint64_t base, HANDLE file ) {

	module_info item;

	item.base = base;
	item.size = image_size ( base );
	path_of ( file, item.path, 300 );

	AcquireSRWLockExclusive ( &lock );
	loaded.push ( item );
	ReleaseSRWLockExclusive ( &lock );

}

void debugger::module_ready ( uint64_t base ) {

	image_base = base;

	if ( !attach_id && entry )
		arm_temporary ( image_base + entry, purpose::ENTRY );

	arm_coverage ( );
	synced_serial = -1;
	hardware_synced = -1;
	sync_points ( );
	sync_hardware ( );

	wchar_t note [ 200 ] = { };
	int32_t at = 0;

	append_text ( note, at, module_name [ 0 ] ? module_name : leaf_of ( target ) );
	append_text ( note, at, L" is loaded at 0x" );
	append_number ( note, at, base, true );
	log_text ( note );

}

bool debugger::matches_module ( const wchar_t* path ) {

	return module_name [ 0 ] && path [ 0 ] && base::compare_folded ( leaf_of ( path ), module_name ) == 0;

}

int32_t debugger::modules ( base::list < module_info >& out ) {

	out.clear ( );

	AcquireSRWLockShared ( &lock );

	for ( const module_info& item : loaded )
		out.push ( item );

	ReleaseSRWLockShared ( &lock );

	return out.count ( );

}