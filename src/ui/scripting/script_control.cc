#include "ui/scripting.hh"
#include <windows.h>

namespace ui {

	struct script_setting {

		const wchar_t* key;
		bool app::* member;
		bool code;

	};

	static const script_setting script_settings [ ] = {

		{ L"light", &app::light, false },
		{ L"spaced", &app::c_spaced, true },
		{ L"no_ternary", &app::c_no_ternary, true },
		{ L"hide_casts", &app::c_hide_casts, true },
		{ L"collapse_locals", &app::c_collapse_locals, true },
		{ L"name_virtuals", &app::name_virtuals, true },
		{ L"show_bytes", &app::show_bytes, false },
		{ L"autosave", &app::autosave, false },
		{ L"jit", &app::script_jit, false },

	};

	static const wchar_t* view_words [ ] = { L"listing", L"graph", L"c", L"hex", L"headers", L"calls" };

	static const wchar_t* state_word ( debugger::status value ) {

		switch ( value ) {

			case debugger::status::RUNNING: return L"running";
			case debugger::status::PAUSED: return L"paused";
			case debugger::status::EXITED: return L"exited";
			case debugger::status::FAILED: return L"failed";
			default: return L"idle";

		}

	}

	static int32_t font_role ( const wchar_t* text ) {

		static const wchar_t* words [ ] = { L"interface", L"disassembly", L"c" };

		for ( int32_t i = 0; i < 3; i++ ) {

			if ( base::compare_folded ( text, words [ i ] ) == 0 )
				return i;

		}

		return -1;

	}

	static int32_t color_choice ( const wchar_t* text ) {

		for ( int32_t i = 0; i < tone_choice_count ( ); i++ ) {

			const char* key = tone_choice_key ( i );
			int32_t k = 0;

			while ( key [ k ] && text [ k ] && base::fold_case ( static_cast < wchar_t > ( key [ k ] ) ) == base::fold_case ( text [ k ] ) )
				k++;

			if ( !key [ k ] && !text [ k ] )
				return i;

		}

		return -1;

	}

