#ifdef PLUMB_DEBUG

#include "ui/app.hh"
#include "x86/x86.hh"
#include "analysis/database.hh"
#include "base/files.hh"
#include <windows.h>
#include <shellapi.h>

static int32_t argument_count = 0;
static wchar_t** arguments = nullptr;

static const wchar_t* argument ( const wchar_t* name ) {

	for ( int32_t i = 1; i + 1 < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], name ) )
			return arguments [ i + 1 ];

	}

	return nullptr;

}

static float parse_float ( const wchar_t* text ) {

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

struct script_reader {

	const wchar_t* cursor;

	bool word ( wchar_t* out, int32_t room ) {

		while ( *cursor == L' ' || *cursor == L';' || *cursor == L'\n' || *cursor == L'\r' || *cursor == L'\t' )
			cursor++;

		if ( !*cursor )
			return false;

		int32_t length = 0;

		while ( *cursor && *cursor != L' ' && *cursor != L';' && *cursor != L'\n' && *cursor != L'\r' && length < room - 1 )
			out [ length++ ] = *cursor++;

		out [ length ] = 0;

		return true;

	}

	float number ( ) {

		wchar_t token [ 64 ];

		if ( !word ( token, 64 ) )
			return 0.0f;

		return parse_float ( token );

	}

};

static uint32_t key_code ( const wchar_t* name, bool& control, bool& shift, bool& alt ) {

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

	const wchar_t* names [ ] = { L"left", L"right", L"up", L"down", L"escape", L"enter", L"pagedown", L"pageup", L"home", L"end", L"f1", L"f5", L"tab", L"back", L"f2", L"f7", L"f8", L"f9", L"f12", L"space", L"backslash" };
	const uint32_t codes [ ] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_ESCAPE, VK_RETURN, VK_NEXT, VK_PRIOR, VK_HOME, VK_END, VK_F1, VK_F5, VK_TAB, VK_BACK, VK_F2, VK_F7, VK_F8, VK_F9, VK_F12, VK_SPACE, VK_OEM_5 };

	for ( int32_t i = 0; i < 21; i++ ) {

		if ( base::same_text ( name, names [ i ] ) )
			return codes [ i ];

	}

	if ( name [ 0 ] && !name [ 1 ] )
		return static_cast < uint32_t > ( name [ 0 ] >= L'a' && name [ 0 ] <= L'z' ? name [ 0 ] - 32 : name [ 0 ] );

	return 0;

}

static bool capture_mode ( ) {

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

static bool disasm_mode ( ) {

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

static void write_text ( const wchar_t* path, const base::list < char >& out ) {

	HANDLE handle = CreateFileW ( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( handle == INVALID_HANDLE_VALUE )
		return;

	DWORD written = 0;

	WriteFile ( handle, out.data ( ), static_cast < DWORD > ( out.size ( ) ), &written, nullptr );
	CloseHandle ( handle );

}

static void append ( base::list < char >& out, const base::writer& line ) {

	char utf8 [ 4200 ];
	int32_t length = WideCharToMultiByte ( CP_UTF8, 0, line.text ( ), line.size ( ), utf8, 4199, nullptr, nullptr );

	for ( int32_t i = 0; i < length; i++ )
		out.push ( utf8 [ i ] );

	out.push ( '\n' );

}

static bool analyze_mode ( ) {

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

struct plain_names : decompiler::namer {

	ui::app* owner = nullptr;

	bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

		return owner->symbol_name ( address, code, out, room );

	}

	bool literal ( uint64_t address, wchar_t* out, int32_t room ) override {

		uint64_t offset = 0;
		int32_t length = 0;
		const loader::binary& file = owner->file;

		if ( !file.offset_of ( address, offset ) )
			return false;

		for ( uint64_t i = 0; length < room - 4 && i < 200 && offset + i < file.size; i++ ) {

			uint8_t c = file.byte ( offset + i );

			if ( !c )
				break;

			if ( c < 0x20 || c > 0x7E )
				return false;

			out [ length++ ] = static_cast < wchar_t > ( c );

		}

		out [ length ] = 0;

		return length >= 2;

	}

	bool expand_ternary ( ) override {

		const wchar_t* style = argument ( L"--ternary" );

		return style && base::same_text ( style, L"if" );

	}

};

static const wchar_t* crash_path = nullptr;

static long __stdcall crash_report ( EXCEPTION_POINTERS* info ) {

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

static bool decompile_mode ( ) {

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

	for ( int32_t i = 0; i < app->db->functions.count ( ) && done < limit; i++ ) {

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

		if ( limit > 200 )
			continue;

		for ( const decompiler::line& line : code.lines ) {

			base::writer text;

			for ( int32_t k = 0; k < line.indent; k++ )
				text.add ( L"    " );

			for ( int32_t k = 0; k < line.length && k < 1800; k++ )
				text.add ( code.text [ static_cast < size_t > ( line.text + k ) ] );

			append ( out, text );

		}

		append ( out, base::writer ( ) );

	}

	base::writer summary;

	summary.add ( L"// " ).number ( done ).add ( L" functions, " ).number ( failed ).add ( L" failed, " ).fixed ( total * 1000.0, 1 ).add ( L" ms" );
	append ( out, summary );
	write_text ( target, out );

	return true;

}

static bool assemble_mode ( ) {

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

bool debug_main ( ) {

	arguments = CommandLineToArgvW ( GetCommandLineW ( ), &argument_count );

	if ( !arguments || argument_count < 2 )
		return false;

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

#endif