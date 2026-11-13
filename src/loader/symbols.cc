#include "loader/symbols.hh"
#include <windows.h>

namespace loader {

	struct msf {

		const uint8_t* data = nullptr;
		uint64_t size = 0;
		uint32_t block = 0;
		uint32_t streams = 0;
		base::list < uint32_t > directory;
		base::list < uint32_t > sizes;
		base::list < uint32_t > firsts;

		uint32_t read32 ( uint64_t at ) const {

			if ( at + 4 > size )
				return 0;

			uint32_t value = 0;

			__builtin_memcpy ( &value, data + at, 4 );

			return value;

		}

		bool open ( ) {

			static const char magic [ ] = "Microsoft C/C++ MSF 7.00\r\n\x1A" "DS";

			if ( size < 64 || __builtin_memcmp ( data, magic, sizeof ( magic ) - 1 ) )
				return false;

			block = read32 ( 32 );

			uint32_t blocks = read32 ( 40 );
			uint32_t directory_bytes = read32 ( 44 );
			uint32_t map_block = read32 ( 52 );

			if ( block != 512 && block != 1024 && block != 2048 && block != 4096 && block != 8192 && block != 16384 && block != 32768 )
				return false;

			if ( static_cast < uint64_t > ( blocks ) * block > size + block || !directory_bytes )
				return false;

			uint32_t directory_blocks = ( directory_bytes + block - 1 ) / block;
			base::list < uint8_t > bytes;

			bytes.resize ( static_cast < size_t > ( directory_blocks ) * block );

			for ( uint32_t i = 0; i < directory_blocks; i++ ) {

				uint32_t index = read32 ( static_cast < uint64_t > ( map_block ) * block + i * 4u );
				uint64_t at = static_cast < uint64_t > ( index ) * block;

				if ( at + block > size )
					return false;

				__builtin_memcpy ( bytes.data ( ) + static_cast < size_t > ( i ) * block, data + at, block );

			}

			directory.resize ( directory_bytes / 4 );
			__builtin_memcpy ( directory.data ( ), bytes.data ( ), directory.size ( ) * 4 );

			if ( directory.empty ( ) )
				return false;

			streams = directory [ 0 ];

			if ( streams > directory.size ( ) )
				return false;

			uint32_t cursor = 1 + streams;

			sizes.resize ( streams );
			firsts.resize ( streams );

			for ( uint32_t i = 0; i < streams; i++ ) {

				uint32_t length = directory [ 1 + i ];

				sizes [ i ] = length == 0xFFFFFFFFu ? 0 : length;
				firsts [ i ] = cursor;
				cursor += ( sizes [ i ] + block - 1 ) / block;

				if ( cursor > directory.size ( ) )
					return false;

			}

			return true;

		}

		bool stream ( uint32_t index, base::list < uint8_t >& out ) const {

			out.clear ( );

			if ( index >= streams )
				return false;

			uint32_t length = sizes [ index ];
			uint32_t count = ( length + block - 1 ) / block;

			out.resize ( length );

			for ( uint32_t i = 0; i < count; i++ ) {

				uint64_t at = static_cast < uint64_t > ( directory [ firsts [ index ] + i ] ) * block;
				uint32_t take = base::min ( block, length - i * block );

				if ( at + take > size )
					return false;

				__builtin_memcpy ( out.data ( ) + static_cast < size_t > ( i ) * block, data + at, take );

			}

			return true;

		}

	};

	static uint16_t get16 ( const base::list < uint8_t >& bytes, size_t at ) {

		return at + 2 <= bytes.size ( ) ? static_cast < uint16_t > ( bytes [ at ] | ( bytes [ at + 1 ] << 8 ) ) : 0;

	}

	static uint32_t get32 ( const base::list < uint8_t >& bytes, size_t at ) {

		uint32_t value = 0;

		if ( at + 4 <= bytes.size ( ) )
			__builtin_memcpy ( &value, bytes.data ( ) + at, 4 );

		return value;

	}

	struct name_reader {

		const char* at;
		const char* end;
		const char* backrefs [ 10 ];
		int32_t backref_lengths [ 10 ];
		int32_t backref_count = 0;

