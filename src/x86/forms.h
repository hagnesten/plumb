#pragma once

#include "x86/x86.h"

namespace x86 {

	struct simd_operand {

		uint8_t source;
		uint8_t kind;
		uint8_t size;

	};

	struct simd_form {

		uint8_t map;
		uint8_t opcode;
		uint8_t pp;
		uint8_t w;
		int8_t reg;
		uint8_t mode;
		uint8_t lengths;
		uint8_t flags;
		mnemonic name;
		uint8_t element;
		uint8_t scale;
		simd_operand ops [ 5 ];

	};

	extern const simd_form evex_forms [ ];

	extern const int32_t evex_forms_count;

	extern const simd_form vex_forms [ ];

	extern const int32_t vex_forms_count;

}