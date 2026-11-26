#pragma once

#include "base/types/types.h"

namespace base {

	inline void rgb_to_hsv ( uint32_t rgb, float& hue, float& saturation, float& value ) {

		float r = static_cast < float > ( ( rgb >> 16 ) & 255 ) / 255.0f;
		float g = static_cast < float > ( ( rgb >> 8 ) & 255 ) / 255.0f;
		float b = static_cast < float > ( rgb & 255 ) / 255.0f;
		float high = r > g ? ( r > b ? r : b ) : ( g > b ? g : b );
		float low = r < g ? ( r < b ? r : b ) : ( g < b ? g : b );
		float range = high - low;

		value = high;
		saturation = high > 0.0f ? range / high : 0.0f;

		if ( range <= 0.0f )
			hue = 0.0f;
		else if ( high == r )
			hue = ( g - b ) / range;
		else if ( high == g )
			hue = 2.0f + ( b - r ) / range;
		else
			hue = 4.0f + ( r - g ) / range;

		hue /= 6.0f;

		if ( hue < 0.0f )
			hue += 1.0f;

	}

	inline uint32_t hsv_to_rgb ( float hue, float saturation, float value ) {

		float scaled = ( hue >= 1.0f ? 0.0f : hue ) * 6.0f;
		int32_t sector = static_cast < int32_t > ( scaled );
		float t = scaled - static_cast < float > ( sector );
		float p = value * ( 1.0f - saturation );
		float q = value * ( 1.0f - saturation * t );
		float u = value * ( 1.0f - saturation * ( 1.0f - t ) );
		float r = value;
		float g = u;
		float b = p;

		switch ( sector % 6 ) {

			case 1:
				r = q;
				g = value;
				b = p;
				break;

			case 2:
				r = p;
				g = value;
				b = u;
				break;

			case 3:
				r = p;
				g = q;
				b = value;
				break;

			case 4:
				r = u;
				g = p;
				b = value;
				break;

			case 5:
				r = value;
				g = p;
				b = q;
				break;

			default:
				break;

		}

		auto channel = [ ] ( float x ) {

			int32_t v = static_cast < int32_t > ( x * 255.0f + 0.5f );

			return static_cast < uint32_t > ( v < 0 ? 0 : ( v > 255 ? 255 : v ) );

		};

		return ( channel ( r ) << 16 ) | ( channel ( g ) << 8 ) | channel ( b );

	}

}