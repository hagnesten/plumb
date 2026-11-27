#include "decompiler/literals/literals.h"
#include "decompiler/builder/builder.h"

bool decompiler::string_literal ( const loader::binary& file, uint64_t address, wchar_t* out, int32_t room ) {

	uint64_t offset = 0;
	int32_t length = 0;

	if ( !file.offset_of ( address, offset ) || room < 16 )
		return false;

	bool wide = offset + 3 < file.size && file.byte ( offset + 1 ) == 0 && file.byte ( offset ) >= 0x20 && file.byte ( offset + 3 ) == 0;
	uint64_t step = wide ? 2 : 1;

	if ( wide )
		out [ length++ ] = L'L';

	out [ length++ ] = L'"';

	int32_t first = length;

	for ( uint64_t i = 0; length < room - 8 && i < 400; i++ ) {

		uint64_t at = offset + i * step;

		if ( at >= file.size )
			return false;

		wchar_t c = wide ? static_cast < wchar_t > ( file.read16 ( at ) ) : static_cast < wchar_t > ( file.byte ( at ) );

		if ( !c )
			break;

		if ( c == L'\n' ) {

			out [ length++ ] = L'\\';
			out [ length++ ] = L'n';

		} else if ( c == L'\r' ) {

			out [ length++ ] = L'\\';
			out [ length++ ] = L'r';

		} else if ( c == L'\t' ) {

			out [ length++ ] = L'\\';
			out [ length++ ] = L't';

		} else if ( c == L'"' || c == L'\\' ) {

			out [ length++ ] = L'\\';
			out [ length++ ] = c;

		} else if ( c < 0x20 || c > 0x7E )
			return false;
		else
			out [ length++ ] = c;

	}

	if ( length - first < 2 )
		return false;

	out [ length++ ] = L'"';
	out [ length ] = 0;

	return true;

}

bool decompiler::char_literal ( int64_t value, wchar_t* out, int32_t room ) {

	uint64_t raw = static_cast < uint64_t > ( value );
	uint8_t bytes [ 8 ];
	int32_t count = 0;

	if ( value < 0 || room < 12 )
		return false;

	do {

		bytes [ count++ ] = static_cast < uint8_t > ( raw & 0xFF );
		raw >>= 8;

	} while ( raw && count < 8 );

	if ( raw )
		return false;

	int32_t length = 0;

	out [ length++ ] = L'\'';

	for ( int32_t i = count - 1; i >= 0; i-- ) {

		uint8_t c = bytes [ i ];
		const wchar_t* escape = nullptr;

		switch ( c ) {

			case 0:
				escape = L"\\0";
				break;

			case '\n':
				escape = L"\\n";
				break;

			case '\r':
				escape = L"\\r";
				break;

			case '\t':
				escape = L"\\t";
				break;

			case '\'':
				escape = L"\\'";
				break;

			case '\\':
				escape = L"\\\\";
				break;

			default:
				break;

		}

		if ( length + 6 >= room )
			return false;

		if ( escape ) {

			if ( count > 1 && c != 0 && c != '\'' && c != '\\' )
				return false;

			for ( ; *escape; escape++ )
				out [ length++ ] = *escape;

		} else if ( c >= 0x20 && c <= 0x7E )
			out [ length++ ] = static_cast < wchar_t > ( c );
		else if ( count == 1 ) {

			static const wchar_t digits [ ] = L"0123456789ABCDEF";

			out [ length++ ] = L'\\';
			out [ length++ ] = L'x';
			out [ length++ ] = digits [ c >> 4 ];
			out [ length++ ] = digits [ c & 15 ];

		} else
			return false;

	}

	out [ length++ ] = L'\'';
	out [ length ] = 0;

	return true;

}