		bool fragment ( const char*& text, int32_t& length ) {

			if ( at >= end )
				return false;

			if ( *at >= '0' && *at <= '9' ) {

				int32_t index = *at - '0';

				at++;

				if ( index >= backref_count )
					return false;

				text = backrefs [ index ];
				length = backref_lengths [ index ];

				return true;

			}

			if ( *at == '?' )
				return false;

			text = at;

			while ( at < end && *at != '@' )
				at++;

			if ( at >= end )
				return false;

			length = static_cast < int32_t > ( at - text );
			at++;

			if ( backref_count < 10 ) {

				backrefs [ backref_count ] = text;
				backref_lengths [ backref_count ] = length;
				backref_count++;

			}

			return length > 0;

		}

	};

	static void copy_plain ( const char* name, int32_t length, wchar_t* out, int32_t room ) {

		int32_t n = base::min ( length, room - 1 );

		for ( int32_t i = 0; i < n; i++ )
			out [ i ] = static_cast < wchar_t > ( static_cast < uint8_t > ( name [ i ] ) );

		out [ n ] = 0;

	}

	void undecorate ( const char* name, bool x86, wchar_t* out, int32_t room ) {

		int32_t length = base::text_length ( name );

		out [ 0 ] = 0;

		if ( name [ 0 ] != '?' ) {

			const char* start = name;
			int32_t size = length;

			if ( x86 && ( *start == '_' || *start == '@' ) ) {

				start++;
				size--;

				for ( int32_t i = size - 1; i > 0; i-- ) {

					if ( start [ i ] == '@' ) {

						bool digits = i + 1 < size;

						for ( int32_t k = i + 1; k < size; k++ )
							digits = digits && start [ k ] >= '0' && start [ k ] <= '9';

						if ( digits )
							size = i;

						break;

					}

				}

			}

			copy_plain ( start, size, out, room );

			return;

		}

		name_reader reader;
		const char* special = nullptr;
		int32_t special_kind = 0;

		reader.at = name + 1;
		reader.end = name + length;

		if ( reader.at < reader.end && *reader.at == '?' ) {

			reader.at++;

			if ( reader.at >= reader.end )
				return copy_plain ( name, length, out, room );

			char code = *reader.at++;

			if ( code == '0' )
				special_kind = 1;
			else if ( code == '1' )
				special_kind = 2;
			else if ( code == '_' && reader.at < reader.end ) {

				char extra = *reader.at++;

				if ( extra == '7' )
					special = "vftable";
				else if ( extra == '8' )
					special = "vbtable";
				else if ( extra == 'G' )
					special = "scalar_deleting_dtor";
				else if ( extra == 'E' )
					special = "vector_deleting_dtor";
				else if ( extra == 'R' )
					return copy_plain ( name, length, out, room );
				else if ( extra == 'C' ) {

					out [ 0 ] = 0;

					return;

				} else
					special = "special";

			} else {

				static const char codes [ ] = "2345689ARCDEGHIJKLMNOPQSTUVWXYZ";
				static const char* const operators [ ] = { "operator_new", "operator_delete", "operator=", "operator>>", "operator<<", "operator!", "operator==", "operator!=", "operator[]", "operator()", "operator->", "operator*", "operator++", "operator--", "operator-", "operator+", "operator&", "operator->*", "operator/", "operator%", "operator<", "operator<=", "operator>", "operator>=", "operator,", "operator()", "operator~", "operator^", "operator|", "operator&&", "operator||" };

				for ( int32_t i = 0; codes [ i ]; i++ ) {

					if ( codes [ i ] == code ) {

						special = operators [ i ];

						break;

					}

				}

				if ( !special )
					return copy_plain ( name, length, out, room );

			}

		}

		const char* parts [ 16 ];
		int32_t part_lengths [ 16 ];
		int32_t count = 0;

		while ( reader.at < reader.end && *reader.at != '@' && count < 16 ) {

			if ( !reader.fragment ( parts [ count ], part_lengths [ count ] ) )
				return copy_plain ( name, length, out, room );

			count++;

		}

		if ( !count )
			return copy_plain ( name, length, out, room );

		base::writer text;
		wchar_t piece [ 200 ];

		for ( int32_t i = count - 1; i >= 0; i-- ) {

			copy_plain ( parts [ i ], part_lengths [ i ], piece, 200 );
			text.add ( piece );

			if ( i > 0 )
				text.add ( L"::" );

		}

		if ( special_kind || special ) {

			text.add ( L"::" );

			if ( special_kind == 2 )
				text.add ( L"~" );

			if ( special_kind ) {

				copy_plain ( parts [ 0 ], part_lengths [ 0 ], piece, 200 );
				text.add ( piece );

			} else {

				copy_plain ( special, base::text_length ( special ), piece, 200 );
				text.add ( piece );

			}

		}

		int32_t n = base::min ( text.size ( ), room - 1 );

		for ( int32_t i = 0; i < n; i++ )
			out [ i ] = text.text ( ) [ i ];

		out [ n ] = 0;

	}

