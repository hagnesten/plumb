#include "host/headless/capture.h"
#include "ui/core/app/app.h"
#include "x86/instruction/instruction.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "x86/assembler/assembler.h"
#include "analysis/database/database.h"
#include "base/files/files.h"
#include "base/clock/clock.h"
#include "base/strings/strings.h"
#include <windows.h>
#include <shellapi.h>

int32_t host::argument_count = 0;

wchar_t** host::arguments = nullptr;

const wchar_t* host::argument ( const wchar_t* name ) {

	for ( int32_t i = 1; i + 1 < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], name ) )
			return arguments [ i + 1 ];

	}

	return nullptr;

}

float host::parse_float ( const wchar_t* text ) {

	float value = 0.0f;
	float scale = 0.0f;
	bool negative = false;

	if ( *text == L'-' ) {

		negative = true;
		text++;

	}

	for ( ; *text; text++ ) {

		if ( *text == L'.' ) {

			scale = 0.1f;

			continue;

		}

		if ( *text < L'0' || *text > L'9' )
			break;

		if ( scale > 0.0f ) {

			value += static_cast < float > ( *text - L'0' ) * scale;
			scale *= 0.1f;

		} else
			value = value * 10.0f + static_cast < float > ( *text - L'0' );

	}

	return negative ? -value : value;

}

bool host::plain_names::name ( uint64_t address, bool code, wchar_t* out, int32_t room ) {

	return owner->symbol_name ( address, code, out, room );

}

bool host::plain_names::literal ( uint64_t address, wchar_t* out, int32_t room ) {

	return decompiler::string_literal ( owner->file, address, out, room );

}

wchar_t host::plain_names::number_form ( uint64_t address, int64_t value, int32_t occurrence ) {

	return owner->code_number_form ( address, value, occurrence );

}

const wchar_t* host::plain_names::prototype ( uint64_t function ) {

	return owner->prototype_for ( function );

}

const wchar_t* host::plain_names::derived ( uint64_t function ) {

	for ( int32_t i = 0; i < derived_at.count ( ); i++ ) {

		if ( derived_at [ static_cast < size_t > ( i ) ] == function )
			return derived_chars.data ( ) + derived_offset [ static_cast < size_t > ( i ) ];

	}

	return nullptr;

}

void host::plain_names::derive ( uint64_t function, const decompiler::result& code ) {

	int32_t start = derived_chars.count ( );
	bool spaced = false;

	for ( const decompiler::line& line : code.lines ) {

		if ( line.length == 1 && code.text [ static_cast < size_t > ( line.text ) ] == L'{' )
			break;

		for ( int32_t k = 0; k < line.length; k++ ) {

			wchar_t c = code.text [ static_cast < size_t > ( line.text + k ) ];

			if ( c == L' ' || c == L'\t' ) {

				spaced = derived_chars.count ( ) > start;

				continue;

			}

			if ( spaced )
				derived_chars.push ( L' ' );

			spaced = false;
			derived_chars.push ( c );

		}

		spaced = derived_chars.count ( ) > start;

	}

	if ( derived_chars.count ( ) == start )
		return;

	derived_chars.push ( 0 );

	decompiler::c_prototype* shape = new decompiler::c_prototype ( );
	base::writer problem;
	bool parsed = decompiler::parse_prototype ( derived_chars.data ( ) + start, true, *shape, problem );

	derived_chars.resize ( static_cast < size_t > ( start ) );

	if ( parsed ) {

		base::writer text;

		text.add ( shape->result ).add ( L" __fastcall " ).add ( shape->name ).add ( L"(" );

		for ( int32_t j = 0; j < shape->count; j++ )
			text.add ( j ? L", " : L"" ).add ( shape->types [ j ] );

		if ( shape->variadic )
			text.add ( shape->count ? L", ..." : L"..." );

		text.add ( L")" );

		for ( const wchar_t* c = text.text ( ); *c; c++ )
			derived_chars.push ( *c );

		derived_chars.push ( 0 );
		derived_at.push ( function );
		derived_offset.push ( start );

	}

	delete shape;

}

bool host::plain_names::number_notes ( ) {

	return argument ( L"--numbers" ) != nullptr;

}

bool host::plain_names::expand_ternary ( ) {

	const wchar_t* style = argument ( L"--ternary" );

	return style && base::same_text ( style, L"if" );

}

