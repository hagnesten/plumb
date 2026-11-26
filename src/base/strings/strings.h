#pragma once

#include "base/types/types.h"

namespace base {

	inline int32_t text_length ( const wchar_t* text ) {

		int32_t length = 0;

		while ( text && text [ length ] )
			length++;

		return length;

	}

	inline int32_t text_length ( const char* text ) {

		int32_t length = 0;

		while ( text && text [ length ] )
			length++;

		return length;

	}

	inline bool same_text ( const char* a, const char* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	inline wchar_t fold_case ( wchar_t value ) {

		return value >= L'A' && value <= L'Z' ? static_cast < wchar_t > ( value + 32 ) : value;

	}

	inline int32_t compare_folded ( const wchar_t* a, const wchar_t* b ) {

		while ( *a && fold_case ( *a ) == fold_case ( *b ) ) {

			a++;
			b++;

		}

		return static_cast < int32_t > ( fold_case ( *a ) ) - static_cast < int32_t > ( fold_case ( *b ) );

	}

	inline bool contains_folded ( const wchar_t* haystack, const wchar_t* needle ) {

		if ( !needle [ 0 ] )
			return true;

		for ( int32_t i = 0; haystack [ i ]; i++ ) {

			int32_t k = 0;

			while ( needle [ k ] && haystack [ i + k ] && fold_case ( haystack [ i + k ] ) == fold_case ( needle [ k ] ) )
				k++;

			if ( !needle [ k ] )
				return true;

		}

		return false;

	}

	inline bool same_text ( const wchar_t* a, const wchar_t* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	inline bool is_identifier ( const wchar_t* text ) {

		if ( !text [ 0 ] || ( text [ 0 ] >= L'0' && text [ 0 ] <= L'9' ) )
			return false;

		for ( const wchar_t* at = text; *at; at++ ) {

			wchar_t c = *at;

			if ( !( ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L'_' ) )
				return false;

		}

		return true;

	}

	inline bool parse_hex ( const wchar_t* text, uint64_t& value ) {

		value = 0;

		if ( text [ 0 ] == L'#' )
			text++;
		else if ( text [ 0 ] == L'0' && ( text [ 1 ] == L'x' || text [ 1 ] == L'X' ) )
			text += 2;

		int32_t digits = 0;

		for ( ; *text; text++, digits++ ) {

			wchar_t c = fold_case ( *text );

			if ( digits >= 16 || !( ( c >= L'0' && c <= L'9' ) || ( c >= L'a' && c <= L'f' ) ) )
				return false;

			value = ( value << 4 ) | static_cast < uint64_t > ( c <= L'9' ? c - L'0' : c - L'a' + 10 );

		}

		return digits > 0;

	}

}