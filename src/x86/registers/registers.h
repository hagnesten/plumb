#pragma once

#include "base/types/types.h"

namespace x86 {

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

	const char* register_name ( uint8_t value );

	uint8_t register_size ( uint8_t value );

	int32_t register_family ( uint8_t value );

	uint8_t resize_register ( uint8_t value, uint8_t size );

	bool is_vector ( uint8_t value );

	bool same_family ( uint8_t a, uint8_t b );

	void build_register_names ( );

}