uint32_t host::key_code ( const wchar_t* name, bool& control, bool& shift, bool& alt ) {

	control = false;
	shift = false;
	alt = false;

	while ( true ) {

		if ( name [ 0 ] == L'c' && name [ 1 ] == L't' && name [ 2 ] == L'r' && name [ 3 ] == L'l' && name [ 4 ] == L'+' ) {

			control = true;
			name += 5;

		} else if ( name [ 0 ] == L's' && name [ 1 ] == L'h' && name [ 2 ] == L'i' && name [ 3 ] == L'f' && name [ 4 ] == L't' && name [ 5 ] == L'+' ) {

			shift = true;
			name += 6;

		} else if ( name [ 0 ] == L'a' && name [ 1 ] == L'l' && name [ 2 ] == L't' && name [ 3 ] == L'+' ) {

			alt = true;
			name += 4;

		} else
			break;

	}

	const wchar_t* names [ ] = { L"left", L"right", L"up", L"down", L"escape", L"enter", L"pagedown", L"pageup", L"home", L"end", L"f1", L"f5", L"tab", L"back", L"f2", L"f7", L"f8", L"f9", L"f12", L"space", L"backslash", L"numminus", L"numplus", L"comma", L"slash" };
	const uint32_t codes [ ] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_ESCAPE, VK_RETURN, VK_NEXT, VK_PRIOR, VK_HOME, VK_END, VK_F1, VK_F5, VK_TAB, VK_BACK, VK_F2, VK_F7, VK_F8, VK_F9, VK_F12, VK_SPACE, VK_OEM_5, VK_SUBTRACT, VK_ADD, VK_OEM_COMMA, VK_OEM_2 };

	for ( int32_t i = 0; i < 25; i++ ) {

		if ( base::same_text ( name, names [ i ] ) )
			return codes [ i ];

	}

	if ( name [ 0 ] && !name [ 1 ] )
		return static_cast < uint32_t > ( name [ 0 ] >= L'a' && name [ 0 ] <= L'z' ? name [ 0 ] - 32 : name [ 0 ] );

	return 0;

}