	bool read_pdb ( binary& file, const wchar_t* path, base::writer& report ) {

		HANDLE handle = CreateFileW ( path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( handle == INVALID_HANDLE_VALUE )
			return false;

		LARGE_INTEGER length = { };
		HANDLE mapping = nullptr;
		const uint8_t* view = nullptr;
		bool loaded = false;

		if ( GetFileSizeEx ( handle, &length ) && length.QuadPart >= 64 )
			mapping = CreateFileMappingW ( handle, nullptr, PAGE_READONLY, 0, 0, nullptr );

		if ( mapping )
			view = static_cast < const uint8_t* > ( MapViewOfFile ( mapping, FILE_MAP_READ, 0, 0, 0 ) );

		msf container;

		container.data = view;
		container.size = static_cast < uint64_t > ( length.QuadPart );

		base::list < uint8_t > info;
		base::list < uint8_t > dbi;
		base::list < uint8_t > records;
		base::list < uint8_t > headers;

		if ( !view || !container.open ( ) ) {

			report.add ( L"isn't a PDB file" );

		} else if ( !container.stream ( 1, info ) || info.size ( ) < 28 || ( file.debug_known && __builtin_memcmp ( info.data ( ) + 12, file.debug_guid, 16 ) ) ) {

			report.add ( L"belongs to a different build" );

		} else if ( !container.stream ( 3, dbi ) || dbi.size ( ) < 64 ) {

			report.add ( L"has no debug info stream" );

		} else {

			uint16_t record_stream = get16 ( dbi, 20 );
			size_t optional = 64 + get32 ( dbi, 24 ) + get32 ( dbi, 28 ) + get32 ( dbi, 32 ) + get32 ( dbi, 36 ) + get32 ( dbi, 40 ) + get32 ( dbi, 52 );
			uint16_t header_stream = get16 ( dbi, optional + 10 );
			base::list < uint32_t > bases;
			bool x86 = file.arch == machine::X86;
			int32_t functions = 0;

			if ( get32 ( dbi, 48 ) >= 12 && container.stream ( header_stream, headers ) ) {

				for ( size_t at = 0; at + 40 <= headers.size ( ); at += 40 )
					bases.push ( get32 ( headers, at + 12 ) );

			} else {

				for ( const section& item : file.sections ) {

					if ( item.type != kind::HEADER )
						bases.push ( static_cast < uint32_t > ( item.address - file.base ) );

				}

			}

			uint32_t module_bytes = get32 ( dbi, 24 );
			base::list < uint8_t > module;

			for ( size_t at = 64; at + 64 <= 64 + static_cast < size_t > ( module_bytes ) && at + 64 <= dbi.size ( ); ) {

				uint16_t stream = get16 ( dbi, at + 34 );
				uint32_t symbol_bytes = get32 ( dbi, at + 36 );
				size_t text = at + 64;
				int32_t strings = 0;

				while ( text < dbi.size ( ) && strings < 2 ) {

					if ( !dbi [ text ] )
						strings++;

					text++;

				}

				at = ( text + 3 ) & ~static_cast < size_t > ( 3 );

				if ( stream == 0xFFFF || symbol_bytes < 4 || !container.stream ( stream, module ) )
					continue;

				size_t end = base::min ( static_cast < size_t > ( symbol_bytes ), module.size ( ) );
				size_t cursor = 4;

				while ( cursor + 4 <= end ) {

					uint16_t size = get16 ( module, cursor );
					uint16_t kind_code = get16 ( module, cursor + 2 );
					size_t next = cursor + 2 + size;

					if ( size < 2 || next > end )
						break;

					bool procedure = kind_code == 0x110F || kind_code == 0x1110 || kind_code == 0x1146 || kind_code == 0x1147;
					bool variable = kind_code == 0x110C || kind_code == 0x110D;
					size_t name_at = procedure ? cursor + 39 : cursor + 14;
					size_t offset_at = procedure ? cursor + 32 : cursor + 8;
					size_t segment_at = procedure ? cursor + 36 : cursor + 12;

					if ( ( procedure || variable ) && name_at < next ) {

						uint16_t segment = get16 ( module, segment_at );
						size_t length = 0;

						while ( name_at + length < next && module [ name_at + length ] )
							length++;

						if ( segment >= 1 && segment <= bases.count ( ) && length && name_at + length < next ) {

							symbol item;
							wchar_t name [ 400 ];
							int32_t n = static_cast < int32_t > ( base::min ( length, static_cast < size_t > ( 399 ) ) );

							for ( int32_t k = 0; k < n; k++ )
								name [ k ] = static_cast < wchar_t > ( module [ name_at + static_cast < size_t > ( k ) ] );

							name [ n ] = 0;
							item.name.set ( name );
							item.address = file.base + bases [ static_cast < size_t > ( segment - 1 ) ] + get32 ( module, offset_at );
							item.type = symbol_kind::DEBUG;

							if ( procedure ) {

								file.functions.push ( base::move ( item ) );
								functions++;

							} else
								file.labels.push ( base::move ( item ) );

						}

					}

					cursor = next;

				}

			}

			if ( container.stream ( record_stream, records ) ) {

				size_t at = 0;
				wchar_t name [ 400 ];

				while ( at + 4 <= records.size ( ) ) {

					uint16_t size = get16 ( records, at );
					uint16_t kind_code = get16 ( records, at + 2 );
					size_t next = at + 2 + size;

					if ( size < 2 || next > records.size ( ) )
						break;

					bool pub = kind_code == 0x110E;
					bool data = kind_code == 0x110C || kind_code == 0x110D;

					if ( pub || data ) {

						uint32_t flags = pub ? get32 ( records, at + 4 ) : 0;
						uint32_t offset = get32 ( records, at + 8 );
						uint16_t segment = get16 ( records, at + 12 );
						const char* raw = reinterpret_cast < const char* > ( records.data ( ) + at + 14 );
						bool terminated = false;

						for ( size_t k = at + 14; k < next; k++ ) {

							if ( !records [ k ] ) {

								terminated = true;

								break;

							}

						}

						if ( terminated && segment >= 1 && segment <= bases.count ( ) ) {

							uint64_t address = file.base + bases [ static_cast < size_t > ( segment - 1 ) ] + offset;

							undecorate ( raw, x86, name, 400 );

							if ( name [ 0 ] ) {

								symbol item;

								item.name.set ( name );
								item.address = address;
								item.type = symbol_kind::DEBUG;

								if ( pub && ( flags & 2 ) ) {

									file.functions.push ( base::move ( item ) );
									functions++;

								} else if ( !( pub && ( flags & 1 ) ) ) {

									file.labels.push ( base::move ( item ) );

								}

							}

						}

					}

					at = next;

				}

			}

			base::sort ( file.labels.data ( ), file.labels.count ( ), [ ] ( const symbol& a, const symbol& b ) { return a.address < b.address; } );

			int32_t kept = 0;

			for ( int32_t i = 0; i < file.labels.count ( ); i++ ) {

				if ( kept && file.labels [ static_cast < size_t > ( kept - 1 ) ].address == file.labels [ static_cast < size_t > ( i ) ].address )
					continue;

				if ( kept != i )
					file.labels [ static_cast < size_t > ( kept ) ] = base::move ( file.labels [ static_cast < size_t > ( i ) ] );

				kept++;

			}

			file.labels.resize ( static_cast < size_t > ( kept ) );
			file.symbol_file.set ( path );
			report.grouped ( static_cast < uint64_t > ( functions ) ).add ( L" functions and " ).grouped ( static_cast < uint64_t > ( kept ) ).add ( L" data names" );
			loaded = functions + kept > 0;

			if ( !loaded )
				report.add ( L", nothing usable" );

		}

		if ( view )
			UnmapViewOfFile ( view );

		if ( mapping )
			CloseHandle ( mapping );

		CloseHandle ( handle );

		return loaded;

	}

	static const wchar_t* base_name ( const wchar_t* path ) {

		const wchar_t* name = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				name = at + 1;

		}

		return name;

	}

