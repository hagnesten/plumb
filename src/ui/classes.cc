#include "ui/app.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	void app::export_classes ( ) {

		if ( !db || !analysis_applied || db->classes.empty ( ) )
			return;

		const base::list < analysis::class_info >& classes = db->classes;
		uint32_t pointer = file.wide ( ) ? 8 : 4;
		base::list < wchar_t > out;
		base::list < int32_t > order;
		wchar_t name [ 200 ];
		int32_t methods_total = 0;

		auto put = [ & ] ( const wchar_t* text ) {

			for ( ; *text; text++ )
				out.push ( *text );

		};

		auto find_class = [ & ] ( const wchar_t* wanted ) {

			int32_t found = -1;

			for ( int32_t i = 0; i < classes.count ( ); i++ ) {

				const analysis::class_info& item = classes [ static_cast < size_t > ( i ) ];

				if ( !base::same_text ( item.name.get ( ), wanted ) )
					continue;

				if ( found < 0 || ( item.offset == 0 && classes [ static_cast < size_t > ( found ) ].offset != 0 ) )
					found = i;

			}

			return found;

		};

		auto slot_entry = [ & ] ( const analysis::class_info& item, int32_t slot, uint64_t& entry ) {

			uint64_t offset = 0;

			if ( slot < 0 || slot >= item.methods || !file.offset_of ( item.vtable + static_cast < uint64_t > ( slot ) * pointer, offset ) || offset + pointer > file.size )
				return false;

			entry = pointer == 8 ? file.read64 ( offset ) : file.read32 ( offset );

			return true;

		};

		auto has_base = [ & ] ( int32_t index, const wchar_t* wanted ) {

			if ( index < 0 )
				return false;

			const analysis::class_info& item = classes [ static_cast < size_t > ( index ) ];

			for ( int32_t b = 0; b < item.base_count; b++ ) {

				if ( base::same_text ( db->class_bases [ static_cast < size_t > ( item.first_base + b ) ].get ( ), wanted ) )
					return true;

			}

			return false;

		};

		for ( int32_t i = 0; i < classes.count ( ); i++ ) {

			if ( find_class ( classes [ static_cast < size_t > ( i ) ].name.get ( ) ) == i )
				order.push ( i );

		}

		base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t a, int32_t b ) {

			return classes [ static_cast < size_t > ( a ) ].base_count < classes [ static_cast < size_t > ( b ) ].base_count;

		} );

		put ( L"// Classes recovered from RTTI in " );
		put ( file.name.get ( ) );
		put ( L" by Plumb.\r\n// Virtual functions are in vtable order. Bases come before the classes that derive from them.\r\n\r\n#pragma once\r\n\r\n" );

		for ( int32_t index : order ) {

			const analysis::class_info& item = classes [ static_cast < size_t > ( index ) ];
			base::list < int32_t > direct;

			for ( int32_t b = 0; b < item.base_count; b++ ) {

				const wchar_t* candidate = db->class_bases [ static_cast < size_t > ( item.first_base + b ) ].get ( );
				bool indirect = false;

				for ( int32_t other = 0; other < item.base_count && !indirect; other++ ) {

					if ( other != b )
						indirect = has_base ( find_class ( db->class_bases [ static_cast < size_t > ( item.first_base + other ) ].get ( ) ), candidate );

				}

				if ( !indirect )
					direct.push ( item.first_base + b );

			}

			const wchar_t* full = item.name.get ( );
			int32_t split = -1;
			int32_t nesting = 0;

			for ( int32_t i = 0; full [ i ]; i++ ) {

				if ( full [ i ] == L'<' )
					nesting++;
				else if ( full [ i ] == L'>' )
					nesting--;
				else if ( !nesting && full [ i ] == L':' && full [ i + 1 ] == L':' )
					split = i;

			}

			if ( split > 0 ) {

				put ( L"namespace " );

				for ( int32_t i = 0; i < split; i++ )
					out.push ( full [ i ] );

				put ( L" {\r\n\r\n" );

			}

			put ( L"class " );
			put ( split > 0 ? full + split + 2 : full );

			for ( int32_t i = 0; i < direct.count ( ); i++ ) {

				put ( i ? L", public " : L" : public " );
				put ( db->class_bases [ static_cast < size_t > ( direct [ static_cast < size_t > ( i ) ] ) ].get ( ) );

			}

			put ( L" {\r\n\r\npublic:\r\n\r\n" );

			int32_t parent = direct.empty ( ) ? -1 : find_class ( db->class_bases [ static_cast < size_t > ( direct [ 0 ] ) ].get ( ) );

			for ( int32_t k = 0; k < item.methods; k++ ) {

				uint64_t entry = 0;
				uint64_t inherited = 0;

				if ( !slot_entry ( item, k, entry ) )
					break;

				bool from_parent = parent >= 0 && slot_entry ( classes [ static_cast < size_t > ( parent ) ], k, inherited ) && inherited == entry;
				const wchar_t* method = nullptr;
				base::writer line;

				if ( symbol_name ( entry, true, name, 200 ) ) {

					method = name;

					for ( const wchar_t* at = name; *at; at++ ) {

						if ( at [ 0 ] == L':' && at [ 1 ] == L':' && at [ 2 ] )
							method = at + 2;

					}

				}

				bool pure = method && ( base::same_text ( method, L"_purecall" ) || base::same_text ( method, L"purecall" ) );
				const wchar_t* declared = prototype_for ( entry );
				decompiler::c_prototype shape;
				base::writer problem;
				bool typed = declared && decompiler::parse_prototype ( declared, file.wide ( ), shape, problem );
				base::writer label;

				if ( !method || pure || ( method [ 0 ] == L's' && method [ 1 ] == L'u' && method [ 2 ] == L'b' && method [ 3 ] == L'_' ) )
					label.add ( L"vfunc_" ).number ( k );
				else
					label.add ( method );

				line.add ( L"\tvirtual " ).add ( typed && shape.result [ 0 ] ? shape.result : L"void" ).add ( L" " ).add ( label.text ( ) ).add ( c_spaced ? L" (" : L"(" );

				int32_t first = typed && shape.count > 0 && base::same_text ( shape.names [ 0 ], L"this" ) ? 1 : 0;
				int32_t shown = 0;

				for ( int32_t p = first; typed && p < shape.count; p++ ) {

					line.add ( shown ? L", " : ( c_spaced ? L" " : L"" ) ).add ( shape.types [ p ] );

					if ( shape.names [ p ] [ 0 ] )
						line.add ( L" " ).add ( shape.names [ p ] );

					shown++;

				}

				if ( typed && shape.variadic )
					line.add ( shown++ ? L", ..." : ( c_spaced ? L" ..." : L"..." ) );

				line.add ( c_spaced ? L" )" : L")" ).add ( pure ? L" = 0;" : L";" );

				while ( line.size ( ) < 56 )
					line.add ( L' ' );

				line.add ( L" // " ).number ( k ).add ( L"  " ).hex ( entry );

				if ( from_parent )
					line.add ( L", inherited from " ).add ( classes [ static_cast < size_t > ( parent ) ].name.get ( ) );

				line.add ( L"\r\n" );
				put ( line.text ( ) );
				methods_total++;

			}

			put ( L"\r\n};\r\n\r\n" );

			if ( split > 0 )
				put ( L"}\r\n\r\n" );

		}

		out.push ( 0 );
		copy_text ( out.data ( ) );

		base::writer target;

		if ( !write_beside ( L".classes.h", out, target ) ) {

			log ( log_kind::WARN, L"Copied the classes to the clipboard, but couldn't write the header file." );

			return;

		}

		log ( log_kind::GOOD, base::writer ( ).add ( L"Exported " ).grouped ( static_cast < uint64_t > ( order.count ( ) ) ).add ( L" classes with " ).grouped ( static_cast < uint64_t > ( methods_total ) ).add ( L" virtual functions to " ).add ( target.text ( ) ).add ( L" and the clipboard." ).text ( ) );

	}

}