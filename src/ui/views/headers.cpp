#include "ui/app.h"
#include "ui/icons.h"
#include "ui/format.h"

namespace ui {

	struct header_row {

		int32_t kind = 1;
		const wchar_t* name = L"";
		uint64_t offset = 0;
		uint32_t size = 0;
		wchar_t value [ 48 ] = { };
		wchar_t meaning [ 160 ] = { };

	};

	static void copy_to ( wchar_t* target, int32_t room, const wchar_t* source ) {

		int32_t i = 0;

		for ( ; source && source [ i ] && i < room - 1; i++ )
			target [ i ] = source [ i ];

		target [ i ] = 0;

	}

	static void describe_time ( uint32_t stamp, base::writer& out ) {

		int64_t days = static_cast < int64_t > ( stamp ) / 86400;
		int64_t rest = static_cast < int64_t > ( stamp ) % 86400;
		int64_t z = days + 719468;
		int64_t era = z / 146097;
		int64_t doe = z - era * 146097;
		int64_t yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
		int64_t year = yoe + era * 400;
		int64_t doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 );
		int64_t mp = ( 5 * doy + 2 ) / 153;
		int64_t day = doy - ( 153 * mp + 2 ) / 5 + 1;
		int64_t month = mp < 10 ? mp + 3 : mp - 9;

		if ( month <= 2 )
			year++;

		out.number ( year ).add ( L'-' );

		if ( month < 10 )
			out.add ( L'0' );

		out.number ( month ).add ( L'-' );

		if ( day < 10 )
			out.add ( L'0' );

		out.number ( day ).add ( L' ' );

		if ( rest / 3600 < 10 )
			out.add ( L'0' );

		out.number ( rest / 3600 ).add ( L':' );

		if ( rest / 60 % 60 < 10 )
			out.add ( L'0' );

