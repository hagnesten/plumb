#include "ui/app.hh"
#include "analysis/signatures.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	static bool hex_digit ( char c, uint32_t& value ) {

		if ( c >= '0' && c <= '9' )
			value = static_cast < uint32_t > ( c - '0' );
		else if ( c >= 'a' && c <= 'f' )
			value = static_cast < uint32_t > ( c - 'a' + 10 );
		else if ( c >= 'A' && c <= 'F' )
			value = static_cast < uint32_t > ( c - 'A' + 10 );
		else
			return false;

		return true;

	}

	void app::library_file ( base::writer& out ) const {

		out.clear ( );

		if ( !library_path.empty ( ) )
			out.add ( library_path.get ( ) );
		else
			out.add ( files::folder ( ) ).add ( L"\\library.txt" );

	}

	void app::read_library ( base::list < library_entry >& out ) const {

		base::writer path;
		base::list < char > data;

		out.clear ( );
		library_file ( path );

		if ( !files::read ( path.text ( ), data ) )
			return;

		int32_t at = 0;

		while ( at < data.count ( ) ) {

			int32_t end = at;

			while ( end < data.count ( ) && data [ static_cast < size_t > ( end ) ] != '\n' )
				end++;

			uint64_t key = 0;
			int32_t digits = 0;
			int32_t p = at;
			uint32_t digit = 0;

			while ( p < end && hex_digit ( data [ static_cast < size_t > ( p ) ], digit ) ) {

				key = ( key << 4 ) | digit;
				digits++;
				p++;

			}

			if ( digits == 16 && p < end && data [ static_cast < size_t > ( p ) ] == ' ' ) {

				wchar_t name [ 400 ];
				int32_t length = end - p - 1;

				if ( length > 0 && data [ static_cast < size_t > ( end - 1 ) ] == '\r' )
					length--;

				int32_t made = length > 0 ? MultiByteToWideChar ( CP_UTF8, 0, data.data ( ) + p + 1, length, name, 399 ) : 0;

				if ( made > 0 ) {

					library_entry& item = out.emplace ( );

					name [ made ] = 0;
					item.key = key;
					item.name.set ( name );

				}

			}

			at = end + 1;

		}

		base::sort ( out.data ( ), out.count ( ), [ ] ( const library_entry& a, const library_entry& b ) { return a.key < b.key; } );

	}

	static int32_t find_entry ( const base::list < app::library_entry >& entries, uint64_t key ) {

		int32_t low = 0;
		int32_t high = entries.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = entries [ static_cast < size_t > ( middle ) ].key;

			if ( value == key )
				return middle;

			if ( value < key )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return -( low + 1 );

	}

	bool app::meaningful_name ( int32_t index, wchar_t* name, int32_t room ) const {

		const analysis::function_info& info = db->functions [ static_cast < size_t > ( index ) ];

		if ( info.flags & analysis::THUNK )
			return false;

		if ( info.name < 0 && !user_text ( renamed, info.start ) )
			return false;

		function_name ( index, name, room );

		bool generated = ( name [ 0 ] == L'f' && name [ 1 ] == L'n' && name [ 2 ] == L'_' ) || ( name [ 0 ] == L's' && name [ 1 ] == L'u' && name [ 2 ] == L'b' && name [ 3 ] == L'_' ) || base::contains_folded ( name, L"vfunc_" ) || base::contains_folded ( name, L"Ordinal_" );

		return name [ 0 ] && !generated;

	}

	void app::learn_library ( ) {

		if ( !db || !analysis_applied || db->functions.empty ( ) )
			return;

		base::list < library_entry > entries;
		wchar_t name [ 400 ];
		int32_t added = 0;
		int32_t clashes = 0;

		read_library ( entries );

		for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

			uint64_t key = 0;

			if ( !meaningful_name ( i, name, 400 ) || !analysis::function_key ( *db, i, key ) )
				continue;

			int32_t found = find_entry ( entries, key );

			if ( found >= 0 ) {

				library_entry& item = entries [ static_cast < size_t > ( found ) ];

				if ( !base::same_text ( item.name.get ( ), name ) && !base::same_text ( item.name.get ( ), L"?" ) ) {

					item.name.set ( L"?" );
					clashes++;

				}

				continue;

			}

			library_entry item;

			item.key = key;
			item.name.set ( name );

			int32_t at = -found - 1;

			entries.push ( library_entry ( ) );

			for ( int32_t k = entries.count ( ) - 1; k > at; k-- )
				entries [ static_cast < size_t > ( k ) ] = base::move ( entries [ static_cast < size_t > ( k - 1 ) ] );

			entries [ static_cast < size_t > ( at ) ] = base::move ( item );
			added++;

		}

		base::list < char > data;
		char utf8 [ 1200 ];

		for ( const library_entry& item : entries ) {

			static const char digits [ ] = "0123456789abcdef";

			for ( int32_t shift = 60; shift >= 0; shift -= 4 )
				data.push ( digits [ ( item.key >> shift ) & 15 ] );

			data.push ( ' ' );

			int32_t made = WideCharToMultiByte ( CP_UTF8, 0, item.name.get ( ), -1, utf8, 1200, nullptr, nullptr );

			for ( int32_t k = 0; k + 1 < made; k++ )
				data.push ( utf8 [ k ] );

			data.push ( '\n' );

		}

		base::writer path;

		library_file ( path );

		if ( !files::write ( path.text ( ), data.data ( ), data.count ( ) ) ) {

			log ( log_kind::FAIL, base::writer ( ).add ( L"Couldn't write the name library to " ).add ( path.text ( ) ).text ( ) );

			return;

		}

		log ( log_kind::GOOD, base::writer ( ).add ( L"Learned " ).grouped ( static_cast < uint64_t > ( added ) ).add ( added == 1 ? L" function" : L" functions" ).add ( clashes ? L", " : L"" ).add ( clashes ? base::writer ( ).number ( clashes ).add ( L" now ambiguous" ).text ( ) : L"" ).add ( L". The library holds " ).grouped ( static_cast < uint64_t > ( entries.count ( ) ) ).add ( L" names." ).text ( ) );
		apply_library ( );

	}

	const wchar_t* app::learned_name ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = learned.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = learned [ static_cast < size_t > ( middle ) ].address;

			if ( value == address )
				return learned [ static_cast < size_t > ( middle ) ].text.get ( );

			if ( value < address )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return nullptr;

	}

	void app::apply_library ( ) {

		learned.clear ( );

		if ( !db || db->functions.empty ( ) )
			return;

		base::list < library_entry > entries;

		read_library ( entries );

		if ( entries.empty ( ) )
			return;

		for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

			const analysis::function_info& info = db->functions [ static_cast < size_t > ( i ) ];
			uint64_t key = 0;

			if ( info.name >= 0 || user_text ( renamed, info.start ) || !analysis::function_key ( *db, i, key ) )
				continue;

			int32_t found = find_entry ( entries, key );

			if ( found < 0 || base::same_text ( entries [ static_cast < size_t > ( found ) ].name.get ( ), L"?" ) )
				continue;

			user_name& item = learned.emplace ( );

			item.address = info.start;
			item.text.set ( entries [ static_cast < size_t > ( found ) ].name.get ( ) );

		}

		names_revision++;
		code_cache.clear ( );
		refresh_filter ( panel::FUNCTIONS );

		if ( !learned.empty ( ) )
			log ( log_kind::GOOD, base::writer ( ).add ( L"Named " ).grouped ( static_cast < uint64_t > ( learned.count ( ) ) ).add ( learned.count ( ) == 1 ? L" function" : L" functions" ).add ( L" from the name library." ).text ( ) );

	}

}
