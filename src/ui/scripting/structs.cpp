#include "ui/scripting.h"

namespace ui {

	static int32_t field_index ( const user_struct& owner, script_context& call, int32_t index ) {

		if ( index >= call.count )
			return -1;

		const script::value& given = call.args [ index ];

		for ( int32_t i = 0; i < owner.fields.count ( ); i++ ) {

			const user_field& item = owner.fields [ static_cast < size_t > ( i ) ];

			if ( given.type == script::kind::NUMBER && item.offset == given.number )
				return i;

			if ( given.type != script::kind::TEXT )
				continue;

			const wchar_t* field_name = item.name.get ( );
			const wchar_t* wanted = call.engine->text_of ( given );
			int32_t k = 0;

			while ( field_name [ k ] && field_name [ k ] == wanted [ k ] )
				k++;

			if ( !wanted [ k ] && ( !field_name [ k ] || field_name [ k ] == L'[' ) )
				return i;

		}

		return -1;

	}

	static void push_field ( script_context& call, script::value list, const user_field& item ) {

		script::value row = call.list ( );

		call.push_number ( row, item.offset );
		call.push_text ( row, item.name.get ( ) );
		call.push_text ( row, item.type.get ( ) );
		call.push_number ( row, item.size );
		call.push_list ( list, row );

	}

