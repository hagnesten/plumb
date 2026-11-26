#include "base/writer/writer.h"

namespace base {

	writer& writer::add ( const wchar_t* value ) {

		while ( value && *value && length < capacity - 1 )
			buffer [ length++ ] = *value++;

		buffer [ length ] = 0;

		return *this;

	}

	writer& writer::add ( const char* value ) {

		while ( value && *value && length < capacity - 1 )
			buffer [ length++ ] = static_cast < wchar_t > ( static_cast < uint8_t > ( *value++ ) );

		buffer [ length ] = 0;

		return *this;

	}

	writer& writer::add ( wchar_t value ) {

		if ( length < capacity - 1 )
			buffer [ length++ ] = value;

		buffer [ length ] = 0;

		return *this;

	}

	writer& writer::number ( int64_t value ) {

		wchar_t digits [ 24 ];
		int32_t count = 0;
		bool negative = value < 0;
		uint64_t magnitude = negative ? static_cast < uint64_t > ( -( value + 1 ) ) + 1 : static_cast < uint64_t > ( value );

		do {

			digits [ count++ ] = static_cast < wchar_t > ( L'0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		if ( negative )
			add ( L'\x2212' );

		while ( count )
			add ( digits [ --count ] );

		return *this;

	}

	writer& writer::hex ( uint64_t value, int32_t digits ) {

		wchar_t buffer [ 16 ];
		int32_t count = 0;

		do {

			uint32_t nibble = static_cast < uint32_t > ( value & 0xF );

			buffer [ count++ ] = static_cast < wchar_t > ( nibble < 10 ? L'0' + nibble : L'A' + nibble - 10 );
			value >>= 4;

		} while ( value && count < 16 );

		while ( count < digits && count < 16 )
			buffer [ count++ ] = L'0';

		while ( count )
			add ( buffer [ --count ] );

		return *this;

	}

	writer& writer::grouped ( uint64_t value ) {

		wchar_t digits [ 32 ];
		int32_t count = 0;
		int32_t run = 0;

		do {

			if ( run == 3 ) {

				digits [ count++ ] = L',';
				run = 0;

			}

			digits [ count++ ] = static_cast < wchar_t > ( L'0' + value % 10 );
			value /= 10;
			run++;

		} while ( value );

		while ( count )
			add ( digits [ --count ] );

		return *this;

	}

	writer& writer::fixed ( double value, int32_t decimals ) {

		if ( value != value ) {

			add ( L"\x2014" );

			return *this;

		}

		double factor = 1.0;

		for ( int32_t i = 0; i < decimals; i++ )
			factor *= 10.0;

		bool negative = value < 0.0;
		double magnitude = negative ? -value : value;
		uint64_t scaled = static_cast < uint64_t > ( magnitude * factor + 0.5 );
		uint64_t whole = scaled / static_cast < uint64_t > ( factor );
		uint64_t part = scaled % static_cast < uint64_t > ( factor );

		if ( negative && scaled )
			add ( L'\x2212' );

		number ( static_cast < int64_t > ( whole ) );

		if ( decimals > 0 ) {

			wchar_t digits [ 16 ];

			for ( int32_t i = decimals - 1; i >= 0; i-- ) {

				digits [ i ] = static_cast < wchar_t > ( L'0' + part % 10 );
				part /= 10;

			}

			add ( L'.' );

			for ( int32_t i = 0; i < decimals; i++ )
				add ( digits [ i ] );

		}

		return *this;

	}

	writer& writer::signed_fixed ( double value, int32_t decimals ) {

		double factor = 1.0;

		for ( int32_t i = 0; i < decimals; i++ )
			factor *= 10.0;

		if ( value > 0.0 && static_cast < uint64_t > ( value * factor + 0.5 ) > 0 )
			add ( L'+' );

		return fixed ( value, decimals );

	}

}