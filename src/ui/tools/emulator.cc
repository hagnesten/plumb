#include "ui/app.hh"
#include "analysis/emulate.hh"

namespace ui {

	static bool parse_setup ( const wchar_t* text, uint64_t* seed, bool* seeded, base::writer& problem ) {

		const wchar_t* at = text;

		while ( at && *at ) {

			while ( *at == L' ' || *at == L',' )
				at++;

			if ( !*at )
				break;

			wchar_t name [ 16 ];
			int32_t length = 0;

			while ( *at && *at != L'=' && *at != L' ' && length < 15 )
				name [ length++ ] = base::fold_case ( *at++ );

			name [ length ] = 0;

			while ( *at == L' ' )
				at++;

			if ( *at != L'=' ) {

				problem.add ( L"Write registers as  rcx=0x40 rdx=5." );

				return false;

			}

			at++;

			while ( *at == L' ' )
				at++;

			bool hex = at [ 0 ] == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' );
			uint64_t value = 0;
			bool any = false;

			if ( hex )
				at += 2;

			while ( *at && *at != L' ' && *at != L',' ) {

				wchar_t c = base::fold_case ( *at++ );
				uint32_t digit = 0;

				if ( c >= L'0' && c <= L'9' )
					digit = static_cast < uint32_t > ( c - L'0' );
				else if ( hex && c >= L'a' && c <= L'f' )
					digit = static_cast < uint32_t > ( c - L'a' + 10 );
				else {

					problem.add ( L"\"" ).add ( name ).add ( L"\" has a value that isn't a number." );

					return false;

				}

				value = value * ( hex ? 16 : 10 ) + digit;
				any = true;

			}

			int32_t family = -1;

			for ( uint8_t id = x86::RAX; id < x86::AX && family < 0; id++ ) {

				const char* label = x86::register_name ( id );
				int32_t k = 0;

				while ( label && label [ k ] && name [ k ] && static_cast < wchar_t > ( label [ k ] ) == name [ k ] )
					k++;

				if ( label && !label [ k ] && !name [ k ] )
					family = x86::register_family ( id );

			}

			if ( family < 0 || !any ) {

				problem.add ( L"\"" ).add ( name ).add ( L"\" isn't a general register." );

				return false;

			}

			seed [ family ] = value;
			seeded [ family ] = true;

		}

		return true;

	}

	bool app::emulate_function ( uint64_t address, const wchar_t* setup, base::list < wchar_t >* report ) {

		auto say = [ & ] ( log_kind kind, const wchar_t* text ) {

			log ( kind, text );

			if ( report ) {

				for ( const wchar_t* at = text; *at; at++ )
					report->push ( *at );

				report->push ( L'\n' );

			}

		};

		if ( !db || !analysis_applied ) {

			say ( log_kind::WARN, L"Wait for the analysis to finish before emulating." );

			return false;

		}

		int32_t owner = db->function_at ( address );

		if ( owner < 0 ) {

			say ( log_kind::WARN, L"Put the cursor inside a function to emulate it." );

			return false;

		}

		uint64_t seed [ 16 ] = { };
		bool seeded [ 16 ] = { };
		base::writer problem;

		if ( !parse_setup ( setup, seed, seeded, problem ) ) {

			say ( log_kind::FAIL, problem.text ( ) );

			return false;

		}

		uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;
		analysis::emulation result;
		wchar_t name [ 200 ];
		bool wide = file.wide ( );
		double started = base::seconds ( );

		analysis::emulate ( *db, start, seed, seeded, 2000000, result );
		function_name ( owner, name, 200 );

		base::writer summary;

		summary.add ( L"Emulated " ).add ( name ).add ( L" for " ).grouped ( result.steps ).add ( L" instructions in " ).fixed ( ( base::seconds ( ) - started ) * 1000.0, 1 ).add ( L" ms: " ).add ( result.reason.get ( ) );

		if ( !result.returned )
			summary.add ( L" at " ).hex ( result.stopped_at, wide ? 9 : 8 );

		summary.add ( result.returned ? L", " : L". " ).add ( wide ? L"rax = " : L"eax = " ).hex ( wide ? result.registers [ 0 ] : result.registers [ 0 ] & 0xFFFFFFFF );
		say ( result.returned ? log_kind::GOOD : log_kind::WARN, summary.text ( ) );

		int32_t shown_calls = 0;

		for ( const analysis::emulated_call& item : result.calls ) {

			if ( shown_calls++ >= 40 )
				break;

			base::writer line;

			line.add ( L"  calls " );

			if ( symbol_name ( item.target, true, name, 200 ) )
				line.add ( name );
			else
				line.hex ( item.target );

			line.add ( wide ? L" ( rcx=" : L" ( " ).hex ( item.arguments [ 0 ] ).add ( wide ? L", rdx=" : L", " ).hex ( item.arguments [ 1 ] ).add ( L", ... ) at " ).hex ( item.site, wide ? 9 : 8 );
			say ( log_kind::INFO, line.text ( ) );

		}

		bool noted = false;

		search_hits.clear ( );

		for ( const analysis::emulated_string& item : result.strings ) {

			base::writer line;
			base::writer quoted;

			quoted.add ( L"decoded " ).add ( item.wide ? L"L\"" : L"\"" ).add ( item.text.get ( ) ).add ( L"\"" );
			line.add ( L"  " ).add ( quoted.text ( ) ).add ( L" at " ).hex ( item.address, wide ? 9 : 8 );

			if ( item.writer )
				line.add ( L", written at " ).hex ( item.writer, wide ? 9 : 8 );

			say ( log_kind::GOOD, line.text ( ) );

			if ( item.writer && db->is_start ( item.writer ) && !user_text ( comments, item.writer ) ) {

				if ( !noted ) {

					remember_undo ( );
					noted = true;

				}

				set_user_text ( comments, item.writer, quoted.text ( ) );

			}

			if ( item.writer ) {

				search_hit& hit = search_hits.emplace ( );

				hit.address = item.writer;
				hit.note.set ( quoted.text ( ) );

			}

		}

		if ( noted )
			changed ( );

		if ( !search_hits.empty ( ) ) {

			search_query.set ( base::writer ( ).add ( L"strings decoded by " ).add ( name ).text ( ) );
			browsing = panel::SEARCH;
			browser_open = true;
			refresh_filter ( panel::SEARCH );

		} else
			say ( log_kind::INFO, L"  No readable strings were written." );

		return result.returned;

	}

}
