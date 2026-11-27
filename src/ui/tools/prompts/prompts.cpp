#include "ui/tools/prompts/prompts.h"
#include "ui/core/app/app.h"
#include "base/clock/clock.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "ui/style/icons/icons.h"
#include "debugger/traps/conditions/conditions.h"
#include "debugger/traps/hardware/hardware.h"

void ui::app::open_code_prompt ( command action, int32_t mode, const wchar_t* key, const wchar_t* fill ) {

	prompt_open = true;
	prompt_action = action;
	prompt_mode = mode;
	prompt_key.set ( key );
	prompt_function = now->function;
	prompt_address = now->function;
	prompt_field = field ( );

	int32_t i = 0;

	for ( ; fill && fill [ i ] && i < 250; i++ )
		prompt_field.text [ i ] = fill [ i ];

	prompt_field.text [ i ] = 0;
	prompt_field.length = i;
	focus_field ( make_id ( "prompt" ), prompt_field, true );

}

bool ui::app::code_variable ( wchar_t* key, wchar_t* shown ) const {

	const decompiler::result* code = const_cast < app* > ( this )->pseudocode_for ( now->function );

	if ( !code || now->code_line < 0 || now->code_line >= code->lines.count ( ) )
		return false;

	const decompiler::line& item = code->lines [ static_cast < size_t > ( now->code_line ) ];
	int32_t column = now->code_col - item.indent * metric::code_indent;

	for ( int32_t s = 0; s < item.span_count; s++ ) {

		const decompiler::span& part = code->spans [ static_cast < size_t > ( item.first_span + s ) ];
		int32_t start = part.start - item.text;

		if ( part.kind != decompiler::syntax::LOCAL && part.kind != decompiler::syntax::PARAMETER )
			continue;

		if ( column < start - 1 || column > start + part.length )
			continue;

		int32_t length = base::min ( part.length, 62 );

		for ( int32_t k = 0; k < length; k++ )
			shown [ k ] = code->text [ static_cast < size_t > ( part.start + k ) ];

		shown [ length ] = 0;

		for ( int32_t k = 0; k <= length; k++ )
			key [ k ] = shown [ k ];

		for ( const decompiler::name_alias& alias : code->aliases ) {

			if ( base::same_text ( alias.shown, shown ) ) {

				for ( int32_t k = 0; k < 64; k++ )
					key [ k ] = alias.key [ k ];

			}

		}

		for ( const local_entry& entry : locals ) {

			if ( entry.function == now->function && !entry.name.empty ( ) && base::same_text ( entry.name.get ( ), shown ) ) {

				int32_t k = 0;

				for ( const wchar_t* at = entry.key.get ( ); *at && k < 62; at++ )
					key [ k++ ] = *at;

				key [ k ] = 0;

			}

		}

		return true;

	}

	return false;

}

uint64_t ui::app::code_number_key ( uint64_t address, int64_t value, int32_t occurrence ) const {

	return address ^ ( static_cast < uint64_t > ( value ) * 0x9E3779B97F4A7C15ull ) ^ ( static_cast < uint64_t > ( occurrence ) * 0xD6E8FEB86659FD93ull );

}

wchar_t ui::app::code_number_form ( uint64_t address, int64_t value, int32_t occurrence ) const {

	if ( !address || code_numbers.empty ( ) )
		return 0;

	const wchar_t* form = user_text ( code_numbers, code_number_key ( address, value, occurrence ) );

	return form ? form [ 0 ] : 0;

}

bool ui::app::code_number ( uint64_t& key, int64_t& value, bool& hex_now ) const {

	for ( int32_t attempt = 0; attempt < 2; attempt++ ) {

		bool hovered = attempt == 0;

		if ( !hovered && showing != view::PSEUDOCODE )
			continue;

		const decompiler::result* code = const_cast < app* > ( this )->pseudocode_for ( hovered ? code_hover_function : now->function );
		int32_t row = hovered ? code_hover_line : now->code_line;
		int32_t col = hovered ? code_hover_col : now->code_col;

		if ( !code )
			continue;

		if ( row < 0 || row >= code->lines.count ( ) )
			continue;

		const decompiler::line& item = code->lines [ static_cast < size_t > ( row ) ];

		if ( number_in_line ( *code, item, col - item.indent * metric::code_indent, key, value, hex_now ) )
			return true;

	}

	return false;

}

