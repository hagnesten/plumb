#include "loader/binary.hh"
#include <windows.h>

namespace loader {

	static const wchar_t* directory_names [ 16 ] = { L"Export", L"Import", L"Resource", L"Exception", L"Security", L"Base relocation", L"Debug", L"Architecture", L"Global pointer", L"TLS", L"Load config", L"Bound import", L"IAT", L"Delay import", L"CLR runtime", L"Reserved" };

	const wchar_t* directory_name ( int32_t index ) {

		return index >= 0 && index < 16 ? directory_names [ index ] : L"";

	}

	const wchar_t* kind_name ( kind value ) {

		switch ( value ) {

			case kind::HEADER:
				return L"Header";

			case kind::CODE:
				return L"Code";

			case kind::CONSTANT:
				return L"Read-only data";

			case kind::EMPTY:
				return L"Uninitialized";

			default:
				return L"Data";

		}

	}

	uint8_t binary::byte ( uint64_t offset ) const {

		return offset < size ? data [ offset ] : 0;

	}

	void binary::poke ( uint64_t offset, uint8_t value ) {

		if ( offset < size )
			const_cast < uint8_t* > ( data ) [ offset ] = value;

	}

	uint16_t binary::read16 ( uint64_t offset ) const {

		if ( offset + 2 > size )
			return 0;

		return static_cast < uint16_t > ( data [ offset ] | ( data [ offset + 1 ] << 8 ) );

	}

	uint32_t binary::read32 ( uint64_t offset ) const {

		if ( offset + 4 > size )
			return 0;

		return static_cast < uint32_t > ( data [ offset ] ) | ( static_cast < uint32_t > ( data [ offset + 1 ] ) << 8 ) | ( static_cast < uint32_t > ( data [ offset + 2 ] ) << 16 ) | ( static_cast < uint32_t > ( data [ offset + 3 ] ) << 24 );

	}

	uint64_t binary::read64 ( uint64_t offset ) const {

		if ( offset + 8 > size )
			return 0;

		return static_cast < uint64_t > ( read32 ( offset ) ) | ( static_cast < uint64_t > ( read32 ( offset + 4 ) ) << 32 );

	}

	bool binary::loaded ( ) const {

		return type != format::NONE;

	}

	bool binary::wide ( ) const {

		return type == format::PE64 || ( type == format::RAW && size > 0xFFFFFFFFull );

	}

	const wchar_t* binary::machine_name ( ) const {

		switch ( arch ) {

			case machine::X86:
				return L"x86";

			case machine::X64:
				return L"x86-64";

			case machine::ARM:
				return L"ARM Thumb-2";

			case machine::ARM64:
				return L"ARM64";

			default:
				return L"Unknown";

		}

	}

	const wchar_t* binary::format_name ( ) const {

		switch ( type ) {

			case format::PE32:
				return L"PE32";

			case format::PE64:
				return L"PE32+";

			case format::RAW:
				return L"Raw binary";

			default:
				return L"None";

		}

	}

	void binary::note ( const wchar_t* text ) {

		notes.push ( base::text ( text ) );

	}

	void binary::close ( ) {

		if ( data )
			UnmapViewOfFile ( data );

		if ( mapping )
			CloseHandle ( mapping );

		if ( file )
			CloseHandle ( file );

		*this = binary ( );

	}

