#pragma once

#include "base/types/types.h"
#include "x86/mnemonics/mnemonics.h"
#include "x86/registers/registers.h"

namespace x86 {

	enum class kind : uint8_t {

		NONE,
		REG,
		MEM,
		IMM,
		REL,
		FAR,

	};

	enum class flow : uint8_t {

		NONE,
		CALL,
		JUMP,
		BRANCH,
		RETURN,
		STOP,

	};

	enum prefix_bits : uint16_t {

		LOCK = 1,
		REP = 2,
		REPNE = 4,
		OPSIZE = 8,
		ADDRSIZE = 16,
		REX_W = 32,
		VEX = 64,
		VEX_L = 128,
		HAS_REX = 256,
		BRANCH_TAKEN = 512,
		BRANCH_NOT_TAKEN = 1024,
		NOTRACK = 2048,
		EVEX = 4096,

	};

	enum access_bits : uint8_t {

		READ = 1,
		WRITE = 2,

	};

	struct operand {

		kind type = kind::NONE;
		uint8_t size = 0;
		uint8_t reg = 0;
		uint8_t base = 0;
		uint8_t index = 0;
		uint8_t scale = 0;
		uint8_t segment = 0;
		uint8_t access = 0;
		int64_t value = 0;

		bool memory ( ) const {

			return type == kind::MEM;

		}

		bool rip_relative ( ) const {

			return type == kind::MEM && ( base == RIP || base == EIP );

		}

	};

	struct instruction {

		uint64_t address = 0;
		mnemonic name = mnemonic::invalid;
		uint8_t length = 0;
		uint8_t count = 0;
		uint16_t prefixes = 0;
		flow control = flow::NONE;
		uint8_t condition = 0xFF;
		uint8_t operand_size = 0;
		uint8_t segment = 0;
		uint8_t displacement_offset = 0;
		uint8_t displacement_size = 0;
		uint8_t immediate_offset = 0;
		uint8_t immediate_size = 0;
		uint8_t mask = 0;
		uint8_t broadcast = 0;
		bool zeroing = false;
		uint8_t rounding = 0;
		operand operands [ 5 ];

		bool valid ( ) const {

			return name != mnemonic::invalid;

		}

		uint64_t target ( ) const;

		bool direct ( ) const;

		uint64_t next ( ) const {

			return address + length;

		}

		bool memory_target ( uint64_t& result ) const;

	};

}