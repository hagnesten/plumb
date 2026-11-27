#include "ui/scripting/core/core.h"
#include "ui/scripting/bridge/bridge.h"
#include "ui/style/metrics/metrics.h"

namespace ui {

	static bool clean_name ( const wchar_t* given, wchar_t* clean, int32_t room ) {

		int32_t length = 0;

		for ( ; given [ length ] && length < room - 1; length++ ) {

			wchar_t c = given [ length ];
			bool word = ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L'_' || c == L':' || c == L'@' || c == L'?' || c == L'$' || c == L'~';

			clean [ length ] = word ? c : L'_';

		}

		clean [ length ] = 0;

		return !( clean [ 0 ] >= L'0' && clean [ 0 ] <= L'9' );

	}

	bool app::script_core ( script_context& call ) {

		wchar_t label [ 400 ];
		bool analyzed = db && analysis_applied && !db->functions.empty ( );

		if ( call.is ( L"here" ) ) {

			if ( call.takes ( 0, 0 ) )
				call.give_number ( static_cast < int64_t > ( now->cursor_address ) );

			return true;

		}

		if ( call.is ( L"jump" ) ) {

			uint64_t where = 0;

			if ( call.takes ( 1, 1 ) && call.address ( 0, where ) ) {

				go ( where, true );
				call.give_number ( 1 );

			}

			return true;

		}

		if ( call.is ( L"image_base" ) || call.is ( L"entry" ) || call.is ( L"image_end" ) ) {

			if ( call.takes ( 0, 0 ) )
				call.give_number ( static_cast < int64_t > ( call.is ( L"image_base" ) ? file.base : ( call.is ( L"entry" ) ? file.entry : file.high ( ) ) ) );

			return true;

		}

		if ( call.is ( L"byte" ) || call.is ( L"word" ) || call.is ( L"dword" ) || call.is ( L"qword" ) ) {

			uint64_t where = 0;
			uint64_t read = 0;
			int32_t size = call.is ( L"byte" ) ? 1 : ( call.is ( L"word" ) ? 2 : ( call.is ( L"dword" ) ? 4 : 8 ) );

			if ( call.takes ( 1, 1 ) && call.address ( 0, where ) && call.read ( where, size, read ) )
				call.give_number ( static_cast < int64_t > ( read ) );

			return true;

		}

		if ( call.is ( L"bytes" ) ) {

			uint64_t where = 0;
			int64_t wanted = 0;

			if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) || !call.number ( 1, wanted ) )
				return true;

			if ( wanted < 0 || wanted > 16 * 1024 * 1024 )
				return call.fail ( L"The byte count must be a number up to 16 MB." );

			script::value made = call.list ( );