	bool binary::open ( const wchar_t* file_path, base::writer& problem ) {

		close ( );

		HANDLE handle = CreateFileW ( file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( handle == INVALID_HANDLE_VALUE ) {

			problem.add ( L"Couldn't open the file. It may be in use or you may not have access to it." );

			return false;

		}

		LARGE_INTEGER length = { };

		GetFileSizeEx ( handle, &length );

		if ( length.QuadPart <= 0 ) {

			CloseHandle ( handle );
			problem.add ( L"The file is empty." );

			return false;

		}

		HANDLE map = CreateFileMappingW ( handle, nullptr, PAGE_WRITECOPY, 0, 0, nullptr );
		void* view = map ? MapViewOfFile ( map, FILE_MAP_COPY, 0, 0, 0 ) : nullptr;

		if ( !view ) {

			if ( map )
				CloseHandle ( map );

			CloseHandle ( handle );
			problem.add ( L"Couldn't map the file into memory." );

			return false;

		}

		file = handle;
		mapping = map;
		data = static_cast < const uint8_t* > ( view );
		size = static_cast < uint64_t > ( length.QuadPart );
		path.set ( file_path );

		const wchar_t* tail = file_path;

		for ( const wchar_t* at = file_path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				tail = at + 1;

		}

		name.set ( tail );

		if ( !parse_pe ( ) ) {

			sections.clear ( );
			modules.clear ( );
			imports.clear ( );
			exports.clear ( );
			functions.clear ( );
			type = format::RAW;
			arch = machine::UNKNOWN;
			base = 0;
			entry = 0;

			section& whole = sections.emplace ( );

			whole.name [ 0 ] = L'f';
			whole.name [ 1 ] = L'i';
			whole.name [ 2 ] = L'l';
			whole.name [ 3 ] = L'e';
			whole.size = size;
			whole.raw = size;
			whole.type = kind::DATA;
			whole.read = true;

			if ( size >= 4 && data [ 0 ] == 0x7F && data [ 1 ] == 'E' && data [ 2 ] == 'L' && data [ 3 ] == 'F' )
				note ( L"ELF image detected. ELF parsing isn't in yet, so it opens as raw bytes." );
			else if ( size >= 4 && ( read32 ( 0 ) == 0xFEEDFACEu || read32 ( 0 ) == 0xFEEDFACFu || read32 ( 0 ) == 0xBEBAFECAu ) )
				note ( L"Mach-O image detected. Mach-O parsing isn't in yet, so it opens as raw bytes." );
			else
				note ( L"No executable header recognized. Showing the file as raw bytes." );

		}

		scan_strings ( );

		return true;

	}

	bool binary::rva_offset ( uint32_t rva, uint64_t& result ) const {

		if ( rva < header_size ) {

			if ( rva >= size )
				return false;

			result = rva;

			return true;

		}

		for ( int32_t i = 1; i < sections.count ( ); i++ ) {

			const section& item = sections [ static_cast < size_t > ( i ) ];
			uint64_t start = item.address - base;
			uint64_t span = base::max ( item.size, item.raw );

			if ( rva >= start && rva < start + span ) {

				uint64_t delta = rva - start;

				if ( delta >= item.raw )
					return false;

				result = item.offset + delta;

				return result < size;

			}

		}

		return false;

	}

	bool binary::offset_of ( uint64_t address, uint64_t& result ) const {

		if ( type == format::RAW ) {

			result = address;

			return address < size;

		}

		if ( address < base || address - base > 0xFFFFFFFFull )
			return false;

		return rva_offset ( static_cast < uint32_t > ( address - base ), result );

	}

	bool binary::address_of ( uint64_t offset, uint64_t& result ) const {

		for ( const section& item : sections ) {

			if ( offset >= item.offset && offset < item.offset + item.raw ) {

				result = item.address + ( offset - item.offset );

				return true;

			}

		}

		return false;

	}

	int32_t binary::section_at ( uint64_t address ) const {

		for ( int32_t i = 0; i < sections.count ( ); i++ ) {

			const section& item = sections [ static_cast < size_t > ( i ) ];

			if ( address >= item.address && address < item.address + base::max ( item.size, item.raw ) )
				return i;

		}

		return -1;

	}

	uint64_t binary::low ( ) const {

		return sections.empty ( ) ? 0 : sections [ 0 ].address;

	}

	uint64_t binary::high ( ) const {

		uint64_t top = 0;

		for ( const section& item : sections )
			top = base::max ( top, item.address + base::max ( item.size, item.raw ) );

		return top;

	}

	bool binary::read_name ( uint32_t rva, base::text& result, int32_t limit ) const {

		uint64_t at = 0;

		if ( !rva || !rva_offset ( rva, at ) )
			return false;

		wchar_t buffer [ 512 ];
		int32_t length = 0;

		limit = base::min ( limit, 511 );

		while ( length < limit && at + static_cast < uint64_t > ( length ) < size ) {

			uint8_t value = data [ at + static_cast < uint64_t > ( length ) ];

			if ( !value )
				break;

			buffer [ length++ ] = static_cast < wchar_t > ( value );

		}

		buffer [ length ] = 0;
		result.set ( buffer );

		return length > 0;

	}

	bool binary::parse_pe ( ) {

		if ( size < 0x40 || read16 ( 0 ) != 0x5A4D )
			return false;

		uint32_t header = read32 ( 0x3C );

		if ( header < 4 || static_cast < uint64_t > ( header ) + 24 > size || read32 ( header ) != 0x00004550u )
			return false;

		uint64_t file_header = static_cast < uint64_t > ( header ) + 4;
		uint16_t machine_code = read16 ( file_header );
		uint16_t count = read16 ( file_header + 2 );

		pe_offset = header;
		stamp = read32 ( file_header + 4 );
		optional_size = read16 ( file_header + 16 );
		characteristics = read16 ( file_header + 18 );
		optional_offset = file_header + 20;

		if ( optional_offset + optional_size > size || optional_size < 2 )
			return false;

		uint16_t magic = read16 ( optional_offset );

		if ( magic != 0x10B && magic != 0x20B )
			return false;

		bool plus = magic == 0x20B;
		uint32_t fixed = plus ? 112 : 96;

		if ( optional_size < fixed )
			return false;

		uint32_t entry_rva = read32 ( optional_offset + 16 );

		base = plus ? read64 ( optional_offset + 24 ) : read32 ( optional_offset + 28 );
		header_size = read32 ( optional_offset + 60 );
		subsystem = read16 ( optional_offset + 68 );
		dll_flags = read16 ( optional_offset + 70 );
		type = plus ? format::PE64 : format::PE32;
		dll = ( characteristics & 0x2000 ) != 0;

		switch ( machine_code ) {

			case 0x14C:
				arch = machine::X86;
				break;

			case 0x8664:
				arch = machine::X64;
				break;

			case 0x1C0:
			case 0x1C2:
			case 0x1C4:
				arch = machine::ARM;
				break;

			case 0xAA64:
				arch = machine::ARM64;
				break;

			default:
				arch = machine::UNKNOWN;
				break;

		}

		uint32_t listed = read32 ( optional_offset + ( plus ? 108 : 92 ) );
		uint32_t room = ( optional_size - fixed ) / 8;

		directory_count = static_cast < int32_t > ( base::min ( base::min ( listed, room ), 16u ) );

		for ( int32_t i = 0; i < directory_count; i++ ) {

			uint64_t at = optional_offset + fixed + static_cast < uint64_t > ( i ) * 8;

			directories [ i ].address = read32 ( at );
			directories [ i ].size = read32 ( at + 4 );

		}

		section& head = sections.emplace ( );
		const wchar_t* head_name = L"HEADER";

		for ( int32_t i = 0; head_name [ i ]; i++ )
			head.name [ i ] = head_name [ i ];

		head.address = base;
		head.size = base::max ( header_size, 0x200u );
		head.offset = 0;
		head.raw = base::min ( static_cast < uint64_t > ( header_size ), size );
		head.type = kind::HEADER;
		head.read = true;

		uint64_t table = optional_offset + optional_size;

		for ( int32_t i = 0; i < count && i < 96; i++ ) {

			uint64_t at = table + static_cast < uint64_t > ( i ) * 40;

			if ( at + 40 > size )
				break;

			section& item = sections.emplace ( );

			for ( int32_t k = 0; k < 8; k++ ) {

				uint8_t value = data [ at + static_cast < uint64_t > ( k ) ];

				if ( !value )
					break;

				item.name [ k ] = static_cast < wchar_t > ( value );

			}

			uint32_t virtual_size = read32 ( at + 8 );
			uint32_t virtual_address = read32 ( at + 12 );
			uint32_t raw_size = read32 ( at + 16 );
			uint32_t raw_pointer = read32 ( at + 20 );

			item.flags = read32 ( at + 36 );
			item.address = base + virtual_address;
			item.size = virtual_size ? virtual_size : raw_size;
			item.offset = raw_pointer;
			item.raw = raw_pointer < size ? base::min ( static_cast < uint64_t > ( raw_size ), size - raw_pointer ) : 0;
			item.execute = ( item.flags & 0x20000000u ) != 0;
			item.read = ( item.flags & 0x40000000u ) != 0;
			item.write = ( item.flags & 0x80000000u ) != 0;

			if ( item.execute || ( item.flags & 0x20u ) )
				item.type = kind::CODE;
			else if ( !item.raw )
				item.type = kind::EMPTY;
			else if ( item.write )
				item.type = kind::DATA;
			else
				item.type = kind::CONSTANT;

		}

		entry = entry_rva ? base + entry_rva : 0;

		if ( entry ) {

			symbol& start = functions.emplace ( );

			start.name.set ( dll ? L"DllEntryPoint" : L"start" );
			start.address = entry;
			start.type = symbol_kind::ENTRY;

		}

		read_imports ( );
		read_exports ( );
		read_tls ( );
		read_debug ( );

		for ( const exported& item : exports ) {

			if ( !item.address || !item.forward.empty ( ) )
				continue;

			int32_t owner = section_at ( item.address );

			if ( owner < 0 || sections [ static_cast < size_t > ( owner ) ].type != kind::CODE )
				continue;

			symbol& function = functions.emplace ( );
			base::writer label;

			if ( item.name.empty ( ) )
				label.add ( L"Ordinal_" ).number ( item.ordinal );
			else
				label.add ( item.name.get ( ) );

			function.name.set ( label.text ( ) );
			function.address = item.address;
			function.type = symbol_kind::EXPORT;

		}

		base::writer summary;

		summary.add ( format_name ( ) ).add ( L" " ).add ( dll ? L"DLL" : L"executable" ).add ( L" for " ).add ( machine_name ( ) ).add ( L", " ).number ( sections.count ( ) - 1 ).add ( L" sections, image base 0x" ).hex ( base );
		note ( summary.text ( ) );

		return true;

	}

	void binary::read_imports ( ) {

		if ( directory_count < 2 || !directories [ 1 ].address )
			return;

		uint64_t at = 0;

		if ( !rva_offset ( directories [ 1 ].address, at ) )
			return;

		uint32_t step = wide ( ) ? 8 : 4;

		for ( int32_t index = 0; index < 4096; index++, at += 20 ) {

			if ( at + 20 > size )
				break;

			uint32_t lookup = read32 ( at );
			uint32_t name_rva = read32 ( at + 12 );
			uint32_t thunk = read32 ( at + 16 );

			if ( !lookup && !name_rva && !thunk )
				break;

			int32_t owner_index = modules.count ( );

			modules.emplace ( );

			module& owner = modules [ static_cast < size_t > ( owner_index ) ];

			if ( !read_name ( name_rva, owner.name, 256 ) )
				owner.name.set ( L"(unnamed)" );

			owner.first = imports.count ( );

			uint64_t walk = 0;

			if ( !rva_offset ( lookup ? lookup : thunk, walk ) )
				continue;

			for ( uint32_t k = 0; k < 65536; k++, walk += step ) {

				if ( walk + step > size )
					break;

				uint64_t value = step == 8 ? read64 ( walk ) : read32 ( walk );

				if ( !value )
					break;

				import& item = imports.emplace ( );
				bool ordinal = step == 8 ? ( value >> 63 ) != 0 : ( value >> 31 ) != 0;

				item.owner = owner_index;
				item.slot = base + thunk + static_cast < uint64_t > ( k ) * step;

				if ( ordinal ) {

					item.by_ordinal = true;
					item.ordinal = static_cast < uint32_t > ( value & 0xFFFF );

				} else {

					uint32_t hint_rva = static_cast < uint32_t > ( value & 0x7FFFFFFF );
					uint64_t hint_at = 0;

					if ( rva_offset ( hint_rva, hint_at ) ) {

						item.ordinal = read16 ( hint_at );
						read_name ( hint_rva + 2, item.name, 256 );

					}

				}

			}

			modules [ static_cast < size_t > ( owner_index ) ].count = imports.count ( ) - modules [ static_cast < size_t > ( owner_index ) ].first;

		}

	}

	void binary::read_exports ( ) {

		if ( directory_count < 1 || !directories [ 0 ].address )
			return;

		uint64_t at = 0;

		if ( !rva_offset ( directories [ 0 ].address, at ) || at + 40 > size )
			return;

		uint32_t ordinal_base = read32 ( at + 16 );
		uint32_t function_count = base::min ( read32 ( at + 20 ), 65536u );
		uint32_t name_count = base::min ( read32 ( at + 24 ), 65536u );
		uint64_t functions_at = 0;
		uint64_t names_at = 0;
		uint64_t ordinals_at = 0;

		if ( !rva_offset ( read32 ( at + 28 ), functions_at ) )
			return;

		bool named = rva_offset ( read32 ( at + 32 ), names_at ) && rva_offset ( read32 ( at + 36 ), ordinals_at );
		base::list < int32_t > slot_of;

		slot_of.fill ( function_count, -1 );

		for ( uint32_t i = 0; i < function_count; i++ ) {

			uint32_t rva = read32 ( functions_at + static_cast < uint64_t > ( i ) * 4 );

			if ( !rva )
				continue;

			slot_of [ i ] = exports.count ( );

			exported& item = exports.emplace ( );

			item.ordinal = ordinal_base + i;

			if ( rva >= directories [ 0 ].address && rva < directories [ 0 ].address + directories [ 0 ].size )
				read_name ( rva, item.forward, 256 );
			else
				item.address = base + rva;

		}

		for ( uint32_t n = 0; named && n < name_count; n++ ) {

			uint32_t name_rva = read32 ( names_at + static_cast < uint64_t > ( n ) * 4 );
			uint16_t index = read16 ( ordinals_at + static_cast < uint64_t > ( n ) * 2 );

			if ( index < function_count && slot_of [ index ] >= 0 )
				read_name ( name_rva, exports [ static_cast < size_t > ( slot_of [ index ] ) ].name, 256 );

		}

	}

	void binary::read_tls ( ) {

		if ( directory_count < 10 || !directories [ 9 ].address )
			return;

		uint64_t at = 0;

		if ( !rva_offset ( directories [ 9 ].address, at ) )
			return;

		uint64_t list = wide ( ) ? read64 ( at + 24 ) : read32 ( at + 12 );
		uint64_t walk = 0;

		if ( !list || !offset_of ( list, walk ) )
			return;

		uint32_t step = wide ( ) ? 8 : 4;

		for ( int32_t i = 0; i < 64; i++, walk += step ) {

			uint64_t callback = step == 8 ? read64 ( walk ) : read32 ( walk );

			if ( !callback )
				break;

			symbol& item = functions.emplace ( );
			base::writer label;

			label.add ( L"TlsCallback_" ).number ( i );
			item.name.set ( label.text ( ) );
			item.address = callback;
			item.type = symbol_kind::TLS;

		}

	}

	void binary::read_debug ( ) {

		if ( directory_count < 7 || !directories [ 6 ].address )
			return;

		uint64_t at = 0;

		if ( !rva_offset ( directories [ 6 ].address, at ) )
			return;

		uint32_t entries = base::min ( directories [ 6 ].size / 28, 32u );

		for ( uint32_t i = 0; i < entries; i++ ) {

			uint64_t item = at + static_cast < uint64_t > ( i ) * 28;

			if ( read32 ( item + 12 ) != 2 )
				continue;

			uint64_t record = read32 ( item + 24 );

			if ( read32 ( record ) != 0x53445352u )
				continue;

			wchar_t buffer [ 260 ];
			int32_t length = 0;

			for ( uint64_t p = record + 24; p < size && length < 259 && data [ p ]; p++ )
				buffer [ length++ ] = static_cast < wchar_t > ( data [ p ] );

			buffer [ length ] = 0;
			debug_path.set ( buffer );

			for ( int32_t g = 0; g < 16; g++ )
				debug_guid [ g ] = byte ( record + 4 + static_cast < uint64_t > ( g ) );

			debug_age = read32 ( record + 20 );
			debug_known = true;

			break;

		}

	}

	void binary::scan_strings ( ) {

		base::list < found_string > narrow;
		base::list < found_string > wide_list;
		uint64_t limit = base::min ( size, 0x10000000ull );
		uint64_t start = 0;
		uint32_t run = 0;

		for ( uint64_t i = 0; i <= limit; i++ ) {

			uint8_t value = i < limit ? data [ i ] : 0;
			bool printable = ( value >= 0x20 && value < 0x7F ) || value == 9;

			if ( printable && i < limit ) {

				if ( !run )
					start = i;

				run++;

				continue;

			}

			if ( run >= 5 && narrow.count ( ) < 200000 ) {

				found_string& item = narrow.emplace ( );

				item.offset = start;
				item.length = run;

			}

			run = 0;

		}

		run = 0;

		for ( uint64_t i = 0; i <= limit; i += 2 ) {

			bool inside = i + 1 < limit;
			uint8_t low_byte = inside ? data [ i ] : 0;
			uint8_t high_byte = inside ? data [ i + 1 ] : 1;
			bool printable = inside && high_byte == 0 && ( ( low_byte >= 0x20 && low_byte < 0x7F ) || low_byte == 9 );

			if ( printable ) {

				if ( !run )
					start = i;

				run++;

				continue;

			}

			if ( run >= 5 && wide_list.count ( ) < 100000 ) {

				found_string& item = wide_list.emplace ( );

				item.offset = start;
				item.length = run;
				item.wide = true;

			}

			run = 0;

		}

		strings.clear ( );
		strings.reserve ( narrow.size ( ) + wide_list.size ( ) );

		size_t a = 0;
		size_t b = 0;

		while ( a < narrow.size ( ) || b < wide_list.size ( ) ) {

			if ( b >= wide_list.size ( ) || ( a < narrow.size ( ) && narrow [ a ].offset <= wide_list [ b ].offset ) )
				strings.push ( narrow [ a++ ] );
			else
				strings.push ( wide_list [ b++ ] );

		}

	}

	const symbol* binary::label_at ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = labels.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = labels [ static_cast < size_t > ( middle ) ].address;

			if ( value == address )
				return &labels [ static_cast < size_t > ( middle ) ];

			if ( value < address )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return nullptr;

	}

	void binary::string_text ( const found_string& item, wchar_t* out, int32_t room ) const {

		int32_t length = base::min ( static_cast < int32_t > ( item.length ), room - 1 );

		for ( int32_t i = 0; i < length; i++ )
			out [ i ] = static_cast < wchar_t > ( byte ( item.offset + static_cast < uint64_t > ( i ) * ( item.wide ? 2 : 1 ) ) );

		out [ length ] = 0;

	}

}