#include "ui/app.hh"
#include <windows.h>

namespace ui {

	static void insert_sorted ( base::list < uint64_t >& list, uint64_t value ) {

		int32_t low = 0;
		int32_t high = list.count ( );

		while ( low < high ) {

			int32_t middle = ( low + high ) / 2;

			if ( list [ static_cast < size_t > ( middle ) ] < value )
				low = middle + 1;
			else
				high = middle;

		}

		if ( low < list.count ( ) && list [ static_cast < size_t > ( low ) ] == value )
			return;

		list.push ( 0 );

		for ( int32_t i = list.count ( ) - 1; i > low; i-- )
			list [ static_cast < size_t > ( i ) ] = list [ static_cast < size_t > ( i - 1 ) ];

		list [ static_cast < size_t > ( low ) ] = value;

	}

	static bool sorted_contains ( const base::list < uint64_t >& list, uint64_t value ) {

		int32_t low = 0;
		int32_t high = list.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t item = list [ static_cast < size_t > ( middle ) ];

			if ( item == value )
				return true;

			if ( item < value )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return false;

	}

	bool app::is_covered ( uint64_t address ) const {

		return !coverage.empty ( ) && sorted_contains ( coverage, address );

	}

	void app::start_debugging ( bool record ) {

		coverage_blocks.clear ( );

		if ( record && db && analysis_applied ) {

			base::list < uint64_t > starts;

			for ( const analysis::function_info& item : db->functions )
				starts.push ( item.start );

			for ( const analysis::xref& item : db->refs ) {

				if ( item.type != analysis::ref::JUMP && item.type != analysis::ref::BRANCH && item.type != analysis::ref::TABLE )
					continue;

				uint64_t target = file.base + item.target;

				if ( db->is_start ( target ) )
					starts.push ( target );

				if ( item.type == analysis::ref::BRANCH ) {

					x86::instruction ins;
					uint64_t source = file.base + item.source;

					if ( db->decode ( source, ins ) && db->is_start ( ins.next ( ) ) )
						starts.push ( ins.next ( ) );

				}

			}

			base::sort ( starts.data ( ), starts.count ( ), [ ] ( uint64_t a, uint64_t b ) { return a < b; } );

			for ( uint64_t value : starts ) {

				if ( coverage_blocks.empty ( ) || coverage_blocks.back ( ) != value )
					coverage_blocks.push ( value );

			}

			coverage.clear ( );
			coverage_functions = 0;

		}

		base::list < uint64_t > rvas;

		for ( uint64_t value : coverage_blocks )
			rvas.push ( value - file.base );

		debugger::set_coverage ( rvas.data ( ), rvas.count ( ) );
		sync_breakpoints ( );
		sync_hardware ( );

		base::writer host;
		base::writer arguments;
		const wchar_t* given = debug_command.empty ( ) ? L"" : debug_command.get ( );

		if ( file.dll ) {

			if ( given [ 0 ] ) {

				const wchar_t* at = given;
				bool quoted = *at == L'"';

				if ( quoted )
					at++;

				while ( *at && ( quoted ? *at != L'"' : *at != L' ' ) )
					host.add ( *at++ );

				if ( quoted && *at == L'"' )
					at++;

				while ( *at == L' ' )
					at++;

				arguments.add ( at );

			} else {

				wchar_t folder [ 300 ];
				UINT length = file.wide ( ) ? GetSystemDirectoryW ( folder, 300 ) : GetSystemWow64DirectoryW ( folder, 300 );

				if ( !length )
					length = GetSystemDirectoryW ( folder, 300 );

				folder [ length < 300 ? length : 299 ] = 0;
				host.add ( folder ).add ( L"\\rundll32.exe" );
				arguments.add ( L"\"" ).add ( file.path.get ( ) ).add ( L"\"," );

				const wchar_t* export_name = nullptr;

				for ( const loader::exported& item : file.exports ) {

					if ( !export_name && !item.name.empty ( ) )
						export_name = item.name.get ( );

				}

				if ( export_name )
					arguments.add ( export_name );
				else
					arguments.add ( L"#1" );

			}

		} else {

			host.add ( file.path.get ( ) );
			arguments.add ( given );

		}

		if ( debugger::start ( host.text ( ), arguments.text ( ), file.dll ? file.name.get ( ) : L"", file.entry ? file.entry - file.base : 0, file.wide ( ) ) ) {

			if ( record )
				log ( log_kind::INFO, base::writer ( ).add ( L"Starting " ).add ( file.name.get ( ) ).add ( L" and recording which of its " ).grouped ( static_cast < uint64_t > ( coverage_blocks.count ( ) ) ).add ( L" blocks run." ).text ( ) );
			else if ( file.dll )
				log ( log_kind::INFO, base::writer ( ).add ( L"Starting " ).add ( host.text ( ) ).add ( L" to load " ).add ( file.name.get ( ) ).add ( L". It stops at the DLL's entry point." ).text ( ) );
			else
				log ( log_kind::INFO, base::writer ( ).add ( L"Starting " ).add ( file.name.get ( ) ).add ( L" under the debugger. It stops at the entry point." ).text ( ) );

		}

	}