bool host::capture_mode ( ) {

	const wchar_t* prefix = argument ( L"--capture" );

	if ( !prefix )
		return false;

	float width = 1440.0f;
	float height = 900.0f;
	float scale = 1.0f;
	const wchar_t* size_text = argument ( L"--size" );
	const wchar_t* scale_text = argument ( L"--scale" );
	const wchar_t* script = argument ( L"--script" );
	base::list < char > script_file;
	base::list < wchar_t > script_wide;

	if ( size_text ) {

		width = parse_float ( size_text );

		for ( const wchar_t* p = size_text; *p; p++ ) {

			if ( *p == L'x' ) {

				height = parse_float ( p + 1 );

				break;

			}

		}

	}

	if ( scale_text )
		scale = parse_float ( scale_text );

	const wchar_t* script_path = argument ( L"--script-file" );

	if ( script_path && files::read ( script_path, script_file ) ) {

		int32_t length = MultiByteToWideChar ( CP_UTF8, 0, script_file.data ( ), -1, nullptr, 0 );

		script_wide.resize ( static_cast < size_t > ( base::max ( length, 1 ) ) );
		script_wide [ 0 ] = 0;
		MultiByteToWideChar ( CP_UTF8, 0, script_file.data ( ), -1, script_wide.data ( ), length );
		script = script_wide.data ( );

	}

	render::gpu* gpu = new render::gpu ( );

	if ( !gpu->setup ( ) )
		return true;

	ui::app* app = new ui::app ( );

	app->fixed_step = true;
	app->persist = false;

	if ( !app->setup ( gpu, nullptr ) )
		return true;

	app->resize ( static_cast < int32_t > ( width * scale ), static_cast < int32_t > ( height * scale ), scale );

	double clock = 0.0;
	auto step = [ & ] ( int32_t frames ) {

		for ( int32_t i = 0; i < frames; i++ ) {

			clock += 1.0 / 60.0;
			app->frame ( clock );

		}

	};

	step ( 20 );

	script_reader reader = { script ? script : L"shot main" };
	wchar_t command [ 600 ];

	while ( reader.word ( command, 600 ) ) {

		if ( base::same_text ( command, L"wait" ) )
			step ( base::max ( static_cast < int32_t > ( reader.number ( ) * 60.0f + 0.5f ), 1 ) );
		else if ( base::same_text ( command, L"frames" ) )
			step ( static_cast < int32_t > ( reader.number ( ) ) );
		else if ( base::same_text ( command, L"persist" ) ) {

			app->persist = true;
			app->settings_frozen = true;

		}
		else if ( base::same_text ( command, L"library" ) ) {

			wchar_t path [ 600 ];

			reader.word ( path, 600 );
			app->library_path.set ( path );

		}
		else if ( base::same_text ( command, L"async" ) )
			app->analysis_sync = false;
		else if ( base::same_text ( command, L"sleep" ) ) {

			Sleep ( static_cast < DWORD > ( reader.number ( ) * 1000.0f ) );
			step ( 3 );

		}
		else if ( base::same_text ( command, L"open" ) ) {

			wchar_t path [ 600 ];

			reader.word ( path, 600 );

			for ( int32_t i = 0; path [ i ]; i++ ) {

				if ( path [ i ] == L'|' )
					path [ i ] = L' ';

			}

			app->open_file ( path );
			step ( 2 );

		} else if ( base::same_text ( command, L"theme" ) ) {

			wchar_t name [ 32 ];

			reader.word ( name, 32 );
			app->set_theme ( base::same_text ( name, L"light" ) );
			step ( 1 );

		} else if ( base::same_text ( command, L"move" ) ) {

			float x = reader.number ( );
			float y = reader.number ( );

			app->mouse_move ( x * scale, y * scale );
			step ( 1 );

		} else if ( base::same_text ( command, L"click" ) ) {

			float x = reader.number ( );
			float y = reader.number ( );

			app->mouse_move ( x * scale, y * scale );
			step ( 2 );
			app->mouse_button ( 0, true );
			step ( 1 );
			app->mouse_button ( 0, false );
			step ( 1 );

		} else if ( base::same_text ( command, L"caret" ) ) {

			app->now->code_line = static_cast < int32_t > ( reader.number ( ) ) - 1;
			app->now->code_col = static_cast < int32_t > ( reader.number ( ) );
			app->now->code_anchor = app->now->code_line;
			app->now->code_anchor_col = app->now->code_col;
			app->now->code_ranged = false;
			app->mouse_leave ( );
			step ( 2 );

		} else if ( base::same_text ( command, L"rclick" ) ) {

			float x = reader.number ( );
			float y = reader.number ( );

			app->mouse_move ( x * scale, y * scale );
			step ( 2 );
			app->mouse_button ( 1, true );
			step ( 2 );

		} else if ( base::same_text ( command, L"down" ) ) {

			app->mouse_button ( 0, true );
			step ( 1 );

		} else if ( base::same_text ( command, L"up" ) ) {

			app->mouse_button ( 0, false );
			step ( 1 );

		} else if ( base::same_text ( command, L"leave" ) ) {

			app->mouse_leave ( );
			step ( 1 );

		} else if ( base::same_text ( command, L"wheel" ) ) {

			app->mouse_wheel ( reader.number ( ), false );
			step ( 1 );

		} else if ( base::same_text ( command, L"key" ) ) {

			wchar_t name [ 64 ];
			bool control = false;
			bool shift = false;
			bool alt = false;

			reader.word ( name, 64 );

			uint32_t code = key_code ( name, control, shift, alt );

			if ( code )
				app->key ( code, control, shift, alt );

			step ( 1 );

		} else if ( base::same_text ( command, L"type" ) ) {

			wchar_t text [ 256 ];

			reader.word ( text, 256 );

			for ( int32_t i = 0; text [ i ]; i++ ) {

				app->character ( text [ i ] == L'_' ? L' ' : text [ i ] );
				step ( 2 );

			}

		} else if ( base::same_text ( command, L"run" ) ) {

			app->run ( static_cast < ui::command > ( static_cast < int32_t > ( reader.number ( ) ) ) );
			step ( 1 );

		} else if ( base::same_text ( command, L"size" ) ) {

			width = reader.number ( );
			height = reader.number ( );
			app->resize ( static_cast < int32_t > ( width * scale ), static_cast < int32_t > ( height * scale ), scale );
			step ( 2 );

		} else if ( base::same_text ( command, L"shot" ) ) {

			wchar_t name [ 64 ];
			base::writer line;

			reader.word ( name, 64 );
			line.add ( prefix ).add ( L"_" ).add ( name ).add ( L".png" );
			gpu->save ( gpu->chrome, line.text ( ), app->tones [ static_cast < int32_t > ( ui::tone::BASE ) ], app->width, app->height );

		}

	}

	return true;

}