	static bool exists ( const wchar_t* path ) {

		DWORD attributes = GetFileAttributesW ( path );

		return attributes != INVALID_FILE_ATTRIBUTES && !( attributes & FILE_ATTRIBUTE_DIRECTORY );

	}

	static void cache_path ( const binary& file, const wchar_t* folder, base::writer& out ) {

		const wchar_t* name = base_name ( file.debug_path.get ( ) );
		const uint8_t* g = file.debug_guid;
		uint32_t first = static_cast < uint32_t > ( g [ 0 ] | ( g [ 1 ] << 8 ) | ( g [ 2 ] << 16 ) | ( g [ 3 ] << 24 ) );
		uint32_t second = static_cast < uint32_t > ( g [ 4 ] | ( g [ 5 ] << 8 ) );
		uint32_t third = static_cast < uint32_t > ( g [ 6 ] | ( g [ 7 ] << 8 ) );

		out.clear ( );
		out.add ( folder );

		if ( out.size ( ) && out.text ( ) [ out.size ( ) - 1 ] != L'\\' )
			out.add ( L"\\" );

		out.add ( name ).add ( L"\\" ).hex ( first, 8 ).hex ( second, 4 ).hex ( third, 4 );

		for ( int32_t i = 8; i < 16; i++ )
			out.hex ( g [ i ], 2 );

		out.hex ( file.debug_age ).add ( L"\\" ).add ( name );

	}

