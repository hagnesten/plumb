#include "script/lexer/lexer.h"
#include "base/strings/strings.h"

bool script::letter ( wchar_t c ) {

	return ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || c == L'_';

}

bool script::digit ( wchar_t c ) {

	return c >= L'0' && c <= L'9';

}

const script::symbol_spelling script::spellings [ ] = {
	{ L"<<=", symbol::SHIFT_LEFT_ASSIGN }, { L">>=", symbol::SHIFT_RIGHT_ASSIGN },
	{ L"<<", symbol::SHIFT_LEFT }, { L">>", symbol::SHIFT_RIGHT }, { L"<=", symbol::LESS_EQUAL }, { L">=", symbol::GREATER_EQUAL },
	{ L"==", symbol::EQUAL_EQUAL }, { L"!=", symbol::BANG_EQUAL }, { L"&&", symbol::AND_AND }, { L"||", symbol::PIPE_PIPE },
	{ L"+=", symbol::PLUS_ASSIGN }, { L"-=", symbol::MINUS_ASSIGN }, { L"*=", symbol::STAR_ASSIGN }, { L"/=", symbol::SLASH_ASSIGN },
	{ L"%=", symbol::PERCENT_ASSIGN }, { L"&=", symbol::AND_ASSIGN }, { L"|=", symbol::OR_ASSIGN }, { L"^=", symbol::XOR_ASSIGN },
	{ L"++", symbol::PLUS_PLUS }, { L"--", symbol::MINUS_MINUS },
	{ L"(", symbol::OPEN_PAREN }, { L")", symbol::CLOSE_PAREN }, { L"{", symbol::OPEN_BRACE }, { L"}", symbol::CLOSE_BRACE },
	{ L"[", symbol::OPEN_BRACKET }, { L"]", symbol::CLOSE_BRACKET }, { L",", symbol::COMMA }, { L";", symbol::SEMICOLON },
	{ L"?", symbol::QUESTION }, { L":", symbol::COLON }, { L"+", symbol::PLUS }, { L"-", symbol::MINUS }, { L"*", symbol::STAR },
	{ L"/", symbol::SLASH }, { L"%", symbol::PERCENT }, { L"&", symbol::AMPERSAND }, { L"|", symbol::PIPE }, { L"^", symbol::CARET },
	{ L"~", symbol::TILDE }, { L"!", symbol::BANG }, { L"<", symbol::LESS }, { L">", symbol::GREATER }, { L"=", symbol::ASSIGN }
};

int32_t script::engine::store ( const wchar_t* chars, int32_t length ) {

	if ( length < 0 ) {

		length = 0;

		while ( chars [ length ] )
			length++;

	}

	if ( text_chars + length > 64ll * 1024 * 1024 )
		return -1;

	base::list < wchar_t >& slot = texts.emplace ( );

	slot.resize ( static_cast < size_t > ( length + 1 ) );

	if ( length )
		__builtin_memcpy ( slot.data ( ), chars, static_cast < size_t > ( length ) * sizeof ( wchar_t ) );

	slot [ static_cast < size_t > ( length ) ] = 0;
	text_chars += length;

	return texts.count ( ) - 1;

}

int32_t script::engine::intern ( const wchar_t* chars, int32_t length ) {

	for ( int32_t index : interned ) {

		const base::list < wchar_t >& known = texts [ static_cast < size_t > ( index ) ];

		if ( known.count ( ) != length + 1 )
			continue;

		bool same = true;

		for ( int32_t i = 0; i < length && same; i++ )
			same = known [ static_cast < size_t > ( i ) ] == chars [ i ];

		if ( same )
			return index;

	}

	int32_t made = store ( chars, length );

	if ( made >= 0 )
		interned.push ( made );

	return made;

}

bool script::engine::lex_error ( int32_t line, int32_t column, int32_t file, const wchar_t* message ) {

	error.set = true;
	error.line = line;
	error.column = column;

	const wchar_t* path = file >= 0 && file < files.count ( ) ? files [ static_cast < size_t > ( file ) ].get ( ) : L"";
	int32_t k = 0;

	for ( ; path [ k ] && k < 259; k++ )
		error.file [ k ] = path [ k ];

	error.file [ k ] = 0;
	k = 0;

	for ( ; message [ k ] && k < 299; k++ )
		error.message [ k ] = message [ k ];

	error.message [ k ] = 0;

	return false;

}

