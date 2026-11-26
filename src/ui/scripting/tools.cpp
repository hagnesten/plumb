#include "ui/scripting.h"
#include "base/strings/strings.h"
#include "analysis/signatures.h"

namespace ui {

	static bool pattern_style_of ( const wchar_t* text, analysis::pattern_style& out ) {

		static const wchar_t* words [ ] = { L"ida", L"x64dbg", L"code", L"array" };

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( base::compare_folded ( text, words [ i ] ) == 0 ) {

				out = static_cast < analysis::pattern_style > ( i );

				return true;

			}

		}

		return false;

	}

	bool app::script_tools ( script_context& call ) {

		if ( call.is ( L"wait" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			for ( int32_t round = 0; round < 4; round++ ) {

				wait_analysis ( );

				if ( analysis_done && ( !analysis_applied || building ) )
					analysis_finished ( );

				if ( !rebuild_pending || building )
					break;

			}

			call.give_number ( db && analysis_applied );

			return true;

		}

		if ( call.is ( L"reanalyze" ) ) {

			if ( call.takes ( 0, 0 ) ) {

				reanalyze ( );
				call.give_number ( 1 );

			}

			return true;

		}

		if ( call.is ( L"search" ) || call.is ( L"query" ) ) {

			const wchar_t* given = nullptr;

			if ( !call.takes ( 1, 1 ) || !( given = call.text ( 0 ) ) || !call.need_analysis ( ) )
				return true;

			bool query = call.is ( L"query" );

			if ( query ) {

				while ( *given == L' ' )
					given++;

				if ( base::compare_folded ( given, L"functions" ) == 0 )
					given += 9;
				else if ( given [ 0 ] && given [ 9 ] == L' ' ) {

					wchar_t head [ 10 ];

					for ( int32_t i = 0; i < 9; i++ )
						head [ i ] = given [ i ];

					head [ 9 ] = 0;

					if ( base::compare_folded ( head, L"functions" ) == 0 )
						given += 10;

				}

				if ( !run_query ( given ) )
					return call.fail ( L"That query doesn't parse. Try \"calls VirtualAlloc and size > 0x100 and not thunk\"." );

			} else
				search_everything ( given );

			refresh_filter ( panel::SEARCH );

			script::value made = call.list ( );

			for ( const search_hit& item : search_hits ) {

				if ( query ) {

					call.push_number ( made, static_cast < int64_t > ( item.address ) );

					continue;

				}

				script::value row = call.list ( );

				call.push_number ( row, static_cast < int64_t > ( item.address ) );
				call.push_number ( row, item.length );
				call.push_text ( row, item.note.empty ( ) ? L"" : item.note.get ( ) );
				call.push_list ( made, row );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"make_signature" ) || call.is ( L"signature_info" ) ) {

			uint64_t where = 0;
			analysis::pattern_style style = analysis::pattern_style::IDA;
			const wchar_t* style_text = nullptr;

			if ( !call.takes ( 1, 2 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
				return true;

			if ( call.has ( 1 ) && ( !( style_text = call.text ( 1 ) ) || !pattern_style_of ( style_text, style ) ) )
				return style_text ? call.fail ( L"Signature styles are \"ida\", \"x64dbg\", \"code\" and \"array\"." ) : true;

			analysis::signature found;
			bool unique = analysis::make_signature ( *db, where, found );
			base::list < wchar_t > text;

			analysis::format_pattern ( found.bytes, style, text );

			int32_t length = text.count ( );

			while ( length > 0 && !text [ static_cast < size_t > ( length - 1 ) ] )
				length--;

			if ( call.is ( L"make_signature" ) ) {

				if ( unique )
					call.give_text ( text.data ( ), length );

				return true;

			}

			script::value made = call.list ( );

			call.push_text ( made, text.data ( ), length );
			call.push_number ( made, unique );
			call.push_number ( made, found.matches );
			call.push_number ( made, static_cast < int64_t > ( found.start ) );
			call.push_number ( made, found.via_reference );
			call.push_number ( made, found.target_offset );
			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"find_signature" ) ) {

			const wchar_t* given = nullptr;
			int64_t limit = 20000;

			if ( !call.takes ( 1, 2 ) || !( given = call.text ( 0 ) ) || ( call.has ( 1 ) && !call.number ( 1, limit ) ) )
				return true;

			analysis::pattern find;

			if ( !analysis::parse_pattern ( given, find ) )
				return call.fail ( base::writer ( ).add ( L"Can't read \"" ).add ( given ).add ( L"\" as a pattern. Try \"48 8B 05 ? ? ? ?\"." ).text ( ) );

			base::list < uint64_t > hits;

			analysis::scan ( file, find, false, hits, static_cast < int32_t > ( base::clamp ( limit, 1ll, 1000000ll ) ) );

			script::value made = call.list ( );

			for ( uint64_t at : hits )
				call.push_number ( made, static_cast < int64_t > ( at ) );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"xor_decode" ) ) {

			uint64_t where = 0;
			const wchar_t* spec = L"auto";

			if ( !call.takes ( 1, 2 ) || !call.address ( 0, where ) || ( call.has ( 1 ) && !( spec = call.text ( 1 ) ) ) )
				return true;

			script_touch ( );
			xor_decode ( where, spec );
			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"emulate" ) ) {

			uint64_t where = 0;
			const wchar_t* setup = L"";

			if ( !call.takes ( 1, 2 ) || !call.function_start ( 0, where ) || ( call.has ( 1 ) && !( setup = call.text ( 1 ) ) ) )
				return true;

			base::list < wchar_t > report;

			emulate_function ( where, setup, &report );
			call.give_text ( report.data ( ), report.count ( ) );

			return true;

		}

		if ( call.is ( L"is_covered" ) ) {

			uint64_t where = 0;

			if ( call.takes ( 1, 1 ) && call.address ( 0, where ) )
				call.give_number ( is_covered ( where ) );

			return true;

		}

		if ( call.is ( L"compare" ) ) {

			const wchar_t* path = nullptr;

			if ( !call.takes ( 1, 1 ) || !( path = call.text ( 0 ) ) || !call.need_analysis ( ) )
				return true;

			if ( !compare_with ( path ) )
				return true;

			refresh_filter ( panel::COMPARE );

			script::value made = call.list ( );

			for ( int32_t i = 0; i < 4; i++ )
				call.push_number ( made, diff_counts [ i ] );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"close_compare" ) ) {

			if ( call.takes ( 0, 0 ) ) {

				close_compare ( );
				refresh_filter ( panel::COMPARE );

			}

			return true;

		}

		if ( call.is ( L"learn_names" ) || call.is ( L"apply_names" ) ) {

			if ( !call.takes ( 0, 0 ) || !call.need_analysis ( ) )
				return true;

			if ( call.is ( L"learn_names" ) )
				learn_library ( );
			else {

				script_touch ( );
				apply_library ( );

			}

			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"export_report" ) || call.is ( L"export_classes" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			if ( call.is ( L"export_report" ) )
				export_report ( );
			else
				export_classes ( );

			call.give_number ( 1 );

			return true;

		}

		return false;

	}

}
