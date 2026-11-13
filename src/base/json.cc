#include "base/json.hh"

namespace json {

	struct reader {

		const char* at;
		const char* end;
		int32_t depth = 0;

		void skip ( ) {

			while ( at < end && ( *at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' ) )
				at++;

		}

		bool literal ( const char* word ) {

			const char* p = at;

			for ( ; *word; word++, p++ ) {

				if ( p >= end || *p != *word )
					return false;

			}

			at = p;

			return true;

		}

		static void put_utf8 ( base::list < char >& out, uint32_t code ) {

			if ( code < 0x80 )
				out.push ( static_cast < char > ( code ) );
			else if ( code < 0x800 ) {

				out.push ( static_cast < char > ( 0xC0 | ( code >> 6 ) ) );
				out.push ( static_cast < char > ( 0x80 | ( code & 0x3F ) ) );

			} else if ( code < 0x10000 ) {

				out.push ( static_cast < char > ( 0xE0 | ( code >> 12 ) ) );
				out.push ( static_cast < char > ( 0x80 | ( ( code >> 6 ) & 0x3F ) ) );
				out.push ( static_cast < char > ( 0x80 | ( code & 0x3F ) ) );

			} else {

				out.push ( static_cast < char > ( 0xF0 | ( code >> 18 ) ) );
				out.push ( static_cast < char > ( 0x80 | ( ( code >> 12 ) & 0x3F ) ) );
				out.push ( static_cast < char > ( 0x80 | ( ( code >> 6 ) & 0x3F ) ) );
				out.push ( static_cast < char > ( 0x80 | ( code & 0x3F ) ) );

			}

		}

		bool hex4 ( uint32_t& code ) {

			code = 0;

			for ( int32_t i = 0; i < 4; i++ ) {

				if ( at >= end )
					return false;

				char c = *at++;
				uint32_t digit = 0;

				if ( c >= '0' && c <= '9' )
					digit = static_cast < uint32_t > ( c - '0' );
				else if ( c >= 'a' && c <= 'f' )
					digit = static_cast < uint32_t > ( c - 'a' + 10 );
				else if ( c >= 'A' && c <= 'F' )
					digit = static_cast < uint32_t > ( c - 'A' + 10 );
				else
					return false;

				code = code * 16 + digit;

			}

			return true;

		}

		bool string ( base::list < char >& out ) {

			if ( at >= end || *at != '"' )
				return false;

			at++;
			out.clear ( );

			while ( at < end && *at != '"' ) {

				char c = *at++;

				if ( c != '\\' ) {

					out.push ( c );

					continue;

				}

				if ( at >= end )
					return false;

				char escape = *at++;
				uint32_t code = 0;

				switch ( escape ) {

					case 'n': out.push ( '\n' ); break;
					case 't': out.push ( '\t' ); break;
					case 'r': out.push ( '\r' ); break;
					case 'b': out.push ( '\b' ); break;
					case 'f': out.push ( '\f' ); break;

					case 'u':

						if ( !hex4 ( code ) )
							return false;

						if ( code >= 0xD800 && code < 0xDC00 && at + 6 <= end && at [ 0 ] == '\\' && at [ 1 ] == 'u' ) {

							uint32_t low = 0;

							at += 2;

							if ( !hex4 ( low ) )
								return false;

							code = 0x10000 + ( ( code - 0xD800 ) << 10 ) + ( low - 0xDC00 );

						}

						put_utf8 ( out, code );
						break;

					default:
						out.push ( escape );
						break;

				}

			}

			if ( at >= end )
				return false;

			at++;
			out.push ( 0 );

			return true;

		}

		bool parse ( value& out ) {

			skip ( );

			if ( at >= end || ++depth > 64 )
				return false;

			bool ok = true;

			if ( *at == '{' ) {

				out.kind = type::OBJECT;
				at++;
				skip ( );

				if ( at < end && *at == '}' )
					at++;
				else {

					while ( ok ) {

						skip ( );

						base::list < char >& name = out.keys.emplace ( );

						if ( !string ( name ) )
							return false;

						skip ( );

						if ( at >= end || *at != ':' )
							return false;

						at++;

						if ( !parse ( out.items.emplace ( ) ) )
							return false;

						skip ( );

						if ( at < end && *at == ',' ) {

							at++;

							continue;

						}

						if ( at < end && *at == '}' ) {

							at++;

							break;

						}

						ok = false;

					}

				}

			} else if ( *at == '[' ) {

				out.kind = type::ARRAY;
				at++;
				skip ( );

				if ( at < end && *at == ']' )
					at++;
				else {

					while ( ok ) {

						if ( !parse ( out.items.emplace ( ) ) )
							return false;

						skip ( );

						if ( at < end && *at == ',' ) {

							at++;

							continue;

						}

						if ( at < end && *at == ']' ) {

							at++;

							break;

						}

						ok = false;

					}

				}

			} else if ( *at == '"' ) {

				out.kind = type::STRING;
				ok = string ( out.text );

			} else if ( literal ( "true" ) ) {

				out.kind = type::BOOLEAN;
				out.flag = true;

			} else if ( literal ( "false" ) )
				out.kind = type::BOOLEAN;
			else if ( literal ( "null" ) )
				out.kind = type::NONE;
			else if ( *at == '-' || ( *at >= '0' && *at <= '9' ) ) {

				bool negative = *at == '-';
				double whole = 0.0;
				double scale = 0.0;

				if ( negative )
					at++;

				while ( at < end && ( ( *at >= '0' && *at <= '9' ) || *at == '.' ) ) {

					if ( *at == '.' )
						scale = 0.1;
					else if ( scale > 0.0 ) {

						whole += static_cast < double > ( *at - '0' ) * scale;
						scale *= 0.1;

					} else
						whole = whole * 10.0 + static_cast < double > ( *at - '0' );

					at++;

				}

				if ( at < end && ( *at == 'e' || *at == 'E' ) ) {

					at++;

					bool down = at < end && *at == '-';

					if ( at < end && ( *at == '-' || *at == '+' ) )
						at++;

					int32_t power = 0;

					while ( at < end && *at >= '0' && *at <= '9' )
						power = power * 10 + ( *at++ - '0' );

					for ( int32_t i = 0; i < power && i < 400; i++ )
						whole = down ? whole / 10.0 : whole * 10.0;

				}

				out.kind = type::NUMBER;
				out.number = negative ? -whole : whole;

			} else
				ok = false;

			depth--;

			return ok;

		}

	};

	const value* value::get ( const char* key ) const {

		if ( kind != type::OBJECT )
			return nullptr;

		for ( int32_t i = 0; i < keys.count ( ) && i < items.count ( ); i++ ) {

			if ( base::same_text ( keys [ static_cast < size_t > ( i ) ].data ( ), key ) )
				return &items [ static_cast < size_t > ( i ) ];

		}

		return nullptr;

	}

	bool parse ( const char* source, size_t length, value& out ) {

		reader state;

		state.at = source;
		state.end = source + length;
		out = value ( );

		return state.parse ( out );

	}

	int32_t to_wide ( const char* utf8, wchar_t* out, int32_t room ) {

		int32_t length = 0;
		const uint8_t* at = reinterpret_cast < const uint8_t* > ( utf8 );

		while ( *at && length < room - 2 ) {

			uint32_t code = *at++;

			if ( code >= 0xF0 && at [ 0 ] && at [ 1 ] && at [ 2 ] ) {

				code = ( ( code & 0x07 ) << 18 ) | ( ( at [ 0 ] & 0x3F ) << 12 ) | ( ( at [ 1 ] & 0x3F ) << 6 ) | ( at [ 2 ] & 0x3F );
				at += 3;

			} else if ( code >= 0xE0 && at [ 0 ] && at [ 1 ] ) {

				code = ( ( code & 0x0F ) << 12 ) | ( ( at [ 0 ] & 0x3F ) << 6 ) | ( at [ 1 ] & 0x3F );
				at += 2;

			} else if ( code >= 0xC0 && at [ 0 ] ) {

				code = ( ( code & 0x1F ) << 6 ) | ( at [ 0 ] & 0x3F );
				at += 1;

			}

			if ( code >= 0x10000 ) {

				code -= 0x10000;
				out [ length++ ] = static_cast < wchar_t > ( 0xD800 + ( code >> 10 ) );
				out [ length++ ] = static_cast < wchar_t > ( 0xDC00 + ( code & 0x3FF ) );

			} else
				out [ length++ ] = static_cast < wchar_t > ( code );

		}

		out [ length ] = 0;

		return length;

	}

	void writer::separate ( ) {

		if ( after_key ) {

			after_key = false;

			return;

		}

		if ( !counts.empty ( ) ) {

			if ( counts.back ( ) )
				byte ( ',' );

			counts.back ( ) = 1;

		}

	}

	writer& writer::raw ( const char* text ) {

		separate ( );

		for ( ; *text; text++ )
			byte ( *text );

		return *this;

	}

	writer& writer::key ( const char* name ) {

		string ( name );
		byte ( ':' );
		after_key = true;

		return *this;

	}

	writer& writer::string ( const char* utf8 ) {

		separate ( );
		byte ( '"' );

		for ( const char* at = utf8; *at; at++ ) {

			uint8_t c = static_cast < uint8_t > ( *at );

			if ( c == '"' || c == '\\' ) {

				byte ( '\\' );
				byte ( static_cast < char > ( c ) );

			} else if ( c == '\n' ) {

				byte ( '\\' );
				byte ( 'n' );

			} else if ( c == '\r' ) {

				byte ( '\\' );
				byte ( 'r' );

			} else if ( c == '\t' ) {

				byte ( '\\' );
				byte ( 't' );

			} else if ( c < 0x20 ) {

				static const char digits [ ] = "0123456789abcdef";

				byte ( '\\' );
				byte ( 'u' );
				byte ( '0' );
				byte ( '0' );
				byte ( digits [ c >> 4 ] );
				byte ( digits [ c & 15 ] );

			} else
				byte ( static_cast < char > ( c ) );

		}

		byte ( '"' );

		return *this;

	}

	writer& writer::string ( const wchar_t* text ) {

		return string ( text, base::text_length ( text ) );

	}

	writer& writer::string ( const wchar_t* text, int32_t length ) {

		base::list < char > utf8;

		for ( int32_t i = 0; i < length; i++ ) {

			uint32_t code = static_cast < uint16_t > ( text [ i ] );

			if ( code >= 0xD800 && code < 0xDC00 && i + 1 < length ) {

				uint32_t low = static_cast < uint16_t > ( text [ i + 1 ] );

				if ( low >= 0xDC00 && low < 0xE000 ) {

					code = 0x10000 + ( ( code - 0xD800 ) << 10 ) + ( low - 0xDC00 );
					i++;

				}

			}

			reader::put_utf8 ( utf8, code );

		}

		utf8.push ( 0 );

		return string ( utf8.data ( ) );

	}

	writer& writer::number ( int64_t value ) {

		char digits [ 24 ];
		int32_t count = 0;
		uint64_t magnitude = value < 0 ? static_cast < uint64_t > ( -( value + 1 ) ) + 1 : static_cast < uint64_t > ( value );

		do {

			digits [ count++ ] = static_cast < char > ( '0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		separate ( );

		if ( value < 0 )
			byte ( '-' );

		while ( count )
			byte ( digits [ --count ] );

		return *this;

	}

	writer& writer::boolean ( bool value ) {

		return raw ( value ? "true" : "false" );

	}

	writer& writer::copy ( const value& item ) {

		switch ( item.kind ) {

			case type::BOOLEAN:
				return boolean ( item.flag );

			case type::NUMBER:
				return number ( static_cast < int64_t > ( item.number ) );

			case type::STRING:
				return string ( item.string ( ) );

			case type::ARRAY:

				open_array ( );

				for ( const value& child : item.items )
					copy ( child );

				return close_array ( );

			case type::OBJECT:

				open_object ( );

				for ( int32_t i = 0; i < item.keys.count ( ) && i < item.items.count ( ); i++ ) {

					key ( item.keys [ static_cast < size_t > ( i ) ].data ( ) );
					copy ( item.items [ static_cast < size_t > ( i ) ] );

				}

				return close_object ( );

			default:
				return raw ( "null" );

		}

	}

	writer& writer::open_object ( ) {

		separate ( );
		byte ( '{' );
		counts.push ( 0 );

		return *this;

	}

	writer& writer::close_object ( ) {

		byte ( '}' );
		counts.pop ( );

		return *this;

	}

	writer& writer::open_array ( ) {

		separate ( );
		byte ( '[' );
		counts.push ( 0 );

		return *this;

	}

	writer& writer::close_array ( ) {

		byte ( ']' );
		counts.pop ( );

		return *this;

	}

	const char* writer::text ( ) {

		out.push ( 0 );
		out.pop ( );

		return out.data ( );

	}

}