	bool app::attach_debugger ( const wchar_t* text ) {

		base::list < debugger::process_entry > list;
		uint32_t chosen = 0;
		int32_t matches = 0;
		bool digits = text [ 0 ] != 0;

		for ( const wchar_t* at = text; *at; at++ )
			digits = digits && *at >= L'0' && *at <= L'9';

		debugger::processes ( list );

		if ( digits ) {

			for ( const wchar_t* at = text; *at; at++ )
				chosen = chosen * 10 + static_cast < uint32_t > ( *at - L'0' );

			matches = 1;

		} else {

			const wchar_t* wanted = text [ 0 ] ? text : file.name.get ( );
			base::writer with_extension;

			with_extension.add ( wanted ).add ( L".exe" );

			for ( const debugger::process_entry& item : list ) {

				if ( base::compare_folded ( item.name, wanted ) != 0 && base::compare_folded ( item.name, with_extension.text ( ) ) != 0 )
					continue;

				if ( !chosen )
					chosen = item.id;

				matches++;

			}

		}

		if ( !chosen ) {

			log ( log_kind::WARN, text [ 0 ] ? base::writer ( ).add ( L"No running process matches \"" ).add ( text ).add ( L"\"." ).text ( ) : base::writer ( ).add ( file.name.get ( ) ).add ( L" isn't running. Type the name or ID of the process that loads it." ).text ( ) );

			return false;

		}

		const wchar_t* process_name = L"";

		for ( const debugger::process_entry& item : list ) {

			if ( item.id == chosen )
				process_name = item.name;

		}

		bool main_image = !file.dll && base::compare_folded ( process_name, file.name.get ( ) ) == 0;

		coverage_blocks.clear ( );
		debugger::set_coverage ( nullptr, 0 );
		sync_breakpoints ( );
		sync_hardware ( );

		if ( !debugger::attach ( chosen, main_image ? L"" : file.name.get ( ), file.entry ? file.entry - file.base : 0, file.wide ( ) ) )
			return false;

		base::writer line;

		line.add ( L"Attaching to " ).add ( process_name [ 0 ] ? process_name : L"process" ).add ( L" (" ).number ( static_cast < int64_t > ( chosen ) ).add ( L")" );

		if ( matches > 1 )
			line.add ( L", the first of " ).number ( matches ).add ( L" with that name" );

		line.add ( L"." );
		log ( log_kind::INFO, line.text ( ) );

		return true;

	}

	bool app::set_hardware_point ( uint64_t address, const wchar_t* text ) {

		uint64_t rva = address - file.base;
		int32_t slot = -1;

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( hardware_points [ i ].used && hardware_points [ i ].rva == rva )
				slot = i;

		}

		while ( *text == L' ' )
			text++;

		if ( !text [ 0 ] ) {

			if ( slot >= 0 ) {

				hardware_points [ slot ] = debugger::hardware_point ( );
				sync_hardware ( );
				log ( log_kind::INFO, base::writer ( ).add ( L"Hardware breakpoint removed at " ).hex ( address ).text ( ) );

			}

			return slot >= 0;

		}

		debugger::watch_kind kind = debugger::watch_kind::EXECUTE;
		const wchar_t* at = text;

		if ( ( at [ 0 ] == L'r' || at [ 0 ] == L'R' ) && ( at [ 1 ] == L'w' || at [ 1 ] == L'W' ) ) {

			kind = debugger::watch_kind::ACCESS;
			at += 2;

		} else if ( at [ 0 ] == L'a' || at [ 0 ] == L'A' ) {

			kind = debugger::watch_kind::ACCESS;
			at++;

		} else if ( at [ 0 ] == L'w' || at [ 0 ] == L'W' ) {

			kind = debugger::watch_kind::WRITE;
			at++;

		} else if ( at [ 0 ] == L'x' || at [ 0 ] == L'X' || at [ 0 ] == L'e' || at [ 0 ] == L'E' )
			at++;
		else {

			log ( log_kind::WARN, L"Write x, w 4 or rw 8." );

			return false;

		}

		while ( *at == L' ' )
			at++;

		int32_t length = 1;

		if ( *at >= L'0' && *at <= L'9' ) {

			length = 0;

			while ( *at >= L'0' && *at <= L'9' )
				length = length * 10 + ( *at++ - L'0' );

		} else if ( kind != debugger::watch_kind::EXECUTE )
			length = 4;

		if ( kind == debugger::watch_kind::EXECUTE )
			length = 1;

		if ( length != 1 && length != 2 && length != 4 && length != 8 ) {

			log ( log_kind::WARN, L"Hardware breakpoints watch 1, 2, 4 or 8 bytes." );

			return false;

		}

