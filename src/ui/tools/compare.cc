#include "ui/app.hh"
#include "analysis/signatures.hh"
#include "loader/symbols.hh"

namespace ui {

	struct keyed_index {

		uint64_t key;
		int32_t index;

	};

	void app::other_name ( int32_t index, wchar_t* out, int32_t room ) const {

		const analysis::function_info& info = other_db->functions [ static_cast < size_t > ( index ) ];
		base::writer text;

		if ( info.name >= 0 )
			text.add ( other_db->names [ static_cast < size_t > ( info.name ) ].get ( ) );
		else
			text.add ( L"fn_" ).hex ( info.start );

		int32_t n = base::min ( text.size ( ), room - 1 );

		for ( int32_t i = 0; i < n; i++ )
			out [ i ] = text.text ( ) [ i ];

		out [ n ] = 0;

	}

	void app::close_compare ( ) {

		if ( other_db ) {

			delete other_db;
			other_db = nullptr;

		}

		if ( other_file ) {

			other_file->close ( );
			delete other_file;
			other_file = nullptr;

		}

		diff_rows.clear ( );

		for ( int32_t& count : diff_counts )
			count = 0;

		refresh_filter ( panel::COMPARE );

	}

	bool app::compare_with ( const wchar_t* path ) {

		if ( !db || !analysis_applied || db->functions.empty ( ) ) {

			log ( log_kind::WARN, L"Wait for the analysis to finish before comparing." );

			return false;

		}

		double started = base::seconds ( );
		base::writer problem;
		base::writer symbols;

		close_compare ( );
		other_file = new loader::binary ( );

		if ( !other_file->open ( path, problem ) ) {

			log ( log_kind::FAIL, base::writer ( ).add ( L"Couldn't open " ).add ( path ).add ( L" to compare. " ).add ( problem.text ( ) ).text ( ) );
			close_compare ( );

			return false;

		}

		loader::load_symbols ( *other_file, symbols );
		other_db = new analysis::database ( );

		if ( !other_db->build ( *other_file, nullptr ) || other_db->functions.empty ( ) ) {

			log ( log_kind::FAIL, L"The other file has no x86 code to compare." );
			close_compare ( );

			return false;

		}

		int32_t here_count = db->functions.count ( );
		int32_t there_count = other_db->functions.count ( );
		base::list < uint64_t > here_keys;
		base::list < uint64_t > there_keys;
		base::list < int32_t > here_match;
		base::list < int32_t > there_match;
		base::list < uint8_t > here_state;
		wchar_t name [ 400 ];
		wchar_t other [ 400 ];

		here_keys.fill ( static_cast < size_t > ( here_count ), 0 );
		there_keys.fill ( static_cast < size_t > ( there_count ), 0 );
		here_match.fill ( static_cast < size_t > ( here_count ), -1 );
		there_match.fill ( static_cast < size_t > ( there_count ), -1 );
		here_state.fill ( static_cast < size_t > ( here_count ), 0 );

		for ( int32_t i = 0; i < here_count; i++ )
			analysis::function_key ( *db, i, here_keys [ static_cast < size_t > ( i ) ], false );

		for ( int32_t j = 0; j < there_count; j++ )
			analysis::function_key ( *other_db, j, there_keys [ static_cast < size_t > ( j ) ], false );

		auto pair = [ & ] ( int32_t i, int32_t j ) {

			here_match [ static_cast < size_t > ( i ) ] = j;
			there_match [ static_cast < size_t > ( j ) ] = i;
			here_state [ static_cast < size_t > ( i ) ] = here_keys [ static_cast < size_t > ( i ) ] && here_keys [ static_cast < size_t > ( i ) ] == there_keys [ static_cast < size_t > ( j ) ] ? 0 : 1;

		};

		auto sorted_keys = [ & ] ( const base::list < uint64_t >& keys, base::list < keyed_index >& out ) {

			for ( int32_t i = 0; i < keys.count ( ); i++ ) {

				if ( keys [ static_cast < size_t > ( i ) ] )
					out.push ( keyed_index { keys [ static_cast < size_t > ( i ) ], i } );

			}

			base::sort ( out.data ( ), out.count ( ), [ ] ( const keyed_index& a, const keyed_index& b ) { return a.key < b.key; } );

		};

		base::list < keyed_index > here_sorted;
		base::list < keyed_index > there_sorted;

		sorted_keys ( here_keys, here_sorted );
		sorted_keys ( there_keys, there_sorted );

		int32_t a = 0;
		int32_t b = 0;

		while ( a < here_sorted.count ( ) && b < there_sorted.count ( ) ) {

			uint64_t left = here_sorted [ static_cast < size_t > ( a ) ].key;
			uint64_t right = there_sorted [ static_cast < size_t > ( b ) ].key;

			if ( left < right ) {

				a++;

				continue;

			}

			if ( right < left ) {

				b++;

				continue;

			}

			int32_t a_end = a;
			int32_t b_end = b;

			while ( a_end < here_sorted.count ( ) && here_sorted [ static_cast < size_t > ( a_end ) ].key == left )
				a_end++;

			while ( b_end < there_sorted.count ( ) && there_sorted [ static_cast < size_t > ( b_end ) ].key == left )
				b_end++;

			if ( a_end - a == 1 && b_end - b == 1 )
				pair ( here_sorted [ static_cast < size_t > ( a ) ].index, there_sorted [ static_cast < size_t > ( b ) ].index );

			a = a_end;
			b = b_end;

		}

		base::list < base::text > there_names;
		base::list < int32_t > there_by_name;

		there_names.resize ( static_cast < size_t > ( there_count ) );

		for ( int32_t j = 0; j < there_count; j++ ) {

			if ( other_db->functions [ static_cast < size_t > ( j ) ].name < 0 )
				continue;

			other_name ( j, other, 400 );
			there_names [ static_cast < size_t > ( j ) ].set ( other );
			there_by_name.push ( j );

		}

		base::sort ( there_by_name.data ( ), there_by_name.count ( ), [ & ] ( int32_t x, int32_t y ) { return base::compare_folded ( there_names [ static_cast < size_t > ( x ) ].get ( ), there_names [ static_cast < size_t > ( y ) ].get ( ) ) < 0; } );

		for ( int32_t i = 0; i < here_count; i++ ) {

			if ( here_match [ static_cast < size_t > ( i ) ] >= 0 || !meaningful_name ( i, name, 400 ) )
				continue;

			int32_t low = 0;
			int32_t high = there_by_name.count ( ) - 1;

			while ( low <= high ) {

				int32_t middle = ( low + high ) / 2;
				int32_t j = there_by_name [ static_cast < size_t > ( middle ) ];
				int32_t order = base::compare_folded ( there_names [ static_cast < size_t > ( j ) ].get ( ), name );

				if ( !order ) {

					if ( there_match [ static_cast < size_t > ( j ) ] < 0 )
						pair ( i, j );

					break;

				}

				if ( order < 0 )
					low = middle + 1;
				else
					high = middle - 1;

			}

		}

		int32_t last_here = -1;
		int32_t last_there = -1;

		for ( int32_t i = 0; i <= here_count; i++ ) {

			int32_t j = i < here_count ? here_match [ static_cast < size_t > ( i ) ] : there_count;

			if ( j < 0 || j <= last_there )
				continue;

			base::list < int32_t > gap_here;
			base::list < int32_t > gap_there;

			for ( int32_t h = last_here + 1; h < i; h++ ) {

				if ( here_match [ static_cast < size_t > ( h ) ] < 0 )
					gap_here.push ( h );

			}

			for ( int32_t t = last_there + 1; t < j; t++ ) {

				if ( there_match [ static_cast < size_t > ( t ) ] < 0 )
					gap_there.push ( t );

			}

			if ( !gap_here.empty ( ) && gap_here.count ( ) == gap_there.count ( ) ) {

				for ( int32_t k = 0; k < gap_here.count ( ); k++ )
					pair ( gap_here [ static_cast < size_t > ( k ) ], gap_there [ static_cast < size_t > ( k ) ] );

			}

			last_here = i;
			last_there = j;

		}

		diff_rows.clear ( );

		for ( int32_t& count : diff_counts )
			count = 0;

		for ( int32_t state = 1; state <= 4; state++ ) {

			uint8_t wanted = static_cast < uint8_t > ( state % 4 );

			if ( wanted <= 1 ) {

				for ( int32_t i = 0; i < here_count; i++ ) {

					if ( here_match [ static_cast < size_t > ( i ) ] < 0 || here_state [ static_cast < size_t > ( i ) ] != wanted )
						continue;

					diff_row& row = diff_rows.emplace ( );

					row.here = i;
					row.there = here_match [ static_cast < size_t > ( i ) ];
					row.state = wanted;
					diff_counts [ wanted ]++;

				}

			} else if ( wanted == 2 ) {

				for ( int32_t i = 0; i < here_count; i++ ) {

					if ( here_match [ static_cast < size_t > ( i ) ] >= 0 )
						continue;

					diff_row& row = diff_rows.emplace ( );

					row.here = i;
					row.state = 2;
					diff_counts [ 2 ]++;

				}

			} else {

				for ( int32_t j = 0; j < there_count; j++ ) {

					if ( there_match [ static_cast < size_t > ( j ) ] >= 0 )
						continue;

					diff_row& row = diff_rows.emplace ( );

					row.there = j;
					row.state = 3;
					diff_counts [ 3 ]++;

				}

			}

		}

		browsing = panel::COMPARE;
		browser_open = true;
		browser_scroll [ static_cast < int32_t > ( panel::COMPARE ) ] = scroller ( );
		refresh_filter ( panel::COMPARE );
		log ( log_kind::GOOD, base::writer ( ).add ( L"Compared with " ).add ( other_file->name.get ( ) ).add ( L": " ).grouped ( static_cast < uint64_t > ( diff_counts [ 0 ] ) ).add ( L" same, " ).grouped ( static_cast < uint64_t > ( diff_counts [ 1 ] ) ).add ( L" changed, " ).grouped ( static_cast < uint64_t > ( diff_counts [ 2 ] ) ).add ( L" only here, " ).grouped ( static_cast < uint64_t > ( diff_counts [ 3 ] ) ).add ( L" only there, in " ).fixed ( ( base::seconds ( ) - started ) * 1000.0, 0 ).add ( L" ms." ).text ( ) );

		return true;

	}

}
