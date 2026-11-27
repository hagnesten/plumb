#pragma once

#include "base/types/types.h"
#include "decompiler/builder/builder.h"

namespace decompiler {

	const wchar_t* intrinsic_name ( int32_t id );

	using x86::mnemonic;

	extern const wchar_t* const intrinsics [ ];

	extern const wchar_t* const compare_names [ ];

	constexpr int32_t compare_base = 2000;

	enum intrinsic_id : int32_t {

		I_DEBUGBREAK, I_MEMSET, I_MEMCPY, I_TZCNT, I_LZCNT, I_POPCNT, I_BYTESWAP, I_ROTL, I_ROTR, I_UMULH, I_MULH,
		I_SQRTF, I_SQRT, I_FMINF, I_FMAXF, I_FMIN, I_FMAX, I_RDTSC, I_CPUID, I_XADD, I_CMPXCHG,
		I_XCHG, I_OVERFLOW, I_PARITY, I_UNORDERED, I_FS, I_GS, I_FASTFAIL, I_BITTEST, I_HALT,
		I_AND, I_OR, I_XOR, I_ANDNOT, I_SHLD, I_SHRD, I_FLAGS, I_XGETBV, I_SYSCALL,
		I_TEB, I_GSBYTE, I_GSWORD, I_GSDWORD, I_FSBYTE, I_FSWORD, I_FSDWORD,
		I_STOREL_PS, I_STOREH_PS, I_LOADL_PI, I_LOADH_PI, I_STOREL_PD, I_STOREH_PD, I_LOADL_PD, I_LOADH_PD,
		I_XOR_PS, I_XOR_PD, I_XOR_SI128, I_AND_PS, I_AND_PD, I_OR_PS, I_OR_PD,

	};

	enum packed_shape : uint8_t {

		P_BINARY,
		P_UNARY,
		P_BINARY_IMM,
		P_UNARY_IMM,
		P_SHIFT,
		P_TO_GPR,
		P_TO_GPR_IMM,

	};

	extern const packed_form packed_forms [ ];

	extern const int32_t packed_forms_count;

	constexpr int32_t packed_base = 1000;

}