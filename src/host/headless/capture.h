#pragma once

#include "ui/core/app/app.h"
#include "decompiler/namer/namer.h"
#include "base/list/list.h"
#include "base/writer/writer.h"
#include "base/strings/strings.h"

bool debug_main ( );

const wchar_t* argument ( const wchar_t* name );

float parse_float ( const wchar_t* text );

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

	bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

		return owner->symbol_name ( address, code, out, room );

	}

	bool literal ( uint64_t address, wchar_t* out, int32_t room ) override {

		return decompiler::string_literal ( owner->file, address, out, room );

	}

	wchar_t number_form ( uint64_t address, int64_t value, int32_t occurrence ) override {

		return owner->code_number_form ( address, value, occurrence );

	}

	base::list < uint64_t > derived_at;
	base::list < int32_t > derived_offset;
	base::list < wchar_t > derived_chars;

	const wchar_t* prototype ( uint64_t function ) override {

		return owner->prototype_for ( function );

	}

	const wchar_t* derived ( uint64_t function ) override {

		for ( int32_t i = 0; i < derived_at.count ( ); i++ ) {

			if ( derived_at [ static_cast < size_t > ( i ) ] == function )
				return derived_chars.data ( ) + derived_offset [ static_cast < size_t > ( i ) ];

		}

		return nullptr;

	}

	void derive ( uint64_t function, const decompiler::result& code ) {

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

	bool number_notes ( ) override {

		return argument ( L"--numbers" ) != nullptr;

	}

	bool expand_ternary ( ) override {

		const wchar_t* style = argument ( L"--ternary" );

		return style && base::same_text ( style, L"if" );

	}

};