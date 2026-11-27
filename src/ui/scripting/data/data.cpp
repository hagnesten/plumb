#include "ui/scripting/data/data.h"
#include "ui/scripting/bridge/bridge.h"
#include "base/strings/strings.h"

void ui::push_pairs ( script_context& call, const base::list < user_name >& list ) {

	script::value made = call.list ( );

	for ( const user_name& item : list ) {

		if ( item.text.empty ( ) )
			continue;

		script::value row = call.list ( );

		call.push_number ( row, static_cast < int64_t > ( item.address ) );
		call.push_text ( row, item.text.get ( ) );
		call.push_list ( made, row );

	}

	call.give_list ( made );

}

bool ui::app::script_data ( script_context& call ) {

	if ( call.is ( L"original_byte" ) ) {

		uint64_t where = 0;
		uint64_t offset = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !file.offset_of ( where, offset ) || offset >= file.size )
			return true;

		int32_t found = patch_index ( offset );

		call.give_number ( found >= 0 ? patches [ static_cast < size_t > ( found ) ].original : file.byte ( offset ) );

		return true;

	}

	if ( call.is ( L"is_patched" ) ) {

		uint64_t where = 0;
		int64_t length = 1;

		if ( call.takes ( 1, 2 ) && call.address ( 0, where ) && ( !call.has ( 1 ) || call.number ( 1, length ) ) )
			call.give_number ( is_patched ( where, static_cast < int32_t > ( base::clamp ( length, 1ll, 1ll << 30 ) ) ) );

		return true;

	}

	if ( call.is ( L"patch_word" ) || call.is ( L"patch_dword" ) || call.is ( L"patch_qword" ) ) {

		uint64_t where = 0;
		int64_t value = 0;
		int32_t size = call.is ( L"patch_word" ) ? 2 : ( call.is ( L"patch_dword" ) ? 4 : 8 );

		if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) || !call.number ( 1, value ) )
			return true;

		base::writer hex_text;

		for ( int32_t i = 0; i < size; i++ )
			hex_text.hex ( ( static_cast < uint64_t > ( value ) >> ( i * 8 ) ) & 0xFF, 2 ).add ( L" " );

		script_touch ( );
		call.give_number ( patch_text ( where, hex_text.text ( ) ) ? 1 : 0 );

		return true;

	}

	if ( call.is ( L"revert" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) )
			return true;

		bool had = is_patched ( where, 1 );

		script_touch ( );
		revert_patch ( where );
		call.give_number ( had && !is_patched ( where, 1 ) );

		return true;

	}

	if ( call.is ( L"revert_all" ) ) {

		if ( !call.takes ( 0, 0 ) )
			return true;

		int32_t total = patches.count ( );

		script_touch ( );

		while ( !patches.empty ( ) ) {

			uint64_t where = 0;
			int32_t before = patches.count ( );

			if ( !file.address_of ( patches [ 0 ].offset, where ) )
				break;

			revert_patch ( where );

			if ( patches.count ( ) == before )
				break;

		}

		call.give_number ( total - patches.count ( ) );

		return true;

	}

	if ( call.is ( L"patches" ) ) {

		if ( !call.takes ( 0, 0 ) )
			return true;

		script::value made = call.list ( );

		for ( const patched_byte& item : patches ) {

			uint64_t where = 0;
			script::value row = call.list ( );

			file.address_of ( item.offset, where );
			call.push_number ( row, static_cast < int64_t > ( where ) );
			call.push_number ( row, item.original );
			call.push_number ( row, item.value );
			call.push_list ( made, row );

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"write_patched" ) ) {

		if ( call.takes ( 0, 0 ) ) {

			export_patched ( );
			call.give_number ( 1 );

		}

		return true;

	}

	if ( call.is ( L"names" ) || call.is ( L"comments" ) || call.is ( L"notes" ) || call.is ( L"bookmarks" ) || call.is ( L"prototypes" ) || call.is ( L"conditions" ) ) {

		if ( call.takes ( 0, 0 ) )
			push_pairs ( call, call.is ( L"names" ) ? renamed : ( call.is ( L"comments" ) ? comments : ( call.is ( L"notes" ) ? notes : ( call.is ( L"bookmarks" ) ? bookmarks : ( call.is ( L"prototypes" ) ? prototypes : conditions ) ) ) ) );

		return true;

	}

	if ( call.is ( L"del_name" ) || call.is ( L"del_comment" ) || call.is ( L"del_note" ) || call.is ( L"del_prototype" ) || call.is ( L"del_bookmark" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) )
			return true;

		bool whole = call.is ( L"del_note" ) || call.is ( L"del_prototype" );

		if ( whole ? !call.function_start ( 0, where ) : !call.address ( 0, where ) )
			return true;

		base::list < user_name >& list = call.is ( L"del_name" ) ? renamed : ( call.is ( L"del_comment" ) ? comments : ( call.is ( L"del_note" ) ? notes : ( call.is ( L"del_prototype" ) ? prototypes : bookmarks ) ) );

		if ( call.is ( L"del_name" ) && db && analysis_applied && db->function_at ( where ) >= 0 && !db->is_label ( where ) && db->function_starting ( where ) < 0 )
			where = db->functions [ static_cast < size_t > ( db->function_at ( where ) ) ].start;

		bool had = user_text ( list, where ) != nullptr;

		set_user_text ( list, where, L"" );
		script_changed ( call.is ( L"del_name" ) ? panel::FUNCTIONS : panel::NOTES );
		call.give_number ( had );

		return true;

	}

	if ( call.is ( L"bookmark" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) )
			return true;

		const wchar_t* found = user_text ( bookmarks, where );

		if ( found )
			call.give_text ( found );

		return true;

	}

	if ( call.is ( L"set_bookmark" ) ) {

		uint64_t where = 0;
		const wchar_t* given = nullptr;
		wchar_t name [ 200 ];

		if ( !call.takes ( 1, 2 ) || !call.address ( 0, where ) )
			return true;

		if ( call.has ( 1 ) ) {

			if ( !( given = call.text ( 1 ) ) )
				return true;

		} else if ( symbol_name ( where, true, name, 200 ) )
			given = name;
		else {

			base::writer fallback;

			fallback.hex ( where );

			for ( int32_t i = 0; i <= fallback.size ( ) && i < 200; i++ )
				name [ i ] = fallback.text ( ) [ i ];

			given = name;

		}

		set_user_text ( bookmarks, where, given );
		script_changed ( panel::NOTES );
		call.give_number ( 1 );

		return true;

	}

	if ( call.is ( L"number_form" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
			return true;

		const wchar_t* found = user_text ( number_forms, db->instruction_at ( where ) );

		if ( found && found [ 0 ] )
			call.give_text ( found );

		return true;

	}

	if ( call.is ( L"set_number_form" ) ) {

		uint64_t where = 0;
		const wchar_t* form = L"";

		if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
			return true;

		if ( call.has ( 1 ) && !( form = call.text ( 1 ) ) )
			return true;

		if ( form [ 0 ] && !base::same_text ( form, L"hex" ) && !base::same_text ( form, L"dec" ) && !base::same_text ( form, L"char" ) && !base::same_text ( form, L"bin" ) && !base::same_text ( form, L"float" ) )
			return call.fail ( L"Number forms are \"hex\", \"dec\", \"char\", \"bin\", \"float\" or \"\" to reset." );

		uint64_t start = db->instruction_at ( where );
		x86::instruction ins;
		uint8_t size = 0;

		if ( call.decode ( start, ins ) ) {

			for ( int32_t i = 0; i < ins.count; i++ ) {

				if ( ins.operands [ i ].type == x86::kind::IMM )
					size = ins.operands [ i ].size;

			}

		}

		if ( !size )
			return call.fail ( base::writer ( ).add ( L"The instruction at " ).hex ( start ).add ( L" has no immediate to reformat." ).text ( ) );

		if ( base::same_text ( form, L"float" ) && size != 4 && size != 8 )
			return call.fail ( L"Only 4 and 8 byte immediates can be shown as float." );

		set_user_text ( number_forms, start, form );
		script_touch ( );
		call.give_number ( 1 );

		return true;

	}

	if ( call.is ( L"set_c_number_form" ) || call.is ( L"c_number_form" ) ) {

		uint64_t where = 0;
		int64_t value = 0;
		int64_t occurrence = 0;
		bool setting = call.is ( L"set_c_number_form" );
		const wchar_t* form = L"";

		if ( !call.takes ( setting ? 3 : 2, setting ? 4 : 3 ) || !call.address ( 0, where ) || !call.number ( 1, value ) )
			return true;

		int32_t extra = setting ? 3 : 2;

		if ( call.has ( extra ) && !call.number ( extra, occurrence ) )
			return true;

		uint64_t key = code_number_key ( where, value, static_cast < int32_t > ( occurrence ) );

		if ( !setting ) {

			const wchar_t* found = user_text ( code_numbers, key );
			const wchar_t* shown = !found ? nullptr : ( found [ 0 ] == L'h' ? L"hex" : ( found [ 0 ] == L'd' ? L"dec" : ( found [ 0 ] == L'c' ? L"char" : ( found [ 0 ] == L'b' ? L"bin" : nullptr ) ) ) );

			if ( shown )
				call.give_text ( shown );

			return true;

		}

		if ( call.has ( 2 ) && !( form = call.text ( 2 ) ) )
			return true;

		const wchar_t* stored = !form [ 0 ] ? L"" : ( base::same_text ( form, L"hex" ) ? L"h" : ( base::same_text ( form, L"dec" ) ? L"d" : ( base::same_text ( form, L"char" ) ? L"c" : ( base::same_text ( form, L"bin" ) ? L"b" : nullptr ) ) ) );

		if ( !stored )
			return call.fail ( L"C number forms are \"hex\", \"dec\", \"char\", \"bin\" or \"\" to reset." );

		set_user_text ( code_numbers, key, stored );
		script_touch ( );
		call.give_number ( 1 );

		return true;

	}

	if ( call.is ( L"stack_string" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
			return true;

		const wchar_t* found = stack_string ( where );

		if ( found )
			call.give_text ( found );

		return true;

	}

	return false;

}