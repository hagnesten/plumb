#include "ui/app.hh"

namespace ui {

	struct query_reader {

		const wchar_t* at = nullptr;

		bool word ( wchar_t* out, int32_t room ) {

			while ( *at == L' ' )
				at++;

			if ( !*at )
				return false;

			int32_t length = 0;
			bool quoted = *at == L'"';

			if ( quoted )
				at++;

			while ( *at && ( quoted ? *at != L'"' : *at != L' ' ) && length < room - 1 )
				out [ length++ ] = *at++;

			if ( quoted && *at == L'"' )
				at++;

			out [ length ] = 0;

			return true;

		}

	};

	static bool parse_number ( const wchar_t* text, uint64_t& value ) {

		bool hex = text [ 0 ] == L'0' && ( text [ 1 ] == L'x' || text [ 1 ] == L'X' );
		const wchar_t* digits = hex ? text + 2 : text;

		value = 0;

		if ( !*digits )
			return false;

		for ( const wchar_t* c = digits; *c; c++ ) {

			uint32_t digit = 0;

			if ( *c >= L'0' && *c <= L'9' )
				digit = static_cast < uint32_t > ( *c - L'0' );
			else if ( hex && ( ( *c >= L'a' && *c <= L'f' ) || ( *c >= L'A' && *c <= L'F' ) ) )
				digit = static_cast < uint32_t > ( base::fold_case ( *c ) - L'a' + 10 );
			else
				return false;

			value = value * ( hex ? 16 : 10 ) + digit;

		}

		return true;

	}

	static bool compare ( const wchar_t* op, uint64_t left, uint64_t right ) {

		if ( base::same_text ( op, L">" ) )
			return left > right;

		if ( base::same_text ( op, L"<" ) )
			return left < right;

		if ( base::same_text ( op, L">=" ) )
			return left >= right;

		if ( base::same_text ( op, L"<=" ) )
			return left <= right;

		if ( base::same_text ( op, L"!=" ) )
			return left != right;

		return left == right;

	}

	static bool is_operator ( const wchar_t* op ) {

		return base::same_text ( op, L">" ) || base::same_text ( op, L"<" ) || base::same_text ( op, L">=" ) || base::same_text ( op, L"<=" ) || base::same_text ( op, L"=" ) || base::same_text ( op, L"==" ) || base::same_text ( op, L"!=" );

	}

