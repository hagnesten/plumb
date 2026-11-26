#pragma once

#include "x86/mnemonics/mnemonics.h"

namespace x86 {

	enum spec : uint8_t {

		NO,
		EB, EW, ED, EQ, EV, EY,
		GB, GW, GD, GQ, GV, GY,
		M0, MW, MD, MQ, MT, MO, MX, MY, MV, MP, MF,
		IB, IW, IZ, IV, IBS,
		JB, JZ,
		OB, OV,
		R_AL, R_CL, R_DX, R_AX, R_EAX, ONE, XMM0_,
		ZB, ZV, ZY,
		SW, CRX, DRX, RY,
		STI, ST0_,
		VX, VO, VY, HX, HO, HY,
		WX, WO, WQ, WD, WW, WB, WH, WQ4, WE, WY, WQX, WS,
		UX, UO, LX,
		PQ, QQ, QD, NQ,
		BY, ER8, ER16, AP,
		R_ES, R_CS, R_SS, R_DS, R_FS, R_GS,
		KR, KRM, KV, KB, KGR, KGB,
		EWV, EZ, GZ, GA, MZ, RD, RQ, RA, R_AX16,

	};

	enum form_bits : uint16_t {

		MODRM = 1,
		D64 = 2,
		F64 = 4,
		I64 = 8,
		O64 = 16,
		NDS = 32,
		VEX_ONLY = 64,
		NO_VEX = 128,
		NOWRITE = 256,
		W_NAME = 512,
		SIZE_NAME = 1024,
		NDS_REG = 2048,
		X87 = 4096,
		REG_ONLY = 8192,

	};

	enum group_id : uint8_t {

		G_NONE,
		G1, G1A, G2, G3B, G3V, G4, G5, G6, G7, G8, G9, G10, G11B, G11V, G12, G13, G14, G15, G16, G17, G_PREFETCH, G_HINT,
		GROUP_COUNT,

	};

	struct form {

		mnemonic name = mnemonic::invalid;
		uint8_t ops [ 4 ] = { };
		uint8_t group = 0;
		uint16_t flags = 0;

	};

	enum prefix_mask : uint8_t {

		PN = 1,
		P66 = 2,
		PF3 = 4,
		PF2 = 8,
		PANY = 15,

	};

	extern form one_byte [ 256 ];
	extern form groups [ GROUP_COUNT ] [ 8 ];
	extern form pool [ 1400 ];
	extern int32_t pool_count;
	extern uint16_t extended [ 3 ] [ 256 ] [ 4 ];
	extern form x87_memory [ 8 ] [ 8 ];
	extern form x87_register [ 8 ] [ 64 ];
	extern int16_t evex_first [ 8 ] [ 256 ];
	extern int16_t evex_count [ 8 ] [ 256 ];
	extern int16_t vex_first [ 4 ] [ 256 ];
	extern int16_t vex_count [ 4 ] [ 256 ];

	mnemonic offset ( mnemonic base, int32_t delta );

	void build_one_byte ( );

	void build_two_byte ( );

	void build_three_byte ( );

	void build_x87 ( );

}