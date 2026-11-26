#pragma once

#include "base/types/types.h"
#include "base/list/list.h"

namespace base {

	class text {

	public:

		text ( ) = default;

		text ( const wchar_t* value ) {

			set ( value );

		}

		void set ( const wchar_t* value ) {

			chars.clear ( );

			for ( int32_t i = 0; value && value [ i ]; i++ )
				chars.push ( value [ i ] );

			chars.push ( 0 );

		}

		const wchar_t* get ( ) const {

			return chars.empty ( ) ? L"" : chars.data ( );

		}

		int32_t size ( ) const {

			return chars.empty ( ) ? 0 : chars.count ( ) - 1;

		}

		bool empty ( ) const {

			return size ( ) == 0;

		}

	private:

		list < wchar_t > chars;

	};

}