#pragma once

#include "base/base.h"

namespace ui {

	inline void asm_number ( base::writer& out, uint64_t value ) {

		if ( value < 10 ) {

			out.number ( static_cast < int64_t > ( value ) );

			return;

		}

		out.add ( L"0x" ).hex ( value );

	}

	inline void float_literal ( base::writer& out, uint64_t bits, uint8_t size ) {

		double value = size == 4 ? static_cast < double > ( __builtin_bit_cast ( float, static_cast < uint32_t > ( bits ) ) ) : __builtin_bit_cast ( double, bits );

		if ( value != value ) {

			out.add ( L"nan" );

			return;

		}

		if ( value > 1.7e308 || value < -1.7e308 || ( size == 4 && ( value > 3.4e38 || value < -3.4e38 ) ) ) {

			out.add ( value < 0.0 ? L"-inf" : L"inf" );

			return;

		}

		double magnitude = value < 0.0 ? -value : value;

		if ( magnitude >= 1e15 ) {

			out.fixed ( value, 0 );

			return;

		}

		int32_t decimals = 1;
		double scale = 10.0;

		for ( ; decimals < 12; decimals++ ) {

			double rounded = static_cast < double > ( static_cast < int64_t > ( value * scale + ( value < 0.0 ? -0.5 : 0.5 ) ) ) / scale;

			if ( size == 4 ? static_cast < float > ( rounded ) == static_cast < float > ( value ) : rounded == value )
				break;

			scale *= 10.0;

		}

		out.fixed ( value, decimals );

		if ( size == 4 )
			out.add ( L"f" );

	}

	inline bool float_hint ( base::writer& out, uint64_t bits, uint8_t size ) {

		if ( size != 4 && size != 8 )
			return false;

		if ( size == 4 )
			bits &= 0xFFFFFFFFull;

		if ( __builtin_popcountll ( bits ) < 3 )
			return false;

		double value = size == 4 ? static_cast < double > ( __builtin_bit_cast ( float, static_cast < uint32_t > ( bits ) ) ) : __builtin_bit_cast ( double, bits );

		if ( value != value )
			return false;

		double magnitude = value < 0.0 ? -value : value;

		if ( magnitude < 1e-4 || magnitude > 1e7 )
			return false;

		double scale = 1.0;

		for ( int32_t decimals = 0; decimals <= 4; decimals++ ) {

			double rounded = static_cast < double > ( static_cast < int64_t > ( value * scale + ( value < 0.0 ? -0.5 : 0.5 ) ) ) / scale;
			bool same = size == 4 ? static_cast < float > ( rounded ) == static_cast < float > ( value ) : rounded == value;

			if ( same ) {

				out.add ( size == 4 ? L"float " : L"double " ).fixed ( value, base::max ( decimals, 1 ) );

				return true;

			}

			scale *= 10.0;

		}

		return false;

	}

	inline wchar_t printable ( uint8_t value ) {

		return value >= 0x20 && value < 0x7F ? static_cast < wchar_t > ( value ) : L'.';

	}

}