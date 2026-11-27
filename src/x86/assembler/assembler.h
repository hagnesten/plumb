#pragma once

#include "base/types/types.h"

namespace x86 {

	struct resolver {

		virtual bool lookup ( const wchar_t* name, uint64_t& value ) = 0;

	};

	int32_t assemble ( const wchar_t* text, uint64_t address, bool long_mode, resolver* names, uint8_t* out, int32_t room, wchar_t* problem, int32_t problem_room );

	struct named_register {

		const char* name;
		int8_t number;
		int8_t size;
		bool high;
		bool rex;

	};

	extern const named_register registers [ ];

	extern const char* conditions [ 16 ] [ 3 ];

	enum class kind_of : int32_t {

		NONE,
		REG,
		MEM,
		IMM,

	};

	struct piece {

		kind_of kind = kind_of::NONE;
		int32_t reg = 0;
		int32_t size = 0;
		bool high = false;
		bool rex = false;
		int32_t base = -1;
		int32_t index = -1;
		int32_t scale = 1;
		int64_t value = 0;
		bool rip = false;
		bool absolute = false;
		bool symbol = false;

	};

	struct writer {

		uint8_t* out;
		int32_t room;
		int32_t length = 0;
		bool failed = false;

		void put ( uint8_t value ) {

			if ( length < room )
				out [ length++ ] = value;
			else
				failed = true;

		}

		void put_value ( int64_t value, int32_t bytes ) {

			for ( int32_t i = 0; i < bytes; i++ )
				put ( static_cast < uint8_t > ( static_cast < uint64_t > ( value ) >> ( i * 8 ) ) );

		}

	};

	bool same ( const char* a, const char* b );

	bool fits8 ( int64_t value );

	bool fits32 ( int64_t value );

	const named_register* find_register ( const char* word );

	int32_t find_condition ( const char* word );

	bool parse_number ( const char* text, int64_t& value );

	struct simple_op {

		const char* name;
		uint8_t bytes [ 3 ];
		int32_t length;

	};

	extern const simple_op simple_ops [ 21 ];

}