	bool app::script_structs ( script_context& call ) {

		if ( call.is ( L"structs" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			for ( const user_struct& item : structs )
				call.push_text ( made, item.name.get ( ) );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"struct_create" ) ) {

			const wchar_t* given = nullptr;

			if ( !call.takes ( 1, 1 ) || !( given = call.text ( 0 ) ) )
				return true;

			if ( !base::is_identifier ( given ) )
				return call.fail ( L"Struct names are letters, digits and underscores, and can't start with a digit." );

			if ( struct_named ( given ) >= 0 )
				return call.fail ( base::writer ( ).add ( L"There's already a struct called " ).add ( given ).add ( L"." ).text ( ) );

			user_struct& made = structs.emplace ( );

			made.name.set ( given );
			script_changed ( panel::STRUCTS );
			call.give_number ( 1 );

			return true;

		}

		if ( !call.is ( L"struct_exists" ) && !call.is ( L"struct_delete" ) && !call.is ( L"struct_rename" ) && !call.is ( L"struct_size" ) && !call.is ( L"struct_fields" ) && !call.is ( L"struct_field" ) && !call.is ( L"struct_field_offset" ) && !call.is ( L"struct_field_at" ) && !call.is ( L"struct_add_field" ) && !call.is ( L"struct_del_field" ) && !call.is ( L"struct_rename_field" ) && !call.is ( L"struct_set_field_type" ) && !call.is ( L"struct_text" ) )
			return false;

		const wchar_t* wanted = nullptr;

		if ( !call.takes ( 1, 4 ) || !( wanted = call.text ( 0 ) ) )
			return true;

		int32_t index = struct_named ( wanted );

		if ( call.is ( L"struct_exists" ) ) {

			if ( call.takes ( 1, 1 ) )
				call.give_number ( index >= 0 );

			return true;

		}

		if ( index < 0 )
			return call.fail ( base::writer ( ).add ( L"There's no struct called " ).add ( wanted ).add ( L"." ).text ( ) );

		user_struct& owner = structs [ static_cast < size_t > ( index ) ];

		if ( call.is ( L"struct_delete" ) ) {

			if ( !call.takes ( 1, 1 ) )
				return true;

			structs.remove_at ( static_cast < size_t > ( index ) );
			expanded_struct = -1;
			script_changed ( panel::STRUCTS );
			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"struct_rename" ) ) {

			const wchar_t* given = nullptr;

			if ( !call.takes ( 2, 2 ) || !( given = call.text ( 1 ) ) )
				return true;

			if ( !base::is_identifier ( given ) )
				return call.fail ( L"Struct names are letters, digits and underscores, and can't start with a digit." );

			int32_t clash = struct_named ( given );

			if ( clash >= 0 && clash != index )
				return call.fail ( base::writer ( ).add ( L"There's already a struct called " ).add ( given ).add ( L"." ).text ( ) );

			owner.name.set ( given );
			script_changed ( panel::STRUCTS );
			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"struct_size" ) ) {

			if ( call.takes ( 1, 1 ) )
				call.give_number ( struct_size ( index ) );

			return true;

		}

		if ( call.is ( L"struct_fields" ) ) {

			if ( !call.takes ( 1, 1 ) )
				return true;

			script::value made = call.list ( );

			for ( const user_field& item : owner.fields )
				push_field ( call, made, item );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"struct_text" ) ) {

			if ( !call.takes ( 1, 1 ) )
				return true;

			base::list < wchar_t > out;

			auto put = [ & ] ( const wchar_t* text ) {

				for ( ; *text; text++ )
					out.push ( *text );

			};

			put ( L"struct " );
			put ( owner.name.get ( ) );
			put ( L"\n{\n" );

			for ( const user_field& item : owner.fields ) {

				base::writer line;

				line.add ( L"  " ).add ( item.type.get ( ) ).add ( L" " ).add ( item.name.get ( ) ).add ( L"; // +0x" ).hex ( static_cast < uint64_t > ( item.offset ) ).add ( L"\n" );
				put ( line.text ( ) );

			}

			put ( L"};\n" );
			call.give_text ( out.data ( ), out.count ( ) );

			return true;

		}

		if ( call.is ( L"struct_field_at" ) ) {

			int64_t offset = 0;

			if ( !call.takes ( 2, 2 ) || !call.number ( 1, offset ) )
				return true;

			for ( const user_field& item : owner.fields ) {

				if ( offset >= item.offset && offset < item.offset + base::max ( item.size, 1 ) ) {

					script::value made = call.list ( );

					push_field ( call, made, item );
					call.give_list ( call.engine->list_item ( made, 0 ) );

					return true;

				}

			}

			return true;

		}

		if ( call.is ( L"struct_add_field" ) ) {

			int64_t offset = 0;
			const wchar_t* field_name = nullptr;
			const wchar_t* type = nullptr;

			if ( !call.takes ( 4, 4 ) || !call.number ( 1, offset ) || !( field_name = call.text ( 2 ) ) || !( type = call.text ( 3 ) ) )
				return true;

			wchar_t bare [ 64 ];
			int32_t length = 0;

			for ( ; field_name [ length ] && field_name [ length ] != L'[' && length < 63; length++ )
				bare [ length ] = field_name [ length ];

			bare [ length ] = 0;

			if ( !base::is_identifier ( bare ) )
				return call.fail ( L"Field names are letters, digits and underscores, and can't start with a digit." );

			if ( offset < 0 )
				offset = struct_size ( index );

			if ( offset > 0x7FFFFFFF )
				return call.fail ( L"That offset is too large." );

			decompiler::c_type shape;
			wchar_t clean [ 64 ];
			base::writer problem;

			if ( !decompiler::parse_type ( type, file.wide ( ), shape, clean, 64 ) || shape.empty )
				return call.fail ( base::writer ( ).add ( L"Couldn't read the type \"" ).add ( type ).add ( L"\"." ).text ( ) );

			if ( !known_type ( shape, problem ) )
				return call.fail ( problem.text ( ) );

			if ( !add_field ( index, static_cast < int32_t > ( offset ), field_name, type ) )
				return call.fail ( L"Couldn't add that field." );

			script_changed ( panel::STRUCTS );
			call.give_number ( offset );

			return true;

		}

		int32_t found = field_index ( owner, call, 1 );

		if ( call.is ( L"struct_field" ) || call.is ( L"struct_field_offset" ) ) {

			if ( !call.takes ( 2, 2 ) || found < 0 )
				return true;

			const user_field& item = owner.fields [ static_cast < size_t > ( found ) ];

			if ( call.is ( L"struct_field_offset" ) )
				call.give_number ( item.offset );
			else {

				script::value made = call.list ( );

				push_field ( call, made, item );
				call.give_list ( call.engine->list_item ( made, 0 ) );

			}

			return true;

		}

		if ( !call.takes ( call.is ( L"struct_del_field" ) ? 2 : 3, call.is ( L"struct_del_field" ) ? 2 : 3 ) )
			return true;

		if ( found < 0 )
			return call.fail ( base::writer ( ).add ( owner.name.get ( ) ).add ( L" has no such field." ).text ( ) );

		user_field& item = owner.fields [ static_cast < size_t > ( found ) ];

		if ( call.is ( L"struct_del_field" ) ) {

			owner.fields.remove_at ( static_cast < size_t > ( found ) );
			script_changed ( panel::STRUCTS );
			call.give_number ( 1 );

			return true;

		}

		const wchar_t* given = call.text ( 2 );

		if ( !given )
			return true;

		if ( call.is ( L"struct_rename_field" ) ) {

			if ( !base::is_identifier ( given ) )
				return call.fail ( L"Field names are letters, digits and underscores, and can't start with a digit." );

			item.name.set ( given );
			script_changed ( panel::STRUCTS );
			call.give_number ( 1 );

			return true;

		}

		decompiler::c_type shape;
		wchar_t clean [ 64 ];
		base::writer problem;

		if ( !decompiler::parse_type ( given, file.wide ( ), shape, clean, 64 ) || shape.empty )
			return call.fail ( base::writer ( ).add ( L"Couldn't read the type \"" ).add ( given ).add ( L"\"." ).text ( ) );

		if ( !known_type ( shape, problem ) )
			return call.fail ( problem.text ( ) );

		base::text keep;

		keep.set ( item.name.get ( ) );

		if ( !add_field ( index, item.offset, keep.get ( ), given ) )
			return call.fail ( L"Couldn't change that field's type." );

		script_changed ( panel::STRUCTS );
		call.give_number ( 1 );

		return true;

	}

}
