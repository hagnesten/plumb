#pragma once

#include "base/list/list.h"
#include "base/text/text.h"
#include "base/writer/writer.h"

namespace decompiler {

	struct c_type {

		uint8_t size = 0;
		uint8_t bits = 0;
		bool pointer = false;
		bool empty = false;
		bool custom = false;
		int32_t stars = 0;
		wchar_t pointee [ 48 ] = { };
		wchar_t custom_name [ 48 ] = { };

	};

	struct c_prototype {

		wchar_t result [ 64 ] = { };
		wchar_t name [ 64 ] = { };
		c_type result_type;
		wchar_t types [ 16 ] [ 64 ] = { };
		wchar_t names [ 16 ] [ 64 ] = { };
		c_type params [ 16 ];
		int32_t count = 0;
		bool variadic = false;

	};
	bool parse_type ( const wchar_t* text, bool long_mode, c_type& out, wchar_t* clean, int32_t room );
	bool parse_prototype ( const wchar_t* text, bool long_mode, c_prototype& out, base::writer& problem );

	struct named_type {

		const wchar_t* name;
		uint8_t size;
		uint8_t bits;

	};

	extern const named_type known_types [ ];

	extern const wchar_t* pointer_types [ ];

	extern const wchar_t* handle_types [ ];

	extern const wchar_t* ignored_words [ ];

	extern const wchar_t* calling_words [ ];

	struct token_list {

		wchar_t text [ 24 ] [ 64 ];
		int32_t count = 0;

	};

	bool same ( const wchar_t* a, const wchar_t* b );

	bool identifier_char ( wchar_t c );

	void split ( const wchar_t* text, int32_t length, token_list& out );

	template < int32_t count >
	static bool in_list ( const wchar_t* word, const wchar_t* const ( &list ) [ count ] ) {

		for ( int32_t i = 0; i < count; i++ ) {

			if ( same ( word, list [ i ] ) )
				return true;

		}

		return false;

	}

	bool is_type_word ( const wchar_t* word );

	bool parse_tokens ( const token_list& tokens, int32_t from, int32_t to, bool long_mode, c_type& out, wchar_t* text, int32_t room );

}