bool ui::app::number_in_line ( const decompiler::result& code_ref, const decompiler::line& item, int32_t column, uint64_t& key, int64_t& value, bool& hex_now ) const {

	const decompiler::result* code = &code_ref;

	if ( !item.address )
		return false;

	for ( int32_t s = 0; s < item.span_count; s++ ) {

		const decompiler::span& part = code->spans [ static_cast < size_t > ( item.first_span + s ) ];
		int32_t start = part.start - item.text;

		if ( part.kind != decompiler::syntax::NUMBER || column < start || column > start + part.length )
			continue;

		const wchar_t* text = code->text.data ( ) + part.start;
		int32_t skip = part.length > 0 && text [ 0 ] == L'-' ? 1 : 0;

		value = part.value;
		key = code_number_key ( item.address, value, part.occurrence );
		hex_now = part.length > skip + 1 && text [ skip ] == L'0' && text [ skip + 1 ] == L'x';

		return true;

	}

	return false;

}

const ui::local_entry* ui::app::local_for ( uint64_t function, const wchar_t* key ) const {

	for ( const local_entry& entry : locals ) {

		if ( entry.function == function && base::same_text ( entry.key.get ( ), key ) )
			return &entry;

	}

	return nullptr;

}

const wchar_t* ui::app::prototype_for ( uint64_t function ) const {

	const wchar_t* declared = prototypes.empty ( ) ? nullptr : user_text ( prototypes, function );

	if ( declared && declared [ 0 ] )
		return declared;

	int32_t import = -1;

	for ( int32_t i = 0; i < file.imports.count ( ) && import < 0; i++ ) {

		if ( file.imports [ static_cast < size_t > ( i ) ].slot == function )
			import = i;

	}

	if ( import < 0 && db && analysis_applied ) {

		int32_t owner = db->function_starting ( function );

		if ( owner >= 0 && ( db->functions [ static_cast < size_t > ( owner ) ].flags & analysis::THUNK ) )
			import = db->functions [ static_cast < size_t > ( owner ) ].import;

	}

	if ( import < 0 || import >= file.imports.count ( ) )
		return nullptr;

	const loader::import& item = file.imports [ static_cast < size_t > ( import ) ];

	return item.name.empty ( ) ? nullptr : decompiler::api_prototype ( item.name.get ( ) );

}

void ui::app::set_local ( uint64_t function, const wchar_t* key, const wchar_t* name, const wchar_t* type ) {

	local_entry* found = nullptr;

	for ( local_entry& entry : locals ) {

		if ( entry.function == function && base::same_text ( entry.key.get ( ), key ) )
			found = &entry;

	}

	if ( !found ) {

		found = &locals.emplace ( );
		found->function = function;
		found->key.set ( key );

	}

	if ( name )
		found->name.set ( name );

	if ( type )
		found->type.set ( type );

	if ( found->name.empty ( ) && found->type.empty ( ) ) {

		for ( int32_t i = 0; i < locals.count ( ); i++ ) {

			if ( &locals [ static_cast < size_t > ( i ) ] == found ) {

				locals.remove_at ( static_cast < size_t > ( i ) );

				break;

			}

		}

	}

	names_revision++;

}