bool script::engine::lex ( const wchar_t* source, int32_t file, int32_t depth ) {

	int32_t line = 1;
	int32_t column = 1;
	const wchar_t* at = source;
	base::list < wchar_t > scratch;

	auto advance = [ & ] ( ) {

		if ( *at == L'\n' ) {

			line++;
			column = 1;

		} else
			column++;

		at++;

	};

	while ( *at ) {

		wchar_t c = *at;

		if ( c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xFEFF ) {

			advance ( );

			continue;

		}

		if ( c == L'/' && at [ 1 ] == L'/' ) {

			while ( *at && *at != L'\n' )
				advance ( );

			continue;

		}

		if ( c == L'/' && at [ 1 ] == L'*' ) {

			int32_t open_line = line;
			int32_t open_column = column;

			advance ( );
			advance ( );

			while ( *at && !( *at == L'*' && at [ 1 ] == L'/' ) )
				advance ( );

			if ( !*at )
				return lex_error ( open_line, open_column, file, L"This comment is never closed with */." );

			advance ( );
			advance ( );

			continue;

		}

		token item;

		item.line = line;
		item.column = column;
		item.file = file;

		if ( c == L'#' ) {

			advance ( );

			const wchar_t* word = at;

			while ( letter ( *at ) )
				advance ( );

			int32_t word_length = static_cast < int32_t > ( at - word );
			bool include = word_length == 7 && word [ 0 ] == L'i' && word [ 1 ] == L'n' && word [ 2 ] == L'c' && word [ 3 ] == L'l' && word [ 4 ] == L'u' && word [ 5 ] == L'd' && word [ 6 ] == L'e';

			if ( !include )
				return lex_error ( item.line, item.column, file, L"The only directive is #include \"file.psc\"." );

			while ( *at == L' ' || *at == L'\t' )
				advance ( );

			if ( *at != L'"' )
				return lex_error ( line, column, file, L"#include needs a file name in quotes." );

			advance ( );

			wchar_t path [ 520 ];
			int32_t length = 0;

			while ( *at && *at != L'"' && *at != L'\n' && length < 519 ) {

				path [ length++ ] = *at;
				advance ( );

			}

			path [ length ] = 0;

			if ( *at != L'"' )
				return lex_error ( line, column, file, L"The include file name is never closed with a quote." );

			advance ( );

			if ( depth >= 16 )
				return lex_error ( item.line, item.column, file, L"Includes are nested more than 16 deep." );

			base::list < wchar_t > included;
			base::writer resolved;

			if ( !current || !current->read_file ( path, files [ static_cast < size_t > ( file ) ].get ( ), included, resolved ) ) {

				base::writer message;

				message.add ( L"Couldn't read the include file " ).add ( path ).add ( L"." );

				return lex_error ( item.line, item.column, file, message.text ( ) );

			}

			bool seen = false;

			for ( const base::text& known : files )
				seen = seen || base::same_text ( known.get ( ), resolved.text ( ) );

			if ( seen )
				continue;

			files.emplace ( ).set ( resolved.text ( ) );
			included.push ( 0 );

			if ( !lex ( included.data ( ), files.count ( ) - 1, depth + 1 ) )
				return false;

			continue;

		}

		if ( letter ( c ) ) {

			const wchar_t* start = at;

			while ( letter ( *at ) || digit ( *at ) )
				advance ( );

			item.type = token_kind::NAME;
			item.text = intern ( start, static_cast < int32_t > ( at - start ) );

			if ( item.text < 0 )
				return lex_error ( item.line, item.column, file, L"The script is too large." );

			tokens.push ( item );

			continue;

		}

		if ( digit ( c ) ) {

			uint64_t number = 0;

			item.type = token_kind::NUMBER;

			if ( c == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' ) ) {

				advance ( );
				advance ( );

				if ( base::hex_value ( *at ) < 0 )
					return lex_error ( item.line, item.column, file, L"A hex number needs digits after 0x." );

				while ( base::hex_value ( *at ) >= 0 || *at == L'_' ) {

					if ( *at != L'_' )
						number = ( number << 4 ) | static_cast < uint64_t > ( base::hex_value ( *at ) );

					advance ( );

				}

			} else if ( c == L'0' && ( at [ 1 ] == L'b' || at [ 1 ] == L'B' ) ) {

				advance ( );
				advance ( );

				if ( *at != L'0' && *at != L'1' )
					return lex_error ( item.line, item.column, file, L"A binary number needs digits after 0b." );

				while ( *at == L'0' || *at == L'1' || *at == L'_' ) {

					if ( *at != L'_' )
						number = ( number << 1 ) | static_cast < uint64_t > ( *at - L'0' );

					advance ( );

				}

			} else {

				while ( digit ( *at ) || *at == L'_' ) {

					if ( *at != L'_' )
						number = number * 10 + static_cast < uint64_t > ( *at - L'0' );

					advance ( );

				}

			}

			if ( letter ( *at ) )
				return lex_error ( line, column, file, L"Unexpected letter right after a number." );

			item.number = static_cast < int64_t > ( number );
			tokens.push ( item );

			continue;

		}

		if ( c == L'"' || c == L'\'' ) {

			wchar_t quote = c;

			advance ( );
			scratch.clear ( );

			while ( *at && *at != quote && *at != L'\n' ) {

				wchar_t ch = *at;

				if ( ch != L'\\' ) {

					scratch.push ( ch );
					advance ( );

					continue;

				}

				advance ( );

				wchar_t e = *at;

				if ( !e )
					break;

				advance ( );

				switch ( e ) {

					case L'n':
						scratch.push ( L'\n' );
						break;

					case L'r':
						scratch.push ( L'\r' );
						break;

					case L't':
						scratch.push ( L'\t' );
						break;

					case L'0':
						scratch.push ( 0 );
						break;

					case L'x':
					case L'u': {

						int32_t wanted = e == L'x' ? 2 : 4;
						int32_t code = 0;

						for ( int32_t k = 0; k < wanted; k++ ) {

							if ( base::hex_value ( *at ) < 0 )
								return lex_error ( line, column, file, e == L'x' ? L"\\x needs two hex digits." : L"\\u needs four hex digits." );

							code = code * 16 + base::hex_value ( *at );
							advance ( );

						}

						scratch.push ( static_cast < wchar_t > ( code ) );

						break;

					}

					default:
						scratch.push ( e );
						break;

				}

			}

			if ( *at != quote )
				return lex_error ( item.line, item.column, file, quote == L'"' ? L"This string is never closed." : L"This character constant is never closed." );

			advance ( );

			if ( quote == L'\'' ) {

				if ( scratch.empty ( ) || scratch.count ( ) > 8 )
					return lex_error ( item.line, item.column, file, L"A character constant holds one to eight characters." );

				uint64_t number = 0;

				for ( wchar_t ch : scratch )
					number = ( number << 8 ) | static_cast < uint64_t > ( ch & 0xFF );

				item.type = token_kind::NUMBER;
				item.number = static_cast < int64_t > ( number );

			} else {

				item.type = token_kind::TEXT;
				item.text = store ( scratch.data ( ), scratch.count ( ) );

				if ( item.text < 0 )
					return lex_error ( item.line, item.column, file, L"The script is too large." );

			}

			tokens.push ( item );

			continue;

		}

		bool matched = false;

		for ( const symbol_spelling& spelling : spellings ) {

			int32_t k = 0;

			while ( spelling.text [ k ] && at [ k ] == spelling.text [ k ] )
				k++;

			if ( spelling.text [ k ] )
				continue;

			for ( int32_t i = 0; i < k; i++ )
				advance ( );

			item.type = token_kind::SYMBOL;
			item.mark = spelling.mark;
			tokens.push ( item );
			matched = true;

			break;

		}

		if ( !matched ) {

			base::writer message;

			message.add ( L"Unexpected character '" ).add ( c ).add ( L"'." );

			return lex_error ( item.line, item.column, file, message.text ( ) );

		}

	}

	if ( depth == 0 ) {

		token end;

		end.line = line;
		end.column = column;
		end.file = file;
		tokens.push ( end );

	}

	return true;

}