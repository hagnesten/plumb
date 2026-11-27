#pragma once

#include "base/types/types.h"

namespace host {

	struct launch_options {

		wchar_t** arguments = nullptr;
		const wchar_t* open_path = nullptr;
		bool serve = false;

	};

	void begin ( );

	bool run_debug_modes ( );

	launch_options read_options ( );

	void release_options ( launch_options& options );

	int32_t serve_mcp ( launch_options& options );

	bool start_window ( launch_options& options );

	bool start_renderer ( );

	void register_window_class ( );

	void create_program ( );

	bool create_main_window ( );

	void style_main_window ( );

	void restore_placement ( );

	bool attach_surface ( );

	void start_frame_timer ( );

	void show_main_window ( );

	void fail ( const wchar_t* message );

	int32_t end ( int32_t code );

}