		if ( address & static_cast < uint64_t > ( length - 1 ) ) {

			log ( log_kind::WARN, base::writer ( ).add ( L"A " ).number ( length ).add ( L" byte hardware breakpoint has to start on a multiple of " ).number ( length ).add ( L"." ).text ( ) );

			return false;

		}

		if ( slot < 0 ) {

			for ( int32_t i = 0; i < 4 && slot < 0; i++ ) {

				if ( !hardware_points [ i ].used )
					slot = i;

			}

		}

		if ( slot < 0 ) {

			log ( log_kind::WARN, L"All four hardware breakpoints are in use. Remove one first." );

			return false;

		}

		debugger::hardware_point& item = hardware_points [ slot ];

		item.used = true;
		item.rva = rva;
		item.kind = kind;
		item.length = length;
		sync_hardware ( );
		log ( log_kind::INFO, base::writer ( ).add ( L"Hardware breakpoint " ).number ( slot + 1 ).add ( L" at " ).hex ( address ).add ( kind == debugger::watch_kind::EXECUTE ? L" stops when it runs." : ( kind == debugger::watch_kind::WRITE ? L" stops on writes." : L" stops on any access." ) ).text ( ) );

		return true;

	}

	void app::sync_hardware ( ) {

		debugger::set_hardware ( hardware_points, 4 );

	}

	void app::merge_coverage ( ) {

		base::list < uint64_t > hits;

		if ( !debugger::take_coverage ( hits ) )
			return;

		for ( uint64_t rva : hits ) {

			uint64_t address = file.base + rva;

			for ( int32_t steps = 0; steps < 4096; steps++ ) {

				x86::instruction ins;

				if ( !db || !db->decode ( address, ins ) )
					break;

				insert_sorted ( coverage, address );

				if ( ins.control == x86::flow::JUMP || ins.control == x86::flow::BRANCH || ins.control == x86::flow::RETURN || ins.control == x86::flow::STOP )
					break;

				address = ins.next ( );

				if ( sorted_contains ( coverage_blocks, address ) )
					break;

			}

		}

		coverage_functions = 0;

		if ( db ) {

			for ( const analysis::function_info& item : db->functions ) {

				if ( is_covered ( item.start ) )
					coverage_functions++;

			}

		}

		annotation_revision++;
		pending = base::max ( pending, 2 );

	}

	bool app::evaluate_watch ( const wchar_t* text, uint64_t& value ) const {

		const wchar_t* at = text;
		bool deref = false;
		uint64_t total = 0;
		int32_t sign = 1;
		bool any = false;

		while ( *at == L' ' )
			at++;

		if ( *at == L'[' ) {

			deref = true;
			at++;

		}

		while ( *at && *at != L']' ) {

			while ( *at == L' ' )
				at++;

			if ( *at == L'+' || *at == L'-' ) {

				sign = *at == L'-' ? -1 : 1;
				at++;

				continue;

			}

			wchar_t word [ 200 ];
			int32_t length = 0;

			while ( *at && *at != L'+' && *at != L'-' && *at != L']' && *at != L' ' && length < 199 )
				word [ length++ ] = *at++;

			word [ length ] = 0;

			if ( !length )
				break;

			uint64_t part = 0;
			bool found = false;

			for ( int32_t r = 0; r < 18 && !found; r++ ) {

				const wchar_t* name = debugger::register_name ( r, debug_state.wide );
				int32_t k = 0;

				while ( name [ k ] && word [ k ] && base::fold_case ( name [ k ] ) == base::fold_case ( word [ k ] ) )
					k++;

				if ( name [ 0 ] && !name [ k ] && !word [ k ] ) {

					part = debug_state.registers [ r ];
					found = true;

				}

			}

			if ( !found ) {

				uint64_t address = 0;

				if ( parse_address ( word, address ) ) {

					part = address >= file.base && debug_state.image_base ? address - file.base + debug_state.image_base : address;
					found = true;

				}

			}

			if ( !found )
				return false;

			total = sign < 0 ? total - part : total + part;
			sign = 1;
			any = true;

		}

		if ( !any )
			return false;

		if ( !deref ) {

			value = total;

			return true;

		}

		uint64_t read = 0;

		if ( !debugger::peek ( total, &read, debug_state.wide ? 8 : 4 ) )
			return false;

		value = read;

		return true;

	}

	bool app::return_site ( uint64_t runtime, uint64_t& local ) const {

		if ( !db || !debug_state.image_base || runtime < debug_state.image_base )
			return false;

		uint64_t candidate = runtime - debug_state.image_base + file.base;

		if ( candidate <= file.base || candidate >= file.high ( ) || !db->is_start ( candidate ) )
			return false;

		uint64_t previous = db->instruction_at ( candidate - 1 );
		x86::instruction ins;

		if ( !db->decode ( previous, ins ) || ins.control != x86::flow::CALL || ins.next ( ) != candidate )
			return false;

		local = candidate;

		return true;

	}

}