void ui::app::open_prompt ( command action, uint64_t address ) {

	prompt_open = true;
	prompt_action = action;
	prompt_address = address;
	prompt_mode = 0;
	prompt_field = field ( );

	const wchar_t* fill = nullptr;
	wchar_t name [ 200 ];

	if ( action == command::RENAME ) {

		if ( symbol_name ( address, true, name, 200 ) )
			fill = name;

	} else if ( action == command::COMMENT )
		fill = user_text ( comments, address );
	else if ( action == command::NOTE )
		fill = user_text ( notes, address );
	else if ( action == command::DEBUG_CONDITION )
		fill = user_text ( conditions, address );
	else if ( action == command::DEBUG_COMMAND_LINE && !debug_command.empty ( ) )
		fill = debug_command.get ( );
	else if ( action == command::DEBUG_HARDWARE ) {

		for ( const debugger::hardware_point& item : hardware_points ) {

			if ( item.used && file.base + item.rva == address )
				fill = item.kind == debugger::watch_kind::EXECUTE ? L"x" : ( item.kind == debugger::watch_kind::WRITE ? L"w" : L"rw" );

		}

		if ( !fill )
			fill = db && db->is_start ( address ) ? L"x" : L"w 4";

	}
	else if ( ( action == command::FIND_SIGNATURE || action == command::SEARCH_ALL ) && !search_query.empty ( ) )
		fill = search_query.get ( );
	else if ( action == command::XOR_DECODE )
		fill = L"auto";
	else if ( action == command::ASSEMBLE && db ) {

		x86::instruction ins;

		if ( db->decode ( address, ins ) ) {

			x86::format_text ( ins, name, 200, nullptr, 0 );
			fill = name;

		}

	} else if ( action == command::PATCH_BYTES ) {

		uint64_t offset = 0;
		x86::instruction ins;
		int32_t count = db && db->is_start ( address ) && db->decode ( address, ins ) ? ins.length : 8;
		base::writer bytes;

		if ( file.offset_of ( address, offset ) ) {

			for ( int32_t b = 0; b < count && offset + static_cast < uint64_t > ( b ) < file.size; b++ ) {

				if ( b )
					bytes.add ( L' ' );

				bytes.hex ( file.byte ( offset + static_cast < uint64_t > ( b ) ), 2 );

			}

			for ( int32_t i = 0; i <= bytes.size ( ) && i < 200; i++ )
				name [ i ] = bytes.text ( ) [ i ];

			fill = name;

		}

	}

	if ( fill ) {

		int32_t i = 0;

		for ( ; fill [ i ] && i < 250; i++ )
			prompt_field.text [ i ] = fill [ i ];

		prompt_field.text [ i ] = 0;
		prompt_field.length = i;

	}

	focus_field ( make_id ( "prompt" ), prompt_field, true );

}

