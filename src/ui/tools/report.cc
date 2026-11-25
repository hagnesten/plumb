#include "ui/app.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	bool app::write_beside ( const wchar_t* suffix, const base::list < wchar_t >& text, base::writer& target ) {

		base::list < char > data;
		int32_t length = text.count ( ) > 0 && text.back ( ) == 0 ? text.count ( ) - 1 : text.count ( );
		int32_t needed = length ? WideCharToMultiByte ( CP_UTF8, 0, text.data ( ), length, nullptr, 0, nullptr, nullptr ) : 0;

		data.resize ( static_cast < size_t > ( base::max ( needed, 0 ) ) );

		if ( needed > 0 )
			WideCharToMultiByte ( CP_UTF8, 0, text.data ( ), length, data.data ( ), needed, nullptr, nullptr );

		const wchar_t* path = file.path.get ( );
		int32_t dot = -1;
		int32_t end = 0;

		for ( ; path [ end ]; end++ ) {

			if ( path [ end ] == L'.' )
				dot = end;
			else if ( path [ end ] == L'\\' || path [ end ] == L'/' )
				dot = -1;

		}

		target.clear ( );

		for ( int32_t i = 0; i < ( dot >= 0 ? dot : end ); i++ )
			target.add ( path [ i ] );

		target.add ( suffix );

		if ( files::write ( target.text ( ), data.data ( ), data.count ( ) ) )
			return true;

		const wchar_t* short_name = file.name.get ( );
		int32_t name_dot = -1;

		for ( int32_t i = 0; short_name [ i ]; i++ ) {

			if ( short_name [ i ] == L'.' )
				name_dot = i;

		}

		target.clear ( );
		target.add ( files::folder ( ) ).add ( L"\\exports" );
		CreateDirectoryW ( target.text ( ), nullptr );
		target.add ( L"\\" );

		for ( int32_t i = 0; short_name [ i ] && ( name_dot < 0 || i < name_dot ); i++ )
			target.add ( short_name [ i ] );

		target.add ( suffix );

		return files::write ( target.text ( ), data.data ( ), data.count ( ) );

	}

	void app::export_report ( ) {

		if ( !file.loaded ( ) )
			return;

		base::list < wchar_t > out;
		bool wide = file.wide ( );
		bool analyzed = db && analysis_applied && !db->functions.empty ( );
		wchar_t name [ 200 ];

		auto put = [ & ] ( const wchar_t* text ) {

			for ( ; *text; text++ )
				out.push ( *text );

		};

		auto cell = [ & ] ( const wchar_t* text ) {

			for ( ; *text; text++ ) {

				if ( *text == L'|' )
					out.push ( L'\\' );

				out.push ( *text == L'\r' || *text == L'\n' ? L' ' : *text );

			}

		};

		auto where = [ & ] ( uint64_t address ) {

			base::writer text;
			int32_t owner = analyzed ? db->function_at ( address ) : -1;

			if ( owner >= 0 ) {

				function_name ( owner, name, 200 );
				text.add ( name );

				if ( address != db->functions [ static_cast < size_t > ( owner ) ].start )
					text.add ( L"+" ).hex ( address - db->functions [ static_cast < size_t > ( owner ) ].start );

			} else {

				int32_t section = file.section_at ( address );

				text.add ( section >= 0 ? file.sections [ static_cast < size_t > ( section ) ].name : L"outside" );

			}

			cell ( text.text ( ) );

		};

		auto address_cell = [ & ] ( uint64_t address ) {

			put ( base::writer ( ).add ( L"`" ).hex ( address, wide ? 16 : 8 ).add ( L"`" ).text ( ) );

		};

		auto annotations = [ & ] ( const wchar_t* title, const base::list < user_name >& source ) {

			if ( source.empty ( ) )
				return;

			put ( base::writer ( ).add ( L"\r\n## " ).add ( title ).add ( L"\r\n\r\n| Address | Where | Text |\r\n| --- | --- | --- |\r\n" ).text ( ) );

			for ( const user_name& item : source ) {

				put ( L"| " );
				address_cell ( item.address );
				put ( L" | " );
				where ( item.address );
				put ( L" | " );
				cell ( item.text.get ( ) );
				put ( L" |\r\n" );

			}

		};

		put ( base::writer ( ).add ( L"# " ).add ( file.name.get ( ) ).add ( L"\r\n\r\n" ).text ( ) );
		put ( base::writer ( ).add ( L"- Path: `" ).add ( file.path.get ( ) ).add ( L"`\r\n" ).text ( ) );
		put ( base::writer ( ).add ( L"- Format: " ).add ( file.format_name ( ) ).add ( L", " ).add ( file.machine_name ( ) ).add ( file.dll ? L" DLL" : L"" ).add ( L"\r\n" ).text ( ) );
		put ( base::writer ( ).add ( L"- Size: " ).grouped ( file.size ).add ( L" bytes\r\n" ).text ( ) );
		put ( base::writer ( ).add ( L"- Image base: `" ).hex ( file.base, wide ? 16 : 8 ).add ( L"`\r\n" ).text ( ) );

		if ( file.entry )
			put ( base::writer ( ).add ( L"- Entry point: `" ).hex ( file.entry, wide ? 16 : 8 ).add ( L"`\r\n" ).text ( ) );

		if ( !file.debug_path.empty ( ) )
			put ( base::writer ( ).add ( L"- Debug info: `" ).add ( file.debug_path.get ( ) ).add ( L"`\r\n" ).text ( ) );

		put ( base::writer ( ).add ( L"- Imports: " ).grouped ( static_cast < uint64_t > ( file.imports.count ( ) ) ).add ( L" from " ).number ( file.modules.count ( ) ).add ( L" modules, exports: " ).grouped ( static_cast < uint64_t > ( file.exports.count ( ) ) ).add ( L", strings: " ).grouped ( static_cast < uint64_t > ( file.strings.count ( ) ) ).add ( L"\r\n" ).text ( ) );

		if ( analyzed )
			put ( base::writer ( ).add ( L"- Analysis: " ).grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L" functions, " ).grouped ( db->decoded ).add ( L" instructions, " ).grouped ( static_cast < uint64_t > ( db->refs.count ( ) ) ).add ( L" references, " ).number ( db->classes.count ( ) ).add ( L" RTTI classes\r\n" ).text ( ) );

		put ( L"\r\n## Sections\r\n\r\n| Name | Address | Size | Access | Kind |\r\n| --- | --- | --- | --- | --- |\r\n" );

		for ( const loader::section& item : file.sections ) {

			put ( L"| " );
			cell ( item.name );
			put ( L" | " );
			address_cell ( item.address );
			put ( base::writer ( ).add ( L" | 0x" ).hex ( item.size ).add ( L" | " ).add ( item.read ? L"R" : L"-" ).add ( item.write ? L"W" : L"-" ).add ( item.execute ? L"X" : L"-" ).add ( L" | " ).add ( loader::kind_name ( item.type ) ).add ( L" |\r\n" ).text ( ) );

		}

		if ( !notes.empty ( ) ) {

			put ( L"\r\n## Function notes\r\n\r\n| Address | Function | Note |\r\n| --- | --- | --- |\r\n" );

			for ( const user_name& item : notes ) {

				put ( L"| " );
				address_cell ( item.address );
				put ( L" | " );
				where ( item.address );
				put ( L" | " );
				cell ( item.text.get ( ) );
				put ( L" |\r\n" );

			}

		}

		if ( !renamed.empty ( ) || !prototypes.empty ( ) ) {

			put ( L"\r\n## Named functions\r\n\r\n| Address | Name | Prototype |\r\n| --- | --- | --- |\r\n" );

			for ( const user_name& item : renamed ) {

				const wchar_t* shape = prototype_for ( item.address );

				put ( L"| " );
				address_cell ( item.address );
				put ( L" | " );
				cell ( item.text.get ( ) );
				put ( L" | " );

				if ( shape ) {

					put ( L"`" );
					cell ( shape );
					put ( L"`" );

				}

				put ( L" |\r\n" );

			}

			for ( const user_name& item : prototypes ) {

				if ( user_text ( renamed, item.address ) )
					continue;

				put ( L"| " );
				address_cell ( item.address );
				put ( L" | " );
				where ( item.address );
				put ( L" | `" );
				cell ( item.text.get ( ) );
				put ( L"` |\r\n" );

			}

		}

		annotations ( L"Bookmarks", bookmarks );
		annotations ( L"Comments", comments );

		if ( !patches.empty ( ) ) {

			put ( L"\r\n## Patches\r\n\r\n| Address | Original | Patched |\r\n| --- | --- | --- |\r\n" );

			int32_t i = 0;

			while ( i < patches.count ( ) ) {

				int32_t j = i;

				while ( j + 1 < patches.count ( ) && patches [ static_cast < size_t > ( j + 1 ) ].offset == patches [ static_cast < size_t > ( j ) ].offset + 1 && j - i < 31 )
					j++;

				uint64_t address = 0;
				base::writer before;
				base::writer after;

				for ( int32_t k = i; k <= j; k++ ) {

					before.hex ( patches [ static_cast < size_t > ( k ) ].original, 2 ).add ( k < j ? L" " : L"" );
					after.hex ( patches [ static_cast < size_t > ( k ) ].value, 2 ).add ( k < j ? L" " : L"" );

				}

				put ( L"| " );

				if ( file.address_of ( patches [ static_cast < size_t > ( i ) ].offset, address ) )
					address_cell ( address );
				else
					put ( base::writer ( ).add ( L"file +" ).hex ( patches [ static_cast < size_t > ( i ) ].offset ).text ( ) );

				put ( base::writer ( ).add ( L" | `" ).add ( before.text ( ) ).add ( L"` | `" ).add ( after.text ( ) ).add ( L"` |\r\n" ).text ( ) );
				i = j + 1;

			}

		}

		if ( analyzed && !db->classes.empty ( ) ) {

			put ( L"\r\n## Classes\r\n\r\n| Class | Vtable | Virtuals |\r\n| --- | --- | --- |\r\n" );

			for ( const analysis::class_info& item : db->classes ) {

				put ( L"| " );
				cell ( item.name.get ( ) );
				put ( L" | " );
				address_cell ( item.vtable );
				put ( base::writer ( ).add ( L" | " ).number ( item.methods ).add ( L" |\r\n" ).text ( ) );

			}

		}

		if ( !file.modules.empty ( ) ) {

			put ( L"\r\n## Imported modules\r\n\r\n| Module | Imports |\r\n| --- | --- |\r\n" );

			for ( const loader::module& item : file.modules ) {

				put ( L"| " );
				cell ( item.name.get ( ) );
				put ( base::writer ( ).add ( L" | " ).number ( item.count ).add ( L" |\r\n" ).text ( ) );

			}

		}

		out.push ( 0 );

		base::writer target;

		if ( !write_beside ( L".report.md", out, target ) ) {

			log ( log_kind::FAIL, L"Couldn't write the report." );

			return;

		}

		log ( log_kind::GOOD, base::writer ( ).add ( L"Wrote the report to " ).add ( target.text ( ) ).text ( ) );

	}

}