bool host::disasm_mode ( ) {

	const wchar_t* source = argument ( L"--disasm" );
	const wchar_t* target = argument ( L"--out" );

	if ( !source || !target )
		return false;

	loader::binary file;
	base::writer problem;

	if ( !file.open ( source, problem ) )
		return true;

	x86::prepare ( );

	base::list < char > out;
	bool long_mode = file.arch == loader::machine::X64;
	uint64_t decoded = 0;
	uint64_t bad = 0;
	uint64_t bytes = 0;
	double started = base::seconds ( );
	char utf8 [ 600 ];
	wchar_t text [ 300 ];

	for ( const loader::section& item : file.sections ) {

		if ( item.type != loader::kind::CODE || !item.raw )
			continue;

		const uint8_t* code = file.data + item.offset;
		uint64_t size = base::min ( item.raw, item.size );
		uint64_t at = 0;

		bytes += size;

		while ( at < size ) {

			x86::instruction ins;
			uint64_t address = item.address + at;
			bool ok = x86::decode ( code + at, static_cast < size_t > ( size - at ), address, long_mode, ins );
			base::writer line;

			line.hex ( address ).add ( L"|" );

			if ( ok ) {

				x86::format_text ( ins, text, 300, nullptr, x86::ALWAYS_SIZE | x86::PLAIN_RIP | x86::SIGNED_IMM );
				line.add ( text );
				at += ins.length;
				decoded++;

			} else {

				line.add ( L"(bad)" );
				at++;
				bad++;

			}

			line.add ( L"\n" );

			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, line.text ( ), line.size ( ), utf8, 599, nullptr, nullptr );

			for ( int32_t i = 0; i < length; i++ )
				out.push ( utf8 [ i ] );

		}

	}

	double elapsed = base::seconds ( ) - started;
	HANDLE handle = CreateFileW ( target, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( handle != INVALID_HANDLE_VALUE ) {

		DWORD written = 0;

		WriteFile ( handle, out.data ( ), static_cast < DWORD > ( out.size ( ) ), &written, nullptr );
		CloseHandle ( handle );

	}

	base::writer summary;
	char line_out [ 400 ];

	summary.add ( L"decoded " ).number ( static_cast < int64_t > ( decoded ) ).add ( L" bad " ).number ( static_cast < int64_t > ( bad ) ).add ( L" bytes " ).number ( static_cast < int64_t > ( bytes ) ).add ( L" seconds " ).fixed ( elapsed, 4 ).add ( L"\n" );

	int32_t length = WideCharToMultiByte ( CP_UTF8, 0, summary.text ( ), summary.size ( ), line_out, 399, nullptr, nullptr );
	base::writer summary_path;

	summary_path.add ( target ).add ( L".summary" );
	handle = CreateFileW ( summary_path.text ( ), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( handle != INVALID_HANDLE_VALUE ) {

		DWORD written = 0;

		WriteFile ( handle, line_out, static_cast < DWORD > ( length ), &written, nullptr );
		CloseHandle ( handle );

	}

	return true;

}

void host::write_text ( const wchar_t* path, const base::list < char >& out ) {

	HANDLE handle = CreateFileW ( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( handle == INVALID_HANDLE_VALUE )
		return;

	DWORD written = 0;

	WriteFile ( handle, out.data ( ), static_cast < DWORD > ( out.size ( ) ), &written, nullptr );
	CloseHandle ( handle );

}

void host::append ( base::list < char >& out, const base::writer& line ) {

	char utf8 [ 4200 ];
	int32_t length = WideCharToMultiByte ( CP_UTF8, 0, line.text ( ), line.size ( ), utf8, 4199, nullptr, nullptr );

	for ( int32_t i = 0; i < length; i++ )
		out.push ( utf8 [ i ] );

	out.push ( '\n' );

}

bool host::analyze_mode ( ) {

	const wchar_t* source = argument ( L"--analyze" );
	const wchar_t* target = argument ( L"--out" );

	if ( !source || !target )
		return false;

	loader::binary file;
	base::writer problem;
	double opened = base::seconds ( );

	if ( !file.open ( source, problem ) )
		return true;

	double loaded = base::seconds ( ) - opened;
	analysis::database* db = new analysis::database ( );

	db->build ( file, nullptr );

	base::list < char > out;
	base::writer line;
	uint64_t tables = 0;

	for ( const analysis::jump_table& item : db->tables )
		tables += item.count;

	line.add ( L"load " ).fixed ( loaded * 1000.0, 1 ).add ( L" ms, analysis " ).fixed ( db->seconds * 1000.0, 1 ).add ( L" ms on " ).number ( db->threads ).add ( L" threads" );
	append ( out, line );
	line.clear ( );
	line.add ( L"code " ).grouped ( db->code_bytes ).add ( L" bytes, instructions " ).grouped ( db->decoded ).add ( L", functions " ).grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L", chunks " ).grouped ( static_cast < uint64_t > ( db->chunks.count ( ) ) );
	append ( out, line );
	line.clear ( );
	line.add ( L"refs " ).grouped ( static_cast < uint64_t > ( db->refs.count ( ) ) ).add ( L", switch tables " ).grouped ( static_cast < uint64_t > ( db->tables.count ( ) ) ).add ( L" with " ).grouped ( tables ).add ( L" cases" );
	append ( out, line );

	uint64_t covered = 0;

	for ( const analysis::code_map& map : db->maps ) {

		for ( uint64_t w = 0; w < map.words; w++ )
			covered += static_cast < uint64_t > ( __builtin_popcountll ( map.covered [ w ] ) );

	}

	line.clear ( );
	line.clear ( );
	line.add ( L"classes " ).number ( db->classes.count ( ) ).add ( L", virtual functions named " ).number ( db->virtuals.count ( ) );
	append ( out, line );

	for ( int32_t i = 0; i < db->classes.count ( ) && i < 12; i++ ) {

		const analysis::class_info& item = db->classes [ static_cast < size_t > ( i ) ];

		line.clear ( );
		line.add ( L"  " ).add ( item.name.get ( ) ).add ( L" methods " ).number ( item.methods ).add ( L" bases " ).number ( item.base_count ).add ( L" offset " ).number ( item.offset );

		for ( int32_t b = 0; b < item.base_count; b++ )
			line.add ( L" : " ).add ( db->class_bases [ static_cast < size_t > ( item.first_base + b ) ].get ( ) );

		append ( out, line );

	}

	line.clear ( );
	line.add ( L"coverage " ).fixed ( db->code_bytes ? 100.0 * static_cast < double > ( covered ) / static_cast < double > ( db->code_bytes ) : 0.0, 2 ).add ( L"%" );
	append ( out, line );

	if ( argument ( L"--functions" ) ) {

		for ( const analysis::function_info& item : db->functions ) {

			line.clear ( );
			line.hex ( item.start ).add ( L" " ).hex ( item.end ).add ( L" " ).hex ( item.flags ).add ( L" " ).number ( item.instructions ).add ( L" " ).number ( item.callers );
			append ( out, line );

		}

		for ( const analysis::jump_table& item : db->tables ) {

			line.clear ( );
			line.add ( L"table " ).hex ( item.jump ).add ( L" " ).hex ( item.table ).add ( L" " ).number ( item.count ).add ( L" " ).number ( static_cast < int64_t > ( item.type ) );
			append ( out, line );

		}

	}

	write_text ( target, out );

	return true;

}

const wchar_t* host::crash_path = nullptr;

long __stdcall host::crash_report ( EXCEPTION_POINTERS* info ) {

	if ( !crash_path || info->ExceptionRecord->ExceptionCode != 0xC0000005u )
		return EXCEPTION_CONTINUE_SEARCH;

	base::list < char > out;
	base::writer line;
	uint64_t base_address = reinterpret_cast < uint64_t > ( GetModuleHandleW ( nullptr ) );

	line.add ( L"rip " ).hex ( info->ContextRecord->Rip - base_address + 0x140000000ull );
	append ( out, line );

	uint64_t* stack = reinterpret_cast < uint64_t* > ( info->ContextRecord->Rsp );

	for ( int32_t i = 0; i < 400; i++ ) {

		uint64_t value = stack [ i ];

		if ( value > base_address && value < base_address + 0x200000 ) {

			line.clear ( );
			line.add ( L"ret " ).hex ( value - base_address + 0x140000000ull );
			append ( out, line );

		}

	}

	write_text ( crash_path, out );

	return EXCEPTION_CONTINUE_SEARCH;

}

bool host::decompile_mode ( ) {

	const wchar_t* source = argument ( L"--decompile" );
	const wchar_t* target = argument ( L"--out" );

	if ( !source || !target )
		return false;

	ui::app* app = new ui::app ( );

	crash_path = argument ( L"--crash" );
	AddVectoredExceptionHandler ( 1, crash_report );
	app->persist = false;
	app->analysis_sync = true;
	x86::prepare ( );

	if ( !app->open_file ( source ) )
		return true;

	const wchar_t* which = argument ( L"--function" );
	const wchar_t* limit_text = argument ( L"--count" );
	int32_t limit = limit_text ? static_cast < int32_t > ( parse_float ( limit_text ) ) : 12;
	base::list < char > out;
	plain_names names;
	int32_t done = 0;
	double total = 0.0;
	int32_t failed = 0;

	names.owner = app;

	const analysis::database& db = *app->db;
	int32_t total_functions = db.functions.count ( );
	base::list < int32_t > order;
	base::list < uint8_t > state;
	base::list < base::list < char > > chunks;
	bool derive = !argument ( L"--no-derive" );

	state.fill ( static_cast < size_t > ( total_functions ), 0 );
	chunks.fill ( static_cast < size_t > ( total_functions ), base::list < char > ( ) );

	auto callees = [ & ] ( int32_t index, base::list < int32_t >& out ) {

		const analysis::function_info& item = db.functions [ static_cast < size_t > ( index ) ];
		uint64_t at = item.start;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.is_start ( at ) || !db.decode ( at, ins ) ) {

				at++;

				continue;

			}

			at += ins.length;

			if ( ( ins.control != x86::flow::CALL && ins.control != x86::flow::JUMP ) || !ins.direct ( ) )
				continue;

			uint64_t target = ins.target ( );

			if ( target >= item.start && target < item.end )
				continue;

			int32_t callee = db.function_starting ( target );

			if ( callee >= 0 && callee != index )
				out.push ( callee );

		}

	};

	for ( int32_t root = 0; root < total_functions && derive; root++ ) {

		if ( state [ static_cast < size_t > ( root ) ] )
			continue;

		base::list < int32_t > stack;
		base::list < int32_t > pending;
		base::list < int32_t > cursor;

		stack.push ( root );
		cursor.push ( 0 );
		state [ static_cast < size_t > ( root ) ] = 1;
		pending.clear ( );
		callees ( root, pending );

		base::list < base::list < int32_t > > lists;

		lists.push ( pending );

		while ( stack.count ( ) ) {

			size_t top = static_cast < size_t > ( stack.count ( ) - 1 );
			base::list < int32_t >& next = lists [ top ];
			int32_t& position = cursor [ top ];

			if ( position < next.count ( ) ) {

				int32_t callee = next [ static_cast < size_t > ( position ) ];

				position++;

				if ( state [ static_cast < size_t > ( callee ) ] )
					continue;

				state [ static_cast < size_t > ( callee ) ] = 1;

				base::list < int32_t > found;

				callees ( callee, found );
				stack.push ( callee );
				cursor.push ( 0 );
				lists.push ( found );

				continue;

			}

			order.push ( stack.back ( ) );
			state [ static_cast < size_t > ( stack.back ( ) ) ] = 2;
			stack.remove_at ( top );
			cursor.remove_at ( top );
			lists.remove_at ( top );

		}

	}

	if ( !derive ) {

		for ( int32_t i = 0; i < total_functions; i++ )
			order.push ( i );

	}

	for ( int32_t step = 0; step < order.count ( ) && done < limit; step++ ) {

		int32_t i = order [ static_cast < size_t > ( step ) ];
		const analysis::function_info& item = app->db->functions [ static_cast < size_t > ( i ) ];
		wchar_t name [ 200 ];

		app->function_name ( i, name, 200 );

		if ( which && !base::same_text ( which, L"all" ) && !base::same_text ( which, name ) )
			continue;

		if ( item.flags & analysis::THUNK )
			continue;

		decompiler::result code;

		if ( argument ( L"--trace" ) ) {

			base::list < char > note;

			append ( note, base::writer ( ).hex ( item.start ) );
			write_text ( argument ( L"--trace" ), note );

		}

		decompiler::decompile ( *app->db, item.start, names, code );
		total += code.seconds;
		done++;

		if ( !code.ok ) {

			failed++;

			continue;

		}

		if ( derive )
			names.derive ( item.start, code );

		if ( limit > 200 && !which )
			continue;

		base::list < char >& chunk = chunks [ static_cast < size_t > ( i ) ];
		base::writer header;

		header.add ( L"//----- (" ).hex ( item.start, 16 ).add ( L")----------------------------------------------------" );
		append ( chunk, header );

		for ( const decompiler::line& line : code.lines ) {

			base::writer text;

			for ( int32_t k = 0; k < line.indent; k++ )
				text.add ( L"  " );

			for ( int32_t k = 0; k < line.length && k < 1800; k++ )
				text.add ( code.text [ static_cast < size_t > ( line.text + k ) ] );

			append ( chunk, text );

		}

		append ( chunk, base::writer ( ) );

	}

	for ( const base::list < char >& chunk : chunks ) {

		for ( char c : chunk )
			out.push ( c );

	}

	base::writer summary;

	summary.add ( L"// " ).number ( done ).add ( L" functions, " ).number ( failed ).add ( L" failed, " ).fixed ( total * 1000.0, 1 ).add ( L" ms" );
	append ( out, summary );
	write_text ( target, out );

	return true;

}

