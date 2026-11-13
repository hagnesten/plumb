#include "ui/app.hh"

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

		if ( debugger::start ( file.path.get ( ), file.entry - file.base, file.wide ( ) ) ) {

			if ( record )
				log ( log_kind::INFO, base::writer ( ).add ( L"Starting " ).add ( file.name.get ( ) ).add ( L" and recording which of its " ).grouped ( static_cast < uint64_t > ( coverage_blocks.count ( ) ) ).add ( L" blocks run." ).text ( ) );
			else
				log ( log_kind::INFO, base::writer ( ).add ( L"Starting " ).add ( file.name.get ( ) ).add ( L" under the debugger. It stops at the entry point." ).text ( ) );

		}

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
