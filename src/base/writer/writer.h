#pragma once

#include "base/types/types.h"

namespace base {

	class writer {

	public:

		writer ( ) {

			buffer [ 0 ] = 0;

		}

		writer& add ( const wchar_t* value );

		writer& add ( const char* value );

		writer& add ( wchar_t value );

		writer& number ( int64_t value );

		writer& hex ( uint64_t value, int32_t digits = 0 );

		writer& grouped ( uint64_t value );

		writer& fixed ( double value, int32_t decimals );

		writer& signed_fixed ( double value, int32_t decimals );

		void clear ( ) {

			length = 0;
			buffer [ 0 ] = 0;

		}

		const wchar_t* text ( ) const {

			return buffer;

		}

		int32_t size ( ) const {

			return length;

		}

		static constexpr int32_t capacity = 2048;

	private:

		wchar_t buffer [ capacity ];
		int32_t length = 0;

	};

}