bool host::assemble_mode ( ) {

	const wchar_t* source = argument ( L"--assemble" );
	const wchar_t* target = argument ( L"--out" );

	if ( !source || !target )
		return false;

	x86::prepare ( );

	base::list < char > input;
	base::list < char > out;

	if ( !files::read ( source, input ) )
		return true;

	input.push ( 0 );

	const char* cursor = input.data ( );

	while ( *cursor ) {

		wchar_t line [ 256 ];
		int32_t length = 0;

		while ( *cursor && *cursor != '\n' && length < 255 ) {

			if ( *cursor != '\r' )
				line [ length++ ] = static_cast < wchar_t > ( static_cast < uint8_t > ( *cursor ) );

			cursor++;

		}

		if ( *cursor )
			cursor++;

		line [ length ] = 0;

		if ( !length )
			continue;

		uint8_t bytes [ 16 ];
		wchar_t problem [ 200 ];
		wchar_t text [ 300 ];
		uint64_t address = 0x140001000;
		int32_t made = x86::assemble ( line, address, true, nullptr, bytes, 16, problem, 200 );
		base::writer result;

		result.add ( line ).add ( L"  |  " );

		if ( made < 0 )
			result.add ( L"ERROR " ).add ( problem );
		else {

			x86::instruction ins;

			for ( int32_t i = 0; i < made; i++ )
				result.hex ( bytes [ i ], 2 ).add ( L" " );

			if ( x86::decode ( bytes, static_cast < size_t > ( made ), address, true, ins ) ) {

				x86::format_text ( ins, text, 300, nullptr, x86::PLAIN_RIP );
				result.add ( L" =>  " ).add ( text );

			}

		}

		append ( out, result );

	}

	write_text ( target, out );

	return true;

}