	bool app::run_query ( const wchar_t* text ) {

		if ( !db || !analysis_applied || db->functions.empty ( ) ) {

			log ( log_kind::WARN, L"Queries need the analysis to finish first." );

			return false;

		}

		double started = base::seconds ( );
		int32_t total = db->functions.count ( );
		base::list < uint8_t > pass;
		base::list < uint8_t > hit;
		query_reader reader;
		wchar_t token [ 200 ];
		wchar_t name [ 200 ];
		int32_t conditions = 0;

		pass.fill ( static_cast < size_t > ( total ), 1 );
		reader.at = text;

		auto fail = [ & ] ( const wchar_t* problem ) {

			log ( log_kind::FAIL, problem );
			log ( log_kind::INFO, L"Try: functions where calls VirtualAlloc and size > 0x100 and not thunk" );

			return false;

		};

		auto mark_caller = [ & ] ( uint64_t target, bool any_kind ) {

			int32_t first = 0;
			int32_t count = 0;

			db->refs_to ( target, first, count );

			for ( int32_t r = first; r < first + count; r++ ) {

				const analysis::xref& item = db->refs [ static_cast < size_t > ( r ) ];

				if ( !any_kind && item.type != analysis::ref::CALL && item.type != analysis::ref::JUMP && item.type != analysis::ref::READ )
					continue;

				int32_t owner = db->function_at ( file.base + item.source );

				if ( owner >= 0 )
					hit [ static_cast < size_t > ( owner ) ] = 1;

			}

		};

		while ( reader.word ( token, 200 ) ) {

			if ( base::same_text ( token, L"where" ) || base::same_text ( token, L"and" ) )
				continue;

			bool negate = false;

			if ( base::same_text ( token, L"not" ) ) {

				negate = true;

				if ( !reader.word ( token, 200 ) )
					return fail ( L"\"not\" needs a condition after it." );

			}

			hit.fill ( static_cast < size_t > ( total ), 0 );

			if ( base::same_text ( token, L"calls" ) || base::same_text ( token, L"refs" ) || base::same_text ( token, L"name" ) || base::same_text ( token, L"calledby" ) ) {

				wchar_t needle [ 200 ];

				if ( !reader.word ( needle, 200 ) )
					return fail ( base::writer ( ).add ( L"\"" ).add ( token ).add ( L"\" needs a name or text after it." ).text ( ) );

				if ( base::same_text ( token, L"name" ) ) {

					for ( int32_t i = 0; i < total; i++ ) {

						function_name ( i, name, 200 );
						hit [ static_cast < size_t > ( i ) ] = base::contains_folded ( name, needle ) ? 1 : 0;

					}

				} else if ( base::same_text ( token, L"calls" ) ) {

					for ( int32_t i = 0; i < total; i++ ) {

						function_name ( i, name, 200 );

						if ( base::contains_folded ( name, needle ) )
							mark_caller ( db->functions [ static_cast < size_t > ( i ) ].start, false );

					}

					for ( const loader::import& item : file.imports ) {

						if ( !item.name.empty ( ) && base::contains_folded ( item.name.get ( ), needle ) )
							mark_caller ( item.slot, false );

					}

				} else if ( base::same_text ( token, L"calledby" ) ) {

					base::list < uint8_t > callers;

					callers.fill ( static_cast < size_t > ( total ), 0 );

					for ( int32_t i = 0; i < total; i++ ) {

						function_name ( i, name, 200 );
						callers [ static_cast < size_t > ( i ) ] = base::contains_folded ( name, needle ) ? 1 : 0;

					}

					for ( const analysis::xref& item : db->refs ) {

						if ( item.type != analysis::ref::CALL && item.type != analysis::ref::JUMP )
							continue;

						int32_t from = db->function_at ( file.base + item.source );
						int32_t to = db->function_starting ( file.base + item.target );

						if ( from >= 0 && to >= 0 && to != from && callers [ static_cast < size_t > ( from ) ] )
							hit [ static_cast < size_t > ( to ) ] = 1;

					}

				} else {

					wchar_t preview [ 400 ];

					for ( const loader::found_string& item : file.strings ) {

						uint64_t address = 0;

						file.string_text ( item, preview, 400 );

						if ( base::contains_folded ( preview, needle ) && file.address_of ( item.offset, address ) )
							mark_caller ( address, true );

					}

				}

			} else if ( base::same_text ( token, L"size" ) || base::same_text ( token, L"callers" ) || base::same_text ( token, L"blocks" ) || base::same_text ( token, L"instructions" ) ) {

				wchar_t op [ 8 ];
				wchar_t number [ 64 ];
				uint64_t wanted = 0;

				if ( !reader.word ( op, 8 ) || !is_operator ( op ) || !reader.word ( number, 64 ) || !parse_number ( number, wanted ) )
					return fail ( base::writer ( ).add ( L"\"" ).add ( token ).add ( L"\" needs a comparison like > 0x100." ).text ( ) );

				for ( int32_t i = 0; i < total; i++ ) {

					const analysis::function_info& info = db->functions [ static_cast < size_t > ( i ) ];
					uint64_t value = base::same_text ( token, L"size" ) ? info.end - info.start : ( base::same_text ( token, L"callers" ) ? info.callers : ( base::same_text ( token, L"blocks" ) ? info.blocks : info.instructions ) );

					hit [ static_cast < size_t > ( i ) ] = compare ( op, value, wanted ) ? 1 : 0;

				}

			} else {

				uint32_t flag = 0;
				int32_t special = 0;

				if ( base::same_text ( token, L"thunk" ) )
					flag = analysis::THUNK;
				else if ( base::same_text ( token, L"leaf" ) )
					flag = analysis::LEAF;
				else if ( base::same_text ( token, L"noreturn" ) )
					flag = analysis::NORETURN;
				else if ( base::same_text ( token, L"exported" ) )
					flag = analysis::FROM_EXPORT;
				else if ( base::same_text ( token, L"entry" ) )
					flag = analysis::FROM_ENTRY;
				else if ( base::same_text ( token, L"named" ) )
					special = 1;
				else if ( base::same_text ( token, L"noted" ) )
					special = 2;
				else if ( base::same_text ( token, L"typed" ) )
					special = 3;
				else
					return fail ( base::writer ( ).add ( L"Unknown condition \"" ).add ( token ).add ( L"\". Use calls, calledby, refs, name, size, callers, blocks, instructions, thunk, leaf, noreturn, exported, entry, named, noted or typed." ).text ( ) );

				for ( int32_t i = 0; i < total; i++ ) {

					const analysis::function_info& info = db->functions [ static_cast < size_t > ( i ) ];
					bool matched = false;

					if ( flag )
						matched = ( info.flags & flag ) != 0;
					else if ( special == 1 )
						matched = ( info.flags & analysis::NAMED ) || user_text ( renamed, info.start );
					else if ( special == 2 )
						matched = user_text ( notes, info.start ) != nullptr;
					else
						matched = prototype_for ( info.start ) != nullptr;

					hit [ static_cast < size_t > ( i ) ] = matched ? 1 : 0;

				}

			}

			for ( int32_t i = 0; i < total; i++ ) {

				if ( hit [ static_cast < size_t > ( i ) ] == ( negate ? 1 : 0 ) )
					pass [ static_cast < size_t > ( i ) ] = 0;

			}

			conditions++;

		}

		if ( !conditions )
			return fail ( L"The query has no conditions." );

		search_hits.clear ( );
		search_query.set ( base::writer ( ).add ( L"functions " ).add ( text ).text ( ) );

		for ( int32_t i = 0; i < total; i++ ) {

			if ( !pass [ static_cast < size_t > ( i ) ] )
				continue;

			const analysis::function_info& info = db->functions [ static_cast < size_t > ( i ) ];
			search_hit& item = search_hits.emplace ( );
			base::writer note;

			function_name ( i, name, 200 );
			note.add ( name ).add ( L"  \x00B7  0x" ).hex ( info.end - info.start ).add ( L" bytes, " ).number ( info.callers ).add ( info.callers == 1 ? L" caller" : L" callers" );
			item.address = info.start;
			item.length = 0;
			item.note.set ( note.text ( ) );

		}

		search_seconds = base::seconds ( ) - started;
		browsing = panel::SEARCH;
		browser_open = true;
		browser_scroll [ static_cast < int32_t > ( panel::SEARCH ) ] = scroller ( );
		refresh_filter ( panel::SEARCH );
		log ( log_kind::GOOD, base::writer ( ).grouped ( static_cast < uint64_t > ( search_hits.count ( ) ) ).add ( search_hits.count ( ) == 1 ? L" function matches" : L" functions match" ).add ( L" in " ).fixed ( search_seconds * 1000.0, 1 ).add ( L" ms." ).text ( ) );

		return true;

	}

}
