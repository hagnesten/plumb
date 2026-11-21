#include "ui/scripting.hh"
#include "ui/metrics.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	bool load_text_file ( const wchar_t* path, base::list < wchar_t >& out ) {

		base::list < char > raw;

		out.clear ( );

		if ( !files::read ( path, raw ) )
			return false;

		int32_t skip = raw.count ( ) >= 3 && static_cast < uint8_t > ( raw [ 0 ] ) == 0xEF && static_cast < uint8_t > ( raw [ 1 ] ) == 0xBB && static_cast < uint8_t > ( raw [ 2 ] ) == 0xBF ? 3 : 0;
		int32_t length = raw.count ( ) - skip;

		if ( length <= 0 )
			return true;

		int32_t made = MultiByteToWideChar ( CP_UTF8, 0, raw.data ( ) + skip, length, nullptr, 0 );

		if ( made <= 0 )
			return false;

		out.resize ( static_cast < size_t > ( made ) );
		MultiByteToWideChar ( CP_UTF8, 0, raw.data ( ) + skip, length, out.data ( ), made );

		return true;

	}

	bool script_symbols::name ( uint64_t address, bool code, wchar_t* out, int32_t room ) {

		return owner->symbol_name ( address, code, out, room );

	}

	bool script_bridge::call ( script::engine& engine_ref, const wchar_t* name, const script::value* args, int32_t count, script::value& result ) {

		return owner->script_call ( engine_ref, name, args, count, result );

	}

	void script_bridge::print ( const wchar_t* text ) {

		owner->script_print ( text );

	}

	bool script_bridge::read_file ( const wchar_t* path, const wchar_t* from, base::list < wchar_t >& source, base::writer& resolved ) {

		bool absolute = ( path [ 0 ] && path [ 1 ] == L':' ) || path [ 0 ] == L'\\' || path [ 0 ] == L'/';

		if ( !absolute && from && from [ 0 ] ) {

			int32_t cut = -1;

			for ( int32_t i = 0; from [ i ]; i++ ) {

				if ( from [ i ] == L'\\' || from [ i ] == L'/' )
					cut = i;

			}

			for ( int32_t i = 0; i <= cut; i++ )
				resolved.add ( from [ i ] );

		}

		resolved.add ( path );

		return load_text_file ( resolved.text ( ), source );

	}

	void app::script_print ( const wchar_t* text ) {

		if ( script_capture ) {

			for ( const wchar_t* c = text; *c; c++ )
				script_capture->push ( *c );

			script_capture->push ( L'\n' );

		}

		if ( script_lines < 5000 )
			log ( log_kind::INFO, text );
		else if ( script_lines == 5000 )
			log ( log_kind::WARN, L"The script printed more than 5,000 lines. The rest only go to the capture." );

		script_lines++;

	}

	void app::script_touch ( ) {

		if ( script_touched )
			return;

		remember_undo ( );
		script_touched = true;

	}

	bool app::run_script_file ( const wchar_t* path, base::list < wchar_t >* capture ) {

		base::list < wchar_t > source;

		if ( !load_text_file ( path, source ) ) {

			log ( log_kind::FAIL, base::writer ( ).add ( L"Couldn't read the script " ).add ( path ).add ( L"." ).text ( ) );

			return false;

		}

		source.push ( 0 );

		return run_script_text ( source.data ( ), path, capture );

	}

	bool app::run_script_text ( const wchar_t* source, const wchar_t* label, base::list < wchar_t >* capture ) {

		script::engine* engine_ref = new script::engine ( );
		script_bridge bridge;
		double started = base::seconds ( );
		const wchar_t* shown = label;

		for ( const wchar_t* c = label; *c; c++ ) {

			if ( *c == L'\\' || *c == L'/' )
				shown = c + 1;

		}

		bridge.owner = this;
		script_capture = capture;
		script_touched = false;
		script_lines = 0;

		bool ok = engine_ref->load ( source, label, bridge );

		if ( ok )
			ok = engine_ref->run ( bridge );

		if ( !ok ) {

			const script::problem& error = engine_ref->error;
			const wchar_t* where = error.file [ 0 ] ? error.file : label;
			const wchar_t* short_where = where;

			for ( const wchar_t* c = where; *c; c++ ) {

				if ( *c == L'\\' || *c == L'/' )
					short_where = c + 1;

			}

			base::writer line;

			line.add ( short_where );

			if ( error.line )
				line.add ( L"(" ).number ( error.line ).add ( L"," ).number ( error.column ).add ( L")" );

			line.add ( L": " ).add ( error.message );
			log ( log_kind::FAIL, line.text ( ) );

			if ( capture ) {

				for ( const wchar_t* c = line.text ( ); *c; c++ )
					capture->push ( *c );

				capture->push ( L'\n' );

			}

		} else
			log ( log_kind::GOOD, base::writer ( ).add ( L"Ran " ).add ( shown ).add ( L" in " ).fixed ( ( base::seconds ( ) - started ) * 1000.0, 1 ).add ( L" ms." ).text ( ) );

		if ( script_touched ) {

			names_revision++;
			annotation_revision++;
			code_cache.clear ( );
			changed ( );

		}

		script_capture = nullptr;
		delete engine_ref;

		return ok;

	}

	bool app::script_call ( script::engine& owner, const wchar_t* name, const script::value* args, int32_t count, script::value& result ) {

		using script::kind;
		using script::value;

		bool analyzed = db && analysis_applied && !db->functions.empty ( );
		wchar_t label [ 400 ];

		auto same = [ & ] ( const wchar_t* word ) {

			return base::same_text ( name, word );

		};

		auto takes = [ & ] ( int32_t low, int32_t high ) {

			if ( count >= low && count <= high )
				return true;

			base::writer message;

			message.add ( L"'" ).add ( name ).add ( L"' takes " );

			if ( low == high )
				message.number ( low );
			else
				message.number ( low ).add ( L" to " ).number ( high );

			message.add ( low == 1 && high == 1 ? L" argument." : L" arguments." );

			return owner.fail ( message.text ( ) );

		};

		auto number = [ & ] ( int64_t n ) {

			result.type = kind::NUMBER;
			result.number = n;

		};

		auto address = [ & ] ( int32_t index, uint64_t& out ) {

			if ( index >= count )
				return owner.fail ( L"An address is missing." );

			if ( args [ index ].type == kind::NUMBER ) {

				out = static_cast < uint64_t > ( args [ index ].number );

				return true;

			}

			if ( args [ index ].type == kind::TEXT ) {

				if ( parse_address ( owner.text_of ( args [ index ] ), out ) )
					return true;

				base::writer message;

				message.add ( L"Nothing called '" ).add ( owner.text_of ( args [ index ] ) ).add ( L"' in this image." );

				return owner.fail ( message.text ( ) );

			}

			return owner.fail ( L"An address must be a number or a name." );

		};

		auto text_arg = [ & ] ( int32_t index ) -> const wchar_t* {

			if ( index >= count || args [ index ].type != kind::TEXT ) {

				base::writer message;

				message.add ( L"Argument " ).number ( index + 1 ).add ( L" of '" ).add ( name ).add ( L"' must be text." );
				owner.fail ( message.text ( ) );

				return nullptr;

			}

			return owner.text_of ( args [ index ] );

		};

		auto need_analysis = [ & ] ( ) {

			if ( analyzed )
				return true;

			return owner.fail ( L"There's no analysis yet. Open a file and wait for it to finish." );

		};

		auto read_value = [ & ] ( uint64_t where, int32_t size, uint64_t& out ) {

			uint64_t offset = 0;

			if ( !file.offset_of ( where, offset ) || offset + static_cast < uint64_t > ( size ) > file.size )
				return false;

			out = 0;

			for ( int32_t i = size - 1; i >= 0; i-- )
				out = ( out << 8 ) | file.byte ( offset + static_cast < uint64_t > ( i ) );

			return true;

		};

		auto decode_at = [ & ] ( uint64_t where, x86::instruction& out ) {

			return db && db->decode ( where, out ) && out.valid ( );

		};

		if ( !file.loaded ( ) )
			return owner.fail ( L"No file is open." ) || true;

		if ( same ( L"here" ) ) {

			if ( takes ( 0, 0 ) )
				number ( static_cast < int64_t > ( showing == view::PSEUDOCODE ? now->cursor_address : now->cursor_address ) );

			return true;

		}

		if ( same ( L"jump" ) ) {

			uint64_t where = 0;

			if ( takes ( 1, 1 ) && address ( 0, where ) )
				go ( where, true );

			return true;

		}

		if ( same ( L"image_base" ) || same ( L"entry" ) || same ( L"image_end" ) ) {

			if ( takes ( 0, 0 ) )
				number ( static_cast < int64_t > ( same ( L"image_base" ) ? file.base : ( same ( L"entry" ) ? file.entry : file.high ( ) ) ) );

			return true;

		}

		if ( same ( L"byte" ) || same ( L"word" ) || same ( L"dword" ) || same ( L"qword" ) ) {

			uint64_t where = 0;
			uint64_t read = 0;
			int32_t size = same ( L"byte" ) ? 1 : ( same ( L"word" ) ? 2 : ( same ( L"dword" ) ? 4 : 8 ) );

			if ( takes ( 1, 1 ) && address ( 0, where ) && read_value ( where, size, read ) )
				number ( static_cast < int64_t > ( read ) );

			return true;

		}

		if ( same ( L"bytes" ) ) {

			uint64_t where = 0;

			if ( !takes ( 2, 2 ) || !address ( 0, where ) )
				return true;

			if ( args [ 1 ].type != kind::NUMBER || args [ 1 ].number < 0 || args [ 1 ].number > 16 * 1024 * 1024 )
				return owner.fail ( L"The byte count must be a number up to 16 MB." ) || true;

			value made = owner.list ( );

			for ( int64_t i = 0; i < args [ 1 ].number; i++ ) {

				uint64_t read = 0;

				if ( !read_value ( where + static_cast < uint64_t > ( i ), 1, read ) )
					break;

				value item;

				item.type = kind::NUMBER;
				item.number = static_cast < int64_t > ( read );
				owner.push ( made, item );

			}

			result = made;

			return true;

		}

		if ( same ( L"patch_byte" ) || same ( L"patch_bytes" ) ) {

			uint64_t where = 0;

			if ( !takes ( 2, 2 ) || !address ( 0, where ) )
				return true;

			base::writer hex_text;

			if ( args [ 1 ].type == kind::NUMBER )
				hex_text.hex ( static_cast < uint64_t > ( args [ 1 ].number ) & 0xFF, 2 );
			else if ( args [ 1 ].type == kind::TEXT )
				hex_text.add ( owner.text_of ( args [ 1 ] ) );
			else if ( args [ 1 ].type == kind::LIST ) {

				for ( int32_t i = 0; i < owner.list_count ( args [ 1 ] ) && i < 256; i++ ) {

					value item = owner.list_item ( args [ 1 ], i );

					if ( item.type != kind::NUMBER )
						return owner.fail ( L"Patch bytes must be numbers." ) || true;

					hex_text.hex ( static_cast < uint64_t > ( item.number ) & 0xFF, 2 ).add ( L" " );

				}

			} else
				return owner.fail ( L"Patch with a number, hex text like \"90 90\" or a list of bytes." ) || true;

			script_touch ( );
			number ( patch_text ( where, hex_text.text ( ) ) ? 1 : 0 );

			return true;

		}

		if ( same ( L"assemble" ) ) {

			uint64_t where = 0;
			const wchar_t* code = nullptr;

			if ( !takes ( 2, 2 ) || !address ( 0, where ) || !( code = text_arg ( 1 ) ) )
				return true;

			script_touch ( );
			number ( assemble_at ( where, code ) ? 1 : 0 );

			return true;

		}

		if ( same ( L"name" ) ) {

			uint64_t where = 0;

			if ( takes ( 1, 1 ) && address ( 0, where ) && symbol_name ( where, true, label, 400 ) )
				result = owner.text ( label );

			return true;

		}

		if ( same ( L"locate" ) ) {

			uint64_t where = 0;
			const wchar_t* wanted = nullptr;

			if ( takes ( 1, 1 ) && ( wanted = text_arg ( 0 ) ) && parse_address ( wanted, where ) )
				number ( static_cast < int64_t > ( where ) );

			return true;

		}

		if ( same ( L"comment" ) || same ( L"note" ) || same ( L"prototype" ) ) {

			uint64_t where = 0;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) )
				return true;

			if ( !same ( L"comment" ) ) {

				if ( !need_analysis ( ) )
					return true;

				int32_t owner_index = db->function_at ( where );

				if ( owner_index < 0 )
					return true;

				where = db->functions [ static_cast < size_t > ( owner_index ) ].start;

			}

			const wchar_t* found = user_text ( same ( L"comment" ) ? comments : ( same ( L"note" ) ? notes : prototypes ), where );

			if ( found )
				result = owner.text ( found );

			return true;

		}

		if ( same ( L"set_name" ) || same ( L"set_comment" ) || same ( L"set_note" ) || same ( L"set_prototype" ) ) {

			uint64_t where = 0;
			const wchar_t* given = nullptr;

			if ( !takes ( 2, 2 ) || !address ( 0, where ) || !( given = text_arg ( 1 ) ) )
				return true;

			wchar_t clean [ 256 ];
			int32_t length = 0;

			for ( ; given [ length ] && length < 250; length++ )
				clean [ length ] = given [ length ];

			clean [ length ] = 0;

			bool rename = same ( L"set_name" );
			bool shape = same ( L"set_prototype" );
			bool note_text = same ( L"set_note" );

			if ( shape || note_text ) {

				if ( !need_analysis ( ) )
					return true;

				int32_t owner_index = db->function_at ( where );

				if ( owner_index < 0 )
					return owner.fail ( L"That address isn't inside a function." ) || true;

				where = db->functions [ static_cast < size_t > ( owner_index ) ].start;

			}

			if ( rename ) {

				for ( wchar_t* c = clean; *c; c++ ) {

					bool word = ( *c >= L'a' && *c <= L'z' ) || ( *c >= L'A' && *c <= L'Z' ) || ( *c >= L'0' && *c <= L'9' ) || *c == L'_' || *c == L':' || *c == L'@' || *c == L'?' || *c == L'$' || *c == L'~';

					if ( !word )
						*c = L'_';

				}

				if ( clean [ 0 ] >= L'0' && clean [ 0 ] <= L'9' )
					return owner.fail ( L"Names can't start with a digit." ) || true;

				if ( analyzed && db->function_at ( where ) >= 0 && !db->is_label ( where ) && db->function_starting ( where ) < 0 )
					where = db->functions [ static_cast < size_t > ( db->function_at ( where ) ) ].start;

			}

			if ( shape && clean [ 0 ] ) {

				decompiler::c_prototype parsed;
				base::writer complaint;

				if ( !decompiler::parse_prototype ( clean, file.wide ( ), parsed, complaint ) ) {

					base::writer message;

					message.add ( L"That prototype doesn't parse: " ).add ( complaint.text ( ) );

					return owner.fail ( message.text ( ) ) || true;

				}

			}

			script_touch ( );
			set_user_text ( rename ? renamed : ( shape ? prototypes : ( note_text ? notes : comments ) ), where, clean );
			number ( 1 );

			return true;

		}

		if ( same ( L"functions" ) ) {

			if ( !takes ( 0, 0 ) || !need_analysis ( ) )
				return true;

			value made = owner.list ( );

			for ( const analysis::function_info& item : db->functions ) {

				value start;

				start.type = kind::NUMBER;
				start.number = static_cast < int64_t > ( item.start );
				owner.push ( made, start );

			}

			result = made;

			return true;

		}

		if ( same ( L"function_at" ) || same ( L"function_end" ) || same ( L"function_name" ) || same ( L"function_size" ) || same ( L"is_function" ) ) {

			uint64_t where = 0;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) || !need_analysis ( ) )
				return true;

			int32_t owner_index = same ( L"is_function" ) ? db->function_starting ( where ) : db->function_at ( where );

			if ( same ( L"is_function" ) ) {

				number ( owner_index >= 0 );

				return true;

			}

			if ( owner_index < 0 )
				return true;

			const analysis::function_info& item = db->functions [ static_cast < size_t > ( owner_index ) ];

			if ( same ( L"function_name" ) ) {

				function_name ( owner_index, label, 400 );
				result = owner.text ( label );

			} else
				number ( static_cast < int64_t > ( same ( L"function_at" ) ? item.start : ( same ( L"function_end" ) ? item.end : item.end - item.start ) ) );

			return true;

		}

		if ( same ( L"is_code" ) ) {

			uint64_t where = 0;

			if ( takes ( 1, 1 ) && address ( 0, where ) && need_analysis ( ) )
				number ( db->is_start ( where ) );

			return true;

		}

		if ( same ( L"instruction" ) || same ( L"mnemonic" ) || same ( L"size" ) || same ( L"next" ) || same ( L"operands" ) ) {

			uint64_t where = 0;
			x86::instruction ins;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) || !need_analysis ( ) || !decode_at ( where, ins ) )
				return true;

			if ( same ( L"size" ) )
				number ( ins.length );
			else if ( same ( L"next" ) )
				number ( static_cast < int64_t > ( ins.next ( ) ) );
			else if ( same ( L"operands" ) )
				number ( ins.count );
			else if ( same ( L"mnemonic" ) ) {

				x86::mnemonic_text ( ins, label, 400 );
				result = owner.text ( label );

			} else {

				script_symbols symbols;

				symbols.owner = this;
				x86::format_text ( ins, label, 400, &symbols, 0 );
				result = owner.text ( label );

			}

			return true;

		}

		if ( same ( L"operand_kind" ) || same ( L"operand_value" ) || same ( L"operand_register" ) ) {

			uint64_t where = 0;
			x86::instruction ins;

			if ( !takes ( 2, 2 ) || !address ( 0, where ) || !need_analysis ( ) || !decode_at ( where, ins ) )
				return true;

			if ( args [ 1 ].type != kind::NUMBER )
				return owner.fail ( L"The operand index must be a number." ) || true;

			int64_t which = args [ 1 ].number;

			if ( which < 0 || which >= ins.count )
				return true;

			const x86::operand& item = ins.operands [ which ];

			if ( same ( L"operand_kind" ) ) {

				const wchar_t* kind_text = item.type == x86::kind::REG ? L"register" : ( item.type == x86::kind::IMM ? L"immediate" : ( item.type == x86::kind::MEM ? L"memory" : L"other" ) );

				if ( item.type == x86::kind::IMM && ins.direct ( ) )
					kind_text = L"address";

				result = owner.text ( kind_text );

			} else if ( same ( L"operand_register" ) ) {

				uint8_t reg = item.type == x86::kind::REG ? item.reg : ( item.type == x86::kind::MEM ? item.base : 0 );

				if ( reg && reg != x86::NONE ) {

					const char* raw = x86::register_name ( reg );
					int32_t k = 0;

					for ( ; raw [ k ] && k < 399; k++ )
						label [ k ] = static_cast < wchar_t > ( raw [ k ] );

					label [ k ] = 0;
					result = owner.text ( label );

				}

			} else {

				uint64_t target = 0;

				if ( item.type == x86::kind::MEM && item.rip_relative ( ) )
					number ( static_cast < int64_t > ( ins.next ( ) + static_cast < uint64_t > ( item.value ) ) );
				else if ( ins.direct ( ) && item.type == x86::kind::IMM )
					number ( static_cast < int64_t > ( ins.target ( ) ) );
				else if ( item.type == x86::kind::IMM || item.type == x86::kind::MEM )
					number ( item.value );
				else if ( ins.memory_target ( target ) )
					number ( static_cast < int64_t > ( target ) );

			}

			return true;

		}

		if ( same ( L"refs_to" ) || same ( L"refs_from" ) || same ( L"callers" ) ) {

			uint64_t where = 0;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) || !need_analysis ( ) )
				return true;

			value made = owner.list ( );
			base::list < uint64_t > seen;

			auto add_unique = [ & ] ( uint64_t item ) {

				for ( uint64_t known : seen ) {

					if ( known == item )
						return;

				}

				seen.push ( item );

				value entry;

				entry.type = kind::NUMBER;
				entry.number = static_cast < int64_t > ( item );
				owner.push ( made, entry );

			};

			if ( same ( L"refs_from" ) ) {

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

					if ( same ( L"callers" ) ) {

						int32_t owner_index = db->function_at ( source );

						if ( owner_index >= 0 && ( item.type == analysis::ref::CALL || item.type == analysis::ref::JUMP ) )
							add_unique ( db->functions [ static_cast < size_t > ( owner_index ) ].start );

					} else
						add_unique ( source );

				}

			}

			result = made;

			return true;

		}

		if ( same ( L"decompile" ) ) {

			uint64_t where = 0;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) || !need_analysis ( ) )
				return true;

			int32_t owner_index = db->function_at ( where );

			if ( owner_index < 0 )
				return owner.fail ( L"That address isn't inside a function." ) || true;

			const decompiler::result* code = pseudocode_for ( db->functions [ static_cast < size_t > ( owner_index ) ].start );

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

			result = owner.text ( out.data ( ), out.count ( ) );

			return true;

		}

		if ( same ( L"strings" ) ) {

			if ( !takes ( 0, 0 ) )
				return true;

			value made = owner.list ( );

			for ( const loader::found_string& item : file.strings ) {

				uint64_t where = 0;

				if ( !file.address_of ( item.offset, where ) )
					continue;

				value entry;

				entry.type = kind::NUMBER;
				entry.number = static_cast < int64_t > ( where );
				owner.push ( made, entry );

			}

			result = made;

			return true;

		}

		if ( same ( L"string" ) ) {

			uint64_t where = 0;
			uint64_t offset = 0;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) || !file.offset_of ( where, offset ) )
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

			result = owner.text ( out.data ( ), out.count ( ) );

			return true;

		}

		if ( same ( L"sections" ) ) {

			if ( !takes ( 0, 0 ) )
				return true;

			value made = owner.list ( );

			for ( const loader::section& item : file.sections ) {

				value row = owner.list ( );
				value start;
				value end;
				wchar_t rights [ 4 ] = { item.read ? L'r' : L'-', item.write ? L'w' : L'-', item.execute ? L'x' : L'-', 0 };

				start.type = kind::NUMBER;
				start.number = static_cast < int64_t > ( item.address );
				end.type = kind::NUMBER;
				end.number = static_cast < int64_t > ( item.address + base::max ( item.size, item.raw ) );
				owner.push ( row, owner.text ( item.name ) );
				owner.push ( row, start );
				owner.push ( row, end );
				owner.push ( row, owner.text ( rights ) );
				owner.push ( made, row );

			}

			result = made;

			return true;

		}

		if ( same ( L"section_at" ) ) {

			uint64_t where = 0;

			if ( !takes ( 1, 1 ) || !address ( 0, where ) )
				return true;

			int32_t index = file.section_at ( where );

			if ( index >= 0 )
				result = owner.text ( file.sections [ static_cast < size_t > ( index ) ].name );

			return true;

		}

		if ( same ( L"imports" ) || same ( L"exports" ) ) {

			if ( !takes ( 0, 0 ) )
				return true;

			value made = owner.list ( );

			if ( same ( L"imports" ) ) {

				for ( const loader::import& item : file.imports ) {

					value row = owner.list ( );
					value slot;

					slot.type = kind::NUMBER;
					slot.number = static_cast < int64_t > ( item.slot );
					owner.push ( row, owner.text ( file.modules [ static_cast < size_t > ( item.owner ) ].name.get ( ) ) );

					if ( item.by_ordinal || item.name.empty ( ) ) {

						base::writer ordinal;

						ordinal.add ( L"#" ).number ( item.ordinal );
						owner.push ( row, owner.text ( ordinal.text ( ) ) );

					} else
						owner.push ( row, owner.text ( item.name.get ( ) ) );

					owner.push ( row, slot );
					owner.push ( made, row );

				}

			} else {

				for ( const loader::exported& item : file.exports ) {

					value row = owner.list ( );
					value where;

					where.type = kind::NUMBER;
					where.number = static_cast < int64_t > ( item.address );
					owner.push ( row, owner.text ( item.name.empty ( ) ? L"" : item.name.get ( ) ) );
					owner.push ( row, where );
					owner.push ( made, row );

				}

			}

			result = made;

			return true;

		}

		if ( same ( L"find_bytes" ) ) {

			const wchar_t* pattern = nullptr;
			uint64_t from = file.base;

			if ( !takes ( 1, 2 ) || !( pattern = text_arg ( 0 ) ) || ( count == 2 && !address ( 1, from ) ) )
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
					return owner.fail ( L"Write the pattern as hex bytes with ?? for wildcards, like \"48 8B ?? 05\"." ) || true;

				wanted [ length++ ] = static_cast < int16_t > ( high * 16 + low );
				c += 2;

			}

			if ( !length )
				return owner.fail ( L"The pattern is empty." ) || true;

			for ( const loader::section& part : file.sections ) {

				uint64_t start = base::max ( part.address, from );
				uint64_t end = part.address + part.raw;
				uint64_t offset = 0;

				if ( start >= end || !file.offset_of ( start, offset ) )
					continue;

				for ( uint64_t at = start; at + static_cast < uint64_t > ( length ) <= end; at++ ) {

					uint64_t base_offset = offset + ( at - start );
					bool hit = true;

					for ( int32_t k = 0; k < length && hit; k++ )
						hit = wanted [ k ] < 0 || file.byte ( base_offset + static_cast < uint64_t > ( k ) ) == static_cast < uint8_t > ( wanted [ k ] );

					if ( hit ) {

						number ( static_cast < int64_t > ( at ) );

						return true;

					}

				}

			}

			return true;

		}

		if ( same ( L"save" ) ) {

			if ( takes ( 0, 0 ) ) {

				save_project ( );
				number ( !dirty );

			}

			return true;

		}

		return false;

	}

}
