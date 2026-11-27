#pragma once

#include "ui/core/app/app.h"

namespace ui {

	struct query_reader {

		const wchar_t* at = nullptr;

		bool word ( wchar_t* out, int32_t room ) {

			while ( *at == L' ' )
				at++;

			if ( !*at )
				return false;

			int32_t length = 0;
			bool quoted = *at == L'"';

			if ( quoted )
				at++;

			while ( *at && ( quoted ? *at != L'"' : *at != L' ' ) && length < room - 1 )
				out [ length++ ] = *at++;

			if ( quoted && *at == L'"' )
				at++;

			out [ length ] = 0;

			return true;

		}

	};

	bool parse_number ( const wchar_t* text, uint64_t& value );

	bool compare ( const wchar_t* op, uint64_t left, uint64_t right );

	bool is_operator ( const wchar_t* op );

}