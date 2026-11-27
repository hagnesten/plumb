#pragma once

typedef void ( __cdecl* runtime_callback ) ( );

extern "C" {

	void runtime_exit ( int code );

	int __cdecl atexit ( runtime_callback callback );

}

namespace runtime {

	extern runtime_callback exit_callbacks [ 64 ];

	extern int exit_callback_count;

}

int program_main ( );