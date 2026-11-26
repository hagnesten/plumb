#include "script/builtins/builtins.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"
#include "base/strings/strings.h"

void script::put ( base::list < wchar_t >& out, const wchar_t* text, int32_t length ) {

	for ( int32_t i = 0; length < 0 ? text [ i ] != 0 : i < length; i++ )
		out.push ( text [ i ] );

}

void script::put_digits ( base::list < wchar_t >& out, uint64_t value, int32_t radix, bool upper ) {

	static const wchar_t lower_digits [ ] = L"0123456789abcdef";
	static const wchar_t upper_digits [ ] = L"0123456789ABCDEF";
	wchar_t reverse [ 70 ];
	int32_t n = 0;

	do {

		reverse [ n++ ] = ( upper ? upper_digits : lower_digits ) [ value % static_cast < uint64_t > ( radix ) ];
		value /= static_cast < uint64_t > ( radix );

	} while ( value && n < 68 );

	while ( n )
		out.push ( reverse [ --n ] );

}

wchar_t script::fold_upper ( wchar_t c ) {

	return c >= L'a' && c <= L'z' ? static_cast < wchar_t > ( c - 32 ) : c;

}

bool script::parse_number ( const wchar_t* text, int32_t length, int64_t radix, int64_t& out ) {

	int32_t i = 0;
	bool negative = false;
	uint64_t value = 0;
	int32_t digits = 0;

	while ( i < length && ( text [ i ] == L' ' || text [ i ] == L'\t' ) )
		i++;

	if ( i < length && ( text [ i ] == L'-' || text [ i ] == L'+' ) )
		negative = text [ i++ ] == L'-';

	if ( radix == 0 ) {

		radix = 10;

		if ( i + 1 < length && text [ i ] == L'0' && ( text [ i + 1 ] == L'x' || text [ i + 1 ] == L'X' ) ) {

			radix = 16;
			i += 2;

		} else if ( i + 1 < length && text [ i ] == L'0' && ( text [ i + 1 ] == L'b' || text [ i + 1 ] == L'B' ) ) {

			radix = 2;
			i += 2;

		}

	}

	for ( ; i < length; i++ ) {

		wchar_t c = fold_upper ( text [ i ] );
		int64_t d = c >= L'0' && c <= L'9' ? c - L'0' : ( c >= L'A' && c <= L'Z' ? c - L'A' + 10 : 99 );

		if ( c == L'_' )
			continue;

		if ( c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' )
			break;

		if ( d >= radix )
			return false;

		value = value * static_cast < uint64_t > ( radix ) + static_cast < uint64_t > ( d );
		digits++;

	}

	while ( i < length && ( text [ i ] == L' ' || text [ i ] == L'\t' || text [ i ] == L'\r' || text [ i ] == L'\n' ) )
		i++;

	if ( !digits || i < length )
		return false;

	out = negative ? static_cast < int64_t > ( 0 - value ) : static_cast < int64_t > ( value );

	return true;

}

bool script::engine::format ( const node& at, const value* args, int32_t count, base::list < wchar_t >& out ) {

	if ( count < 1 || args [ 0 ].type != kind::TEXT )
		return runtime_error ( at, L"The first argument must be the format text." );

	const wchar_t* pattern = text_of ( args [ 0 ] );
	int32_t length = text_length ( args [ 0 ] );
	int32_t next = 1;

	for ( int32_t i = 0; i < length; i++ ) {

		wchar_t c = pattern [ i ];

		if ( c != L'%' ) {

			out.push ( c );

			continue;

		}

		if ( i + 1 < length && pattern [ i + 1 ] == L'%' ) {

			out.push ( L'%' );
			i++;

			continue;

		}

		bool left = false;
		bool zero = false;
		bool plus = false;
		bool prefix = false;
		int32_t width = 0;
		int32_t precision = -1;

		i++;

		while ( i < length && ( pattern [ i ] == L'-' || pattern [ i ] == L'0' || pattern [ i ] == L'+' || pattern [ i ] == L'#' || pattern [ i ] == L' ' ) ) {

			left = left || pattern [ i ] == L'-';
			zero = zero || pattern [ i ] == L'0';
			plus = plus || pattern [ i ] == L'+';
			prefix = prefix || pattern [ i ] == L'#';
			i++;

		}

		if ( i < length && pattern [ i ] == L'*' ) {

			if ( next >= count || args [ next ].type != kind::NUMBER )
				return runtime_error ( at, L"* in the format needs a number argument." );

			width = static_cast < int32_t > ( args [ next++ ].number );
			i++;

		}

		while ( i < length && pattern [ i ] >= L'0' && pattern [ i ] <= L'9' )
			width = width * 10 + ( pattern [ i++ ] - L'0' );

		if ( i < length && pattern [ i ] == L'.' ) {

			precision = 0;
			i++;

			while ( i < length && pattern [ i ] >= L'0' && pattern [ i ] <= L'9' )
				precision = precision * 10 + ( pattern [ i++ ] - L'0' );

		}

		while ( i < length && ( pattern [ i ] == L'l' || pattern [ i ] == L'h' || pattern [ i ] == L'z' || pattern [ i ] == L'I' || pattern [ i ] == L'6' || pattern [ i ] == L'4' ) )
			i++;

		if ( i >= length )
			return runtime_error ( at, L"The format ends in the middle of a % conversion." );

		wchar_t conversion = pattern [ i ];

		if ( next >= count ) {

			base::writer message;

			message.add ( L"The format has more conversions than arguments, at %" ).add ( conversion ).add ( L"." );

			return runtime_error ( at, message.text ( ) );

		}

		value item = args [ next++ ];
		base::list < wchar_t > piece;

		switch ( conversion ) {

			case L'd':
			case L'i':
			case L'u':
			case L'x':
			case L'X':
			case L'o':
			case L'b':
			case L'p':
			case L'c': {

				if ( item.type != kind::NUMBER ) {

					base::writer message;

					message.add ( L"%" ).add ( conversion ).add ( L" needs a number, not " ).add ( kind_name ( item.type ) ).add ( L"." );

					return runtime_error ( at, message.text ( ) );

				}

				if ( conversion == L'c' ) {

					piece.push ( static_cast < wchar_t > ( item.number ) );

					break;

				}

				if ( conversion == L'p' ) {

					put ( piece, L"0x" );

					int32_t mark = piece.count ( );

					put_digits ( piece, static_cast < uint64_t > ( item.number ), 16, true );

					while ( piece.count ( ) - mark < 16 )
						piece.insert ( static_cast < size_t > ( mark ), L'0' );

					break;

				}

				bool negative = ( conversion == L'd' || conversion == L'i' ) && item.number < 0;
				uint64_t magnitude = negative ? static_cast < uint64_t > ( 0 ) - static_cast < uint64_t > ( item.number ) : static_cast < uint64_t > ( item.number );
				int32_t radix = conversion == L'x' || conversion == L'X' ? 16 : ( conversion == L'o' ? 8 : ( conversion == L'b' ? 2 : 10 ) );
				base::list < wchar_t > digits_out;

				put_digits ( digits_out, magnitude, radix, conversion == L'X' );

				while ( precision > digits_out.count ( ) )
					digits_out.insert ( 0, L'0' );

				if ( negative )
					piece.push ( L'-' );
				else if ( plus && ( conversion == L'd' || conversion == L'i' ) )
					piece.push ( L'+' );

				if ( prefix && radix == 16 )
					put ( piece, conversion == L'X' ? L"0X" : L"0x" );
				else if ( prefix && radix == 2 )
					put ( piece, L"0b" );
				else if ( prefix && radix == 8 )
					piece.push ( L'0' );

				if ( zero && !left ) {

					while ( piece.count ( ) + digits_out.count ( ) < width )
						piece.push ( L'0' );

				}

				for ( wchar_t d : digits_out )
					piece.push ( d );

				break;

			}

			case L's': {

				base::writer described;

				if ( item.type == kind::TEXT )
					put ( piece, text_of ( item ), text_length ( item ) );
				else {

					describe ( item, described );
					put ( piece, described.text ( ) );

				}

				if ( precision >= 0 ) {

					while ( piece.count ( ) > precision )
						piece.pop ( );

				}

				break;

			}

			default: {

				base::writer message;

				message.add ( L"%" ).add ( conversion ).add ( L" isn't a format conversion. Use d, i, u, x, X, o, b, c, p or s." );

				return runtime_error ( at, message.text ( ) );

			}

		}

		int32_t padding = width - piece.count ( );

		if ( !left ) {

			for ( int32_t k = 0; k < padding; k++ )
				out.push ( L' ' );

		}

		for ( wchar_t ch : piece )
			out.push ( ch );

		if ( left ) {

			for ( int32_t k = 0; k < padding; k++ )
				out.push ( L' ' );

		}

	}

	if ( next < count )
		return runtime_error ( at, L"The format has fewer conversions than arguments." );

	return true;

}

bool script::engine::builtin ( const node& at, const wchar_t* name, const value* args, int32_t count, value& result ) {

	auto takes = [ & ] ( int32_t low, int32_t high ) {

		if ( count >= low && count <= high )
			return true;

		base::writer message;

		message.add ( L"'" ).add ( name ).add ( L"' takes " );

		if ( low == high )
			message.number ( low );
		else if ( high > 100 )
			message.number ( low ).add ( L" or more" );
		else
			message.number ( low ).add ( L" to " ).number ( high );

		message.add ( high == 1 && low == 1 ? L" argument." : L" arguments." );

		return runtime_error ( at, message.text ( ) );

	};

	auto need = [ & ] ( int32_t index, kind type ) {

		if ( args [ index ].type == type )
			return true;

		base::writer message;

		message.add ( L"Argument " ).number ( index + 1 ).add ( L" of '" ).add ( name ).add ( L"' must be " ).add ( type == kind::NUMBER ? L"a number" : ( type == kind::TEXT ? L"text" : L"a list" ) ).add ( L", not " ).add ( kind_name ( args [ index ].type ) ).add ( L"." );

		return runtime_error ( at, message.text ( ) );

	};

	auto number = [ & ] ( int64_t n ) {

		value made;

		made.type = kind::NUMBER;
		made.number = n;
		result = made;

	};

	if ( base::same_text ( name, L"print" ) ) {

		base::list < wchar_t > line;

		for ( int32_t i = 0; i < count; i++ ) {

			base::writer described;

			if ( i )
				line.push ( L' ' );

			if ( args [ i ].type == kind::TEXT )
				put ( line, text_of ( args [ i ] ), text_length ( args [ i ] ) );
			else {

				describe ( args [ i ], described );
				put ( line, described.text ( ) );

			}

		}

		line.push ( 0 );

		if ( current )
			current->print ( line.data ( ) );

		return true;

	}

	if ( base::same_text ( name, L"printf" ) || base::same_text ( name, L"msg" ) || base::same_text ( name, L"format" ) ) {

		base::list < wchar_t > out;

		if ( !format ( at, args, count, out ) )
			return true;

		if ( base::same_text ( name, L"format" ) ) {

			result = text ( out.data ( ), out.count ( ) );

			return true;

		}

		int32_t start = 0;

		out.push ( 0 );

		for ( int32_t i = 0; i < out.count ( ); i++ ) {

			if ( out [ static_cast < size_t > ( i ) ] != L'\n' && out [ static_cast < size_t > ( i ) ] != 0 )
				continue;

			bool last = out [ static_cast < size_t > ( i ) ] == 0;

			out [ static_cast < size_t > ( i ) ] = 0;

			if ( current && ( !last || i > start ) )
				current->print ( out.data ( ) + start );

			start = i + 1;

		}

		return true;

	}

	if ( base::same_text ( name, L"len" ) ) {

		if ( !takes ( 1, 1 ) )
			return true;

		if ( args [ 0 ].type == kind::TEXT )
			number ( text_length ( args [ 0 ] ) );
		else if ( args [ 0 ].type == kind::LIST )
			number ( list_count ( args [ 0 ] ) );
		else
			runtime_error ( at, L"len needs text or a list." );

		return true;

	}

	if ( base::same_text ( name, L"str" ) ) {

		if ( !takes ( 1, 1 ) )
			return true;

		if ( args [ 0 ].type == kind::TEXT ) {

			result = args [ 0 ];

			return true;

		}

		base::writer described;

		describe ( args [ 0 ], described );
		result = text ( described.text ( ) );

		return true;

	}

	if ( base::same_text ( name, L"hex" ) ) {

		if ( !takes ( 1, 2 ) || !need ( 0, kind::NUMBER ) || ( count == 2 && !need ( 1, kind::NUMBER ) ) )
			return true;

		base::list < wchar_t > out;
		base::list < wchar_t > digits_out;

		put_digits ( digits_out, static_cast < uint64_t > ( args [ 0 ].number ), 16, true );

		while ( count == 2 && digits_out.count ( ) < args [ 1 ].number && digits_out.count ( ) < 64 )
			digits_out.insert ( 0, L'0' );

		put ( out, L"0x" );

		for ( wchar_t d : digits_out )
			out.push ( d );

		result = text ( out.data ( ), out.count ( ) );

		return true;

	}

	if ( base::same_text ( name, L"int" ) ) {

		if ( !takes ( 1, 2 ) || ( count == 2 && !need ( 1, kind::NUMBER ) ) )
			return true;

		if ( args [ 0 ].type == kind::NUMBER ) {

			result = args [ 0 ];

			return true;

		}

		if ( !need ( 0, kind::TEXT ) )
			return true;

		int64_t radix = count == 2 ? args [ 1 ].number : 0;
		int64_t parsed = 0;

		if ( radix != 0 && ( radix < 2 || radix > 36 ) ) {

			runtime_error ( at, L"The radix must be between 2 and 36." );

			return true;

		}

		if ( parse_number ( text_of ( args [ 0 ] ), text_length ( args [ 0 ] ), radix, parsed ) )
			number ( parsed );

		return true;

	}

	if ( base::same_text ( name, L"chr" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::NUMBER ) )
			return true;

		wchar_t c = static_cast < wchar_t > ( args [ 0 ].number );

		result = text ( &c, 1 );

		return true;

	}

	if ( base::same_text ( name, L"ord" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::TEXT ) )
			return true;

		if ( !text_length ( args [ 0 ] ) ) {

			runtime_error ( at, L"ord needs at least one character." );

			return true;

		}

		number ( text_of ( args [ 0 ] ) [ 0 ] );

		return true;

	}

	if ( base::same_text ( name, L"substr" ) ) {

		if ( !takes ( 2, 3 ) || !need ( 0, kind::TEXT ) || !need ( 1, kind::NUMBER ) || ( count == 3 && !need ( 2, kind::NUMBER ) ) )
			return true;

		int64_t total = text_length ( args [ 0 ] );
		int64_t start = args [ 1 ].number < 0 ? base::max < int64_t > ( 0, total + args [ 1 ].number ) : base::min < int64_t > ( args [ 1 ].number, total );
		int64_t size = count == 3 ? base::max < int64_t > ( 0, base::min < int64_t > ( args [ 2 ].number, total - start ) ) : total - start;

		result = text ( text_of ( args [ 0 ] ) + start, static_cast < int32_t > ( size ) );

		return true;

	}

	if ( base::same_text ( name, L"find" ) ) {

		if ( !takes ( 2, 3 ) || !need ( 0, kind::TEXT ) || !need ( 1, kind::TEXT ) || ( count == 3 && !need ( 2, kind::NUMBER ) ) )
			return true;

		const wchar_t* hay = text_of ( args [ 0 ] );
		const wchar_t* needle = text_of ( args [ 1 ] );
		int32_t hay_length = text_length ( args [ 0 ] );
		int32_t needle_length = text_length ( args [ 1 ] );
		int64_t found = -1;

		for ( int64_t i = count == 3 ? base::max < int64_t > ( 0, args [ 2 ].number ) : 0; i + needle_length <= hay_length && found < 0; i++ ) {

			bool same = true;

			for ( int32_t k = 0; k < needle_length && same; k++ )
				same = hay [ i + k ] == needle [ k ];

			if ( same )
				found = i;

		}

		number ( found );

		return true;

	}

	if ( base::same_text ( name, L"replace" ) ) {

		if ( !takes ( 3, 3 ) || !need ( 0, kind::TEXT ) || !need ( 1, kind::TEXT ) || !need ( 2, kind::TEXT ) )
			return true;

		const wchar_t* hay = text_of ( args [ 0 ] );
		const wchar_t* from = text_of ( args [ 1 ] );
		int32_t hay_length = text_length ( args [ 0 ] );
		int32_t from_length = text_length ( args [ 1 ] );
		base::list < wchar_t > out;

		if ( !from_length ) {

			result = args [ 0 ];

			return true;

		}

		for ( int32_t i = 0; i < hay_length; ) {

			bool same = i + from_length <= hay_length;

			for ( int32_t k = 0; k < from_length && same; k++ )
				same = hay [ i + k ] == from [ k ];

			if ( same ) {

				put ( out, text_of ( args [ 2 ] ), text_length ( args [ 2 ] ) );
				i += from_length;

			} else
				out.push ( hay [ i++ ] );

		}

		result = text ( out.data ( ), out.count ( ) );

		return true;

	}

	if ( base::same_text ( name, L"upper" ) || base::same_text ( name, L"lower" ) || base::same_text ( name, L"trim" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::TEXT ) )
			return true;

		const wchar_t* source = text_of ( args [ 0 ] );
		int32_t length = text_length ( args [ 0 ] );
		base::list < wchar_t > out;

		if ( name [ 0 ] == L't' ) {

			int32_t start = 0;

			while ( start < length && ( source [ start ] == L' ' || source [ start ] == L'\t' || source [ start ] == L'\r' || source [ start ] == L'\n' ) )
				start++;

			while ( length > start && ( source [ length - 1 ] == L' ' || source [ length - 1 ] == L'\t' || source [ length - 1 ] == L'\r' || source [ length - 1 ] == L'\n' ) )
				length--;

			result = text ( source + start, length - start );

			return true;

		}

		for ( int32_t i = 0; i < length; i++ )
			out.push ( name [ 0 ] == L'u' ? fold_upper ( source [ i ] ) : base::fold_case ( source [ i ] ) );

		result = text ( out.data ( ), out.count ( ) );

		return true;

	}

	if ( base::same_text ( name, L"starts" ) || base::same_text ( name, L"ends" ) ) {

		if ( !takes ( 2, 2 ) || !need ( 0, kind::TEXT ) || !need ( 1, kind::TEXT ) )
			return true;

		int32_t whole = text_length ( args [ 0 ] );
		int32_t part = text_length ( args [ 1 ] );
		bool same = part <= whole;
		int32_t offset = name [ 0 ] == L's' ? 0 : whole - part;

		for ( int32_t i = 0; i < part && same; i++ )
			same = text_of ( args [ 0 ] ) [ offset + i ] == text_of ( args [ 1 ] ) [ i ];

		number ( same );

		return true;

	}

	if ( base::same_text ( name, L"split" ) ) {

		if ( !takes ( 2, 2 ) || !need ( 0, kind::TEXT ) || !need ( 1, kind::TEXT ) )
			return true;

		const wchar_t* source = text_of ( args [ 0 ] );
		const wchar_t* separator = text_of ( args [ 1 ] );
		int32_t length = text_length ( args [ 0 ] );
		int32_t separator_length = text_length ( args [ 1 ] );
		value made = list ( );
		int32_t start = 0;

		if ( !separator_length ) {

			for ( int32_t i = 0; i < length; i++ )
				push ( made, text ( source + i, 1 ) );

			result = made;

			return true;

		}

		for ( int32_t i = 0; i <= length; ) {

			bool same = i + separator_length <= length;

			for ( int32_t k = 0; k < separator_length && same; k++ )
				same = source [ i + k ] == separator [ k ];

			if ( same || i == length ) {

				push ( made, text ( source + start, i - start ) );
				i += same ? separator_length : 1;
				start = i;

				if ( !same )
					break;

			} else
				i++;

		}

		result = made;

		return true;

	}

	if ( base::same_text ( name, L"join" ) ) {

		if ( !takes ( 1, 2 ) || !need ( 0, kind::LIST ) || ( count == 2 && !need ( 1, kind::TEXT ) ) )
			return true;

		base::list < wchar_t > out;
		int32_t items_count = list_count ( args [ 0 ] );

		for ( int32_t i = 0; i < items_count; i++ ) {

			value item = list_item ( args [ 0 ], i );

			if ( i && count == 2 )
				put ( out, text_of ( args [ 1 ] ), text_length ( args [ 1 ] ) );

			if ( item.type == kind::TEXT )
				put ( out, text_of ( item ), text_length ( item ) );
			else {

				base::writer described;

				describe ( item, described );
				put ( out, described.text ( ) );

			}

		}

		result = text ( out.data ( ), out.count ( ) );

		return true;

	}

	if ( base::same_text ( name, L"contains" ) ) {

		if ( !takes ( 2, 2 ) )
			return true;

		if ( args [ 0 ].type == kind::LIST ) {

			bool found = false;

			for ( int32_t i = 0; i < list_count ( args [ 0 ] ) && !found; i++ )
				found = equal ( list_item ( args [ 0 ], i ), args [ 1 ] );

			number ( found );

			return true;

		}

		if ( !need ( 0, kind::TEXT ) || !need ( 1, kind::TEXT ) )
			return true;

		value probe [ 2 ] = { args [ 0 ], args [ 1 ] };
		value found;

		builtin ( at, L"find", probe, 2, found );
		number ( found.number >= 0 );

		return true;

	}

	if ( base::same_text ( name, L"append" ) ) {

		if ( !takes ( 2, 2 ) || !need ( 0, kind::LIST ) )
			return true;

		push ( args [ 0 ], args [ 1 ] );
		result = args [ 0 ];

		return true;

	}

	if ( base::same_text ( name, L"pop" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::LIST ) )
			return true;

		base::list < value >& target = lists [ static_cast < size_t > ( args [ 0 ].ref ) ];

		if ( target.empty ( ) ) {

			runtime_error ( at, L"pop on an empty list." );

			return true;

		}

		result = target.back ( );
		target.pop ( );

		return true;

	}

	if ( base::same_text ( name, L"insert" ) || base::same_text ( name, L"remove" ) ) {

		bool insert = name [ 0 ] == L'i';

		if ( !takes ( insert ? 3 : 2, insert ? 3 : 2 ) || !need ( 0, kind::LIST ) || !need ( 1, kind::NUMBER ) )
			return true;

		base::list < value >& target = lists [ static_cast < size_t > ( args [ 0 ].ref ) ];
		int64_t where = args [ 1 ].number < 0 ? args [ 1 ].number + target.count ( ) + ( insert ? 1 : 0 ) : args [ 1 ].number;

		if ( where < 0 || where > target.count ( ) - ( insert ? 0 : 1 ) ) {

			runtime_error ( at, L"The list index is out of range." );

			return true;

		}

		if ( insert ) {

			target.insert ( static_cast < size_t > ( where ), args [ 2 ] );
			result = args [ 0 ];

		} else {

			result = target [ static_cast < size_t > ( where ) ];
			target.remove_at ( static_cast < size_t > ( where ) );

		}

		return true;

	}

	if ( base::same_text ( name, L"copy" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::LIST ) )
			return true;

		value made = list ( );

		for ( int32_t i = 0; i < list_count ( args [ 0 ] ); i++ )
			push ( made, list_item ( args [ 0 ], i ) );

		result = made;

		return true;

	}

	if ( base::same_text ( name, L"reverse" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::LIST ) )
			return true;

		base::list < value >& target = lists [ static_cast < size_t > ( args [ 0 ].ref ) ];

		for ( int32_t i = 0, j = target.count ( ) - 1; i < j; i++, j-- ) {

			value swap = target [ static_cast < size_t > ( i ) ];

			target [ static_cast < size_t > ( i ) ] = target [ static_cast < size_t > ( j ) ];
			target [ static_cast < size_t > ( j ) ] = swap;

		}

		result = args [ 0 ];

		return true;

	}

	if ( base::same_text ( name, L"sort" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::LIST ) )
			return true;

		base::list < value >& target = lists [ static_cast < size_t > ( args [ 0 ].ref ) ];

		for ( const value& item : target ) {

			if ( item.type != kind::NUMBER && item.type != kind::TEXT ) {

				runtime_error ( at, L"sort can only order numbers and text." );

				return true;

			}

		}

		base::sort ( target.data ( ), target.count ( ), [ this ] ( const value& x, const value& y ) {

			if ( x.type != y.type )
				return x.type == kind::NUMBER;

			if ( x.type == kind::NUMBER )
				return x.number < y.number;

			return compare_text ( x, y ) < 0;

		} );

		result = args [ 0 ];

		return true;

	}

	if ( base::same_text ( name, L"range" ) ) {

		if ( !takes ( 1, 3 ) )
			return true;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( !need ( i, kind::NUMBER ) )
				return true;

		}

		int64_t start = count == 1 ? 0 : args [ 0 ].number;
		int64_t stop = count == 1 ? args [ 0 ].number : args [ 1 ].number;
		int64_t step = count == 3 ? args [ 2 ].number : 1;

		if ( step == 0 ) {

			runtime_error ( at, L"range can't step by zero." );

			return true;

		}

		value made = list ( );
		int64_t produced = 0;

		for ( int64_t i = start; step > 0 ? i < stop : i > stop; i += step ) {

			if ( ++produced > 10000000 ) {

				runtime_error ( at, L"range would make more than ten million numbers." );

				return true;

			}

			value item;

			item.type = kind::NUMBER;
			item.number = i;
			push ( made, item );

		}

		result = made;

		return true;

	}

	if ( base::same_text ( name, L"typeof" ) ) {

		if ( !takes ( 1, 1 ) )
			return true;

		result = text ( kind_name ( args [ 0 ].type ) );

		return true;

	}

	if ( base::same_text ( name, L"abs" ) ) {

		if ( !takes ( 1, 1 ) || !need ( 0, kind::NUMBER ) )
			return true;

		number ( args [ 0 ].number < 0 ? static_cast < int64_t > ( 0 - static_cast < uint64_t > ( args [ 0 ].number ) ) : args [ 0 ].number );

		return true;

	}

	if ( base::same_text ( name, L"min" ) || base::same_text ( name, L"max" ) ) {

		bool lowest = name [ 1 ] == L'i';

		if ( count == 1 && args [ 0 ].type == kind::LIST ) {

			if ( !list_count ( args [ 0 ] ) )
				return true;

			int64_t best = 0;

			for ( int32_t i = 0; i < list_count ( args [ 0 ] ); i++ ) {

				value item = list_item ( args [ 0 ], i );

				if ( item.type != kind::NUMBER ) {

					runtime_error ( at, L"min and max need numbers." );

					return true;

				}

				if ( !i || ( lowest ? item.number < best : item.number > best ) )
					best = item.number;

			}

			number ( best );

			return true;

		}

		if ( !takes ( 1, 1000 ) )
			return true;

		int64_t best = 0;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( !need ( i, kind::NUMBER ) )
				return true;

			if ( !i || ( lowest ? args [ i ].number < best : args [ i ].number > best ) )
				best = args [ i ].number;

		}

		number ( best );

		return true;

	}

	if ( base::same_text ( name, L"assert" ) ) {

		if ( !takes ( 1, 2 ) )
			return true;

		if ( !truthy ( args [ 0 ] ) ) {

			if ( count == 2 && args [ 1 ].type == kind::TEXT )
				runtime_error ( at, text_of ( args [ 1 ] ) );
			else
				runtime_error ( at, L"Assertion failed." );

		}

		return true;

	}

	if ( base::same_text ( name, L"error" ) ) {

		if ( !takes ( 1, 1 ) )
			return true;

		base::writer described;

		describe ( args [ 0 ], described );
		runtime_error ( at, described.text ( ) );

		return true;

	}

	if ( base::same_text ( name, L"clock" ) ) {

		if ( takes ( 0, 0 ) )
			number ( static_cast < int64_t > ( base::seconds ( ) * 1000.0 ) );

		return true;

	}

	if ( base::same_text ( name, L"exit" ) ) {

		if ( !takes ( 0, 0 ) )
			return true;

		stopped = true;

		return true;

	}

	return false;

}