	bool app::script_control ( script_context& call ) {

		if ( call.is ( L"is_open" ) ) {

			if ( call.takes ( 0, 0 ) )
				call.give_number ( file.loaded ( ) );

			return true;

		}

		if ( call.is ( L"open" ) ) {

			const wchar_t* path = nullptr;

			if ( !call.takes ( 1, 1 ) || !( path = call.text ( 0 ) ) )
				return true;

			call.give_number ( open_file ( path ) );

			return true;

		}

		if ( call.is ( L"close" ) ) {

			if ( call.takes ( 0, 0 ) ) {

				close_file ( );
				call.give_number ( !file.loaded ( ) );

			}

			return true;

		}

		if ( call.is ( L"commands" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			for ( int32_t i = 0; i < static_cast < int32_t > ( command::COUNT ); i++ )
				call.push_text ( made, command_label ( static_cast < command > ( i ) ) );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"command" ) ) {

			const wchar_t* wanted = nullptr;

			if ( !call.takes ( 1, 1 ) || !( wanted = call.text ( 0 ) ) )
				return true;

			for ( int32_t i = 0; i < static_cast < int32_t > ( command::COUNT ); i++ ) {

				command action = static_cast < command > ( i );

				if ( base::compare_folded ( command_label ( action ), wanted ) != 0 )
					continue;

				if ( action == command::RUN_SCRIPT || action == command::RERUN_SCRIPT )
					return call.fail ( L"A script can't start another script. Use #include instead." );

				if ( !enabled ( action ) ) {

					call.give_number ( 0 );

					return true;

				}

				run ( action );
				call.give_number ( 1 );

				return true;

			}

			return call.fail ( base::writer ( ).add ( L"There's no command called \"" ).add ( wanted ).add ( L"\". commands() lists them." ).text ( ) );

		}

		if ( call.is ( L"setting" ) || call.is ( L"set_setting" ) ) {

			const wchar_t* wanted = nullptr;
			bool setting = call.is ( L"set_setting" );

			if ( !call.takes ( setting ? 2 : 1, setting ? 2 : 1 ) || !( wanted = call.text ( 0 ) ) )
				return true;

			for ( const script_setting& item : script_settings ) {

				if ( base::compare_folded ( item.key, wanted ) != 0 )
					continue;

				bool& slot = this->*item.member;

				if ( !setting ) {

					call.give_number ( slot );

					return true;

				}

				bool given = false;

				if ( !call.flag ( 1, given ) )
					return true;

				if ( item.member == &app::light )
					set_theme ( given );
				else
					slot = given;

				if ( item.code ) {

					names_revision++;
					code_cache.clear ( );

				}

				pending = base::max ( pending, 3 );
				save_settings ( );
				call.give_number ( 1 );

				return true;

			}

			return call.fail ( L"Settings are light, spaced, no_ternary, hide_casts, collapse_locals, name_virtuals, show_bytes, autosave and jit." );

		}

		if ( call.is ( L"font" ) || call.is ( L"set_font" ) ) {

			const wchar_t* role_text = nullptr;
			bool setting = call.is ( L"set_font" );

			if ( !call.takes ( setting ? 2 : 1, setting ? 3 : 1 ) || !( role_text = call.text ( 0 ) ) )
				return true;

			int32_t role = font_role ( role_text );

			if ( role < 0 )
				return call.fail ( L"Font roles are \"interface\", \"disassembly\" and \"c\"." );

			if ( !setting ) {

				script::value made = call.list ( );

				call.push_text ( made, font_names [ role ] );
				call.push_number ( made, static_cast < int64_t > ( font_sizes [ role ] + 0.5f ) );
				call.give_list ( made );

				return true;

			}

			const wchar_t* face = nullptr;
			int64_t size = static_cast < int64_t > ( font_sizes [ role ] + 0.5f );

			if ( call.has ( 1 ) && !( face = call.text ( 1 ) ) )
				return true;

			if ( call.has ( 2 ) && !call.number ( 2, size ) )
				return true;

			if ( size < 6 || size > 48 )
				return call.fail ( L"Font sizes go from 6 to 48." );

			if ( face && face [ 0 ] ) {

				int32_t k = 0;

				for ( ; face [ k ] && k < 63; k++ )
					font_names [ role ] [ k ] = face [ k ];

				font_names [ role ] [ k ] = 0;

			}

			font_sizes [ role ] = static_cast < float > ( size );
			fonts_dirty = true;
			font_fields_ready = false;
			save_settings ( );
			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"colors" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			for ( int32_t i = 0; i < tone_choice_count ( ); i++ ) {

				const char* key = tone_choice_key ( i );
				wchar_t wide [ 64 ];
				int32_t k = 0;

				for ( ; key [ k ] && k < 63; k++ )
					wide [ k ] = static_cast < wchar_t > ( key [ k ] );

				wide [ k ] = 0;
				call.push_text ( made, wide );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"color" ) || call.is ( L"set_color" ) || call.is ( L"reset_color" ) ) {

			const wchar_t* wanted = nullptr;
			bool setting = call.is ( L"set_color" );

			if ( !call.takes ( setting ? 2 : 1, setting ? 2 : 1 ) || !( wanted = call.text ( 0 ) ) )
				return true;

			int32_t choice = color_choice ( wanted );

			if ( choice < 0 )
				return call.fail ( base::writer ( ).add ( L"There's no color called \"" ).add ( wanted ).add ( L"\". colors() lists them." ).text ( ) );

			tone which = tone_choice ( choice );
			int32_t slot = static_cast < int32_t > ( which );

			if ( call.is ( L"color" ) )
				call.give_number ( tone_hex ( which, light ) );
			else if ( setting ) {

				int64_t value = 0;

				if ( !call.number ( 1, value ) )
					return true;

				set_custom_color ( slot, static_cast < uint32_t > ( value ) );
				save_settings ( );
				call.give_number ( 1 );

			} else {

				custom_set [ light ? 1 : 0 ] [ slot ] = false;
				pending = base::max ( pending, 2 );
				save_settings ( );
				call.give_number ( 1 );

			}

			return true;

		}

		if ( call.is ( L"clear_output" ) ) {

			if ( call.takes ( 0, 0 ) )
				output.clear ( );

			return true;

		}

		if ( call.is ( L"debug_state" ) ) {

			if ( call.takes ( 0, 0 ) ) {

				poll_debugger ( );
				call.give_text ( state_word ( debugger::active ( ) ? debug_state.state : ( debug_state.state == debugger::status::RUNNING || debug_state.state == debugger::status::PAUSED ? debugger::status::IDLE : debug_state.state ) ) );

			}

			return true;

		}

		if ( call.is ( L"debug_wait" ) ) {

			int64_t limit = 10000;

			if ( !call.takes ( 0, 1 ) || ( call.has ( 0 ) && !call.number ( 0, limit ) ) )
				return true;

			double until = base::seconds ( ) + static_cast < double > ( base::clamp ( limit, 0ll, 600000ll ) ) / 1000.0;

			for ( ;; ) {

				poll_debugger ( );

				if ( !debugger::active ( ) || debug_state.state != debugger::status::RUNNING || base::seconds ( ) >= until )
					break;

				Sleep ( 5 );

			}

			call.give_text ( state_word ( debug_state.state ) );

			return true;

		}

		if ( call.is ( L"debug_note" ) ) {

			if ( call.takes ( 0, 0 ) ) {

				poll_debugger ( );

				if ( debug_state.note [ 0 ] )
					call.give_text ( debug_state.note );

			}

			return true;

		}

		if ( call.is ( L"debug_modules" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			base::list < debugger::module_info > list;
			script::value made = call.list ( );

			debugger::modules ( list );

			for ( const debugger::module_info& item : list ) {

				script::value row = call.list ( );

				call.push_text ( row, item.path );
				call.push_number ( row, static_cast < int64_t > ( item.base ) );
				call.push_number ( row, static_cast < int64_t > ( item.size ) );
				call.push_list ( made, row );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"processes" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			base::list < debugger::process_entry > list;
			script::value made = call.list ( );

			debugger::processes ( list );

			for ( const debugger::process_entry& item : list ) {

				script::value row = call.list ( );

				call.push_number ( row, item.id );
				call.push_text ( row, item.name );
				call.push_list ( made, row );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"debug_detach" ) || call.is ( L"debug_step_out" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			bool detach = call.is ( L"debug_detach" );

			if ( !debugger::active ( ) || ( detach ? !debug_state.attached : debug_state.state != debugger::status::PAUSED ) ) {

				call.give_number ( 0 );

				return true;

			}

			uint32_t before = debug_state.serial;

			debugger::send ( detach ? debugger::order::DETACH : debugger::order::STEP_OUT );

			for ( double until = base::seconds ( ) + 1.0; base::seconds ( ) < until; Sleep ( 1 ) ) {

				poll_debugger ( );

				if ( debug_state.serial != before )
					break;

			}

			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"debug_pause" ) || call.is ( L"debug_stop" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			if ( !debugger::active ( ) ) {

				call.give_number ( 0 );

				return true;

			}

			bool stopping = call.is ( L"debug_stop" );

			run ( stopping ? command::DEBUG_STOP : command::DEBUG_PAUSE );

			for ( double until = base::seconds ( ) + 5.0; base::seconds ( ) < until; Sleep ( 2 ) ) {

				poll_debugger ( );

				if ( stopping ? !debugger::active ( ) : debug_state.state != debugger::status::RUNNING )
					break;

			}

			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"registers" ) || call.is ( L"register" ) || call.is ( L"pc" ) ) {

			const wchar_t* wanted = nullptr;
			bool all = call.is ( L"registers" );
			bool one = call.is ( L"register" );

			if ( !call.takes ( one ? 1 : 0, one ? 1 : 0 ) || ( one && !( wanted = call.text ( 0 ) ) ) )
				return true;

			poll_debugger ( );

			if ( !debugger::active ( ) || debug_state.state != debugger::status::PAUSED )
				return true;

			if ( call.is ( L"pc" ) ) {

				call.give_number ( static_cast < int64_t > ( debug_state.pc ) );

				return true;

			}

			script::value made = call.list ( );

			for ( int32_t i = 0; i < 18; i++ ) {

				const wchar_t* label = debugger::register_name ( i, debug_state.wide );

				if ( !label || !label [ 0 ] )
					continue;

				if ( one ) {

					if ( base::compare_folded ( label, wanted ) == 0 ) {

						call.give_number ( static_cast < int64_t > ( debug_state.registers [ i ] ) );

						return true;

					}

					continue;

				}

				script::value row = call.list ( );

				call.push_text ( row, label );
				call.push_number ( row, static_cast < int64_t > ( debug_state.registers [ i ] ) );
				call.push_list ( made, row );

			}

			if ( all )
				call.give_list ( made );
			else
				return call.fail ( base::writer ( ).add ( L"There's no register called " ).add ( wanted ).add ( L"." ).text ( ) );

			return true;

		}

		if ( call.is ( L"debug_read" ) ) {

			int64_t where = 0;
			int64_t length = 0;

			if ( !call.takes ( 2, 2 ) || !call.number ( 0, where ) || !call.number ( 1, length ) )
				return true;

			if ( length < 0 || length > 1024 * 1024 )
				return call.fail ( L"debug_read reads up to 1 MB at a time." );

			poll_debugger ( );

			if ( !debugger::active ( ) || debug_state.state != debugger::status::PAUSED )
				return true;

			uint64_t at = static_cast < uint64_t > ( where );

			if ( file.loaded ( ) && at >= file.base && at < file.high ( ) )
				at = at - file.base + debug_state.image_base;

			base::list < uint8_t > bytes;

			bytes.resize ( static_cast < size_t > ( length ) );

			if ( length && !debugger::peek ( at, bytes.data ( ), static_cast < size_t > ( length ) ) )
				return true;

			script::value made = call.list ( );

			for ( uint8_t item : bytes )
				call.push_number ( made, item );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"debug_write" ) ) {

			int64_t where = 0;

			if ( !call.takes ( 2, 2 ) || !call.number ( 0, where ) )
				return true;

			base::list < uint8_t > bytes;
			const script::value& given = call.args [ 1 ];

			if ( given.type == script::kind::NUMBER )
				bytes.push ( static_cast < uint8_t > ( given.number ) );
			else if ( given.type == script::kind::LIST ) {

				for ( int32_t i = 0; i < call.engine->list_count ( given ); i++ ) {

					script::value item = call.engine->list_item ( given, i );

					if ( item.type != script::kind::NUMBER )
						return call.fail ( L"debug_write takes a byte or a list of bytes." );

					bytes.push ( static_cast < uint8_t > ( item.number ) );

				}

			} else
				return call.fail ( L"debug_write takes a byte or a list of bytes." );

			poll_debugger ( );

			if ( !debugger::active ( ) || debug_state.state != debugger::status::PAUSED ) {

				call.give_number ( 0 );

				return true;

			}

			uint64_t at = static_cast < uint64_t > ( where );

			if ( file.loaded ( ) && at >= file.base && at < file.high ( ) )
				at = at - file.base + debug_state.image_base;

			call.give_number ( debugger::poke ( at, bytes.data ( ), static_cast < size_t > ( bytes.count ( ) ) ) );

			return true;

		}

		if ( call.is ( L"debug_evaluate" ) ) {

			const wchar_t* given = nullptr;
			uint64_t value = 0;

			if ( !call.takes ( 1, 1 ) || !( given = call.text ( 0 ) ) )
				return true;

			poll_debugger ( );

			if ( debugger::active ( ) && debug_state.state == debugger::status::PAUSED && evaluate_watch ( given, value ) )
				call.give_number ( static_cast < int64_t > ( value ) );

			return true;

		}

		if ( !file.loaded ( ) )
			return false;

		if ( call.is ( L"save" ) ) {

			if ( call.takes ( 0, 0 ) ) {

				save_project ( );
				call.give_number ( !dirty );

			}

			return true;

		}

		if ( call.is ( L"undo" ) || call.is ( L"redo" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			bool redo = call.is ( L"redo" );
			int32_t before = ( redo ? redo_stack : undo_stack ).count ( );

			undo ( redo );
			call.give_number ( ( redo ? redo_stack : undo_stack ).count ( ) < before );

			return true;

		}

		if ( call.is ( L"file_path" ) || call.is ( L"file_name" ) || call.is ( L"machine" ) || call.is ( L"project_path" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			if ( call.is ( L"project_path" ) ) {

				if ( !project_file.empty ( ) )
					call.give_text ( project_file.get ( ) );

				return true;

			}

			call.give_text ( call.is ( L"file_path" ) ? file.path.get ( ) : ( call.is ( L"file_name" ) ? file.name.get ( ) : file.machine_name ( ) ) );

			return true;

		}

		if ( call.is ( L"file_size" ) || call.is ( L"is_64bit" ) || call.is ( L"is_dll" ) || call.is ( L"is_dirty" ) ) {

			if ( call.takes ( 0, 0 ) )
				call.give_number ( call.is ( L"file_size" ) ? static_cast < int64_t > ( file.size ) : ( call.is ( L"is_64bit" ) ? file.wide ( ) : ( call.is ( L"is_dll" ) ? file.dll : dirty ) ) );

			return true;

		}

		if ( call.is ( L"back" ) || call.is ( L"forward" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			bool ahead = call.is ( L"forward" );
			bool can = !( ahead ? now->forward_stack : now->back_stack ).empty ( );

			if ( can )
				run ( ahead ? command::FORWARD : command::BACK );

			call.give_number ( can );

			return true;

		}

		if ( call.is ( L"current_view" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			int32_t index = static_cast < int32_t > ( showing );

			if ( index >= 0 && index < 6 )
				call.give_text ( view_words [ index ] );

			return true;

		}

		if ( call.is ( L"view" ) ) {

			const wchar_t* wanted = nullptr;

			if ( !call.takes ( 1, 1 ) || !( wanted = call.text ( 0 ) ) )
				return true;

			int32_t found = -1;

			for ( int32_t i = 0; i < 6; i++ ) {

				if ( base::compare_folded ( view_words [ i ], wanted ) == 0 )
					found = i;

			}

			if ( base::compare_folded ( wanted, L"pseudocode" ) == 0 )
				found = 2;

			if ( found < 0 )
				return call.fail ( L"Views are \"listing\", \"graph\", \"c\", \"hex\", \"headers\" and \"calls\"." );

			view kind = static_cast < view > ( found );

			if ( kind == showing ) {

				call.give_number ( 1 );

				return true;

			}

			command action = command::VIEW_LISTING;

			if ( kind == view::GRAPH || ( kind == view::LISTING && showing == view::GRAPH ) )
				action = command::TOGGLE_GRAPH;
			else if ( kind == view::PSEUDOCODE )
				action = command::VIEW_PSEUDOCODE;
			else if ( kind == view::HEX )
				action = command::VIEW_HEX;
			else if ( kind == view::HEADERS )
				action = command::VIEW_HEADERS;
			else if ( kind == view::CALLS )
				action = command::VIEW_CALLS;

			if ( !enabled ( action ) ) {

				call.give_number ( 0 );

				return true;

			}

			run ( action );
			select_state ( current_document ( ) );
			call.give_number ( showing == kind );

			return true;

		}

		if ( call.is ( L"debug_start" ) || call.is ( L"debug_continue" ) || call.is ( L"debug_step_into" ) || call.is ( L"debug_step_over" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			command action = call.is ( L"debug_step_into" ) ? command::DEBUG_STEP_INTO : ( call.is ( L"debug_step_over" ) ? command::DEBUG_STEP_OVER : command::DEBUG_RUN );
			bool running = debugger::active ( );

			if ( call.is ( L"debug_start" ) && running ) {

				call.give_number ( 0 );

				return true;

			}

			if ( !call.is ( L"debug_start" ) && ( !running || debug_state.state != debugger::status::PAUSED ) ) {

				call.give_number ( 0 );

				return true;

			}

			if ( !running && file.type != loader::format::PE32 && file.type != loader::format::PE64 )
				return call.fail ( L"Only Windows executables and DLLs can be debugged." );

			uint32_t before = debug_state.serial;

			run ( action );

			if ( running ) {

				double until = base::seconds ( ) + 1.0;

				while ( base::seconds ( ) < until ) {

					poll_debugger ( );

					if ( debug_state.serial != before )
						break;

					Sleep ( 1 );

				}

			}

			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"debug_attach" ) ) {

			const wchar_t* given = L"";

			if ( !call.takes ( 0, 1 ) || ( call.has ( 0 ) && !( given = call.text ( 0 ) ) ) )
				return true;

			if ( call.has ( 0 ) && call.args [ 0 ].type == script::kind::NUMBER )
				return call.fail ( L"Pass the process ID as text, like debug_attach(\"1234\")." );

			if ( debugger::active ( ) ) {

				call.give_number ( 0 );

				return true;

			}

			call.give_number ( attach_debugger ( given ) );

			return true;

		}

		if ( call.is ( L"debug_run_to" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) )
				return true;

			if ( debug_state.state != debugger::status::PAUSED ) {

				call.give_number ( 0 );

				return true;

			}

			uint32_t before = debug_state.serial;

			debugger::run_to ( where - file.base );

			for ( double until = base::seconds ( ) + 1.0; base::seconds ( ) < until; Sleep ( 1 ) ) {

				poll_debugger ( );

				if ( debug_state.serial != before )
					break;

			}

			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"set_hardware_breakpoint" ) || call.is ( L"del_hardware_breakpoint" ) ) {

			uint64_t where = 0;
			const wchar_t* spec = L"";
			bool adding = call.is ( L"set_hardware_breakpoint" );

			if ( !call.takes ( adding ? 1 : 1, adding ? 2 : 1 ) || !call.address ( 0, where ) )
				return true;

			if ( adding ) {

				spec = L"x";

				if ( call.has ( 1 ) && !( spec = call.text ( 1 ) ) )
					return true;

			}

			call.give_number ( set_hardware_point ( where, spec ) );

			return true;

		}

		if ( call.is ( L"hardware_breakpoints" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			static const wchar_t* const kinds [ 3 ] = { L"x", L"w", L"rw" };
			script::value made = call.list ( );

			for ( const debugger::hardware_point& item : hardware_points ) {

				if ( !item.used )
					continue;

				script::value row = call.list ( );

				call.push_number ( row, static_cast < int64_t > ( file.base + item.rva ) );
				call.push_text ( row, kinds [ static_cast < int32_t > ( item.kind ) ] );
				call.push_number ( row, item.length );
				call.push_list ( made, row );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"debug_command" ) || call.is ( L"set_debug_command" ) ) {

			const wchar_t* given = nullptr;
			bool setting = call.is ( L"set_debug_command" );

			if ( !call.takes ( setting ? 1 : 0, setting ? 1 : 0 ) || ( setting && !( given = call.text ( 0 ) ) ) )
				return true;

			if ( setting ) {

				debug_command.set ( given );
				call.give_number ( 1 );

			} else if ( !debug_command.empty ( ) )
				call.give_text ( debug_command.get ( ) );

			return true;

		}

		if ( call.is ( L"breakpoints" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			for ( uint64_t item : breakpoints )
				call.push_number ( made, static_cast < int64_t > ( item ) );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"add_breakpoint" ) || call.is ( L"del_breakpoint" ) || call.is ( L"set_condition" ) || call.is ( L"condition" ) ) {

			uint64_t where = 0;
			bool adding = call.is ( L"add_breakpoint" );
			bool rule = call.is ( L"set_condition" );
			bool reading = call.is ( L"condition" );

			if ( !call.takes ( 1, adding || rule ? 2 : 1 ) || !call.address ( 0, where ) )
				return true;

			if ( reading ) {

				const wchar_t* found = user_text ( conditions, where );

				if ( found && found [ 0 ] )
					call.give_text ( found );

				return true;

			}

			const wchar_t* given = nullptr;

			if ( ( adding || rule ) && call.has ( 1 ) && !( given = call.text ( 1 ) ) )
				return true;

			if ( given && given [ 0 ] ) {

				uint64_t zeros [ 18 ] = { };
				bool valid = false;

				debugger::test_condition ( given, zeros, file.wide ( ), valid );

				if ( !valid )
					return call.fail ( L"Conditions look like  rax == 0,  rcx > 0x10  or  rip != 140001000." );

			}

			if ( rule && !given )
				given = L"";

			bool had = is_breakpoint ( where );

			script_touch ( );

			if ( call.is ( L"del_breakpoint" ) ) {

				for ( int32_t i = breakpoints.count ( ) - 1; i >= 0; i-- ) {

					if ( breakpoints [ static_cast < size_t > ( i ) ] == where )
						breakpoints.remove_at ( static_cast < size_t > ( i ) );

				}

				set_user_text ( conditions, where, L"" );

			} else {

				if ( given )
					set_user_text ( conditions, where, given );

				if ( !had && ( adding || ( given && given [ 0 ] ) ) )
					breakpoints.push ( where );

			}

			sync_breakpoints ( );
			call.give_number ( call.is ( L"del_breakpoint" ) ? had : 1 );

			return true;

		}

		return false;

	}

}