	bool load_symbols ( binary& file, base::writer& report ) {

		if ( file.debug_path.empty ( ) )
			return false;

		base::list < base::text > candidates;
		base::writer path;
		const wchar_t* name = base_name ( file.debug_path.get ( ) );
		const wchar_t* folder_end = base_name ( file.path.get ( ) );

		candidates.emplace ( ).set ( file.debug_path.get ( ) );

		for ( const wchar_t* at = file.path.get ( ); at < folder_end; at++ )
			path.add ( *at );

		path.add ( name );
		candidates.emplace ( ).set ( path.text ( ) );

		if ( file.debug_known ) {

			wchar_t variable [ 2048 ];
			DWORD got = GetEnvironmentVariableW ( L"_NT_SYMBOL_PATH", variable, 2048 );

			if ( got && got < 2048 ) {

				wchar_t* at = variable;

				while ( *at ) {

					wchar_t* entry = at;

					while ( *at && *at != L';' )
						at++;

					if ( *at )
						*at++ = 0;

					wchar_t* piece = entry;

					while ( *piece ) {

						wchar_t* part = piece;

						while ( *piece && *piece != L'*' )
							piece++;

						if ( *piece )
							*piece++ = 0;

						if ( part [ 0 ] && part [ 1 ] == L':' ) {

							cache_path ( file, part, path );
							candidates.emplace ( ).set ( path.text ( ) );

						}

					}

				}

			}

			const wchar_t* defaults [ 2 ] = { L"C:\\symbols", L"C:\\ProgramData\\dbg\\sym" };

			for ( const wchar_t* folder : defaults ) {

				cache_path ( file, folder, path );
				candidates.emplace ( ).set ( path.text ( ) );

			}

			wchar_t local [ MAX_PATH ];
			DWORD local_length = GetEnvironmentVariableW ( L"LOCALAPPDATA", local, MAX_PATH );

			if ( local_length && local_length < MAX_PATH - 40 ) {

				base::writer folder;

				folder.add ( local ).add ( L"\\Temp\\SymbolCache" );
				cache_path ( file, folder.text ( ), path );
				candidates.emplace ( ).set ( path.text ( ) );

			}

		}

		for ( const base::text& candidate : candidates ) {

			if ( !exists ( candidate.get ( ) ) )
				continue;

			base::writer detail;

			if ( read_pdb ( file, candidate.get ( ), detail ) ) {

				report.add ( L"Loaded " ).add ( detail.text ( ) ).add ( L" from " ).add ( candidate.get ( ) );

				return true;

			}

			report.add ( candidate.get ( ) ).add ( L" " ).add ( detail.text ( ) ).add ( L". " );

		}

		if ( !report.size ( ) )
			report.add ( L"No matching ").add ( name ).add ( L" next to the file or in the symbol caches." );

		return false;

	}

}
