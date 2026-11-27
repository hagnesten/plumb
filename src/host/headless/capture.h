#pragma once

#include "ui/core/app/app.h"
#include "decompiler/namer/namer.h"
#include "base/list/list.h"
#include "base/writer/writer.h"
#include "base/strings/strings.h"

struct _EXCEPTION_POINTERS;

namespace host {

	bool debug_main ( );

	const wchar_t* argument ( const wchar_t* name );

	float parse_float ( const wchar_t* text );

	extern int32_t argument_count;

	extern wchar_t** arguments;

	struct harvested_key {

		uint64_t key = 0;
		base::text name;
		bool clash = false;

	};

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

	struct plain_names : decompiler::namer {

		ui::app* owner = nullptr;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override;

		bool literal ( uint64_t address, wchar_t* out, int32_t room ) override;

		wchar_t number_form ( uint64_t address, int64_t value, int32_t occurrence ) override;

		base::list < uint64_t > derived_at;
		base::list < int32_t > derived_offset;
		base::list < wchar_t > derived_chars;

		const wchar_t* prototype ( uint64_t function ) override;

		const wchar_t* derived ( uint64_t function ) override;

		const wchar_t* role ( uint64_t function ) override;

		void derive ( uint64_t function, const decompiler::result& code );

		bool number_notes ( ) override;

		bool expand_ternary ( ) override;

	};

	uint32_t key_code ( const wchar_t* name, bool& control, bool& shift, bool& alt );

	bool capture_mode ( );

	bool disasm_mode ( );

	void write_text ( const wchar_t* path, const base::list < char >& out );

	void append ( base::list < char >& out, const base::writer& line );

	bool analyze_mode ( );

	extern const wchar_t* crash_path;

	long __stdcall crash_report ( _EXCEPTION_POINTERS* info );

	bool decompile_mode ( );

	bool assemble_mode ( );

	bool psc_mode ( );

	bool runtime_keys_mode ( );

}