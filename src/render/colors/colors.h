#pragma once

#include "base/types/types.h"

namespace render {

	struct color {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		constexpr color ( ) = default;

		constexpr color ( float red, float green, float blue, float alpha = 1.0f ) : r ( red ), g ( green ), b ( blue ), a ( alpha ) { }

		constexpr color fade ( float amount ) const {

			return color ( r, g, b, a * amount );

		}

	};
	constexpr color rgb ( uint32_t hex, float alpha = 1.0f ) {

		return color ( static_cast < float > ( ( hex >> 16 ) & 0xFF ) / 255.0f, static_cast < float > ( ( hex >> 8 ) & 0xFF ) / 255.0f, static_cast < float > ( hex & 0xFF ) / 255.0f, alpha );

	}
	inline color mix ( color x, color y, float t ) {

		return color ( x.r + ( y.r - x.r ) * t, x.g + ( y.g - x.g ) * t, x.b + ( y.b - x.b ) * t, x.a + ( y.a - x.a ) * t );

	}

}