void ui::app::apply_prompt ( ) {

	wchar_t text [ 256 ];
	int32_t length = 0;

	for ( int32_t i = 0; i < prompt_field.length; i++ ) {

		wchar_t c = prompt_field.text [ i ];

		if ( prompt_action == command::RENAME ) {

			bool word = ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L'_' || c == L':' || c == L'@' || c == L'?' || c == L'$' || c == L'~';

			if ( !word )
				c = L'_';

		}

		text [ length++ ] = c;

	}

	text [ length ] = 0;
	prompt_open = false;
	focus_id = 0;
	focused_field = nullptr;

	if ( prompt_action != command::FIND_SIGNATURE && prompt_action != command::SEARCH_ALL )
		remember_undo ( );

	switch ( prompt_action ) {

		case command::RENAME:

			if ( length && text [ 0 ] >= L'0' && text [ 0 ] <= L'9' ) {

				log ( log_kind::WARN, L"Names can't start with a digit." );

				return;

			}

			if ( prompt_mode == 4 ) {

				if ( apply_struct_prompt ( text ) )
					changed ( );

				break;

			}

			if ( prompt_mode == 1 ) {

				set_local ( prompt_function, prompt_key.get ( ), text, nullptr );
				changed ( );
				log ( log_kind::INFO, length ? base::writer ( ).add ( L"Renamed " ).add ( prompt_key.get ( ) ).add ( L" to " ).add ( text ).text ( ) : L"Variable name reset." );

				break;

			}

			set_user_text ( renamed, prompt_address, text );
			refresh_filter ( panel::FUNCTIONS );
			changed ( );
			log ( log_kind::INFO, length ? base::writer ( ).add ( L"Renamed to " ).add ( text ).text ( ) : L"Name reset." );
			break;

		case command::COMMENT:
			set_user_text ( comments, prompt_address, text );
			refresh_filter ( panel::NOTES );
			changed ( );
			break;

		case command::EMULATE:
			emulate_function ( prompt_address, text, nullptr );
			break;

		case command::DEBUG_WATCH:

			if ( length )
				watches.push ( base::text ( text ) );
			else
				watches.clear ( );

			break;

		case command::DEBUG_CONDITION: {

			uint64_t zeros [ 18 ] = { };
			bool valid = false;

			debugger::test_condition ( text, zeros, file.wide ( ), valid );

			if ( length && !valid ) {

				log ( log_kind::WARN, L"Conditions look like  rax == 0,  rcx > 0x10  or  rip != 140001000." );

				break;

			}

			set_user_text ( conditions, prompt_address, text );

			if ( length && !is_breakpoint ( prompt_address ) )
				breakpoints.push ( prompt_address );

			sync_breakpoints ( );
			changed ( );
			break;

		}

		case command::DEBUG_ATTACH:
			attach_debugger ( text );
			break;

		case command::DEBUG_HARDWARE:
			set_hardware_point ( prompt_address, text );
			break;

		case command::DEBUG_COMMAND_LINE:
			debug_command.set ( text );
			log ( log_kind::INFO, length ? base::writer ( ).add ( file.dll ? L"F9 runs " : L"F9 passes " ).add ( text ).text ( ) : ( file.dll ? L"F9 loads the DLL with rundll32." : L"F9 starts the program without arguments." ) );
			break;

		case command::NOTE:
			set_user_text ( notes, prompt_address, text );
			refresh_filter ( panel::NOTES );
			changed ( );
			break;

		case command::FIND_SIGNATURE:
			find_signature ( text );
			break;

		case command::SEARCH_ALL:
			search_everything ( text );
			break;

		case command::ASSEMBLE:

			if ( length )
				assemble_at ( prompt_address, text );

			break;

		case command::PATCH_BYTES:
			patch_text ( prompt_address, text );
			break;

		case command::XOR_DECODE:
			xor_decode ( prompt_address, text );
			break;

		case command::SET_TYPE: {

			if ( prompt_mode >= 5 ) {

				if ( apply_struct_prompt ( text ) )
					changed ( );

				break;

			}

			if ( prompt_mode == 2 ) {

				if ( !length ) {

					set_local ( prompt_function, prompt_key.get ( ), nullptr, L"" );
					log ( log_kind::INFO, L"Type reset to what the decompiler inferred." );

				} else {

					decompiler::c_type shape;
					wchar_t clean [ 64 ];

					if ( !decompiler::parse_type ( text, file.wide ( ), shape, clean, 64 ) || shape.empty ) {

						log ( log_kind::WARN, base::writer ( ).add ( L"Couldn't read the type \"" ).add ( text ).add ( L"\". Try int32_t, uint8_t*, DWORD, double or a struct pointer." ).text ( ) );

						return;

					}

					base::writer problem;

					if ( !known_type ( shape, problem ) ) {

						log ( log_kind::WARN, problem.text ( ) );

						return;

					}

					set_local ( prompt_function, prompt_key.get ( ), nullptr, clean );
					log ( log_kind::INFO, base::writer ( ).add ( prompt_key.get ( ) ).add ( L" is now " ).add ( clean ).add ( L"." ).text ( ) );

				}

				changed ( );

				break;

			}

			if ( !length ) {

				set_user_text ( prototypes, prompt_function, L"" );
				log ( log_kind::INFO, L"Prototype reset to what the decompiler inferred." );
				changed ( );

				break;

			}

			decompiler::c_prototype shape;
			base::writer problem;

			if ( !decompiler::parse_prototype ( text, file.wide ( ), shape, problem ) ) {

				log ( log_kind::WARN, problem.text ( ) );

				return;

			}

			for ( int32_t i = -1; i < shape.count; i++ ) {

				base::writer problem;

				if ( !known_type ( i < 0 ? shape.result_type : shape.params [ i ], problem ) ) {

					log ( log_kind::WARN, problem.text ( ) );

					return;

				}

			}

			set_user_text ( prototypes, prompt_function, text );

			if ( shape.name [ 0 ] ) {

				wchar_t current [ 200 ];

				if ( !symbol_name ( prompt_function, true, current, 200 ) || !base::same_text ( current, shape.name ) )
					set_user_text ( renamed, prompt_function, shape.name );

			}

			refresh_filter ( panel::FUNCTIONS );
			changed ( );
			log ( log_kind::GOOD, base::writer ( ).add ( L"Prototype applied: " ).number ( shape.count ).add ( shape.count == 1 ? L" argument" : L" arguments" ).add ( shape.variadic ? L" plus varargs" : L"" ).add ( L". Callers decompile with the new signature." ).text ( ) );

			break;

		}

		default:
			break;

	}

	pending = base::max ( pending, 3 );

}

