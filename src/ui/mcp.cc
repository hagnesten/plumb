#include "ui/app.hh"
#include "ui/tokens.hh"
#include "base/json.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	struct mcp_tool {

		const char* name;
		const char* description;
		const char* schema;

	};

	static const mcp_tool mcp_tools [ ] = {
		{ "open_binary", "Open a Windows executable or DLL (or a .plumb project) and analyze it. Everything else works on the open file.", "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Full path to the file\"}},\"required\":[\"path\"]}" },
		{ "binary_info", "Summary of the open file: format, machine, image base, entry point, sections, symbol source and analysis counts.", "{\"type\":\"object\",\"properties\":{}}" },
		{ "list_functions", "List functions as address, size, callers and name. Filter matches part of the name.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"},\"offset\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\",\"description\":\"Default 200\"}}}" },
		{ "disassemble", "Disassemble a whole function (when given its start or name) or a number of instructions from any address. Addresses are hex like 140001000, names, or +RVA.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\",\"description\":\"Instructions to show; omit for the whole function\"}},\"required\":[\"address\"]}" },
		{ "decompile", "Decompile the function containing an address to C, using the names, types and prototypes set so far.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\",\"description\":\"Address or name\"}},\"required\":[\"function\"]}" },
		{ "xrefs_to", "List every reference to an address or name: the referencing function, offset and kind (call, jump, read, write, address).", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"}},\"required\":[\"address\"]}" },
		{ "calls_from", "List the functions and imports a function calls.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"}},\"required\":[\"function\"]}" },
		{ "list_strings", "List strings found in the file with their addresses. Filter matches part of the text.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"},\"offset\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\",\"description\":\"Default 200\"}}}" },
		{ "list_imports", "List imported functions by module with their slot addresses.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"}}}" },
		{ "list_exports", "List exported functions with addresses or forwarders.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"}}}" },
		{ "search_code", "Search the text of every instruction. #0x40 finds immediates and displacements, c:text searches the decompiled C.", "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}" },
		{ "query_functions", "Find functions by conditions joined with and: calls X, calledby X, refs text, name X, size > N, callers > N, blocks > N, instructions > N, thunk, leaf, noreturn, exported, entry, named, noted, typed. Prefix not to negate.", "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"For example: calls VirtualAlloc and size > 0x100 and not thunk\"}},\"required\":[\"query\"]}" },
		{ "read_bytes", "Read raw bytes at an address as hex and ASCII.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\",\"description\":\"Default 64, at most 4096\"}},\"required\":[\"address\"]}" },
		{ "rename", "Rename a function, label or global. An empty name resets it.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}},\"required\":[\"address\",\"name\"]}" },
		{ "set_comment", "Set the comment on an instruction or data address. Empty text removes it.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"address\",\"text\"]}" },
		{ "set_note", "Set the note describing what a function does. It shows in the function header, the Notes panel and reports.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"function\",\"text\"]}" },
		{ "set_prototype", "Set a function's C prototype, like int __fastcall parse(char* text, int length). The C view and callers use it.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"prototype\":{\"type\":\"string\"}},\"required\":[\"function\",\"prototype\"]}" },
		{ "compare_with", "Compare the open file with another build of it. Lists functions that changed, exist only here, or exist only in the other file.", "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Full path to the other build\"},\"show_same\":{\"type\":\"boolean\"}},\"required\":[\"path\"]}" },
		{ "emulate", "Run a function in Plumb's x86 emulator without starting the program. Reports the result, the imports it calls and any readable strings it writes, which recovers decrypted strings. Decoded strings become comments.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"registers\":{\"type\":\"string\",\"description\":\"Optional starting registers like rcx=0x40 rdx=5\"}},\"required\":[\"function\"]}" },
		{ "save_project", "Save names, comments, notes, types and patches to the .plumb project beside the binary.", "{\"type\":\"object\",\"properties\":{}}" }
	};

	struct mcp_text {

		base::list < wchar_t > out;

		void add ( const wchar_t* text ) {

			for ( ; *text; text++ )
				out.push ( *text );

		}

		void line ( const wchar_t* text ) {

			add ( text );
			out.push ( L'\n' );

		}

	};

	static int64_t integer_arg ( const json::value* args, const char* key, int64_t fallback ) {

		const json::value* item = args ? args->get ( key ) : nullptr;

		if ( !item )
			return fallback;

		if ( item->kind == json::type::NUMBER )
			return static_cast < int64_t > ( item->number );

		if ( item->kind == json::type::STRING ) {

			int64_t value = 0;

			for ( const char* at = item->string ( ); *at >= '0' && *at <= '9'; at++ )
				value = value * 10 + ( *at - '0' );

			return value;

		}

		return fallback;

	}

	static void text_arg ( const json::value* args, const char* key, wchar_t* out, int32_t room ) {

		const json::value* item = args ? args->get ( key ) : nullptr;

		out [ 0 ] = 0;

		if ( item && item->kind == json::type::STRING )
			json::to_wide ( item->string ( ), out, room );
		else if ( item && item->kind == json::type::NUMBER ) {

			base::writer text;

			text.hex ( static_cast < uint64_t > ( item->number ) );

			for ( int32_t i = 0; i <= text.size ( ) && i < room - 1; i++ )
				out [ i ] = text.text ( ) [ i ];

		}

	}

	bool app::mcp_address ( const json::value* args, const char* key, uint64_t& address, base::writer& problem ) const {

		wchar_t text [ 512 ];

		text_arg ( args, key, text, 512 );

		const wchar_t* body = text;

		if ( body [ 0 ] == L'0' && ( body [ 1 ] == L'x' || body [ 1 ] == L'X' ) )
			body += 2;

		if ( !body [ 0 ] ) {

			problem.add ( L"Missing \"" );

			for ( const char* at = key; *at; at++ )
				problem.add ( static_cast < wchar_t > ( *at ) );

			problem.add ( L"\"." );

			return false;

		}

		if ( parse_address ( body, address ) || parse_address ( text, address ) )
			return true;

		problem.add ( L"\"" ).add ( text ).add ( L"\" isn't an address or a known name in this file." );

		return false;

	}

	void app::mcp_disassemble ( uint64_t address, int32_t count, uint64_t limit, base::list < wchar_t >& out ) const {

		listing_names namer;
		mcp_text text;
		wchar_t line [ 400 ];
		wchar_t name [ 200 ];
		bool wide = file.wide ( );
		int32_t shown = 0;
		int32_t skipped = 0;
		uint64_t at = address;

		namer.owner = this;

		while ( shown < count && at < limit ) {

			x86::instruction ins;

			if ( !db->is_start ( at ) || !db->decode ( at, ins ) ) {

				if ( ++skipped > 65536 )
					break;

				at++;

				continue;

			}

			if ( ( db->is_label ( at ) || db->function_starting ( at ) >= 0 ) && symbol_name ( at, true, name, 200 ) )
				text.line ( base::writer ( ).add ( name ).add ( L":" ).text ( ) );

			x86::format_text ( ins, line, 400, &namer, 0 );

			base::writer row;
			const wchar_t* remark = user_text ( comments, at );

			row.add ( L"  " ).hex ( at, wide ? 9 : 8 ).add ( L"  " ).add ( line );

			if ( remark )
				row.add ( L"    ; " ).add ( remark );
			else if ( const wchar_t* stacked = stack_string ( at ) )
				row.add ( L"    ; " ).add ( stacked );

			text.line ( row.text ( ) );
			shown++;
			at = ins.next ( );

		}

		if ( shown == count && at < limit )
			text.line ( L"  ..." );

		out = base::move ( text.out );

	}

	bool app::mcp_call ( const char* name, const json::value* args, base::list < wchar_t >& out ) {

		mcp_text text;
		base::writer problem;
		bool analyzed = db && analysis_applied && !db->functions.empty ( );
		bool wide = file.wide ( );
		wchar_t buffer [ 1024 ];
		wchar_t label [ 200 ];

		auto finish = [ & ] ( bool ok ) {

			if ( !ok )
				text.line ( problem.text ( ) );

			out = base::move ( text.out );

			return ok;

		};

		if ( base::same_text ( name, "open_binary" ) ) {

			text_arg ( args, "path", buffer, 1024 );

			if ( !buffer [ 0 ] ) {

				problem.add ( L"Give the full path of the file to open." );

				return finish ( false );

			}

			if ( !open_file ( buffer ) ) {

				problem.add ( L"Couldn't open " ).add ( buffer ).add ( L". " ).add ( load_problem.get ( ) );

				return finish ( false );

			}

			wait_analysis ( );

			if ( analysis_done && !analysis_applied )
				analysis_finished ( );

			return mcp_call ( "binary_info", nullptr, out );

		}

		if ( !file.loaded ( ) ) {

			problem.add ( L"No file is open. Call open_binary first." );

			return finish ( false );

		}

		if ( base::same_text ( name, "binary_info" ) ) {

			text.line ( base::writer ( ).add ( L"File: " ).add ( file.path.get ( ) ).text ( ) );
			text.line ( base::writer ( ).add ( L"Format: " ).add ( file.format_name ( ) ).add ( L", " ).add ( file.machine_name ( ) ).add ( file.dll ? L", DLL" : L"" ).add ( L", " ).grouped ( file.size ).add ( L" bytes" ).text ( ) );
			text.line ( base::writer ( ).add ( L"Image base: " ).hex ( file.base, wide ? 9 : 8 ).add ( L"   Entry point: " ).hex ( file.entry, wide ? 9 : 8 ).text ( ) );

			if ( !file.symbol_file.empty ( ) )
				text.line ( base::writer ( ).add ( L"Symbols: " ).add ( file.symbol_file.get ( ) ).text ( ) );
			else if ( !file.debug_path.empty ( ) )
				text.line ( base::writer ( ).add ( L"Symbols: none loaded, debug info points to " ).add ( file.debug_path.get ( ) ).text ( ) );

			text.line ( base::writer ( ).add ( L"Imports: " ).grouped ( static_cast < uint64_t > ( file.imports.count ( ) ) ).add ( L" from " ).number ( file.modules.count ( ) ).add ( L" modules   Exports: " ).grouped ( static_cast < uint64_t > ( file.exports.count ( ) ) ).add ( L"   Strings: " ).grouped ( static_cast < uint64_t > ( file.strings.count ( ) ) ).text ( ) );

			if ( analyzed )
				text.line ( base::writer ( ).add ( L"Analysis: " ).grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L" functions, " ).grouped ( db->decoded ).add ( L" instructions, " ).grouped ( static_cast < uint64_t > ( db->refs.count ( ) ) ).add ( L" references, " ).number ( db->classes.count ( ) ).add ( L" RTTI classes" ).text ( ) );

			text.line ( L"Sections:" );

			for ( const loader::section& item : file.sections )
				text.line ( base::writer ( ).add ( L"  " ).add ( item.name ).add ( L"  " ).hex ( item.address, wide ? 9 : 8 ).add ( L"  size 0x" ).hex ( item.size ).add ( L"  " ).add ( item.read ? L"R" : L"-" ).add ( item.write ? L"W" : L"-" ).add ( item.execute ? L"X" : L"-" ).add ( L"  " ).add ( loader::kind_name ( item.type ) ).text ( ) );

			return finish ( true );

		}

		if ( base::same_text ( name, "list_imports" ) || base::same_text ( name, "list_exports" ) ) {

			text_arg ( args, "filter", buffer, 1024 );

			bool imports = base::same_text ( name, "list_imports" );
			int32_t count = 0;

			if ( imports ) {

				for ( const loader::import& item : file.imports ) {

					const wchar_t* module = file.modules [ static_cast < size_t > ( item.owner ) ].name.get ( );

					if ( buffer [ 0 ] && !base::contains_folded ( item.name.get ( ), buffer ) && !base::contains_folded ( module, buffer ) )
						continue;

					base::writer row;

					row.hex ( item.slot, wide ? 9 : 8 ).add ( L"  " ).add ( module ).add ( L"!" );

					if ( item.by_ordinal || item.name.empty ( ) )
						row.add ( L"#" ).number ( item.ordinal );
					else
						row.add ( item.name.get ( ) );

					text.line ( row.text ( ) );
					count++;

				}

			} else {

				for ( const loader::exported& item : file.exports ) {

					if ( buffer [ 0 ] && !base::contains_folded ( item.name.get ( ), buffer ) )
						continue;

					base::writer row;

					if ( item.forward.empty ( ) )
						row.hex ( item.address, wide ? 9 : 8 );
					else
						row.add ( L"forward" );

					row.add ( L"  #" ).number ( item.ordinal ).add ( L"  " ).add ( item.name.empty ( ) ? L"(no name)" : item.name.get ( ) );

					if ( !item.forward.empty ( ) )
						row.add ( L" -> " ).add ( item.forward.get ( ) );

					text.line ( row.text ( ) );
					count++;

				}

			}

			if ( !count )
				text.line ( L"Nothing matches." );

			return finish ( true );

		}

		if ( base::same_text ( name, "list_strings" ) ) {

			text_arg ( args, "filter", buffer, 1024 );

			int64_t skip = integer_arg ( args, "offset", 0 );
			int64_t limit = base::clamp < int64_t > ( integer_arg ( args, "limit", 200 ), 1, 5000 );
			int64_t matched = 0;
			int64_t shown = 0;
			wchar_t preview [ 400 ];

			for ( const loader::found_string& item : file.strings ) {

				file.string_text ( item, preview, 400 );

				if ( buffer [ 0 ] && !base::contains_folded ( preview, buffer ) )
					continue;

				if ( matched++ < skip || shown >= limit )
					continue;

				uint64_t address = 0;
				base::writer row;

				for ( wchar_t* c = preview; *c; c++ ) {

					if ( *c < 32 )
						*c = L' ';

				}

				if ( file.address_of ( item.offset, address ) )
					row.hex ( address, wide ? 9 : 8 );
				else
					row.add ( L"file+" ).hex ( item.offset );

				row.add ( item.wide ? L"  W  \"" : L"  A  \"" ).add ( preview ).add ( L"\"" );
				text.line ( row.text ( ) );
				shown++;

			}

			text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( matched ) ).add ( L" strings match, showing " ).grouped ( static_cast < uint64_t > ( shown ) ).add ( L" from offset " ).number ( skip ).add ( L"." ).text ( ) );

			return finish ( true );

		}

		if ( base::same_text ( name, "read_bytes" ) ) {

			uint64_t address = 0;
			uint64_t offset = 0;

			if ( !mcp_address ( args, "address", address, problem ) )
				return finish ( false );

			if ( !file.offset_of ( address, offset ) ) {

				problem.add ( L"That address isn't backed by bytes in the file." );

				return finish ( false );

			}

			int64_t count = base::clamp < int64_t > ( integer_arg ( args, "count", 64 ), 1, 4096 );

			for ( int64_t row = 0; row < count; row += 16 ) {

				base::writer line;
				wchar_t shown [ 17 ];
				int32_t n = 0;

				line.hex ( address + static_cast < uint64_t > ( row ), wide ? 9 : 8 ).add ( L"  " );

				for ( int64_t i = row; i < row + 16 && i < count; i++ ) {

					uint64_t at = offset + static_cast < uint64_t > ( i );
					uint8_t value = at < file.size ? file.byte ( at ) : 0;

					line.hex ( value, 2 ).add ( L" " );
					shown [ n++ ] = value >= 32 && value < 127 ? static_cast < wchar_t > ( value ) : L'.';

				}

				shown [ n ] = 0;
				line.add ( L" " ).add ( shown );
				text.line ( line.text ( ) );

			}

			return finish ( true );

		}

		if ( !analyzed ) {

			problem.add ( L"The analysis hasn't produced any functions for this file." );

			return finish ( false );

		}

		if ( base::same_text ( name, "list_functions" ) ) {

			text_arg ( args, "filter", buffer, 1024 );

			int64_t skip = integer_arg ( args, "offset", 0 );
			int64_t limit = base::clamp < int64_t > ( integer_arg ( args, "limit", 200 ), 1, 5000 );
			int64_t matched = 0;
			int64_t shown = 0;

			for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

				function_name ( i, label, 200 );

				if ( buffer [ 0 ] && !base::contains_folded ( label, buffer ) )
					continue;

				if ( matched++ < skip || shown >= limit )
					continue;

				const analysis::function_info& info = db->functions [ static_cast < size_t > ( i ) ];
				const wchar_t* remark = user_text ( notes, info.start );
				base::writer row;

				row.hex ( info.start, wide ? 9 : 8 ).add ( L"  size 0x" ).hex ( info.end - info.start ).add ( L"  callers " ).number ( info.callers ).add ( L"  " ).add ( label );

				if ( remark )
					row.add ( L"  // " ).add ( remark );

				text.line ( row.text ( ) );
				shown++;

			}

			text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( matched ) ).add ( L" functions match, showing " ).grouped ( static_cast < uint64_t > ( shown ) ).add ( L" from offset " ).number ( skip ).add ( L"." ).text ( ) );

			return finish ( true );

		}

		if ( base::same_text ( name, "disassemble" ) ) {

			uint64_t address = 0;

			if ( !mcp_address ( args, "address", address, problem ) )
				return finish ( false );

			int32_t owner = db->function_starting ( address );
			int64_t count = integer_arg ( args, "count", -1 );
			uint64_t limit = file.high ( );

			if ( count < 0 && owner >= 0 ) {

				count = 2000;
				limit = db->functions [ static_cast < size_t > ( owner ) ].end;

			} else if ( count < 0 )
				count = 40;

			base::list < wchar_t > listing;

			mcp_disassemble ( address, static_cast < int32_t > ( base::clamp < int64_t > ( count, 1, 2000 ) ), limit, listing );

			for ( wchar_t c : listing )
				text.out.push ( c );

			if ( listing.empty ( ) )
				text.line ( L"No decoded instructions there. It may be data." );

			return finish ( true );

		}

		if ( base::same_text ( name, "decompile" ) ) {

			uint64_t address = 0;

			if ( !mcp_address ( args, "function", address, problem ) )
				return finish ( false );

			int32_t owner = db->function_at ( address );

			if ( owner < 0 ) {

				problem.add ( L"That address isn't inside a function." );

				return finish ( false );

			}

			const decompiler::result* code = pseudocode_for ( db->functions [ static_cast < size_t > ( owner ) ].start );

			if ( !code || !code->ok ) {

				problem.add ( L"The decompiler couldn't handle this function. Use disassemble instead." );

				return finish ( false );

			}

			for ( const decompiler::line& item : code->lines ) {

				for ( int32_t i = 0; i < item.indent; i++ )
					text.add ( L"    " );

				for ( int32_t i = 0; i < item.length; i++ )
					text.out.push ( code->text [ static_cast < size_t > ( item.text + i ) ] );

				text.out.push ( L'\n' );

			}

			return finish ( true );

		}

		if ( base::same_text ( name, "xrefs_to" ) ) {

			uint64_t address = 0;
			int32_t first = 0;
			int32_t count = 0;

			if ( !mcp_address ( args, "address", address, problem ) )
				return finish ( false );

			db->refs_to ( address, first, count );

			for ( int32_t r = first; r < first + count && r < first + 2000; r++ ) {

				const analysis::xref& item = db->refs [ static_cast < size_t > ( r ) ];
				uint64_t source = file.base + item.source;
				int32_t owner = db->function_at ( source );
				base::writer row;

				row.hex ( source, wide ? 9 : 8 ).add ( L"  " ).add ( analysis::ref_name ( item.type ) ).add ( L"  " );

				if ( owner >= 0 ) {

					function_name ( owner, label, 200 );
					row.add ( label ).add ( L"+" ).hex ( source - db->functions [ static_cast < size_t > ( owner ) ].start );

				}

				text.line ( row.text ( ) );

			}

			text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( count ) ).add ( count == 1 ? L" reference." : L" references." ).text ( ) );

			return finish ( true );

		}

		if ( base::same_text ( name, "calls_from" ) ) {

			uint64_t address = 0;

			if ( !mcp_address ( args, "function", address, problem ) )
				return finish ( false );

			int32_t owner = db->function_at ( address );

			if ( owner < 0 ) {

				problem.add ( L"That address isn't inside a function." );

				return finish ( false );

			}

			const analysis::function_info& info = db->functions [ static_cast < size_t > ( owner ) ];
			base::list < uint64_t > seen;

			for ( const analysis::xref& item : db->refs ) {

				uint64_t source = file.base + item.source;
				uint64_t target = file.base + item.target;
				bool call_like = item.type == analysis::ref::CALL || ( item.type == analysis::ref::JUMP && db->function_starting ( target ) >= 0 ) || ( item.type == analysis::ref::READ && file.section_at ( target ) >= 0 );

				if ( !call_like || db->function_at ( source ) != owner || source < info.start )
					continue;

				bool import_slot = false;

				for ( const loader::import& entry : file.imports )
					import_slot = import_slot || entry.slot == target;

				if ( item.type == analysis::ref::READ && !import_slot )
					continue;

				bool repeated = false;

				for ( uint64_t value : seen )
					repeated = repeated || value == target;

				if ( repeated )
					continue;

				seen.push ( target );

				base::writer row;

				row.hex ( target, wide ? 9 : 8 ).add ( L"  " );

				if ( symbol_name ( target, true, label, 200 ) )
					row.add ( label );

				row.add ( import_slot ? L"  (import)" : L"" );
				text.line ( row.text ( ) );

			}

			if ( seen.empty ( ) )
				text.line ( L"This function makes no direct calls." );

			return finish ( true );

		}

		if ( base::same_text ( name, "search_code" ) || base::same_text ( name, "query_functions" ) ) {

			bool query = base::same_text ( name, "query_functions" );

			text_arg ( args, query ? "query" : "text", buffer, 1024 );

			if ( !buffer [ 0 ] ) {

				problem.add ( L"The search text is empty." );

				return finish ( false );

			}

			if ( query ) {

				if ( !run_query ( buffer ) ) {

					for ( int32_t i = output.count ( ) - 1; i >= 0 && !problem.size ( ); i-- ) {

						if ( output [ static_cast < size_t > ( i ) ].kind == log_kind::FAIL )
							problem.add ( output [ static_cast < size_t > ( i ) ].text.get ( ) );

					}

					if ( !problem.size ( ) )
						problem.add ( L"The query failed." );

					return finish ( false );

				}

			} else
				search_everything ( buffer );

			int32_t shown = base::min ( search_hits.count ( ), 2000 );

			for ( int32_t i = 0; i < shown; i++ ) {

				const search_hit& item = search_hits [ static_cast < size_t > ( i ) ];
				int32_t owner = db->function_at ( item.address );
				base::writer row;

				row.hex ( item.address, wide ? 9 : 8 ).add ( L"  " );

				if ( !query && owner >= 0 ) {

					function_name ( owner, label, 200 );
					row.add ( label ).add ( L"+" ).hex ( item.address - db->functions [ static_cast < size_t > ( owner ) ].start ).add ( L"  " );

				}

				row.add ( item.note.get ( ) );
				text.line ( row.text ( ) );

			}

			text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( search_hits.count ( ) ) ).add ( search_hits.count ( ) == 1 ? L" match." : L" matches." ).text ( ) );

			return finish ( true );

		}

		if ( base::same_text ( name, "rename" ) || base::same_text ( name, "set_comment" ) || base::same_text ( name, "set_note" ) || base::same_text ( name, "set_prototype" ) ) {

			bool rename = base::same_text ( name, "rename" );
			bool note = base::same_text ( name, "set_note" );
			bool shape = base::same_text ( name, "set_prototype" );
			bool comment = !rename && !note && !shape;
			uint64_t address = 0;

			if ( !mcp_address ( args, rename || comment ? "address" : "function", address, problem ) )
				return finish ( false );

			text_arg ( args, rename ? "name" : ( shape ? "prototype" : "text" ), buffer, 250 );

			if ( note || shape ) {

				int32_t owner = db->function_at ( address );

				if ( owner < 0 ) {

					problem.add ( L"That address isn't inside a function." );

					return finish ( false );

				}

				address = db->functions [ static_cast < size_t > ( owner ) ].start;

			}

			if ( rename ) {

				for ( wchar_t* c = buffer; *c; c++ ) {

					bool word = ( *c >= L'a' && *c <= L'z' ) || ( *c >= L'A' && *c <= L'Z' ) || ( *c >= L'0' && *c <= L'9' ) || *c == L'_' || *c == L':' || *c == L'@' || *c == L'?' || *c == L'$' || *c == L'~';

					if ( !word )
						*c = L'_';

				}

				if ( buffer [ 0 ] >= L'0' && buffer [ 0 ] <= L'9' ) {

					problem.add ( L"Names can't start with a digit." );

					return finish ( false );

				}

			}

			if ( shape && buffer [ 0 ] ) {

				decompiler::c_prototype parsed;
				base::writer complaint;

				if ( !decompiler::parse_prototype ( buffer, file.wide ( ), parsed, complaint ) ) {

					problem.add ( L"That prototype doesn't parse: " ).add ( complaint.text ( ) );

					return finish ( false );

				}

			}

			remember_undo ( );
			set_user_text ( rename ? renamed : ( note ? notes : ( shape ? prototypes : comments ) ), address, buffer );

			if ( rename || shape ) {

				names_revision++;
				code_cache.clear ( );

			}

			changed ( );

			if ( autosave )
				save_project ( );

			symbol_name ( address, true, label, 200 );
			text.line ( base::writer ( ).add ( buffer [ 0 ] ? L"Set at " : L"Cleared at " ).hex ( address, wide ? 9 : 8 ).add ( L" (" ).add ( label ).add ( L"). Call save_project to keep it." ).text ( ) );

			return finish ( true );

		}

		if ( base::same_text ( name, "compare_with" ) ) {

			text_arg ( args, "path", buffer, 1024 );

			const json::value* same = args ? args->get ( "show_same" ) : nullptr;
			bool show_same = same && same->kind == json::type::BOOLEAN && same->flag;
			static const wchar_t* const states [ 4 ] = { L"same", L"changed", L"only here", L"only there" };

			if ( !buffer [ 0 ] || !compare_with ( buffer ) ) {

				problem.add ( L"Couldn't compare with " ).add ( buffer ).add ( L"." );

				return finish ( false );

			}

			text.line ( base::writer ( ).add ( L"Compared with " ).add ( other_file->path.get ( ) ).add ( L": " ).number ( diff_counts [ 0 ] ).add ( L" same, " ).number ( diff_counts [ 1 ] ).add ( L" changed, " ).number ( diff_counts [ 2 ] ).add ( L" only here, " ).number ( diff_counts [ 3 ] ).add ( L" only there." ).text ( ) );

			int32_t shown = 0;

			for ( const diff_row& item : diff_rows ) {

				if ( ( item.state == 0 && !show_same ) || shown >= 3000 )
					continue;

				base::writer row;
				wchar_t other [ 200 ];

				row.add ( states [ item.state ] ).add ( L"  " );

				if ( item.here >= 0 ) {

					function_name ( item.here, label, 200 );
					row.hex ( db->functions [ static_cast < size_t > ( item.here ) ].start, wide ? 9 : 8 ).add ( L"  " ).add ( label );

				}

				if ( item.there >= 0 ) {

					other_name ( item.there, other, 200 );
					row.add ( item.here >= 0 ? L"  <->  " : L"" ).hex ( other_db->functions [ static_cast < size_t > ( item.there ) ].start, wide ? 9 : 8 ).add ( L"  " ).add ( other );

				}

				text.line ( row.text ( ) );
				shown++;

			}

			return finish ( true );

		}

		if ( base::same_text ( name, "emulate" ) ) {

			uint64_t address = 0;
			wchar_t setup [ 512 ];

			if ( !mcp_address ( args, "function", address, problem ) )
				return finish ( false );

			text_arg ( args, "registers", setup, 512 );

			emulate_function ( address, setup, &text.out );

			return finish ( true );

		}

		if ( base::same_text ( name, "save_project" ) ) {

			save_project ( );

			if ( dirty ) {

				problem.add ( L"Couldn't save the project." );

				return finish ( false );

			}

			text.line ( L"Saved the project beside the binary." );

			return finish ( true );

		}

		problem.add ( L"Unknown tool." );

		return finish ( false );

	}

	void app::mcp_message ( const json::value& message, json::writer& reply ) {

		const json::value* id = message.get ( "id" );
		const json::value* method = message.get ( "method" );
		const char* name = method ? method->string ( ) : "";

		if ( !id || id->kind == json::type::NONE )
			return;

		reply.open_object ( ).key ( "jsonrpc" ).string ( "2.0" ).key ( "id" ).copy ( *id );

		if ( base::same_text ( name, "initialize" ) ) {

			const json::value* params = message.get ( "params" );
			const json::value* version = params ? params->get ( "protocolVersion" ) : nullptr;

			reply.key ( "result" ).open_object ( );
			reply.key ( "protocolVersion" ).string ( version && version->kind == json::type::STRING ? version->string ( ) : "2025-06-18" );
			reply.key ( "capabilities" ).open_object ( ).key ( "tools" ).open_object ( ).key ( "listChanged" ).boolean ( false ).close_object ( ).close_object ( );
			reply.key ( "serverInfo" ).open_object ( ).key ( "name" ).string ( "plumb" ).key ( "version" ).string ( "0.4" ).close_object ( );
			reply.key ( "instructions" ).string ( "Plumb is a disassembler and decompiler for Windows executables. Open a file with open_binary (unless one was given on the command line), then explore with list_functions, query_functions, decompile, disassemble, xrefs_to and calls_from. Record findings with rename, set_note, set_comment and set_prototype, and call save_project to keep them in the .plumb project that the Plumb app opens." );
			reply.close_object ( );

		} else if ( base::same_text ( name, "ping" ) ) {

			reply.key ( "result" ).open_object ( ).close_object ( );

		} else if ( base::same_text ( name, "tools/list" ) ) {

			reply.key ( "result" ).open_object ( ).key ( "tools" ).open_array ( );

			for ( const mcp_tool& tool : mcp_tools ) {

				json::value schema;

				json::parse ( tool.schema, static_cast < size_t > ( base::text_length ( tool.schema ) ), schema );
				reply.open_object ( ).key ( "name" ).string ( tool.name ).key ( "description" ).string ( tool.description ).key ( "inputSchema" ).copy ( schema ).close_object ( );

			}

			reply.close_array ( ).close_object ( );

		} else if ( base::same_text ( name, "tools/call" ) ) {

			const json::value* params = message.get ( "params" );
			const json::value* tool = params ? params->get ( "name" ) : nullptr;
			const json::value* args = params ? params->get ( "arguments" ) : nullptr;
			base::list < wchar_t > text;
			bool ok = mcp_call ( tool ? tool->string ( ) : "", args, text );

			reply.key ( "result" ).open_object ( ).key ( "content" ).open_array ( ).open_object ( ).key ( "type" ).string ( "text" ).key ( "text" ).string ( text.data ( ) ? text.data ( ) : L"", text.count ( ) ).close_object ( ).close_array ( );
			reply.key ( "isError" ).boolean ( !ok ).close_object ( );

		} else {

			reply.key ( "error" ).open_object ( ).key ( "code" ).number ( -32601 ).key ( "message" ).string ( "Method not found" ).close_object ( );

		}

		reply.close_object ( );

	}

	int32_t app::serve_mcp ( ) {

		HANDLE input = GetStdHandle ( STD_INPUT_HANDLE );
		HANDLE output_handle = GetStdHandle ( STD_OUTPUT_HANDLE );
		base::list < char > pending_bytes;
		char chunk [ 65536 ];

		if ( !input || input == INVALID_HANDLE_VALUE || !output_handle || output_handle == INVALID_HANDLE_VALUE )
			return 1;

		while ( true ) {

			DWORD got = 0;

			if ( !ReadFile ( input, chunk, sizeof ( chunk ), &got, nullptr ) || !got )
				break;

			for ( DWORD i = 0; i < got; i++ )
				pending_bytes.push ( chunk [ i ] );

			int32_t start = 0;

			for ( int32_t i = 0; i < pending_bytes.count ( ); i++ ) {

				if ( pending_bytes [ static_cast < size_t > ( i ) ] != '\n' )
					continue;

				json::value message;
				json::writer reply;

				if ( i - start >= 3 && static_cast < uint8_t > ( pending_bytes [ static_cast < size_t > ( start ) ] ) == 0xEF && static_cast < uint8_t > ( pending_bytes [ static_cast < size_t > ( start + 1 ) ] ) == 0xBB && static_cast < uint8_t > ( pending_bytes [ static_cast < size_t > ( start + 2 ) ] ) == 0xBF )
					start += 3;

				int32_t length = i - start;

				if ( length > 0 && json::parse ( pending_bytes.data ( ) + start, static_cast < size_t > ( length ), message ) ) {

					if ( message.kind == json::type::ARRAY ) {

						reply.open_array ( );

						for ( const json::value& item : message.items )
							mcp_message ( item, reply );

						reply.close_array ( );

					} else
						mcp_message ( message, reply );

				} else if ( length > 1 )
					reply.raw ( "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}" );

				if ( reply.size ( ) > 2 ) {

					DWORD wrote = 0;

					WriteFile ( output_handle, reply.text ( ), static_cast < DWORD > ( reply.size ( ) ), &wrote, nullptr );
					WriteFile ( output_handle, "\n", 1, &wrote, nullptr );

				}

				start = i + 1;

			}

			base::list < char > rest;

			for ( int32_t i = start; i < pending_bytes.count ( ); i++ )
				rest.push ( pending_bytes [ static_cast < size_t > ( i ) ] );

			pending_bytes = base::move ( rest );

		}

		files::settle ( );

		return 0;

	}

}
