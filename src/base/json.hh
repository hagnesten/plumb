#pragma once

#include "base/base.hh"

namespace json {

	enum class type : uint8_t {

		NONE,
		BOOLEAN,
		NUMBER,
		STRING,
		ARRAY,
		OBJECT,

	};

	struct value {

		type kind = type::NONE;
		bool flag = false;
		double number = 0.0;
		base::list < char > text;
		base::list < value > items;
		base::list < base::list < char > > keys;

		const value* get ( const char* key ) const;

		const char* string ( ) const {

			return kind == type::STRING && !text.empty ( ) ? text.data ( ) : "";

		}

	};

	bool parse ( const char* source, size_t length, value& out );

	int32_t to_wide ( const char* utf8, wchar_t* out, int32_t room );

	class writer {

	public:

		writer& raw ( const char* text );

		writer& key ( const char* name );

		writer& string ( const char* utf8 );

		writer& string ( const wchar_t* text );

		writer& string ( const wchar_t* text, int32_t length );

		writer& number ( int64_t value );

		writer& boolean ( bool value );

		writer& copy ( const value& item );

		writer& open_object ( );

		writer& close_object ( );

		writer& open_array ( );

		writer& close_array ( );

		const char* text ( );

		int32_t size ( ) const {

			return out.count ( );

		}

	private:

		void separate ( );

		void byte ( char value ) {

			out.push ( value );

		}

		base::list < char > out;
		base::list < uint8_t > counts;
		bool after_key = false;

	};

}