void ui::app::make_signature ( uint64_t address ) {

	if ( !db || !analysis_applied )
		return;

	double started = base::seconds ( );

	signature_ok = analysis::make_signature ( *db, address, signature_result );
	signature_seconds = base::seconds ( ) - started;
	signature_open = true;
	signature_copied = -1;

	base::list < wchar_t > text;

	analysis::format_pattern ( signature_result.bytes, analysis::pattern_style::IDA, text );

	if ( signature_ok )
		log ( log_kind::GOOD, base::writer ( ).add ( signature_result.via_reference ? L"Signature through a caller: " : L"Signature: " ).add ( text.data ( ) ).text ( ) );
	else
		log ( log_kind::WARN, base::writer ( ).add ( L"No unique signature here, best has " ).number ( signature_result.matches ).add ( L" matches." ).text ( ) );

}

void ui::app::find_signature ( const wchar_t* text ) {

	if ( !file.loaded ( ) )
		return;

	analysis::pattern find;

	if ( !analysis::parse_pattern ( text, find ) ) {

		log ( log_kind::FAIL, base::writer ( ).add ( L"Can't read \"" ).add ( text ).add ( L"\" as a pattern. Try 48 8B 05 ? ? ? ? or \"text\"." ).text ( ) );

		return;

	}

	double started = base::seconds ( );
	base::list < uint64_t > hits;
	const int32_t limit = 20000;

	analysis::scan ( file, find, false, hits, limit );

	bool plain = text [ 0 ] == L'"';

	for ( const wchar_t* at = text; *at && plain; at++ ) {

		if ( at [ 0 ] == L'\\' && at [ 1 ] == L'x' )
			plain = false;

	}

	if ( plain ) {

		analysis::pattern wide;
		base::list < uint64_t > more;

		for ( int32_t i = 0; i < find.bytes.count ( ); i++ ) {

			wide.bytes.push ( find.bytes [ static_cast < size_t > ( i ) ] );
			wide.mask.push ( 1 );
			wide.bytes.push ( 0 );
			wide.mask.push ( 1 );

		}

		analysis::scan ( file, wide, false, more, limit );

		for ( uint64_t value : more )
			hits.push ( value );

	}

	search_hits.clear ( );

	for ( uint64_t value : hits ) {

		search_hit& item = search_hits.emplace ( );

		item.address = value;
		item.length = static_cast < uint32_t > ( find.bytes.count ( ) );

	}

	search_seconds = base::seconds ( ) - started;
	search_truncated = hits.count ( ) >= limit;
	search_query.set ( text );
	browsing = panel::SEARCH;
	browser_open = true;
	browser_scroll [ static_cast < int32_t > ( panel::SEARCH ) ] = scroller ( );
	refresh_filter ( panel::SEARCH );

	base::writer line;

	line.grouped ( static_cast < uint64_t > ( search_hits.count ( ) ) ).add ( search_hits.count ( ) == 1 ? L" match" : L" matches" ).add ( search_truncated ? L" (stopped at the limit)" : L"" ).add ( L" in " ).fixed ( search_seconds * 1000.0, 1 ).add ( L" ms" );
	log ( search_hits.empty ( ) ? log_kind::WARN : log_kind::GOOD, line.text ( ) );

	if ( search_hits.count ( ) == 1 )
		go ( search_hits [ 0 ].address, true );

}