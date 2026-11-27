#include "host/launch/launch.h"
#include "host/loop/loop.h"
#include "runtime/startup/startup.h"

int program_main ( ) {

	host::begin ( );

	if ( host::run_debug_modes ( ) )
		return host::end ( 0 );

	host::launch_options options = host::read_options ( );

	if ( options.serve )
		return host::serve_mcp ( options );

	if ( !host::start_window ( options ) )
		return 1;

	host::run_loop ( );

	return host::end ( 0 );

}