bool host::psc_mode ( ) {

	const wchar_t* script_path = argument ( L"--psc" );
	const wchar_t* target = argument ( L"--out" );
	const wchar_t* binary = argument ( L"--open" );

	if ( !script_path || !target )
		return false;

	ui::app* app = new ui::app ( );
	base::list < wchar_t > captured;
	base::list < char > out;

	app->persist = false;
	app->settings_frozen = true;
	app->analysis_sync = true;
	app->script_jit = !argument ( L"--no-jit" );

	if ( argument ( L"--library" ) )
		app->library_path.set ( argument ( L"--library" ) );
	x86::prepare ( );

	if ( binary && !app->open_file ( binary ) )
		return true;

	app->run_script_file ( script_path, &captured );

	int32_t length = WideCharToMultiByte ( CP_UTF8, 0, captured.data ( ), captured.count ( ), nullptr, 0, nullptr, nullptr );

	if ( length > 0 ) {

		out.resize ( static_cast < size_t > ( length ) );
		WideCharToMultiByte ( CP_UTF8, 0, captured.data ( ), captured.count ( ), out.data ( ), length, nullptr, nullptr );

	}

	write_text ( target, out );

	return true;

}

bool host::runtime_keys_mode ( ) {

	const wchar_t* target = argument ( L"--runtime-keys" );

	if ( !target )
		return false;

	base::list < harvested_key > found;
	x86::prepare ( );

	for ( int32_t a = 1; a < argument_count; a++ ) {

		const wchar_t* path = arguments [ a ];
		int32_t length = 0;

		while ( path [ length ] )
			length++;

		if ( length < 5 || path [ length - 4 ] != L'.' || ( base::fold_case ( path [ length - 3 ] ) != L'e' && base::fold_case ( path [ length - 3 ] ) != L'd' ) )
			continue;

		ui::app* named = new ui::app ( );

		named->persist = false;
		named->settings_frozen = true;
		named->analysis_sync = true;

		if ( !named->open_file ( path ) || !named->db ) {

			delete named;

			continue;

		}

		base::list < ui::user_name > by_address;

		for ( int32_t i = 0; i < named->db->functions.count ( ); i++ ) {

			wchar_t name [ 400 ];

			if ( !named->meaningful_name ( i, name, 400 ) )
				continue;

			ui::user_name& item = by_address.emplace ( );

			item.address = named->db->functions [ static_cast < size_t > ( i ) ].start;
			item.text.set ( name );

		}

		named->close_file ( );
		delete named;

		ui::app* app = new ui::app ( );

		app->persist = false;
		app->settings_frozen = true;
		app->analysis_sync = true;
		app->load_pdb = false;

		if ( !app->open_file ( path ) || !app->db ) {

			delete app;

			continue;

		}

		for ( int32_t i = 0; i < app->db->functions.count ( ); i++ ) {

			wchar_t name [ 400 ];
			uint64_t key = 0;
			uint64_t start = app->db->functions [ static_cast < size_t > ( i ) ].start;
			const ui::user_name* match = nullptr;
			int32_t low = 0;
			int32_t high = by_address.count ( ) - 1;

			while ( low <= high && !match ) {

				int32_t middle = ( low + high ) / 2;
				uint64_t value = by_address [ static_cast < size_t > ( middle ) ].address;

				if ( value == start )
					match = &by_address [ static_cast < size_t > ( middle ) ];
				else if ( value < start )
					low = middle + 1;
				else
					high = middle - 1;

			}

			if ( !match || app->db->functions [ static_cast < size_t > ( i ) ].name >= 0 || !analysis::function_key ( *app->db, i, key ) )
				continue;

			int32_t k = 0;

			for ( const wchar_t* at = match->text.get ( ); *at && k < 399; at++ )
				name [ k++ ] = *at;

			name [ k ] = 0;

			if ( base::same_text ( name, L"main" ) || base::same_text ( name, L"wmain" ) || ( name [ 0 ] == L'c' && name [ 1 ] == L'o' && name [ 2 ] == L'r' && name [ 3 ] == L'p' && name [ 4 ] == L'u' && name [ 5 ] == L's' ) )
				continue;

			bool known = false;

			for ( harvested_key& item : found ) {

				if ( item.key != key )
					continue;

				known = true;

				if ( !base::same_text ( item.name.get ( ), name ) )
					item.clash = true;

			}

			if ( !known ) {

				harvested_key& item = found.emplace ( );

				item.key = key;
				item.name.set ( name );

			}

		}

		app->close_file ( );
		delete app;

	}

	base::list < char > out;

	for ( const harvested_key& item : found ) {

		if ( item.clash )
			continue;

		base::writer line;

		line.hex ( item.key, 16 ).add ( L" " ).add ( item.name.get ( ) );
		append ( out, line );

	}

	write_text ( target, out );

	return true;

}

bool host::debug_main ( ) {

	arguments = CommandLineToArgvW ( GetCommandLineW ( ), &argument_count );

	if ( !arguments || argument_count < 2 )
		return false;

	if ( runtime_keys_mode ( ) )
		return true;

	if ( psc_mode ( ) )
		return true;

	if ( disasm_mode ( ) )
		return true;

	if ( analyze_mode ( ) )
		return true;

	if ( decompile_mode ( ) )
		return true;

	if ( assemble_mode ( ) )
		return true;

	return capture_mode ( );

}