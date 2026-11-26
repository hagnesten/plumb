#pragma once

#include "base/types/types.h"

namespace base {

	template < typename type >
	struct strip_reference {

		typedef type result;

	};

	template < typename type >
	struct strip_reference < type& > {

		typedef type result;

	};

	template < typename type >
	struct strip_reference < type&& > {

		typedef type result;

	};

	template < typename type >
	constexpr typename strip_reference < type >::result&& move ( type&& value ) {

		return static_cast < typename strip_reference < type >::result&& > ( value );

	}

	template < typename type >
	constexpr type&& forward ( typename strip_reference < type >::result& value ) {

		return static_cast < type&& > ( value );

	}

	template < typename type >
	inline void swap ( type& a, type& b ) {

		type held = move ( a );

		a = move ( b );
		b = move ( held );

	}

	template < typename type >
	constexpr type min ( type a, type b ) {

		return a < b ? a : b;

	}

	template < typename type >
	constexpr type max ( type a, type b ) {

		return a > b ? a : b;

	}

	template < typename type >
	constexpr type clamp ( type value, type low, type high ) {

		return value < low ? low : ( value > high ? high : value );

	}

}