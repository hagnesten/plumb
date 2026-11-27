#include "debugger/events/begin/begin.h"
#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include "debugger/events/publish/publish.h"
#include <windows.h>

bool debugger::begin_target ( ) {

	return attach_id ? attach_target ( ) : create_target ( );

}

bool debugger::attach_target ( ) {

	if ( !DebugActiveProcess ( attach_id ) ) {

		wchar_t note [ 200 ] = { };
		int32_t at = 0;

		append_text ( note, at, L"Couldn't attach to process " );
		append_number ( note, at, attach_id, false );
		append_text ( note, at, L" (Windows error " );
		append_number ( note, at, GetLastError ( ), false );
		append_text ( note, at, L")" );
		publish ( status::FAILED, nullptr, 0, note );

		return false;

	}

	DebugSetProcessKillOnExit ( FALSE );
	publish ( status::RUNNING, nullptr, 0, L"Attached" );

	return true;

}

bool debugger::create_target ( ) {

	STARTUPINFOW startup = { };
	PROCESS_INFORMATION info = { };
	wchar_t command [ 1700 ];
	wchar_t folder [ 600 ];
	int32_t slash = build_command ( command, folder );

	startup.cb = sizeof ( startup );

	if ( !CreateProcessW ( target, command, nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, nullptr, slash ? folder : nullptr, &startup, &info ) ) {

		wchar_t note [ 200 ] = { };
		int32_t at = 0;

		append_text ( note, at, L"Couldn't start the process (Windows error " );
		append_number ( note, at, GetLastError ( ), false );
		append_text ( note, at, L")" );
		publish ( status::FAILED, nullptr, 0, note );

		return false;

	}

	CloseHandle ( info.hThread );
	CloseHandle ( info.hProcess );
	publish ( status::RUNNING, nullptr, 0, L"Process started" );

	return true;

}

int32_t debugger::build_command ( wchar_t* command, wchar_t* folder ) {

	int32_t length = 0;
	int32_t slash = 0;

	command [ length++ ] = L'"';

	for ( int32_t i = 0; target [ i ] && length < 690; i++ ) {

		command [ length++ ] = target [ i ];
		folder [ i ] = target [ i ];

		if ( target [ i ] == L'\\' || target [ i ] == L'/' )
			slash = i;

	}

	command [ length++ ] = L'"';

	if ( command_tail [ 0 ] ) {

		command [ length++ ] = L' ';

		for ( int32_t i = 0; command_tail [ i ] && length < 1690; i++ )
			command [ length++ ] = command_tail [ i ];

	}

	command [ length ] = 0;
	folder [ slash ] = 0;

	return slash;

}