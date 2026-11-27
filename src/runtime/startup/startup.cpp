#include "runtime/startup/startup.h"

extern "C" {

	__declspec ( dllimport ) __declspec ( noreturn ) void __stdcall ExitProcess ( unsigned int code );

}

extern "C" int _fltused = 0;

#pragma section ( ".CRT$XCA", long, read )
#pragma section ( ".CRT$XCZ", long, read )

extern "C" __declspec ( allocate ( ".CRT$XCA" ) ) runtime_callback runtime_initializers_begin [ ] = { nullptr };
extern "C" __declspec ( allocate ( ".CRT$XCZ" ) ) runtime_callback runtime_initializers_end [ ] = { nullptr };

runtime_callback runtime::exit_callbacks [ 64 ];

int runtime::exit_callback_count = 0;

extern "C" int __cdecl atexit ( runtime_callback callback ) {

	if ( runtime::exit_callback_count >= 64 )
		return 1;

	runtime::exit_callbacks [ runtime::exit_callback_count++ ] = callback;

	return 0;

}

extern "C" int __cdecl _purecall ( ) {

	ExitProcess ( 3 );

}

extern "C" void runtime_exit ( int code ) {

	while ( runtime::exit_callback_count > 0 )
		runtime::exit_callbacks [ --runtime::exit_callback_count ] ( );

	ExitProcess ( static_cast < unsigned int > ( code ) );

}

extern "C" void start ( ) {

	for ( runtime_callback* entry = runtime_initializers_begin + 1; entry < runtime_initializers_end; entry++ ) {

		if ( *entry )
			( *entry ) ( );

	}

	runtime_exit ( program_main ( ) );

}