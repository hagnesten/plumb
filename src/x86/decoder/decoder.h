#pragma once

#include "base/types/types.h"
#include "x86/instruction/instruction.h"
#include "x86/opcodes/opcodes.h"

namespace x86 {

	bool decode ( const uint8_t* code, size_t available, uint64_t address, bool long_mode, instruction& out );

	void prepare ( );

	extern volatile bool ready;

	struct cursor {

		const uint8_t* code;
		size_t limit;
		size_t at;
		bool failed;

		uint8_t byte ( ) {

			if ( at >= limit ) {

				failed = true;

				return 0;

			}

			return code [ at++ ];

		}

		uint64_t read ( int32_t size ) {

			uint64_t value = 0;

			for ( int32_t i = 0; i < size; i++ )
				value |= static_cast < uint64_t > ( byte ( ) ) << ( i * 8 );

			return value;

		}

	};

	int64_t sign_extend ( uint64_t value, int32_t bytes );

	uint8_t gpr ( int32_t number, int32_t size, bool rex );

	struct state {

		bool long_mode;
		bool rex_present;
		bool rex_w;
		bool rex_r;
		bool rex_x;
		bool rex_b;
		bool rex_r2;
		uint8_t ksize;
		bool vex;
		bool evex;
		bool broadcast;
		uint8_t width;
		uint8_t vvvv;
		uint8_t mod;
		uint8_t reg;
		uint8_t rm;
		bool has_modrm;
		uint8_t operand_size;
		uint8_t address_size;
		operand memory;
		bool memory_ready;
		uint8_t sib;
		uint8_t opcode;
		bool prefix_used;

	};

	bool read_memory ( cursor& in, state& s, instruction& out );

	uint8_t vector ( int32_t number, int32_t width );

	bool build_operand ( uint8_t which, cursor& in, state& s, instruction& out, operand& item );

	mnemonic amd_3dnow ( uint8_t code );

	bool refine ( form& f, state& s, int32_t map, uint8_t opcode, uint8_t mandatory );

	bool resolve_group ( form& f, state& s, uint8_t mandatory, instruction& out );

	bool writes_only ( mnemonic name );

	int32_t evex_bytes ( uint8_t size, int32_t width );

	int32_t evex_lanes ( uint8_t size, int32_t width );

	bool decode_vector ( cursor& in, state& s, instruction& out, int32_t map, uint8_t opcode, uint8_t pp, bool addrsize, uint16_t prefixes );

}