			for ( int64_t i = 0; i < wanted; i++ ) {

				uint64_t read = 0;

				if ( !call.read ( where + static_cast < uint64_t > ( i ), 1, read ) )
					break;

				call.push_number ( made, static_cast < int64_t > ( read ) );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"patch_byte" ) || call.is ( L"patch_bytes" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) )
				return true;

			base::writer hex_text;
			const script::value& given = call.args [ 1 ];

			if ( given.type == script::kind::NUMBER )
				hex_text.hex ( static_cast < uint64_t > ( given.number ) & 0xFF, 2 );
			else if ( given.type == script::kind::TEXT )
				hex_text.add ( call.engine->text_of ( given ) );
			else if ( given.type == script::kind::LIST ) {

				for ( int32_t i = 0; i < call.engine->list_count ( given ) && i < 600; i++ ) {

					script::value item = call.engine->list_item ( given, i );

					if ( item.type != script::kind::NUMBER )
						return call.fail ( L"Patch bytes must be numbers." );

					hex_text.hex ( static_cast < uint64_t > ( item.number ) & 0xFF, 2 ).add ( L" " );

				}

			} else
				return call.fail ( L"Patch with a number, hex text like \"90 90\" or a list of bytes." );

			script_touch ( );
			call.give_number ( patch_text ( where, hex_text.text ( ) ) ? 1 : 0 );

			return true;

		}

		if ( call.is ( L"assemble" ) ) {

			uint64_t where = 0;
			const wchar_t* code = nullptr;

			if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) || !( code = call.text ( 1 ) ) )
				return true;

			script_touch ( );
			call.give_number ( assemble_at ( where, code ) ? 1 : 0 );

			return true;

		}

		if ( call.is ( L"name" ) ) {

			uint64_t where = 0;

			if ( call.takes ( 1, 1 ) && call.address ( 0, where ) && symbol_name ( where, true, label, 400 ) )
				call.give_text ( label );

			return true;

		}

		if ( call.is ( L"locate" ) ) {

			uint64_t where = 0;
			const wchar_t* wanted = nullptr;

			if ( call.takes ( 1, 1 ) && ( wanted = call.text ( 0 ) ) && parse_address ( wanted, where ) )
				call.give_number ( static_cast < int64_t > ( where ) );

			return true;

		}

		if ( call.is ( L"comment" ) || call.is ( L"note" ) || call.is ( L"prototype" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 1, 1 ) )
				return true;

			if ( call.is ( L"comment" ) ? !call.address ( 0, where ) : !call.function_start ( 0, where ) )
				return true;

			const wchar_t* found = user_text ( call.is ( L"comment" ) ? comments : ( call.is ( L"note" ) ? notes : prototypes ), where );

			if ( found )
				call.give_text ( found );

			return true;

		}

		if ( call.is ( L"set_name" ) || call.is ( L"set_comment" ) || call.is ( L"set_note" ) || call.is ( L"set_prototype" ) ) {

			uint64_t where = 0;
			const wchar_t* given = nullptr;

			if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) )
				return true;

			bool rename = call.is ( L"set_name" );
			bool shape = call.is ( L"set_prototype" );
			bool note_text = call.is ( L"set_note" );
			wchar_t clean [ 256 ];

			if ( call.args [ 1 ].type == script::kind::NONE )
				given = L"";
			else if ( !( given = call.text ( 1 ) ) )
				return true;

			int32_t length = 0;

			for ( ; given [ length ] && length < 250; length++ )
				clean [ length ] = given [ length ];

			clean [ length ] = 0;

			if ( ( shape || note_text ) && !call.function_start ( 0, where ) )
				return true;

			if ( rename ) {

				if ( !clean_name ( given, clean, 250 ) )
					return call.fail ( L"Names can't start with a digit." );

				if ( analyzed && db->function_at ( where ) >= 0 && !db->is_label ( where ) && db->function_starting ( where ) < 0 )
					where = db->functions [ static_cast < size_t > ( db->function_at ( where ) ) ].start;

			}

			if ( shape && clean [ 0 ] ) {

				decompiler::c_prototype parsed;
				base::writer complaint;

				if ( !decompiler::parse_prototype ( clean, file.wide ( ), parsed, complaint ) )
					return call.fail ( base::writer ( ).add ( L"That prototype doesn't parse: " ).add ( complaint.text ( ) ).text ( ) );

			}

			set_user_text ( rename ? renamed : ( shape ? prototypes : ( note_text ? notes : comments ) ), where, clean );
			script_changed ( rename ? panel::FUNCTIONS : panel::NOTES );
			call.give_number ( 1 );

			return true;

		}

		if ( call.is ( L"functions" ) ) {

			if ( !call.takes ( 0, 0 ) || !call.need_analysis ( ) )
				return true;

			script::value made = call.list ( );

			for ( const analysis::function_info& item : db->functions )
				call.push_number ( made, static_cast < int64_t > ( item.start ) );

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"function_at" ) || call.is ( L"function_end" ) || call.is ( L"function_name" ) || call.is ( L"function_size" ) || call.is ( L"is_function" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
				return true;

			if ( call.is ( L"is_function" ) ) {

				call.give_number ( db->function_starting ( where ) >= 0 );

				return true;

			}

			int32_t owner_index = db->function_at ( where );

			if ( owner_index < 0 )
				return true;

			const analysis::function_info& item = db->functions [ static_cast < size_t > ( owner_index ) ];

			if ( call.is ( L"function_name" ) ) {

				function_name ( owner_index, label, 400 );
				call.give_text ( label );

			} else
				call.give_number ( static_cast < int64_t > ( call.is ( L"function_at" ) ? item.start : ( call.is ( L"function_end" ) ? item.end : item.end - item.start ) ) );

			return true;

		}

		if ( call.is ( L"is_code" ) ) {

			uint64_t where = 0;

			if ( call.takes ( 1, 1 ) && call.address ( 0, where ) && call.need_analysis ( ) )
				call.give_number ( db->is_start ( where ) );

			return true;

		}

		if ( call.is ( L"instruction" ) || call.is ( L"mnemonic" ) || call.is ( L"size" ) || call.is ( L"next" ) || call.is ( L"operands" ) ) {

			uint64_t where = 0;
			x86::instruction ins;

			if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) || !call.decode ( where, ins ) )
				return true;

			if ( call.is ( L"size" ) )
				call.give_number ( ins.length );
			else if ( call.is ( L"next" ) )
				call.give_number ( static_cast < int64_t > ( ins.next ( ) ) );
			else if ( call.is ( L"operands" ) )
				call.give_number ( ins.count );
			else if ( call.is ( L"mnemonic" ) ) {

				x86::mnemonic_text ( ins, label, 400 );
				call.give_text ( label );

			} else {

				script_symbols symbols;

				symbols.owner = this;
				x86::format_text ( ins, label, 400, &symbols, 0 );
				call.give_text ( label );

			}

			return true;

		}

		if ( call.is ( L"operand_kind" ) || call.is ( L"operand_value" ) || call.is ( L"operand_register" ) || call.is ( L"operand_size" ) ) {

			uint64_t where = 0;
			int64_t which = 0;
			x86::instruction ins;

			if ( !call.takes ( 2, 2 ) || !call.address ( 0, where ) || !call.number ( 1, which ) || !call.need_analysis ( ) || !call.decode ( where, ins ) )
				return true;

			if ( which < 0 || which >= ins.count )
				return true;

			const x86::operand& item = ins.operands [ which ];

			if ( call.is ( L"operand_size" ) )
				call.give_number ( item.size );
			else if ( call.is ( L"operand_kind" ) ) {

				const wchar_t* kind_text = item.type == x86::kind::REG ? L"register" : ( item.type == x86::kind::IMM ? L"immediate" : ( item.type == x86::kind::MEM ? L"memory" : L"other" ) );

				if ( item.type == x86::kind::IMM && ins.direct ( ) )
					kind_text = L"address";

				call.give_text ( kind_text );

			} else if ( call.is ( L"operand_register" ) ) {

				uint8_t reg = item.type == x86::kind::REG ? item.reg : ( item.type == x86::kind::MEM ? item.base : 0 );

				if ( reg && reg != x86::NONE ) {

					const char* raw = x86::register_name ( reg );
					int32_t k = 0;

					for ( ; raw [ k ] && k < 399; k++ )
						label [ k ] = static_cast < wchar_t > ( raw [ k ] );

					label [ k ] = 0;
					call.give_text ( label );

				}

			} else {

				uint64_t target = 0;

				if ( item.type == x86::kind::MEM && item.rip_relative ( ) )
					call.give_number ( static_cast < int64_t > ( ins.next ( ) + static_cast < uint64_t > ( item.value ) ) );
				else if ( ins.direct ( ) && item.type == x86::kind::IMM )
					call.give_number ( static_cast < int64_t > ( ins.target ( ) ) );
				else if ( item.type == x86::kind::IMM || item.type == x86::kind::MEM )
					call.give_number ( item.value );
				else if ( ins.memory_target ( target ) )
					call.give_number ( static_cast < int64_t > ( target ) );

			}

			return true;

		}

		if ( call.is ( L"refs_to" ) || call.is ( L"refs_from" ) || call.is ( L"callers" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
				return true;

			script::value made = call.list ( );
			base::list < uint64_t > seen;

			auto add_unique = [ & ] ( uint64_t item ) {

				for ( uint64_t known : seen ) {

					if ( known == item )
						return;

				}

				seen.push ( item );
				call.push_number ( made, static_cast < int64_t > ( item ) );

			};

			if ( call.is ( L"refs_from" ) ) {

				uint32_t source = static_cast < uint32_t > ( where - file.base );

				for ( const analysis::xref& item : db->refs ) {

					if ( item.source == source )
						add_unique ( file.base + item.target );

				}

			} else {

				int32_t first = 0;
				int32_t total = 0;

				db->refs_to ( where, first, total );

				for ( int32_t r = first; r < first + total; r++ ) {

					const analysis::xref& item = db->refs [ static_cast < size_t > ( r ) ];
					uint64_t source = file.base + item.source;

					if ( call.is ( L"callers" ) ) {

						int32_t owner_index = db->function_at ( source );

						if ( owner_index >= 0 && ( item.type == analysis::ref::CALL || item.type == analysis::ref::JUMP ) )
							add_unique ( db->functions [ static_cast < size_t > ( owner_index ) ].start );

					} else
						add_unique ( source );

				}

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"decompile" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
				return true;

			const decompiler::result* code = pseudocode_for ( where );

			if ( !code || !code->ok )
				return true;

			base::list < wchar_t > out;

			for ( const decompiler::line& item : code->lines ) {

				for ( int32_t i = 0; i < item.indent * metric::code_indent; i++ )
					out.push ( L' ' );

				for ( int32_t i = 0; i < item.length; i++ )
					out.push ( code->text [ static_cast < size_t > ( item.text + i ) ] );

				out.push ( L'\n' );

			}

			call.give_text ( out.data ( ), out.count ( ) );

			return true;

		}

		if ( call.is ( L"strings" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			for ( const loader::found_string& item : file.strings ) {

				uint64_t where = 0;

				if ( file.address_of ( item.offset, where ) )
					call.push_number ( made, static_cast < int64_t > ( where ) );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"string" ) ) {

			uint64_t where = 0;
			uint64_t offset = 0;

			if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !file.offset_of ( where, offset ) )
				return true;

			bool wide = offset + 3 < file.size && file.byte ( offset + 1 ) == 0 && file.byte ( offset ) >= 0x20 && file.byte ( offset + 3 ) == 0;
			uint64_t step = wide ? 2 : 1;
			base::list < wchar_t > out;

			for ( uint64_t i = 0; i < 65536; i++ ) {

				uint64_t at = offset + i * step;

				if ( at + step > file.size )
					break;

				wchar_t c = wide ? static_cast < wchar_t > ( file.read16 ( at ) ) : static_cast < wchar_t > ( file.byte ( at ) );

				if ( !c )
					break;

				out.push ( c );

			}

			call.give_text ( out.data ( ), out.count ( ) );

			return true;

		}

		if ( call.is ( L"sections" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			for ( const loader::section& item : file.sections ) {

				script::value row = call.list ( );
				wchar_t rights [ 4 ] = { item.read ? L'r' : L'-', item.write ? L'w' : L'-', item.execute ? L'x' : L'-', 0 };

				call.push_text ( row, item.name );
				call.push_number ( row, static_cast < int64_t > ( item.address ) );
				call.push_number ( row, static_cast < int64_t > ( item.address + base::max ( item.size, item.raw ) ) );
				call.push_text ( row, rights );
				call.push_list ( made, row );

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"section_at" ) ) {

			uint64_t where = 0;

			if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) )
				return true;

			int32_t index = file.section_at ( where );

			if ( index >= 0 )
				call.give_text ( file.sections [ static_cast < size_t > ( index ) ].name );

			return true;

		}

		if ( call.is ( L"imports" ) || call.is ( L"exports" ) ) {

			if ( !call.takes ( 0, 0 ) )
				return true;

			script::value made = call.list ( );

			if ( call.is ( L"imports" ) ) {

				for ( const loader::import& item : file.imports ) {

					script::value row = call.list ( );

					call.push_text ( row, file.modules [ static_cast < size_t > ( item.owner ) ].name.get ( ) );

					if ( item.by_ordinal || item.name.empty ( ) )
						call.push_text ( row, base::writer ( ).add ( L"#" ).number ( item.ordinal ).text ( ) );
					else
						call.push_text ( row, item.name.get ( ) );

					call.push_number ( row, static_cast < int64_t > ( item.slot ) );
					call.push_list ( made, row );

				}

			} else {

				for ( const loader::exported& item : file.exports ) {

					script::value row = call.list ( );

					call.push_text ( row, item.name.empty ( ) ? L"" : item.name.get ( ) );
					call.push_number ( row, static_cast < int64_t > ( item.address ) );
					call.push_list ( made, row );

				}

			}

			call.give_list ( made );

			return true;

		}

		if ( call.is ( L"find_bytes" ) || call.is ( L"find_all_bytes" ) ) {

			const wchar_t* pattern = nullptr;
			uint64_t from = file.base;
			bool every = call.is ( L"find_all_bytes" );
			int64_t limit = every ? 100000 : 1;

			if ( !call.takes ( 1, every ? 3 : 2 ) || !( pattern = call.text ( 0 ) ) || ( call.has ( 1 ) && !call.address ( 1, from ) ) || ( call.has ( 2 ) && !call.number ( 2, limit ) ) )
				return true;

			int16_t wanted [ 256 ];
			int32_t length = 0;

			for ( const wchar_t* c = pattern; *c && length < 256; ) {

				if ( *c == L' ' ) {

					c++;

					continue;

				}

				if ( *c == L'?' ) {

					wanted [ length++ ] = -1;
					c += c [ 1 ] == L'?' ? 2 : 1;

					continue;

				}

				auto nibble = [ ] ( wchar_t d ) {

					return d >= L'0' && d <= L'9' ? d - L'0' : ( d >= L'a' && d <= L'f' ? d - L'a' + 10 : ( d >= L'A' && d <= L'F' ? d - L'A' + 10 : -1 ) );

				};

				int32_t high = nibble ( c [ 0 ] );
				int32_t low = c [ 1 ] ? nibble ( c [ 1 ] ) : -1;

				if ( high < 0 || low < 0 )
					return call.fail ( L"Write the pattern as hex bytes with ?? for wildcards, like \"48 8B ?? 05\"." );

				wanted [ length++ ] = static_cast < int16_t > ( high * 16 + low );
				c += 2;

			}

			if ( !length )
				return call.fail ( L"The pattern is empty." );

			script::value made = call.list ( );
			int64_t found = 0;

			for ( const loader::section& part : file.sections ) {

				uint64_t start = base::max ( part.address, from );
				uint64_t end = part.address + part.raw;
				uint64_t offset = 0;

				if ( start >= end || !file.offset_of ( start, offset ) )
					continue;

				for ( uint64_t at = start; at + static_cast < uint64_t > ( length ) <= end && found < limit; at++ ) {

					uint64_t base_offset = offset + ( at - start );
					bool hit = true;

					for ( int32_t k = 0; k < length && hit; k++ )
						hit = wanted [ k ] < 0 || file.byte ( base_offset + static_cast < uint64_t > ( k ) ) == static_cast < uint8_t > ( wanted [ k ] );

					if ( !hit )
						continue;

					if ( !every ) {

						call.give_number ( static_cast < int64_t > ( at ) );

						return true;

					}

					call.push_number ( made, static_cast < int64_t > ( at ) );
					found++;

				}

			}

			if ( every )
				call.give_list ( made );

			return true;

		}

		return false;

	}

}