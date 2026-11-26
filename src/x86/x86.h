#pragma once

#include "base/types/types.h"
#include "x86/mnemonics.h"

namespace x86 {

#define PLUMB_ENUM( value ) value,

	enum class mnemonic : uint16_t {

		PLUMB_MNEMONICS ( PLUMB_ENUM )
		COUNT

	};

#undef PLUMB_ENUM

	enum reg : uint8_t {

		NONE = 0,
		RAX = 1, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15,
		EAX = 17, ECX, EDX, EBX, ESP, EBP, ESI, EDI, R8D, R9D, R10D, R11D, R12D, R13D, R14D, R15D,
		AX = 33, CX, DX, BX, SP, BP, SI, DI, R8W, R9W, R10W, R11W, R12W, R13W, R14W, R15W,
		AL = 49, CL, DL, BL, SPL, BPL, SIL, DIL, R8B, R9B, R10B, R11B, R12B, R13B, R14B, R15B,
		AH = 65, CH, DH, BH,
		ES = 69, CS, SS, DS, FS, GS,
		RIP = 75, EIP,
		ST0 = 77,
		MM0 = 85,
		XMM0 = 93,
		YMM0 = 125,
		ZMM0 = 157,
		CR0 = 189,
		DR0 = 205,
		K0 = 221,
		TMM0 = 229,
		REG_COUNT = 237,

	};

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

	bool decode ( const uint8_t* code, size_t available, uint64_t address, bool long_mode, instruction& out );

	const char* mnemonic_name ( mnemonic value );

	const char* register_name ( uint8_t value );

	uint8_t register_size ( uint8_t value );

	int32_t register_family ( uint8_t value );

	uint8_t resize_register ( uint8_t value, uint8_t size );

	bool is_vector ( uint8_t value );

	bool same_family ( uint8_t a, uint8_t b );

	void prepare ( );

	enum class token : uint8_t {

		TEXT,
		MNEMONIC,
		PREFIX,
		REGISTER,
		NUMBER,
		ADDRESS,
		SIZE,
		PUNCT,
		SYMBOL,

	};

	struct symbolizer {

		virtual bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) = 0;

		virtual bool number ( uint64_t, uint64_t, uint8_t, wchar_t*, int32_t ) {

			return false;

		}

	};

	struct printer {

		virtual void put ( const wchar_t* text, int32_t length, token type, uint64_t value ) = 0;

	};

	enum format_flags : uint32_t {

		ALWAYS_SIZE = 1,
		HEX_SUFFIX = 2,
		PLAIN_RIP = 4,
		SIGNED_IMM = 8,

	};

	void format ( const instruction& item, printer& out, symbolizer* names, uint32_t flags );

	int32_t format_text ( const instruction& item, wchar_t* out, int32_t room, symbolizer* names, uint32_t flags );

	int32_t mnemonic_text ( const instruction& item, wchar_t* out, int32_t room );

	struct resolver {

		virtual bool lookup ( const wchar_t* name, uint64_t& value ) = 0;

	};

	int32_t assemble ( const wchar_t* text, uint64_t address, bool long_mode, resolver* names, uint8_t* out, int32_t room, wchar_t* problem, int32_t problem_room );

}