		out.number ( rest / 60 % 60 ).add ( L" UTC" );

	}

	static void describe_flags ( uint32_t value, const uint32_t* bits, const wchar_t* const* labels, int32_t count, base::writer& out ) {

		bool any = false;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( !( value & bits [ i ] ) )
				continue;

			if ( any )
				out.add ( L", " );

			out.add ( labels [ i ] );
			any = true;

		}

		if ( !any )
			out.add ( L"none" );

	}

	void app::draw_headers ( math::rect area ) {

		render::canvas& c = canvas;

		c.fill ( area, 0.0f, ink ( tone::BASE ) );

		if ( file.type == loader::format::RAW ) {

			c.text ( L"This file didn't parse as a PE image, so there are no headers to show.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

			return;

		}

		static base::list < header_row > rows;
		bool plus = file.type == loader::format::PE64;
		uint64_t dos = 0;
		uint64_t fh = file.pe_offset + 4;
		uint64_t oh = file.optional_offset;

		rows.clear ( );

		auto heading = [ & ] ( const wchar_t* title ) {

			header_row& row = rows.emplace ( );

			row.kind = 0;
			row.name = title;

		};

		auto field_row = [ & ] ( const wchar_t* name, uint64_t at, uint32_t size, const wchar_t* meaning ) {

			header_row& row = rows.emplace ( );
			base::writer value;
			uint64_t number = size == 1 ? file.byte ( at ) : ( size == 2 ? file.read16 ( at ) : ( size == 4 ? file.read32 ( at ) : file.read64 ( at ) ) );

			row.name = name;
			row.offset = at;
			row.size = size;
			value.hex ( number, static_cast < int32_t > ( size * 2 ) );
			copy_to ( row.value, 48, value.text ( ) );
			copy_to ( row.meaning, 160, meaning );

			return number;

		};

		heading ( L"DOS header" );
		field_row ( L"e_magic", dos, 2, L"\"MZ\" signature" );
		field_row ( L"e_cblp", dos + 2, 2, L"Bytes on last page" );
		field_row ( L"e_cp", dos + 4, 2, L"Pages in file" );
		field_row ( L"e_cparhdr", dos + 8, 2, L"Header size in paragraphs" );
		field_row ( L"e_sp", dos + 16, 2, L"Initial SP" );
		field_row ( L"e_ip", dos + 20, 2, L"Initial IP" );
		field_row ( L"e_lfarlc", dos + 24, 2, L"Relocation table offset" );
		field_row ( L"e_lfanew", dos + 60, 4, L"Offset of the PE header" );

		heading ( L"File header" );
		field_row ( L"Signature", file.pe_offset, 4, L"\"PE\\0\\0\"" );

		{

			base::writer meaning;

			meaning.add ( file.machine_name ( ) );
			field_row ( L"Machine", fh, 2, meaning.text ( ) );

		}

		field_row ( L"NumberOfSections", fh + 2, 2, L"" );

		{

			base::writer meaning;

			describe_time ( file.stamp, meaning );
			field_row ( L"TimeDateStamp", fh + 4, 4, meaning.text ( ) );

		}

		field_row ( L"PointerToSymbolTable", fh + 8, 4, L"" );
		field_row ( L"NumberOfSymbols", fh + 12, 4, L"" );
		field_row ( L"SizeOfOptionalHeader", fh + 16, 2, L"" );

		{

			const uint32_t bits [ ] = { 0x1, 0x2, 0x20, 0x100, 0x200, 0x1000, 0x2000 };
			const wchar_t* const labels [ ] = { L"RELOCS_STRIPPED", L"EXECUTABLE", L"LARGE_ADDRESS_AWARE", L"32BIT_MACHINE", L"DEBUG_STRIPPED", L"SYSTEM", L"DLL" };
			base::writer meaning;

			describe_flags ( file.characteristics, bits, labels, 7, meaning );
			field_row ( L"Characteristics", fh + 18, 2, meaning.text ( ) );

		}

		heading ( plus ? L"Optional header (PE32+)" : L"Optional header (PE32)" );
		field_row ( L"Magic", oh, 2, plus ? L"PE32+, 64-bit" : L"PE32, 32-bit" );
		field_row ( L"MajorLinkerVersion", oh + 2, 1, L"" );
		field_row ( L"MinorLinkerVersion", oh + 3, 1, L"" );
		field_row ( L"SizeOfCode", oh + 4, 4, L"" );
		field_row ( L"SizeOfInitializedData", oh + 8, 4, L"" );
		field_row ( L"SizeOfUninitializedData", oh + 12, 4, L"" );
		field_row ( L"AddressOfEntryPoint", oh + 16, 4, L"RVA of the first instruction" );
		field_row ( L"BaseOfCode", oh + 20, 4, L"" );

		if ( !plus )
			field_row ( L"BaseOfData", oh + 24, 4, L"" );

		field_row ( L"ImageBase", plus ? oh + 24 : oh + 28, plus ? 8 : 4, L"Preferred load address" );
		field_row ( L"SectionAlignment", oh + 32, 4, L"" );
		field_row ( L"FileAlignment", oh + 36, 4, L"" );
		field_row ( L"MajorOperatingSystemVersion", oh + 40, 2, L"" );
		field_row ( L"MajorSubsystemVersion", oh + 48, 2, L"" );
		field_row ( L"SizeOfImage", oh + 56, 4, L"" );
		field_row ( L"SizeOfHeaders", oh + 60, 4, L"" );
		field_row ( L"CheckSum", oh + 64, 4, L"" );

		{

			const wchar_t* names_list [ ] = { L"Unknown", L"Native", L"Windows GUI", L"Windows console", L"", L"OS/2 console", L"", L"POSIX console", L"Native Win9x", L"Windows CE GUI", L"EFI application", L"EFI boot driver", L"EFI runtime driver", L"EFI ROM", L"Xbox", L"", L"Boot application" };

			field_row ( L"Subsystem", oh + 68, 2, file.subsystem < 17 ? names_list [ file.subsystem ] : L"" );

		}

		{

			const uint32_t bits [ ] = { 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x4000, 0x8000 };
			const wchar_t* const labels [ ] = { L"HIGH_ENTROPY_VA", L"DYNAMIC_BASE", L"FORCE_INTEGRITY", L"NX_COMPAT", L"NO_ISOLATION", L"NO_SEH", L"NO_BIND", L"APPCONTAINER", L"GUARD_CF", L"TERMINAL_SERVER_AWARE" };
			base::writer meaning;

			describe_flags ( file.dll_flags, bits, labels, 10, meaning );
			field_row ( L"DllCharacteristics", oh + 70, 2, meaning.text ( ) );

		}

		field_row ( L"SizeOfStackReserve", oh + 72, plus ? 8 : 4, L"" );
		field_row ( L"SizeOfStackCommit", plus ? oh + 80 : oh + 76, plus ? 8 : 4, L"" );
		field_row ( L"SizeOfHeapReserve", plus ? oh + 88 : oh + 80, plus ? 8 : 4, L"" );
		field_row ( L"SizeOfHeapCommit", plus ? oh + 96 : oh + 84, plus ? 8 : 4, L"" );
		field_row ( L"NumberOfRvaAndSizes", plus ? oh + 108 : oh + 92, 4, L"" );

		heading ( L"Data directories" );

		for ( int32_t i = 0; i < file.directory_count; i++ ) {

			const loader::directory& entry = file.directories [ i ];
			header_row& row = rows.emplace ( );
			base::writer value;
			base::writer meaning;
			int32_t owner = entry.address ? file.section_at ( file.base + entry.address ) : -1;

			row.name = loader::directory_name ( i );
			row.offset = oh + ( plus ? 112 : 96 ) + static_cast < uint64_t > ( i ) * 8;
			row.size = 8;
			value.hex ( entry.address, 8 ).add ( L"  " ).hex ( entry.size, 8 );
			copy_to ( row.value, 48, value.text ( ) );

			if ( !entry.address )
				meaning.add ( L"empty" );
			else {

				meaning.grouped ( entry.size ).add ( L" bytes" );

				if ( owner >= 0 )
					meaning.add ( L" in " ).add ( file.sections [ static_cast < size_t > ( owner ) ].name );

			}

			copy_to ( row.meaning, 160, meaning.text ( ) );

		}

		heading ( L"Section table" );

		for ( int32_t i = 1; i < file.sections.count ( ); i++ ) {

			const loader::section& part = file.sections [ static_cast < size_t > ( i ) ];
			header_row& row = rows.emplace ( );
			base::writer value;
			base::writer meaning;
			const uint32_t bits [ ] = { 0x20, 0x40, 0x80, 0x02000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000u };
			const wchar_t* const labels [ ] = { L"CODE", L"INITIALIZED", L"UNINITIALIZED", L"DISCARDABLE", L"SHARED", L"EXECUTE", L"READ", L"WRITE" };

			row.name = part.name;
			row.offset = oh + file.optional_size + static_cast < uint64_t > ( i - 1 ) * 40;
			row.size = 40;
			value.hex ( part.address - file.base, 8 ).add ( L"  " ).hex ( part.size, 8 );
			copy_to ( row.value, 48, value.text ( ) );
			meaning.add ( L"raw @" ).hex ( part.offset ).add ( L" +" ).hex ( part.raw ).add ( L"  \x00B7  " );
			describe_flags ( part.flags, bits, labels, 8, meaning );
			copy_to ( row.meaning, 160, meaning.text ( ) );

		}

		float row_h = 22.0f;
		float head_h = 38.0f;
		float content = 16.0f;

		for ( const header_row& row : rows )
			content += row.kind == 0 ? head_h : row_h;

		uint64_t id = make_id ( "headers" );

		scroll_region ( id, area );
		now->headers_scroll.target -= take_wheel ( id ) * 66.0f;
		glide ( now->headers_scroll, content - area.h + 20.0f );

		float x = area.x + 24.0f;
		float x_offset = x + 250.0f;
		float x_value = x_offset + 90.0f;
		float x_meaning = x_value + 160.0f;
		float y = area.y + 12.0f - now->headers_scroll.shown;

		c.push_clip ( area );

		for ( int32_t pass = 0; pass < 2; pass++ ) {

			float cursor = y;
			int32_t stripe = 0;

			for ( const header_row& row : rows ) {

				float row_height = row.kind == 0 ? head_h : row_h;

				if ( cursor + row_height < area.y || cursor > area.bottom ( ) ) {

					cursor += row_height;
					stripe = row.kind == 0 ? 0 : stripe + 1;

					continue;

				}

				if ( row.kind == 0 ) {

					if ( pass == 1 ) {

						c.text ( row.name, render::font::HEADING, math::vec2 ( x, cursor + 22.0f ), ink ( tone::INK ) );
						c.text ( L"OFFSET", render::font::TINY, math::vec2 ( x_offset, cursor + 23.0f ), ink ( tone::INK_FAINT ) );
						c.text ( L"VALUE", render::font::TINY, math::vec2 ( x_value, cursor + 23.0f ), ink ( tone::INK_FAINT ) );
						c.text ( L"MEANING", render::font::TINY, math::vec2 ( x_meaning, cursor + 23.0f ), ink ( tone::INK_FAINT ) );

					} else
						c.fill ( math::rect ( x, cursor + row_height - 2.0f, area.w - 48.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );

					cursor += row_height;
					stripe = 0;

					continue;

				}

				float cy = cursor + row_h * 0.5f;

				if ( pass == 0 ) {

					if ( stripe % 2 == 1 )
						c.fill ( math::rect ( x - 8.0f, cursor, area.w - 32.0f, row_h ), 2.0f, ink ( tone::HOVER ).fade ( 0.6f ) );

				} else {

					base::writer position;

					position.hex ( row.offset, 4 );
					c.text ( row.name, render::font::MONO, math::vec2 ( x, cy ), ink ( tone::INK ) );
					c.text ( position.text ( ), render::font::MONO_SMALL, math::vec2 ( x_offset, cy ), ink ( tone::ADDRESS ) );
					c.text ( row.value, render::font::MONO, math::vec2 ( x_value, cy ), ink ( tone::NUMBER ) );
					c.text ( row.meaning, render::font::SMALL, math::vec2 ( x_meaning, cy ), ink ( tone::INK_SOFT ), render::align::LEFT, area.right ( ) - x_meaning - 20.0f );

				}

				cursor += row_h;
				stripe++;

			}

		}

		c.pop_clip ( );
		draw_scrollbar ( make_id ( "headers_bar" ), area, now->headers_scroll, content + 20.0f, area.h );

	}

}