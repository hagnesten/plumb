#include "base/random/random.h"

namespace base {

	void random::seed ( uint64_t value ) {

		state = value ? value : 0x9E3779B97F4A7C15ull;

	}

	uint64_t random::next ( ) {

		state += 0x9E3779B97F4A7C15ull;

		uint64_t z = state;

		z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
		z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;

		return z ^ ( z >> 31 );

	}

	float random::unit ( ) {

		return static_cast < float > ( next ( ) >> 40 ) / 16777216.0f;

	}

}