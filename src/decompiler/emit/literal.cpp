#include "decompiler/internal.h"

namespace decompiler {

	bool string_literal ( const loader::binary& file, uint64_t address, wchar_t* out, int32_t room ) {

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

}
