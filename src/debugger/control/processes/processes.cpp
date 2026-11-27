#include "debugger/control/processes/processes.h"
#include <windows.h>
#include <tlhelp32.h>

int32_t debugger::processes ( base::list < process_entry >& out ) {

	out.clear ( );

	HANDLE snap = CreateToolhelp32Snapshot ( TH32CS_SNAPPROCESS, 0 );

	if ( snap == INVALID_HANDLE_VALUE )
		return 0;

	PROCESSENTRY32W item = { };

	item.dwSize = sizeof ( item );

	for ( BOOL more = Process32FirstW ( snap, &item ); more; more = Process32NextW ( snap, &item ) ) {

		process_entry& made = out.emplace ( );
		int32_t k = 0;

		made.id = item.th32ProcessID;

		for ( ; item.szExeFile [ k ] && k < 259; k++ )
			made.name [ k ] = item.szExeFile [ k ];

		made.name [ k ] = 0;

	}

	CloseHandle ( snap );

	return out.count ( );

}