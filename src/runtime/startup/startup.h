#pragma once

extern "C" {

	void runtime_exit ( int code );

	int __cdecl atexit ( void ( __cdecl* callback ) ( ) );

}

int program_main ( );