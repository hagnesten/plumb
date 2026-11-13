#include "ui/app.hh"
#include "ui/icons.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	void app::remember_recent ( const wchar_t* path ) {

		for ( int32_t i = 0; i < recent.count ( ); i++ ) {

			if ( base::same_text ( recent [ static_cast < size_t > ( i ) ].get ( ), path ) ) {

				recent.remove_at ( static_cast < size_t > ( i ) );

				break;

			}

		}

		base::list < base::text > next;

		next.push ( base::text ( path ) );

		for ( const base::text& item : recent ) {

			if ( next.count ( ) < 8 )
				next.push ( item );

		}

		recent = base::move ( next );

		if ( persist )
			save_settings ( );

	}

	void app::forget_recent ( int32_t index ) {

		if ( index < 0 || index >= recent.count ( ) )
			return;

		recent.remove_at ( static_cast < size_t > ( index ) );

		if ( persist )
			save_settings ( );

	}

	static void project_path ( const loader::binary& file, wchar_t* out, int32_t room ) {

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( const wchar_t* at = file.path.get ( ); *at; at++ ) {

			wchar_t c = *at >= L'A' && *at <= L'Z' ? static_cast < wchar_t > ( *at + 32 ) : *at;

			hash ^= static_cast < uint64_t > ( c );
			hash *= 0x100000001B3ull;

		}

		hash ^= file.size;
		hash *= 0x100000001B3ull;

		base::writer target;

		target.add ( files::folder ( ) ).add ( L"\\project_" ).hex ( hash, 16 ).add ( L".txt" );

		for ( int32_t i = 0; i <= target.size ( ) && i < room; i++ )
			out [ i ] = target.text ( ) [ i ];

	}

	void app::project_beside ( base::writer& out ) const {

		const wchar_t* path = file.path.get ( );
		int32_t length = 0;
		int32_t dot = -1;

		for ( ; path [ length ]; length++ ) {

			if ( path [ length ] == L'.' )
				dot = length;
			else if ( path [ length ] == L'\\' || path [ length ] == L'/' )
				dot = -1;

		}

		out.clear ( );

		for ( int32_t i = 0; i < ( dot >= 0 ? dot : length ); i++ )
			out.add ( path [ i ] );

		out.add ( L".plumb" );

	}

	bool app::project_ours ( const wchar_t* path ) const {

		base::list < char > data;

		if ( !files::read ( path, data ) )
			return true;

		data.push ( 0 );

		if ( data.count ( ) < 6 || data [ 0 ] != 'f' || data [ 1 ] != 'i' || data [ 2 ] != 'l' || data [ 3 ] != 'e' || data [ 4 ] != ' ' )
			return true;

		int32_t end = 5;

		while ( data [ static_cast < size_t > ( end ) ] && data [ static_cast < size_t > ( end ) ] != '\n' && data [ static_cast < size_t > ( end ) ] != '\r' )
			end++;

		wchar_t owner [ 600 ];
		int32_t made = MultiByteToWideChar ( CP_UTF8, 0, data.data ( ) + 5, end - 5, owner, 599 );

		owner [ made > 0 ? made : 0 ] = 0;

		return CompareStringOrdinal ( owner, -1, file.path.get ( ), -1, TRUE ) == CSTR_EQUAL;

	}

	void app::serialize_project ( base::list < char >& out ) const {

		char utf8 [ 1200 ];

		auto push_line = [ & ] ( const base::writer& line ) {

			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, line.text ( ), line.size ( ), utf8, 1199, nullptr, nullptr );

			for ( int32_t i = 0; i < length; i++ )
				out.push ( utf8 [ i ] == '\n' ? ' ' : utf8 [ i ] );

			out.push ( '\n' );

		};

		auto write_list = [ & ] ( const wchar_t* key, const base::list < user_name >& list ) {

			for ( const user_name& item : list ) {

				base::writer line;

				line.add ( key ).add ( L" " ).hex ( item.address ).add ( L" " ).add ( item.text.get ( ) );
				push_line ( line );

			}

		};

		write_list ( L"name", renamed );
		write_list ( L"comment", comments );
		write_list ( L"bookmark", bookmarks );
		write_list ( L"note", notes );
		write_list ( L"condition", conditions );
		write_list ( L"number", number_forms );
		write_list ( L"proto", prototypes );

		for ( uint64_t item : breakpoints ) {

			base::writer line;

			line.add ( L"bp " ).hex ( item ).add ( L" on" );
			push_line ( line );

		}

		for ( const patched_byte& item : patches ) {

			base::writer line;

			line.add ( L"pb " ).hex ( item.offset ).add ( L" " ).hex ( item.value, 2 ).add ( L" " ).hex ( item.original, 2 );
			push_line ( line );

		}

		for ( const local_entry& entry : locals ) {

			base::writer line;

			line.add ( L"local " ).hex ( entry.function ).add ( L" " ).add ( entry.key.get ( ) ).add ( L" " ).add ( entry.name.empty ( ) ? L"-" : entry.name.get ( ) ).add ( L" " ).add ( entry.type.get ( ) );
			push_line ( line );

		}

		for ( int32_t i = 0; i < structs.count ( ); i++ ) {

			const user_struct& item = structs [ static_cast < size_t > ( i ) ];
			base::writer line;

			line.add ( L"struct " ).hex ( static_cast < uint64_t > ( i ) ).add ( L" " ).add ( item.name.get ( ) );
			push_line ( line );

			for ( const user_field& field : item.fields ) {

				base::writer entry;

				entry.add ( L"field " ).hex ( static_cast < uint64_t > ( i ) ).add ( L" " ).hex ( static_cast < uint64_t > ( field.offset ) ).add ( L" " ).add ( field.name.get ( ) ).add ( L" " ).add ( field.type.get ( ) );
				push_line ( entry );

			}

		}

	}

	void app::apply_project ( const char* data ) {

		annotation_revision++;

		renamed.clear ( );
		comments.clear ( );
		bookmarks.clear ( );
		notes.clear ( );
		conditions.clear ( );
		number_forms.clear ( );
		prototypes.clear ( );
		locals.clear ( );
		breakpoints.clear ( );
		structs.clear ( );

		base::list < patched_byte > before = base::move ( patches );

		for ( int32_t i = before.count ( ) - 1; i >= 0; i-- )
			file.poke ( before [ static_cast < size_t > ( i ) ].offset, before [ static_cast < size_t > ( i ) ].original );

		const char* cursor = data;

		while ( cursor && *cursor ) {

			const char* line = cursor;

			while ( *cursor && *cursor != '\n' )
				cursor++;

			int32_t length = static_cast < int32_t > ( cursor - line );

			if ( *cursor )
				cursor++;

			wchar_t wide [ 1200 ];
			int32_t made = MultiByteToWideChar ( CP_UTF8, 0, line, length, wide, 1199 );

			wide [ made > 0 ? made : 0 ] = 0;

			while ( made > 0 && wide [ made - 1 ] == L'\r' )
				wide [ --made ] = 0;

			wchar_t* at = wide;
			wchar_t* key = at;

			while ( *at && *at != L' ' )
				at++;

			if ( !*at )
				continue;

			*at++ = 0;

			if ( base::same_text ( key, L"file" ) )
				continue;

			uint64_t address = 0;

			while ( *at && *at != L' ' ) {

				wchar_t c = *at++;

				address = ( address << 4 ) | static_cast < uint64_t > ( c >= L'a' ? c - L'a' + 10 : ( c >= L'A' ? c - L'A' + 10 : c - L'0' ) );

			}

			if ( *at == L' ' )
				at++;

			auto take_word = [ & ] ( ) {

				wchar_t* start = at;

				while ( *at && *at != L' ' )
					at++;

				if ( *at )
					*at++ = 0;

				return start;

			};

			if ( base::same_text ( key, L"name" ) )
				set_user_text ( renamed, address, at );
			else if ( base::same_text ( key, L"comment" ) )
				set_user_text ( comments, address, at );
			else if ( base::same_text ( key, L"bookmark" ) )
				set_user_text ( bookmarks, address, at );
			else if ( base::same_text ( key, L"note" ) )
				set_user_text ( notes, address, at );
			else if ( base::same_text ( key, L"condition" ) )
				set_user_text ( conditions, address, at );
			else if ( base::same_text ( key, L"number" ) )
				set_user_text ( number_forms, address, at );
			else if ( base::same_text ( key, L"proto" ) )
				set_user_text ( prototypes, address, at );
			else if ( base::same_text ( key, L"bp" ) )
				breakpoints.push ( address );
			else if ( base::same_text ( key, L"pb" ) ) {

				wchar_t* value_text = take_word ( );
				uint32_t value = 0;

				for ( wchar_t* c = value_text; *c; c++ )
					value = ( value << 4 ) | static_cast < uint32_t > ( *c >= L'a' ? *c - L'a' + 10 : ( *c >= L'A' ? *c - L'A' + 10 : *c - L'0' ) );

				if ( file.loaded ( ) && address < file.size && value_text [ 0 ] )
					set_byte ( address, static_cast < uint8_t > ( value ) );

			}
			else if ( base::same_text ( key, L"local" ) ) {

				wchar_t* local_key = take_word ( );
				wchar_t* name = take_word ( );

				if ( !local_key [ 0 ] )
					continue;

				local_entry& entry = locals.emplace ( );

				entry.function = address;
				entry.key.set ( local_key );

				if ( !base::same_text ( name, L"-" ) )
					entry.name.set ( name );

				entry.type.set ( at );

			} else if ( base::same_text ( key, L"struct" ) ) {

				if ( address > 4096 )
					continue;

				while ( structs.count ( ) <= static_cast < int32_t > ( address ) )
					structs.emplace ( );

				structs [ static_cast < size_t > ( address ) ].name.set ( at );

			} else if ( base::same_text ( key, L"field" ) ) {

				if ( address >= static_cast < uint64_t > ( structs.count ( ) ) )
					continue;

				wchar_t* offset_text = take_word ( );
				wchar_t* name = take_word ( );
				uint64_t offset = 0;

				for ( wchar_t* c = offset_text; *c; c++ )
					offset = ( offset << 4 ) | static_cast < uint64_t > ( *c >= L'a' ? *c - L'a' + 10 : ( *c >= L'A' ? *c - L'A' + 10 : *c - L'0' ) );

				add_field ( static_cast < int32_t > ( address ), static_cast < int32_t > ( offset ), name, at );

			}

		}

		names_revision++;
		refresh_filter ( panel::STRUCTS );

		bool same = before.count ( ) == patches.count ( );

		for ( int32_t i = 0; same && i < patches.count ( ); i++ )
			same = before [ static_cast < size_t > ( i ) ].offset == patches [ static_cast < size_t > ( i ) ].offset && before [ static_cast < size_t > ( i ) ].value == patches [ static_cast < size_t > ( i ) ].value;

		if ( !same )
			reanalyze ( );

	}

	void app::save_project ( ) {

		if ( !persist || !file.loaded ( ) )
			return;

		base::list < char > out;
		wchar_t path [ 600 ];
		char utf8 [ 1200 ];

		serialize_project ( out );
		project_path ( file, path, 600 );

		if ( !project_file.empty ( ) ) {

			base::list < char > shared;
			base::writer head;

			head.add ( L"file " ).add ( file.path.get ( ) ).add ( L"\n" );

			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, head.text ( ), head.size ( ), utf8, 1199, nullptr, nullptr );

			for ( int32_t i = 0; i < length; i++ )
				shared.push ( utf8 [ i ] );

			for ( char c : out )
				shared.push ( c );

			files::write_later ( project_file.get ( ), shared.data ( ), shared.count ( ) );

		}

		if ( out.empty ( ) ) {

			DeleteFileW ( path );

			return;

		}

		files::write_later ( path, out.data ( ), out.count ( ) );

	}

	void app::load_project ( ) {

		apply_project ( nullptr );
		undo_stack.clear ( );
		redo_stack.clear ( );

		if ( !file.loaded ( ) )
			return;

		wchar_t path [ 600 ];
		base::list < char > data;

		project_path ( file, path, 600 );

		bool loaded = false;

		if ( project_file.empty ( ) ) {

			base::writer beside;
			base::writer legacy;

			project_beside ( beside );
			legacy.add ( file.path.get ( ) ).add ( L".plumb" );

			if ( GetFileAttributesW ( beside.text ( ) ) != INVALID_FILE_ATTRIBUTES && project_ours ( beside.text ( ) ) )
				project_file.set ( beside.text ( ) );
			else if ( GetFileAttributesW ( legacy.text ( ) ) != INVALID_FILE_ATTRIBUTES && files::read ( legacy.text ( ), data ) ) {

				loaded = true;
				project_file.set ( project_ours ( beside.text ( ) ) ? beside.text ( ) : legacy.text ( ) );

			}

		}

		if ( !loaded && !( !project_file.empty ( ) && files::read ( project_file.get ( ), data ) ) && !files::read ( path, data ) )
			return;

		data.push ( 0 );
		apply_project ( data.data ( ) );

	}

	void app::changed ( ) {

		dirty = true;

		if ( autosave ) {

			save_project ( );
			dirty = false;

		}

	}

	bool app::confirm_discard ( ) {

		if ( !dirty || !file.loaded ( ) || !window || !persist )
			return true;

		base::writer question;

		question.add ( L"Save your changes to " ).add ( file.name.get ( ) ).add ( L" before closing it?" );

		int answer = MessageBoxW ( static_cast < HWND > ( window ), question.text ( ), L"Plumb", MB_YESNOCANCEL | MB_ICONWARNING );

		if ( answer == IDCANCEL )
			return false;

		if ( answer == IDYES )
			run ( command::SAVE_PROJECT );

		dirty = false;

		return true;

	}

	bool app::can_close ( ) {

		return confirm_discard ( );

	}

	void app::remember_undo ( ) {

		base::list < char >& snapshot = undo_stack.emplace ( );

		serialize_project ( snapshot );
		snapshot.push ( 0 );
		redo_stack.clear ( );

		if ( undo_stack.count ( ) > 200 )
			undo_stack.remove_at ( 0 );

	}

	void app::undo ( bool forward ) {

		base::list < base::list < char > >& from = forward ? redo_stack : undo_stack;
		base::list < base::list < char > >& to = forward ? undo_stack : redo_stack;

		if ( from.empty ( ) ) {

			log ( log_kind::INFO, forward ? L"Nothing to redo." : L"Nothing to undo." );

			return;

		}

		base::list < char >& current = to.emplace ( );

		serialize_project ( current );
		current.push ( 0 );
		apply_project ( from.back ( ).data ( ) );
		from.pop ( );
		sync_breakpoints ( );
		refresh_filter ( panel::FUNCTIONS );
		changed ( );
		log ( log_kind::INFO, forward ? L"Redone." : L"Undone." );

	}

	static void put_line ( base::list < char >& out, const char* key, const wchar_t* value ) {

		for ( const char* at = key; *at; at++ )
			out.push ( *at );

		out.push ( '=' );

		char utf8 [ 1024 ];
		int32_t length = WideCharToMultiByte ( CP_UTF8, 0, value, -1, utf8, 1023, nullptr, nullptr );

		for ( int32_t i = 0; i < length - 1; i++ )
			out.push ( utf8 [ i ] );

		out.push ( '\n' );

	}

	void app::save_settings ( ) {

		if ( settings_frozen )
			return;

		base::list < char > out;
		base::writer value;
		wchar_t path [ 600 ];

		put_line ( out, "theme", light ? L"light" : L"dark" );
		put_line ( out, "c_spaced", c_spaced ? L"1" : L"0" );
		put_line ( out, "c_no_ternary", c_no_ternary ? L"1" : L"0" );
		put_line ( out, "autosave", autosave ? L"1" : L"0" );
		put_line ( out, "opcode_bytes", show_bytes ? L"1" : L"0" );
		value.number ( static_cast < int64_t > ( dock_size [ 0 ] ) ).add ( L',' ).number ( static_cast < int64_t > ( dock_size [ 1 ] ) ).add ( L',' ).number ( static_cast < int64_t > ( dock_size [ 2 ] ) );
		put_line ( out, "dock_sizes", value.text ( ) );
		value.clear ( );
		value.number ( static_cast < int32_t > ( docks [ 0 ] ) ).add ( L',' ).number ( static_cast < int32_t > ( docks [ 1 ] ) );
		put_line ( out, "docks", value.text ( ) );
		value.clear ( );
		value.number ( ( browser_open ? 1 : 0 ) | ( output_open ? 4 : 0 ) );
		put_line ( out, "panels", value.text ( ) );

		if ( placement_valid ) {

			value.clear ( );

			for ( int32_t i = 0; i < 4; i++ ) {

				if ( i )
					value.add ( L',' );

				value.number ( placement [ i ] );

			}

			put_line ( out, "window", value.text ( ) );
			put_line ( out, "maximized", placement_maximized ? L"1" : L"0" );

		}

		for ( const base::text& item : recent )
			put_line ( out, "recent", item.get ( ) );

		base::writer target;

		target.add ( files::folder ( ) ).add ( L"\\settings.ini" );

		for ( int32_t i = 0; i <= target.size ( ); i++ )
			path [ i ] = target.text ( ) [ i ];

		files::write_later ( path, out.data ( ), out.count ( ) );

	}

	static int64_t read_integer ( const wchar_t* text ) {

		int64_t value = 0;
		bool negative = *text == L'-';

		if ( negative )
			text++;

		while ( *text >= L'0' && *text <= L'9' )
			value = value * 10 + ( *text++ - L'0' );

		return negative ? -value : value;

	}

	void app::load_settings ( ) {

		base::list < char > data;
		base::writer target;

		target.add ( files::folder ( ) ).add ( L"\\settings.ini" );

		if ( !files::read ( target.text ( ), data ) )
			return;

		const char* cursor = data.data ( );

		while ( *cursor ) {

			const char* name = cursor;

			while ( *cursor && *cursor != '=' && *cursor != '\n' )
				cursor++;

			if ( *cursor != '=' ) {

				while ( *cursor && *cursor != '\n' )
					cursor++;

				if ( *cursor )
					cursor++;

				continue;

			}

			int32_t name_length = static_cast < int32_t > ( cursor - name );
			const char* start = ++cursor;

			while ( *cursor && *cursor != '\n' && *cursor != '\r' )
				cursor++;

			wchar_t value [ 1024 ];
			int32_t made = MultiByteToWideChar ( CP_UTF8, 0, start, static_cast < int32_t > ( cursor - start ), value, 1023 );

			value [ made > 0 ? made : 0 ] = 0;

			while ( *cursor == '\r' || *cursor == '\n' )
				cursor++;

			auto named = [ & ] ( const char* key ) {

				int32_t i = 0;

				for ( ; key [ i ]; i++ ) {

					if ( i >= name_length || key [ i ] != name [ i ] )
						return false;

				}

				return i == name_length;

			};

			if ( named ( "theme" ) )
				light = base::same_text ( value, L"light" );
			else if ( named ( "c_spaced" ) )
				c_spaced = value [ 0 ] == L'1';
			else if ( named ( "c_no_ternary" ) )
				c_no_ternary = value [ 0 ] == L'1';
			else if ( named ( "autosave" ) )
				autosave = value [ 0 ] == L'1';
			else if ( named ( "opcode_bytes" ) )
				show_bytes = value [ 0 ] == L'1';
			else if ( named ( "dock_sizes" ) || named ( "docks" ) ) {

				const wchar_t* at = value;
				bool sizes = named ( "dock_sizes" );
				int64_t numbers [ 3 ] = { };
				int32_t found = 0;

				for ( int32_t i = 0; i < 3 && *at; i++ ) {

					numbers [ found++ ] = read_integer ( at );

					while ( *at && *at != L',' )
						at++;

					if ( *at == L',' )
						at++;

				}

				if ( sizes ) {

					for ( int32_t i = 0; i < found; i++ )
						dock_size [ i ] = base::clamp ( static_cast < float > ( numbers [ i ] ), 90.0f, 900.0f );

				} else if ( found >= 2 ) {

					docks [ 0 ] = static_cast < dock_side > ( base::clamp < int64_t > ( numbers [ 0 ], 0, 2 ) );
					docks [ 1 ] = static_cast < dock_side > ( base::clamp < int64_t > ( numbers [ found == 3 ? 2 : 1 ], 0, 2 ) );

				}

			}
			else if ( named ( "panels" ) ) {

				int64_t flags = read_integer ( value );

				browser_open = ( flags & 1 ) != 0;
				output_open = ( flags & 4 ) != 0;

			} else if ( named ( "window" ) ) {

				const wchar_t* at = value;

				for ( int32_t i = 0; i < 4; i++ ) {

					placement [ i ] = static_cast < int32_t > ( read_integer ( at ) );

					while ( *at && *at != L',' )
						at++;

					if ( *at == L',' )
						at++;

				}

				placement_valid = placement [ 2 ] > 300 && placement [ 3 ] > 200;

			} else if ( named ( "maximized" ) )
				placement_maximized = value [ 0 ] == L'1';
			else if ( named ( "recent" ) && value [ 0 ] && recent.count ( ) < 8 )
				recent.push ( base::text ( value ) );

		}

		theme_mix = light ? 1.0f : 0.0f;
		browser_show = browser_open ? 1.0f : 0.0f;
		output_show = output_open ? 1.0f : 0.0f;
		update_theme ( );

	}

}