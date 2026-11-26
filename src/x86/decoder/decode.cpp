#include "x86/x86.h"
#include "x86/decoder/forms.h"

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

	static form one_byte [ 256 ];
	static form groups [ GROUP_COUNT ] [ 8 ];
	static form pool [ 1400 ];
	static int32_t pool_count = 1;
	static uint16_t extended [ 3 ] [ 256 ] [ 4 ];
	static form x87_memory [ 8 ] [ 8 ];
	static form x87_register [ 8 ] [ 64 ];
	static const char* names [ static_cast < int32_t > ( mnemonic::COUNT ) ];
	static const char* register_names [ REG_COUNT ];
	static volatile bool ready = false;

	enum prefix_mask : uint8_t {

		PN = 1,
		P66 = 2,
		PF3 = 4,
		PF2 = 8,
		PANY = 15,

	};

	static form make ( mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t flags ) {

		form item;

		item.name = name;
		item.ops [ 0 ] = a;
		item.ops [ 1 ] = b;
		item.ops [ 2 ] = c;
		item.ops [ 3 ] = d;
		item.flags = flags;

		return item;

	}

	static void one ( int32_t code, mnemonic name, uint8_t a = NO, uint8_t b = NO, uint8_t c = NO, uint16_t flags = 0 ) {

		one_byte [ code ] = make ( name, a, b, c, NO, flags );

	}

	static void one_group ( int32_t code, uint8_t group, uint8_t a, uint8_t b, uint16_t flags ) {

		form item = make ( mnemonic::invalid, a, b, NO, NO, flags | MODRM );

		item.group = group;
		one_byte [ code ] = item;

	}

	static void put ( int32_t map, int32_t code, uint8_t prefixes, mnemonic name, uint8_t a = NO, uint8_t b = NO, uint8_t c = NO, uint8_t d = NO, uint16_t flags = MODRM ) {

		int32_t index = pool_count++;

		pool [ index ] = make ( name, a, b, c, d, flags );

		for ( int32_t slot = 0; slot < 4; slot++ ) {

			if ( prefixes & ( 1 << slot ) )
				extended [ map - 1 ] [ code ] [ slot ] = static_cast < uint16_t > ( index );

		}

	}

	static void put_group ( int32_t map, int32_t code, uint8_t prefixes, uint8_t group, uint8_t a, uint8_t b, uint16_t flags ) {

		int32_t index = pool_count++;
		form item = make ( mnemonic::invalid, a, b, NO, NO, flags | MODRM );

		item.group = group;
		pool [ index ] = item;

		for ( int32_t slot = 0; slot < 4; slot++ ) {

			if ( prefixes & ( 1 << slot ) )
				extended [ map - 1 ] [ code ] [ slot ] = static_cast < uint16_t > ( index );

		}

	}

	static void in_group ( uint8_t group, int32_t reg, mnemonic name, uint8_t a = NO, uint8_t b = NO, uint8_t c = NO, uint16_t flags = 0 ) {

		groups [ group ] [ reg ] = make ( name, a, b, c, NO, flags );

	}

	static mnemonic offset ( mnemonic base, int32_t delta ) {

		return static_cast < mnemonic > ( static_cast < int32_t > ( base ) + delta );

	}

	static void simd ( int32_t code, mnemonic name, bool mmx ) {

		if ( mmx )
			put ( 1, code, PN, name, PQ, QQ, NO, NO, MODRM | NO_VEX );

		put ( 1, code, P66, name, VX, WX, NO, NO, MODRM | NDS );

	}

	static void arith ( int32_t code, mnemonic packed_single, bool scalar ) {

		put ( 1, code, PN, packed_single, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, code, P66, offset ( packed_single, -1 ), VX, WX, NO, NO, MODRM | NDS );

		if ( scalar ) {

			put ( 1, code, PF3, offset ( packed_single, 2 ), VO, WD, NO, NO, MODRM | NDS );
			put ( 1, code, PF2, offset ( packed_single, 1 ), VO, WQ, NO, NO, MODRM | NDS );

		}

	}

	static void build_one_byte ( ) {

		const mnemonic alu [ 8 ] = { mnemonic::add, mnemonic::or_, mnemonic::adc, mnemonic::sbb, mnemonic::and_, mnemonic::sub, mnemonic::xor_, mnemonic::cmp };

		for ( int32_t i = 0; i < 8; i++ ) {

			int32_t base_code = i * 8;
			uint16_t extra = i == 7 ? NOWRITE : 0;

			one ( base_code, alu [ i ], EB, GB, NO, MODRM | extra );
			one ( base_code + 1, alu [ i ], EV, GV, NO, MODRM | extra );
			one ( base_code + 2, alu [ i ], GB, EB, NO, MODRM | extra );
			one ( base_code + 3, alu [ i ], GV, EV, NO, MODRM | extra );
			one ( base_code + 4, alu [ i ], R_AL, IB, NO, extra );
			one ( base_code + 5, alu [ i ], R_AX, IZ, NO, extra );

		}

		one ( 0x06, mnemonic::push, R_ES, NO, NO, I64 | NOWRITE );
		one ( 0x07, mnemonic::pop, R_ES, NO, NO, I64 );
		one ( 0x0E, mnemonic::push, R_CS, NO, NO, I64 | NOWRITE );
		one ( 0x16, mnemonic::push, R_SS, NO, NO, I64 | NOWRITE );
		one ( 0x17, mnemonic::pop, R_SS, NO, NO, I64 );
		one ( 0x1E, mnemonic::push, R_DS, NO, NO, I64 | NOWRITE );
		one ( 0x1F, mnemonic::pop, R_DS, NO, NO, I64 );
		one ( 0x27, mnemonic::daa, NO, NO, NO, I64 );
		one ( 0x2F, mnemonic::das, NO, NO, NO, I64 );
		one ( 0x37, mnemonic::aaa, NO, NO, NO, I64 );
		one ( 0x3F, mnemonic::aas, NO, NO, NO, I64 );

		for ( int32_t i = 0; i < 8; i++ ) {

			one ( 0x40 + i, mnemonic::inc, ZV, NO, NO, I64 );
			one ( 0x48 + i, mnemonic::dec, ZV, NO, NO, I64 );
			one ( 0x50 + i, mnemonic::push, ZV, NO, NO, D64 | NOWRITE );
			one ( 0x58 + i, mnemonic::pop, ZV, NO, NO, D64 );
			one ( 0xB0 + i, mnemonic::mov, ZB, IB );
			one ( 0xB8 + i, mnemonic::mov, ZV, IV );

		}

		one ( 0x60, mnemonic::pusha, NO, NO, NO, I64 | SIZE_NAME );
		one ( 0x61, mnemonic::popa, NO, NO, NO, I64 | SIZE_NAME );
		one ( 0x62, mnemonic::bound, GV, MV, NO, MODRM | I64 | NOWRITE );
		one ( 0x63, mnemonic::arpl, EW, GW, NO, MODRM );
		one ( 0x68, mnemonic::push, IZ, NO, NO, D64 | NOWRITE );
		one ( 0x69, mnemonic::imul, GV, EV, IZ, MODRM );
		one ( 0x6A, mnemonic::push, IBS, NO, NO, D64 | NOWRITE );
		one ( 0x6B, mnemonic::imul, GV, EV, IBS, MODRM );
		one ( 0x6C, mnemonic::insb );
		one ( 0x6D, mnemonic::insw, NO, NO, NO, SIZE_NAME );
		one ( 0x6E, mnemonic::outsb );
		one ( 0x6F, mnemonic::outsw, NO, NO, NO, SIZE_NAME );

		for ( int32_t i = 0; i < 16; i++ )
			one ( 0x70 + i, offset ( mnemonic::jo, i ), JB, NO, NO, F64 );

		one_group ( 0x80, G1, EB, IB, 0 );
		one_group ( 0x81, G1, EV, IZ, 0 );
		one_group ( 0x82, G1, EB, IB, I64 );
		one_group ( 0x83, G1, EV, IBS, 0 );
		one ( 0x84, mnemonic::test, EB, GB, NO, MODRM | NOWRITE );
		one ( 0x85, mnemonic::test, EV, GV, NO, MODRM | NOWRITE );
		one ( 0x86, mnemonic::xchg, EB, GB, NO, MODRM );
		one ( 0x87, mnemonic::xchg, EV, GV, NO, MODRM );
		one ( 0x88, mnemonic::mov, EB, GB, NO, MODRM );
		one ( 0x89, mnemonic::mov, EV, GV, NO, MODRM );
		one ( 0x8A, mnemonic::mov, GB, EB, NO, MODRM );
		one ( 0x8B, mnemonic::mov, GV, EV, NO, MODRM );
		one ( 0x8C, mnemonic::mov, EV, SW, NO, MODRM );
		one ( 0x8D, mnemonic::lea, GV, M0, NO, MODRM );
		one ( 0x8E, mnemonic::mov, SW, EW, NO, MODRM );
		one_group ( 0x8F, G1A, EV, NO, D64 );
		one ( 0x90, mnemonic::nop );

		for ( int32_t i = 1; i < 8; i++ )
			one ( 0x90 + i, mnemonic::xchg, R_AX, ZV );

		one ( 0x98, mnemonic::cbw, NO, NO, NO, SIZE_NAME );
		one ( 0x99, mnemonic::cwd, NO, NO, NO, SIZE_NAME );
		one ( 0x9A, mnemonic::callf, AP, NO, NO, I64 );
		one ( 0x9B, mnemonic::fwait );
		one ( 0x9C, mnemonic::pushf, NO, NO, NO, D64 | SIZE_NAME );
		one ( 0x9D, mnemonic::popf, NO, NO, NO, D64 | SIZE_NAME );
		one ( 0x9E, mnemonic::sahf );
		one ( 0x9F, mnemonic::lahf );
		one ( 0xA0, mnemonic::mov, R_AL, OB );
		one ( 0xA1, mnemonic::mov, R_AX, OV );
		one ( 0xA2, mnemonic::mov, OB, R_AL );
		one ( 0xA3, mnemonic::mov, OV, R_AX );
		one ( 0xA4, mnemonic::movsb );
		one ( 0xA5, mnemonic::movsw, NO, NO, NO, SIZE_NAME );
		one ( 0xA6, mnemonic::cmpsb );
		one ( 0xA7, mnemonic::cmpsw, NO, NO, NO, SIZE_NAME );
		one ( 0xA8, mnemonic::test, R_AL, IB, NO, NOWRITE );
		one ( 0xA9, mnemonic::test, R_AX, IZ, NO, NOWRITE );
		one ( 0xAA, mnemonic::stosb );
		one ( 0xAB, mnemonic::stosw, NO, NO, NO, SIZE_NAME );
		one ( 0xAC, mnemonic::lodsb );
		one ( 0xAD, mnemonic::lodsw, NO, NO, NO, SIZE_NAME );
		one ( 0xAE, mnemonic::scasb );
		one ( 0xAF, mnemonic::scasw, NO, NO, NO, SIZE_NAME );
		one_group ( 0xC0, G2, EB, IB, 0 );
		one_group ( 0xC1, G2, EV, IB, 0 );
		one ( 0xC2, mnemonic::ret, IW, NO, NO, F64 | NOWRITE );
		one ( 0xC3, mnemonic::ret, NO, NO, NO, F64 );
		one ( 0xC4, mnemonic::les, GV, MP, NO, MODRM | I64 );
		one ( 0xC5, mnemonic::lds, GV, MP, NO, MODRM | I64 );
		one_group ( 0xC6, G11B, EB, IB, 0 );
		one_group ( 0xC7, G11V, EV, IZ, 0 );
		one ( 0xC8, mnemonic::enter, IW, IB, NO, D64 | NOWRITE );
		one ( 0xC9, mnemonic::leave, NO, NO, NO, D64 );
		one ( 0xCA, mnemonic::retf, IW, NO, NO, NOWRITE );
		one ( 0xCB, mnemonic::retf );
		one ( 0xCC, mnemonic::int3 );
		one ( 0xCD, mnemonic::int_, IB, NO, NO, NOWRITE );
		one ( 0xCE, mnemonic::into, NO, NO, NO, I64 );
		one ( 0xCF, mnemonic::iret, NO, NO, NO, SIZE_NAME );
		one_group ( 0xD0, G2, EB, ONE, 0 );
		one_group ( 0xD1, G2, EV, ONE, 0 );
		one_group ( 0xD2, G2, EB, R_CL, 0 );
		one_group ( 0xD3, G2, EV, R_CL, 0 );
		one ( 0xD4, mnemonic::aam, IB, NO, NO, I64 | NOWRITE );
		one ( 0xD5, mnemonic::aad, IB, NO, NO, I64 | NOWRITE );
		one ( 0xD6, mnemonic::salc, NO, NO, NO, I64 );
		one ( 0xD7, mnemonic::xlatb );

		for ( int32_t i = 0; i < 8; i++ )
			one ( 0xD8 + i, mnemonic::invalid, NO, NO, NO, MODRM | X87 );

		one ( 0xE0, mnemonic::loopne, JB, NO, NO, F64 );
		one ( 0xE1, mnemonic::loope, JB, NO, NO, F64 );
		one ( 0xE2, mnemonic::loop, JB, NO, NO, F64 );
		one ( 0xE3, mnemonic::jcxz, JB, NO, NO, F64 );
		one ( 0xE4, mnemonic::in, R_AL, IB );
		one ( 0xE5, mnemonic::in, R_EAX, IB );
		one ( 0xE6, mnemonic::out, IB, R_AL, NO, NOWRITE );
		one ( 0xE7, mnemonic::out, IB, R_EAX, NO, NOWRITE );
		one ( 0xE8, mnemonic::call, JZ, NO, NO, F64 | NOWRITE );
		one ( 0xE9, mnemonic::jmp, JZ, NO, NO, F64 | NOWRITE );
		one ( 0xEA, mnemonic::jmpf, AP, NO, NO, I64 | NOWRITE );
		one ( 0xEB, mnemonic::jmp, JB, NO, NO, F64 | NOWRITE );
		one ( 0xEC, mnemonic::in, R_AL, R_DX );
		one ( 0xED, mnemonic::in, R_EAX, R_DX );
		one ( 0xEE, mnemonic::out, R_DX, R_AL, NO, NOWRITE );
		one ( 0xEF, mnemonic::out, R_DX, R_EAX, NO, NOWRITE );
		one ( 0xF1, mnemonic::int1 );
		one ( 0xF4, mnemonic::hlt );
		one ( 0xF5, mnemonic::cmc );
		one_group ( 0xF6, G3B, EB, NO, 0 );
		one_group ( 0xF7, G3V, EV, NO, 0 );
		one ( 0xF8, mnemonic::clc );
		one ( 0xF9, mnemonic::stc );
		one ( 0xFA, mnemonic::cli );
		one ( 0xFB, mnemonic::sti );
		one ( 0xFC, mnemonic::cld );
		one ( 0xFD, mnemonic::std );
		one_group ( 0xFE, G4, EB, NO, 0 );
		one_group ( 0xFF, G5, EV, NO, 0 );

		for ( int32_t i = 0; i < 8; i++ )
			in_group ( G1, i, alu [ i ], NO, NO, NO, i == 7 ? NOWRITE : 0 );

		in_group ( G1A, 0, mnemonic::pop );

		const mnemonic shifts [ 8 ] = { mnemonic::rol, mnemonic::ror, mnemonic::rcl, mnemonic::rcr, mnemonic::shl, mnemonic::shr, mnemonic::shl, mnemonic::sar };

		for ( int32_t i = 0; i < 8; i++ )
			in_group ( G2, i, shifts [ i ] );

		in_group ( G3B, 0, mnemonic::test, EB, IB, NO, NOWRITE );
		in_group ( G3B, 1, mnemonic::test, EB, IB, NO, NOWRITE );
		in_group ( G3V, 0, mnemonic::test, EV, IZ, NO, NOWRITE );
		in_group ( G3V, 1, mnemonic::test, EV, IZ, NO, NOWRITE );

		const mnemonic unary [ 6 ] = { mnemonic::not_, mnemonic::neg, mnemonic::mul, mnemonic::imul, mnemonic::div, mnemonic::idiv };

		for ( int32_t i = 0; i < 6; i++ ) {

			in_group ( G3B, i + 2, unary [ i ] );
			in_group ( G3V, i + 2, unary [ i ] );

		}

		in_group ( G4, 0, mnemonic::inc );
		in_group ( G4, 1, mnemonic::dec );
		in_group ( G5, 0, mnemonic::inc );
		in_group ( G5, 1, mnemonic::dec );
		in_group ( G5, 2, mnemonic::call, EV, NO, NO, F64 | NOWRITE );
		in_group ( G5, 3, mnemonic::callf, MF, NO, NO, NOWRITE );
		in_group ( G5, 4, mnemonic::jmp, EV, NO, NO, F64 | NOWRITE );
		in_group ( G5, 5, mnemonic::jmpf, MF, NO, NO, NOWRITE );
		in_group ( G5, 6, mnemonic::push, EV, NO, NO, D64 | NOWRITE );
		in_group ( G11B, 0, mnemonic::mov );
		in_group ( G11V, 0, mnemonic::mov );

	}

	static void build_two_byte ( ) {

		put_group ( 1, 0x00, PANY, G6, NO, NO, 0 );
		put_group ( 1, 0x01, PANY, G7, NO, NO, 0 );
		put ( 1, 0x02, PANY, mnemonic::lar, GV, EW );
		put ( 1, 0x03, PANY, mnemonic::lsl, GV, EW );
		put ( 1, 0x05, PANY, mnemonic::syscall, NO, NO, NO, NO, 0 );
		put ( 1, 0x06, PANY, mnemonic::clts, NO, NO, NO, NO, 0 );
		put ( 1, 0x07, PANY, mnemonic::sysret, NO, NO, NO, NO, 0 );
		put ( 1, 0x08, PANY, mnemonic::invd, NO, NO, NO, NO, 0 );
		put ( 1, 0x09, PANY, mnemonic::wbinvd, NO, NO, NO, NO, 0 );
		put ( 1, 0x09, PF3, mnemonic::wbnoinvd, NO, NO, NO, NO, 0 );
		put ( 1, 0x0B, PANY, mnemonic::ud2, NO, NO, NO, NO, 0 );
		put_group ( 1, 0x0D, PANY, G_PREFETCH, M0, NO, NOWRITE );
		put ( 1, 0x0E, PANY, mnemonic::femms, NO, NO, NO, NO, 0 );
		put ( 1, 0x0F, PANY, mnemonic::pfadd, PQ, QQ, NO, NO, MODRM | NO_VEX );

		put ( 1, 0x10, PN, mnemonic::movups, VX, WX );
		put ( 1, 0x10, P66, mnemonic::movupd, VX, WX );
		put ( 1, 0x10, PF3, mnemonic::movss, VO, WD, NO, NO, MODRM | NDS_REG );
		put ( 1, 0x10, PF2, mnemonic::movsd, VO, WQ, NO, NO, MODRM | NDS_REG );
		put ( 1, 0x11, PN, mnemonic::movups, WX, VX );
		put ( 1, 0x11, P66, mnemonic::movupd, WX, VX );
		put ( 1, 0x11, PF3, mnemonic::movss, WD, VO, NO, NO, MODRM | NDS_REG );
		put ( 1, 0x11, PF2, mnemonic::movsd, WQ, VO, NO, NO, MODRM | NDS_REG );
		put ( 1, 0x12, PN, mnemonic::movlps, VO, MQ, NO, NO, MODRM | NDS );
		put ( 1, 0x12, P66, mnemonic::movlpd, VO, MQ, NO, NO, MODRM | NDS );
		put ( 1, 0x12, PF3, mnemonic::movsldup, VX, WX );
		put ( 1, 0x12, PF2, mnemonic::movddup, VX, WQX );
		put ( 1, 0x13, PN, mnemonic::movlps, MQ, VO );
		put ( 1, 0x13, P66, mnemonic::movlpd, MQ, VO );
		put ( 1, 0x14, PN, mnemonic::unpcklps, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x14, P66, mnemonic::unpcklpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x15, PN, mnemonic::unpckhps, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x15, P66, mnemonic::unpckhpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x16, PN, mnemonic::movhps, VO, MQ, NO, NO, MODRM | NDS );
		put ( 1, 0x16, P66, mnemonic::movhpd, VO, MQ, NO, NO, MODRM | NDS );
		put ( 1, 0x16, PF3, mnemonic::movshdup, VX, WX );
		put ( 1, 0x17, PN, mnemonic::movhps, MQ, VO );
		put ( 1, 0x17, P66, mnemonic::movhpd, MQ, VO );
		put_group ( 1, 0x18, PANY, G16, M0, NO, NOWRITE );

		for ( int32_t code = 0x19; code <= 0x1F; code++ )
			put_group ( 1, code, PANY, G_HINT, EV, NO, NOWRITE );

		put ( 1, 0x20, PANY, mnemonic::mov, RY, CRX, NO, NO, MODRM | REG_ONLY );
		put ( 1, 0x21, PANY, mnemonic::mov, RY, DRX, NO, NO, MODRM | REG_ONLY );
		put ( 1, 0x22, PANY, mnemonic::mov, CRX, RY, NO, NO, MODRM | REG_ONLY );
		put ( 1, 0x23, PANY, mnemonic::mov, DRX, RY, NO, NO, MODRM | REG_ONLY );
		put ( 1, 0x28, PN, mnemonic::movaps, VX, WX );
		put ( 1, 0x28, P66, mnemonic::movapd, VX, WX );
		put ( 1, 0x29, PN, mnemonic::movaps, WX, VX );
		put ( 1, 0x29, P66, mnemonic::movapd, WX, VX );
		put ( 1, 0x2A, PN, mnemonic::cvtpi2ps, VO, QQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2A, P66, mnemonic::cvtpi2pd, VO, QQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2A, PF3, mnemonic::cvtsi2ss, VO, EY, NO, NO, MODRM | NDS );
		put ( 1, 0x2A, PF2, mnemonic::cvtsi2sd, VO, EY, NO, NO, MODRM | NDS );
		put ( 1, 0x2B, PN, mnemonic::movntps, MX, VX );
		put ( 1, 0x2B, P66, mnemonic::movntpd, MX, VX );
		put ( 1, 0x2B, PF3, mnemonic::movntss, MD, VO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2B, PF2, mnemonic::movntsd, MQ, VO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2C, PN, mnemonic::cvttps2pi, PQ, WQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2C, P66, mnemonic::cvttpd2pi, PQ, WO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2C, PF3, mnemonic::cvttss2si, GY, WD );
		put ( 1, 0x2C, PF2, mnemonic::cvttsd2si, GY, WQ );
		put ( 1, 0x2D, PN, mnemonic::cvtps2pi, PQ, WQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2D, P66, mnemonic::cvtpd2pi, PQ, WO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x2D, PF3, mnemonic::cvtss2si, GY, WD );
		put ( 1, 0x2D, PF2, mnemonic::cvtsd2si, GY, WQ );
		put ( 1, 0x2E, PN, mnemonic::ucomiss, VO, WD, NO, NO, MODRM | NOWRITE );
		put ( 1, 0x2E, P66, mnemonic::ucomisd, VO, WQ, NO, NO, MODRM | NOWRITE );
		put ( 1, 0x2F, PN, mnemonic::comiss, VO, WD, NO, NO, MODRM | NOWRITE );
		put ( 1, 0x2F, P66, mnemonic::comisd, VO, WQ, NO, NO, MODRM | NOWRITE );
		put ( 1, 0x30, PANY, mnemonic::wrmsr, NO, NO, NO, NO, 0 );
		put ( 1, 0x31, PANY, mnemonic::rdtsc, NO, NO, NO, NO, 0 );
		put ( 1, 0x32, PANY, mnemonic::rdmsr, NO, NO, NO, NO, 0 );
		put ( 1, 0x33, PANY, mnemonic::rdpmc, NO, NO, NO, NO, 0 );
		put ( 1, 0x34, PANY, mnemonic::sysenter, NO, NO, NO, NO, 0 );
		put ( 1, 0x35, PANY, mnemonic::sysexit, NO, NO, NO, NO, 0 );
		put ( 1, 0x37, PANY, mnemonic::getsec, NO, NO, NO, NO, 0 );

		for ( int32_t i = 0; i < 16; i++ ) {

			put ( 1, 0x40 + i, PANY, offset ( mnemonic::cmovo, i ), GV, EV );
			put ( 1, 0x80 + i, PANY, offset ( mnemonic::jo, i ), JZ, NO, NO, NO, F64 );
			put ( 1, 0x90 + i, PANY, offset ( mnemonic::seto, i ), EB );

		}

		put ( 1, 0x50, PN, mnemonic::movmskps, GD, UX );
		put ( 1, 0x50, P66, mnemonic::movmskpd, GD, UX );
		put ( 1, 0x51, PN, mnemonic::sqrtps, VX, WX );
		put ( 1, 0x51, P66, mnemonic::sqrtpd, VX, WX );
		put ( 1, 0x51, PF3, mnemonic::sqrtss, VO, WD, NO, NO, MODRM | NDS );
		put ( 1, 0x51, PF2, mnemonic::sqrtsd, VO, WQ, NO, NO, MODRM | NDS );
		put ( 1, 0x52, PN, mnemonic::rsqrtps, VX, WX );
		put ( 1, 0x52, PF3, mnemonic::rsqrtss, VO, WD, NO, NO, MODRM | NDS );
		put ( 1, 0x53, PN, mnemonic::rcpps, VX, WX );
		put ( 1, 0x53, PF3, mnemonic::rcpss, VO, WD, NO, NO, MODRM | NDS );
		arith ( 0x54, mnemonic::andps, false );
		arith ( 0x55, mnemonic::andnps, false );
		arith ( 0x56, mnemonic::orps, false );
		arith ( 0x57, mnemonic::xorps, false );
		put ( 1, 0x58, PN, mnemonic::addps, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x58, P66, mnemonic::addpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x58, PF3, mnemonic::addss, VO, WD, NO, NO, MODRM | NDS );
		put ( 1, 0x58, PF2, mnemonic::addsd, VO, WQ, NO, NO, MODRM | NDS );
		put ( 1, 0x59, PN, mnemonic::mulps, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x59, P66, mnemonic::mulpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x59, PF3, mnemonic::mulss, VO, WD, NO, NO, MODRM | NDS );
		put ( 1, 0x59, PF2, mnemonic::mulsd, VO, WQ, NO, NO, MODRM | NDS );
		put ( 1, 0x5A, PN, mnemonic::cvtps2pd, VX, WH );
		put ( 1, 0x5A, P66, mnemonic::cvtpd2ps, VO, WX );
		put ( 1, 0x5A, PF3, mnemonic::cvtss2sd, VO, WD, NO, NO, MODRM | NDS );
		put ( 1, 0x5A, PF2, mnemonic::cvtsd2ss, VO, WQ, NO, NO, MODRM | NDS );
		put ( 1, 0x5B, PN, mnemonic::cvtdq2ps, VX, WX );
		put ( 1, 0x5B, P66, mnemonic::cvtps2dq, VX, WX );
		put ( 1, 0x5B, PF3, mnemonic::cvttps2dq, VX, WX );

		const mnemonic scalar_ops [ 4 ] [ 4 ] = {
			{ mnemonic::subps, mnemonic::subpd, mnemonic::subss, mnemonic::subsd },
			{ mnemonic::minps, mnemonic::minpd, mnemonic::minss, mnemonic::minsd },
			{ mnemonic::divps, mnemonic::divpd, mnemonic::divss, mnemonic::divsd },
			{ mnemonic::maxps, mnemonic::maxpd, mnemonic::maxss, mnemonic::maxsd }
		};

		for ( int32_t i = 0; i < 4; i++ ) {

			put ( 1, 0x5C + i, PN, scalar_ops [ i ] [ 0 ], VX, WX, NO, NO, MODRM | NDS );
			put ( 1, 0x5C + i, P66, scalar_ops [ i ] [ 1 ], VX, WX, NO, NO, MODRM | NDS );
			put ( 1, 0x5C + i, PF3, scalar_ops [ i ] [ 2 ], VO, WD, NO, NO, MODRM | NDS );
			put ( 1, 0x5C + i, PF2, scalar_ops [ i ] [ 3 ], VO, WQ, NO, NO, MODRM | NDS );

		}

		const mnemonic unpack [ 12 ] = { mnemonic::punpcklbw, mnemonic::punpcklwd, mnemonic::punpckldq, mnemonic::packsswb, mnemonic::pcmpgtb, mnemonic::pcmpgtw, mnemonic::pcmpgtd, mnemonic::packuswb, mnemonic::punpckhbw, mnemonic::punpckhwd, mnemonic::punpckhdq, mnemonic::packssdw };

		for ( int32_t i = 0; i < 12; i++ ) {

			if ( i < 3 ) {

				put ( 1, 0x60 + i, PN, unpack [ i ], PQ, QD, NO, NO, MODRM | NO_VEX );
				put ( 1, 0x60 + i, P66, unpack [ i ], VX, WX, NO, NO, MODRM | NDS );

			} else
				simd ( 0x60 + i, unpack [ i ], true );

		}

		put ( 1, 0x6C, P66, mnemonic::punpcklqdq, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x6D, P66, mnemonic::punpckhqdq, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x6E, PN, mnemonic::movd, PQ, EY, NO, NO, MODRM | NO_VEX | W_NAME );
		put ( 1, 0x6E, P66, mnemonic::movd, VO, EY, NO, NO, MODRM | W_NAME );
		put ( 1, 0x6F, PN, mnemonic::movq, PQ, QQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x6F, P66, mnemonic::movdqa, VX, WX );
		put ( 1, 0x6F, PF3, mnemonic::movdqu, VX, WX );
		put ( 1, 0x70, PN, mnemonic::pshufw, PQ, QQ, IB, NO, MODRM | NO_VEX );
		put ( 1, 0x70, P66, mnemonic::pshufd, VX, WX, IB );
		put ( 1, 0x70, PF3, mnemonic::pshufhw, VX, WX, IB );
		put ( 1, 0x70, PF2, mnemonic::pshuflw, VX, WX, IB );
		put_group ( 1, 0x71, PN | P66, G12, NO, NO, 0 );
		put_group ( 1, 0x72, PN | P66, G13, NO, NO, 0 );
		put_group ( 1, 0x73, PN | P66, G14, NO, NO, 0 );
		simd ( 0x74, mnemonic::pcmpeqb, true );
		simd ( 0x75, mnemonic::pcmpeqw, true );
		simd ( 0x76, mnemonic::pcmpeqd, true );
		put ( 1, 0x77, PN, mnemonic::emms, NO, NO, NO, NO, 0 );
		put ( 1, 0x78, PN, mnemonic::vmread, EZ, GZ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x79, PN, mnemonic::vmwrite, GZ, EZ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x78, P66, mnemonic::extrq, UO, IB, IB, NO, MODRM | NO_VEX );
		put ( 1, 0x78, PF2, mnemonic::insertq, VO, UO, IB, IB, MODRM | NO_VEX );
		put ( 1, 0x79, P66, mnemonic::extrq, VO, UO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x79, PF2, mnemonic::insertq, VO, UO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x7C, P66, mnemonic::haddpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x7C, PF2, mnemonic::haddps, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x7D, P66, mnemonic::hsubpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x7D, PF2, mnemonic::hsubps, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0x7E, PN, mnemonic::movd, EY, PQ, NO, NO, MODRM | NO_VEX | W_NAME );
		put ( 1, 0x7E, P66, mnemonic::movd, EY, VO, NO, NO, MODRM | W_NAME );
		put ( 1, 0x7E, PF3, mnemonic::movq, VO, WQ );
		put ( 1, 0x7F, PN, mnemonic::movq, QQ, PQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0x7F, P66, mnemonic::movdqa, WX, VX );
		put ( 1, 0x7F, PF3, mnemonic::movdqu, WX, VX );

		put ( 1, 0xA0, PANY, mnemonic::push, R_FS, NO, NO, NO, D64 | NOWRITE );
		put ( 1, 0xA1, PANY, mnemonic::pop, R_FS, NO, NO, NO, D64 );
		put ( 1, 0xA2, PANY, mnemonic::cpuid, NO, NO, NO, NO, 0 );
		put ( 1, 0xA3, PANY, mnemonic::bt, EV, GV, NO, NO, MODRM | NOWRITE );
		put ( 1, 0xA4, PANY, mnemonic::shld, EV, GV, IB );
		put ( 1, 0xA5, PANY, mnemonic::shld, EV, GV, R_CL );
		put ( 1, 0xA8, PANY, mnemonic::push, R_GS, NO, NO, NO, D64 | NOWRITE );
		put ( 1, 0xA9, PANY, mnemonic::pop, R_GS, NO, NO, NO, D64 );
		put ( 1, 0xAA, PANY, mnemonic::rsm, NO, NO, NO, NO, 0 );
		put ( 1, 0xAB, PANY, mnemonic::bts, EV, GV );
		put ( 1, 0xAC, PANY, mnemonic::shrd, EV, GV, IB );
		put ( 1, 0xAD, PANY, mnemonic::shrd, EV, GV, R_CL );
		put_group ( 1, 0xAE, PANY, G15, NO, NO, 0 );
		put ( 1, 0xAF, PANY, mnemonic::imul, GV, EV );
		put ( 1, 0xB0, PANY, mnemonic::cmpxchg, EB, GB );
		put ( 1, 0xB1, PANY, mnemonic::cmpxchg, EV, GV );
		put ( 1, 0xB2, PANY, mnemonic::lss, GV, MP );
		put ( 1, 0xB3, PANY, mnemonic::btr, EV, GV );
		put ( 1, 0xB4, PANY, mnemonic::lfs, GV, MP );
		put ( 1, 0xB5, PANY, mnemonic::lgs, GV, MP );
		put ( 1, 0xB6, PANY, mnemonic::movzx, GV, EB );
		put ( 1, 0xB7, PANY, mnemonic::movzx, GV, EW );
		put ( 1, 0xB8, PF3, mnemonic::popcnt, GV, EV );
		put ( 1, 0xB9, PANY, mnemonic::ud1, GV, EV );
		put_group ( 1, 0xBA, PANY, G8, EV, IB, 0 );
		put ( 1, 0xBB, PANY, mnemonic::btc, EV, GV );
		put ( 1, 0xBC, PN | P66 | PF2, mnemonic::bsf, GV, EV );
		put ( 1, 0xBC, PF3, mnemonic::tzcnt, GV, EV );
		put ( 1, 0xBD, PN | P66 | PF2, mnemonic::bsr, GV, EV );
		put ( 1, 0xBD, PF3, mnemonic::lzcnt, GV, EV );
		put ( 1, 0xBE, PANY, mnemonic::movsx, GV, EB );
		put ( 1, 0xBF, PANY, mnemonic::movsx, GV, EW );
		put ( 1, 0xC0, PANY, mnemonic::xadd, EB, GB );
		put ( 1, 0xC1, PANY, mnemonic::xadd, EV, GV );
		put ( 1, 0xC2, PN, mnemonic::cmpps, VX, WX, IB, NO, MODRM | NDS );
		put ( 1, 0xC2, P66, mnemonic::cmppd, VX, WX, IB, NO, MODRM | NDS );
		put ( 1, 0xC2, PF3, mnemonic::cmpss, VO, WD, IB, NO, MODRM | NDS );
		put ( 1, 0xC2, PF2, mnemonic::cmpsd, VO, WQ, IB, NO, MODRM | NDS );
		put ( 1, 0xC3, PN, mnemonic::movnti, MY, GY, NO, NO, MODRM | NO_VEX );
		put ( 1, 0xC4, PN, mnemonic::pinsrw, PQ, ER16, IB, NO, MODRM | NO_VEX );
		put ( 1, 0xC4, P66, mnemonic::pinsrw, VO, ER16, IB, NO, MODRM | NDS );
		put ( 1, 0xC5, PN, mnemonic::pextrw, GD, NQ, IB, NO, MODRM | NO_VEX );
		put ( 1, 0xC5, P66, mnemonic::pextrw, GD, UO, IB );
		put ( 1, 0xC6, PN, mnemonic::shufps, VX, WX, IB, NO, MODRM | NDS );
		put ( 1, 0xC6, P66, mnemonic::shufpd, VX, WX, IB, NO, MODRM | NDS );
		put_group ( 1, 0xC7, PANY, G9, NO, NO, 0 );

		for ( int32_t i = 0; i < 8; i++ )
			put ( 1, 0xC8 + i, PANY, mnemonic::bswap, ZV, NO, NO, NO, 0 );

		put ( 1, 0xD0, P66, mnemonic::addsubpd, VX, WX, NO, NO, MODRM | NDS );
		put ( 1, 0xD0, PF2, mnemonic::addsubps, VX, WX, NO, NO, MODRM | NDS );

		const int32_t shift_codes [ 8 ] = { 0xD1, 0xD2, 0xD3, 0xE1, 0xE2, 0xF1, 0xF2, 0xF3 };
		const mnemonic shift_names [ 8 ] = { mnemonic::psrlw, mnemonic::psrld, mnemonic::psrlq, mnemonic::psraw, mnemonic::psrad, mnemonic::psllw, mnemonic::pslld, mnemonic::psllq };

		for ( int32_t i = 0; i < 8; i++ ) {

			put ( 1, shift_codes [ i ], PN, shift_names [ i ], PQ, QQ, NO, NO, MODRM | NO_VEX );
			put ( 1, shift_codes [ i ], P66, shift_names [ i ], VX, WO, NO, NO, MODRM | NDS );

		}

		simd ( 0xD4, mnemonic::paddq, true );
		simd ( 0xD5, mnemonic::pmullw, true );
		put ( 1, 0xD6, P66, mnemonic::movq, WQ, VO );
		put ( 1, 0xD6, PF3, mnemonic::movq2dq, VO, NQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0xD6, PF2, mnemonic::movdq2q, PQ, UO, NO, NO, MODRM | NO_VEX );
		put ( 1, 0xD7, PN, mnemonic::pmovmskb, GD, NQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0xD7, P66, mnemonic::pmovmskb, GD, UX );

		const mnemonic block_d8 [ 8 ] = { mnemonic::psubusb, mnemonic::psubusw, mnemonic::pminub, mnemonic::pand, mnemonic::paddusb, mnemonic::paddusw, mnemonic::pmaxub, mnemonic::pandn };
		const mnemonic block_e8 [ 8 ] = { mnemonic::psubsb, mnemonic::psubsw, mnemonic::pminsw, mnemonic::por, mnemonic::paddsb, mnemonic::paddsw, mnemonic::pmaxsw, mnemonic::pxor };
		const mnemonic block_f8 [ 7 ] = { mnemonic::psubb, mnemonic::psubw, mnemonic::psubd, mnemonic::psubq, mnemonic::paddb, mnemonic::paddw, mnemonic::paddd };

		for ( int32_t i = 0; i < 8; i++ ) {

			simd ( 0xD8 + i, block_d8 [ i ], true );
			simd ( 0xE8 + i, block_e8 [ i ], true );

		}

		for ( int32_t i = 0; i < 7; i++ )
			simd ( 0xF8 + i, block_f8 [ i ], true );

		simd ( 0xE0, mnemonic::pavgb, true );
		simd ( 0xE3, mnemonic::pavgw, true );
		simd ( 0xE4, mnemonic::pmulhuw, true );
		simd ( 0xE5, mnemonic::pmulhw, true );
		put ( 1, 0xE6, P66, mnemonic::cvttpd2dq, VO, WX );
		put ( 1, 0xE6, PF3, mnemonic::cvtdq2pd, VX, WH );
		put ( 1, 0xE6, PF2, mnemonic::cvtpd2dq, VO, WX );
		put ( 1, 0xE7, PN, mnemonic::movntq, MQ, PQ, NO, NO, MODRM | NO_VEX );
		put ( 1, 0xE7, P66, mnemonic::movntdq, MX, VX );
		put ( 1, 0xF0, PF2, mnemonic::lddqu, VX, MX );
		simd ( 0xF4, mnemonic::pmuludq, true );
		simd ( 0xF5, mnemonic::pmaddwd, true );
		simd ( 0xF6, mnemonic::psadbw, true );
		put ( 1, 0xF7, PN, mnemonic::maskmovq, PQ, NQ, NO, NO, MODRM | NO_VEX | NOWRITE );
		put ( 1, 0xF7, P66, mnemonic::maskmovdqu, VO, UO, NO, NO, MODRM | NOWRITE );
		put ( 1, 0xFF, PANY, mnemonic::ud0, GD, ED );

		const mnemonic g6 [ 6 ] = { mnemonic::sldt, mnemonic::str, mnemonic::lldt, mnemonic::ltr, mnemonic::verr, mnemonic::verw };

		for ( int32_t i = 0; i < 6; i++ )
			in_group ( G6, i, g6 [ i ], i < 2 ? EWV : EW );

		in_group ( G7, 0, mnemonic::sgdt, M0 );
		in_group ( G7, 1, mnemonic::sidt, M0 );
		in_group ( G7, 2, mnemonic::lgdt, M0 );
		in_group ( G7, 3, mnemonic::lidt, M0 );
		in_group ( G7, 4, mnemonic::smsw, EW );
		in_group ( G7, 6, mnemonic::lmsw, EW );
		in_group ( G7, 7, mnemonic::invlpg, M0 );
		in_group ( G8, 4, mnemonic::bt, NO, NO, NO, NOWRITE );
		in_group ( G8, 5, mnemonic::bts );
		in_group ( G8, 6, mnemonic::btr );
		in_group ( G8, 7, mnemonic::btc );
		in_group ( G16, 0, mnemonic::prefetchnta );
		in_group ( G16, 1, mnemonic::prefetcht0 );
		in_group ( G16, 2, mnemonic::prefetcht1 );
		in_group ( G16, 3, mnemonic::prefetcht2 );
		in_group ( G_PREFETCH, 0, mnemonic::prefetch );
		in_group ( G_PREFETCH, 1, mnemonic::prefetchw );
		in_group ( G_PREFETCH, 2, mnemonic::prefetchwt1 );

	}

	static void build_three_byte ( ) {

		const mnemonic ssse3 [ 12 ] = { mnemonic::pshufb, mnemonic::phaddw, mnemonic::phaddd, mnemonic::phaddsw, mnemonic::pmaddubsw, mnemonic::phsubw, mnemonic::phsubd, mnemonic::phsubsw, mnemonic::psignb, mnemonic::psignw, mnemonic::psignd, mnemonic::pmulhrsw };

		for ( int32_t i = 0; i < 12; i++ ) {

			put ( 2, i, PN, ssse3 [ i ], PQ, QQ, NO, NO, MODRM | NO_VEX );
			put ( 2, i, P66, ssse3 [ i ], VX, WX, NO, NO, MODRM | NDS );

		}

		put ( 2, 0x0C, P66, mnemonic::vpermilps, VX, HX, WX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x0D, P66, mnemonic::vpermilpd, VX, HX, WX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x0E, P66, mnemonic::vtestps, VX, WX, NO, NO, MODRM | VEX_ONLY | NOWRITE );
		put ( 2, 0x0F, P66, mnemonic::vtestpd, VX, WX, NO, NO, MODRM | VEX_ONLY | NOWRITE );
		put ( 2, 0x10, P66, mnemonic::pblendvb, VO, WO, XMM0_, NO, MODRM | NO_VEX );
		put ( 2, 0x13, P66, mnemonic::vcvtph2ps, VX, WH, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x14, P66, mnemonic::blendvps, VO, WO, XMM0_, NO, MODRM | NO_VEX );
		put ( 2, 0x15, P66, mnemonic::blendvpd, VO, WO, XMM0_, NO, MODRM | NO_VEX );
		put ( 2, 0x16, P66, mnemonic::vpermps, VX, HX, WX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x17, P66, mnemonic::ptest, VX, WX, NO, NO, MODRM | NOWRITE );
		put ( 2, 0x18, P66, mnemonic::vbroadcastss, VX, WD, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x19, P66, mnemonic::vbroadcastsd, VY, WQ, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x1A, P66, mnemonic::vbroadcastf128, VY, MO, NO, NO, MODRM | VEX_ONLY );

		const mnemonic absolute [ 3 ] = { mnemonic::pabsb, mnemonic::pabsw, mnemonic::pabsd };

		for ( int32_t i = 0; i < 3; i++ ) {

			put ( 2, 0x1C + i, PN, absolute [ i ], PQ, QQ, NO, NO, MODRM | NO_VEX );
			put ( 2, 0x1C + i, P66, absolute [ i ], VX, WX );

		}

		const mnemonic extend_s [ 6 ] = { mnemonic::pmovsxbw, mnemonic::pmovsxbd, mnemonic::pmovsxbq, mnemonic::pmovsxwd, mnemonic::pmovsxwq, mnemonic::pmovsxdq };
		const mnemonic extend_z [ 6 ] = { mnemonic::pmovzxbw, mnemonic::pmovzxbd, mnemonic::pmovzxbq, mnemonic::pmovzxwd, mnemonic::pmovzxwq, mnemonic::pmovzxdq };
		const uint8_t extend_src [ 6 ] = { WH, WQ4, WE, WH, WQ4, WH };

		for ( int32_t i = 0; i < 6; i++ ) {

			put ( 2, 0x20 + i, P66, extend_s [ i ], VX, extend_src [ i ] );
			put ( 2, 0x30 + i, P66, extend_z [ i ], VX, extend_src [ i ] );

		}

		put ( 2, 0x28, P66, mnemonic::pmuldq, VX, WX, NO, NO, MODRM | NDS );
		put ( 2, 0x29, P66, mnemonic::pcmpeqq, VX, WX, NO, NO, MODRM | NDS );
		put ( 2, 0x2A, P66, mnemonic::movntdqa, VX, MX );
		put ( 2, 0x2B, P66, mnemonic::packusdw, VX, WX, NO, NO, MODRM | NDS );
		put ( 2, 0x2C, P66, mnemonic::vmaskmovps, VX, HX, MX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x2D, P66, mnemonic::vmaskmovpd, VX, HX, MX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x2E, P66, mnemonic::vmaskmovps, MX, HX, VX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x2F, P66, mnemonic::vmaskmovpd, MX, HX, VX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x36, P66, mnemonic::vpermd, VX, HX, WX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x37, P66, mnemonic::pcmpgtq, VX, WX, NO, NO, MODRM | NDS );

		const mnemonic minmax [ 9 ] = { mnemonic::pminsb, mnemonic::pminsd, mnemonic::pminuw, mnemonic::pminud, mnemonic::pmaxsb, mnemonic::pmaxsd, mnemonic::pmaxuw, mnemonic::pmaxud, mnemonic::pmulld };

		for ( int32_t i = 0; i < 9; i++ )
			put ( 2, 0x38 + i, P66, minmax [ i ], VX, WX, NO, NO, MODRM | NDS );

		put ( 2, 0x41, P66, mnemonic::phminposuw, VO, WO );
		put ( 2, 0x45, P66, mnemonic::vpsrlvd, VX, HX, WX, NO, MODRM | VEX_ONLY | W_NAME );
		put ( 2, 0x46, P66, mnemonic::vpsravd, VX, HX, WX, NO, MODRM | VEX_ONLY );
		put ( 2, 0x47, P66, mnemonic::vpsllvd, VX, HX, WX, NO, MODRM | VEX_ONLY | W_NAME );
		put ( 2, 0x58, P66, mnemonic::vpbroadcastd, VX, WD, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x59, P66, mnemonic::vpbroadcastq, VX, WQ, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x5A, P66, mnemonic::vbroadcasti128, VY, MO, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x78, P66, mnemonic::vpbroadcastb, VX, WB, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x79, P66, mnemonic::vpbroadcastw, VX, WW, NO, NO, MODRM | VEX_ONLY );
		put ( 2, 0x8C, P66, mnemonic::vpmaskmovd, VX, HX, MX, NO, MODRM | VEX_ONLY | W_NAME );
		put ( 2, 0x8E, P66, mnemonic::vpmaskmovd, MX, HX, VX, NO, MODRM | VEX_ONLY | W_NAME );

		const mnemonic fma_bases [ 3 ] = { mnemonic::vfmaddsub132ps, mnemonic::vfmaddsub213ps, mnemonic::vfmaddsub231ps };
		const int32_t fma_codes [ 3 ] = { 0x96, 0xA6, 0xB6 };

		for ( int32_t order = 0; order < 3; order++ ) {

			int32_t base_name = static_cast < int32_t > ( fma_bases [ order ] );

			put ( 2, fma_codes [ order ], P66, static_cast < mnemonic > ( base_name ), VX, HX, WX, NO, MODRM | VEX_ONLY | W_NAME );
			put ( 2, fma_codes [ order ] + 1, P66, static_cast < mnemonic > ( base_name + 2 ), VX, HX, WX, NO, MODRM | VEX_ONLY | W_NAME );

			for ( int32_t k = 0; k < 4; k++ ) {

				int32_t packed = base_name + 4 + k * 4;

				put ( 2, fma_codes [ order ] + 2 + k * 2, P66, static_cast < mnemonic > ( packed ), VX, HX, WX, NO, MODRM | VEX_ONLY | W_NAME );
				put ( 2, fma_codes [ order ] + 3 + k * 2, P66, static_cast < mnemonic > ( packed + 2 ), VO, HO, WS, NO, MODRM | VEX_ONLY | W_NAME );

			}

		}

		put ( 2, 0x80, P66, mnemonic::invept, GZ, MO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0x81, P66, mnemonic::invvpid, GZ, MO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0x82, P66, mnemonic::invpcid, GZ, MO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xC8, PN, mnemonic::sha1nexte, VO, WO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xC9, PN, mnemonic::sha1msg1, VO, WO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xCA, PN, mnemonic::sha1msg2, VO, WO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xCB, PN, mnemonic::sha256rnds2, VO, WO, XMM0_, NO, MODRM | NO_VEX );
		put ( 2, 0xCC, PN, mnemonic::sha256msg1, VO, WO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xCD, PN, mnemonic::sha256msg2, VO, WO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xCF, P66, mnemonic::gf2p8mulb, VX, WX, NO, NO, MODRM | NDS );
		put ( 2, 0xD8, PF3, mnemonic::aesencwide128kl, M0, NO, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xDC, PF3, mnemonic::aesenc128kl, VO, M0, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xDD, PF3, mnemonic::aesdec128kl, VO, M0, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xDE, PF3, mnemonic::aesenc256kl, VO, M0, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xDF, PF3, mnemonic::aesdec256kl, VO, M0, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF5, P66, mnemonic::wrussd, MY, GY, NO, NO, MODRM | NO_VEX | W_NAME );
		put ( 2, 0xF6, PN, mnemonic::wrssd, MY, GY, NO, NO, MODRM | NO_VEX | W_NAME );
		put ( 2, 0xF8, P66, mnemonic::movdir64b, GA, MZ, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF8, PF3, mnemonic::enqcmds, GA, MZ, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF8, PF2, mnemonic::enqcmd, GA, MZ, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF9, PN, mnemonic::movdiri, MY, GY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xFA, PF3, mnemonic::encodekey128, GD, RD, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xFB, PF3, mnemonic::encodekey256, GD, RD, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xFC, PN, mnemonic::aadd, MY, GY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xFC, P66, mnemonic::aand, MY, GY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xFC, PF2, mnemonic::aor, MY, GY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xFC, PF3, mnemonic::axor, MY, GY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xDB, P66, mnemonic::aesimc, VO, WO );
		put ( 2, 0xDC, P66, mnemonic::aesenc, VO, WO, NO, NO, MODRM | NDS );
		put ( 2, 0xDD, P66, mnemonic::aesenclast, VO, WO, NO, NO, MODRM | NDS );
		put ( 2, 0xDE, P66, mnemonic::aesdec, VO, WO, NO, NO, MODRM | NDS );
		put ( 2, 0xDF, P66, mnemonic::aesdeclast, VO, WO, NO, NO, MODRM | NDS );
		put ( 2, 0xF0, PN | P66, mnemonic::movbe, GV, MV, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF1, PN | P66, mnemonic::movbe, MV, GV, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF0, PF2, mnemonic::crc32, GY, EB, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF1, PF2, mnemonic::crc32, GY, EV, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF2, PN, mnemonic::andn, GY, BY, EY, NO, MODRM | VEX_ONLY );
		put_group ( 2, 0xF3, PN, G17, BY, EY, VEX_ONLY );
		put ( 2, 0xF5, PN, mnemonic::bzhi, GY, EY, BY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF5, PF3, mnemonic::pext, GY, BY, EY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF5, PF2, mnemonic::pdep, GY, BY, EY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF6, P66, mnemonic::adcx, GY, EY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF6, PF3, mnemonic::adox, GY, EY, NO, NO, MODRM | NO_VEX );
		put ( 2, 0xF6, PF2, mnemonic::mulx, GY, BY, EY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF7, PN, mnemonic::bextr, GY, EY, BY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF7, P66, mnemonic::shlx, GY, EY, BY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF7, PF3, mnemonic::sarx, GY, EY, BY, NO, MODRM | VEX_ONLY );
		put ( 2, 0xF7, PF2, mnemonic::shrx, GY, EY, BY, NO, MODRM | VEX_ONLY );
		in_group ( G17, 1, mnemonic::blsr );
		in_group ( G17, 2, mnemonic::blsmsk );
		in_group ( G17, 3, mnemonic::blsi );

		put ( 3, 0x00, P66, mnemonic::vpermq, VY, WY, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x01, P66, mnemonic::vpermpd, VY, WY, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x02, P66, mnemonic::vpblendd, VX, HX, WX, IB, MODRM | VEX_ONLY );
		put ( 3, 0x04, P66, mnemonic::vpermilps, VX, WX, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x05, P66, mnemonic::vpermilpd, VX, WX, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x06, P66, mnemonic::vperm2f128, VY, HY, WY, IB, MODRM | VEX_ONLY );
		put ( 3, 0x08, P66, mnemonic::roundps, VX, WX, IB );
		put ( 3, 0x09, P66, mnemonic::roundpd, VX, WX, IB );
		put ( 3, 0x0A, P66, mnemonic::roundss, VO, WD, IB, NO, MODRM | NDS );
		put ( 3, 0x0B, P66, mnemonic::roundsd, VO, WQ, IB, NO, MODRM | NDS );
		put ( 3, 0x0C, P66, mnemonic::blendps, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0x0D, P66, mnemonic::blendpd, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0x0E, P66, mnemonic::pblendw, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0x0F, PN, mnemonic::palignr, PQ, QQ, IB, NO, MODRM | NO_VEX );
		put ( 3, 0x0F, P66, mnemonic::palignr, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0x14, P66, mnemonic::pextrb, ER8, VO, IB );
		put ( 3, 0x15, P66, mnemonic::pextrw, ER16, VO, IB );
		put ( 3, 0x16, P66, mnemonic::pextrd, EY, VO, IB, NO, MODRM | W_NAME );
		put ( 3, 0x17, P66, mnemonic::extractps, ED, VO, IB );
		put ( 3, 0x18, P66, mnemonic::vinsertf128, VY, HY, WO, IB, MODRM | VEX_ONLY );
		put ( 3, 0x19, P66, mnemonic::vextractf128, WO, VY, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x1D, P66, mnemonic::vcvtps2ph, WH, VX, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x20, P66, mnemonic::pinsrb, VO, ER8, IB, NO, MODRM | NDS );
		put ( 3, 0x21, P66, mnemonic::insertps, VO, WD, IB, NO, MODRM | NDS );
		put ( 3, 0x22, P66, mnemonic::pinsrd, VO, EY, IB, NO, MODRM | NDS | W_NAME );
		put ( 3, 0x38, P66, mnemonic::vinserti128, VY, HY, WO, IB, MODRM | VEX_ONLY );
		put ( 3, 0x39, P66, mnemonic::vextracti128, WO, VY, IB, NO, MODRM | VEX_ONLY );
		put ( 3, 0x40, P66, mnemonic::dpps, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0x41, P66, mnemonic::dppd, VO, WO, IB, NO, MODRM | NDS );
		put ( 3, 0x42, P66, mnemonic::mpsadbw, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0x44, P66, mnemonic::pclmulqdq, VO, WO, IB, NO, MODRM | NDS );
		put ( 3, 0x46, P66, mnemonic::vperm2i128, VY, HY, WY, IB, MODRM | VEX_ONLY );
		put ( 3, 0x4A, P66, mnemonic::vblendvps, VX, HX, WX, LX, MODRM | VEX_ONLY );
		put ( 3, 0x4B, P66, mnemonic::vblendvpd, VX, HX, WX, LX, MODRM | VEX_ONLY );
		put ( 3, 0x4C, P66, mnemonic::vpblendvb, VX, HX, WX, LX, MODRM | VEX_ONLY );
		put ( 3, 0x60, P66, mnemonic::pcmpestrm, VO, WO, IB, NO, MODRM | NOWRITE );
		put ( 3, 0x61, P66, mnemonic::pcmpestri, VO, WO, IB, NO, MODRM | NOWRITE );
		put ( 3, 0x62, P66, mnemonic::pcmpistrm, VO, WO, IB, NO, MODRM | NOWRITE );
		put ( 3, 0x63, P66, mnemonic::pcmpistri, VO, WO, IB, NO, MODRM | NOWRITE );
		put ( 3, 0xCC, PN, mnemonic::sha1rnds4, VO, WO, IB, NO, MODRM | NO_VEX );
		put ( 3, 0xCE, P66, mnemonic::gf2p8affineqb, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0xCF, P66, mnemonic::gf2p8affineinvqb, VX, WX, IB, NO, MODRM | NDS );
		put ( 3, 0xDF, P66, mnemonic::aeskeygenassist, VO, WO, IB );
		put ( 3, 0xF0, PF2, mnemonic::rorx, GY, EY, IB, NO, MODRM | VEX_ONLY );

	}

	static void build_x87 ( ) {

		const mnemonic arithmetic [ 8 ] = { mnemonic::fadd, mnemonic::fmul, mnemonic::fcom, mnemonic::fcomp, mnemonic::fsub, mnemonic::fsubr, mnemonic::fdiv, mnemonic::fdivr };
		const mnemonic integer [ 8 ] = { mnemonic::fiadd, mnemonic::fimul, mnemonic::ficom, mnemonic::ficomp, mnemonic::fisub, mnemonic::fisubr, mnemonic::fidiv, mnemonic::fidivr };

		for ( int32_t i = 0; i < 8; i++ ) {

			x87_memory [ 0 ] [ i ] = make ( arithmetic [ i ], MD, NO, NO, NO, 0 );
			x87_memory [ 2 ] [ i ] = make ( integer [ i ], MD, NO, NO, NO, 0 );
			x87_memory [ 4 ] [ i ] = make ( arithmetic [ i ], MQ, NO, NO, NO, 0 );
			x87_memory [ 6 ] [ i ] = make ( integer [ i ], MW, NO, NO, NO, 0 );

		}

		x87_memory [ 1 ] [ 0 ] = make ( mnemonic::fld, MD, NO, NO, NO, 0 );
		x87_memory [ 1 ] [ 2 ] = make ( mnemonic::fst, MD, NO, NO, NO, 0 );
		x87_memory [ 1 ] [ 3 ] = make ( mnemonic::fstp, MD, NO, NO, NO, 0 );
		x87_memory [ 1 ] [ 4 ] = make ( mnemonic::fldenv, M0, NO, NO, NO, 0 );
		x87_memory [ 1 ] [ 5 ] = make ( mnemonic::fldcw, MW, NO, NO, NO, 0 );
		x87_memory [ 1 ] [ 6 ] = make ( mnemonic::fnstenv, M0, NO, NO, NO, 0 );
		x87_memory [ 1 ] [ 7 ] = make ( mnemonic::fnstcw, MW, NO, NO, NO, 0 );
		x87_memory [ 3 ] [ 0 ] = make ( mnemonic::fild, MD, NO, NO, NO, 0 );
		x87_memory [ 3 ] [ 1 ] = make ( mnemonic::fisttp, MD, NO, NO, NO, 0 );
		x87_memory [ 3 ] [ 2 ] = make ( mnemonic::fist, MD, NO, NO, NO, 0 );
		x87_memory [ 3 ] [ 3 ] = make ( mnemonic::fistp, MD, NO, NO, NO, 0 );
		x87_memory [ 3 ] [ 5 ] = make ( mnemonic::fld, MT, NO, NO, NO, 0 );
		x87_memory [ 3 ] [ 7 ] = make ( mnemonic::fstp, MT, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 0 ] = make ( mnemonic::fld, MQ, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 1 ] = make ( mnemonic::fisttp, MQ, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 2 ] = make ( mnemonic::fst, MQ, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 3 ] = make ( mnemonic::fstp, MQ, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 4 ] = make ( mnemonic::frstor, M0, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 6 ] = make ( mnemonic::fnsave, M0, NO, NO, NO, 0 );
		x87_memory [ 5 ] [ 7 ] = make ( mnemonic::fnstsw, MW, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 0 ] = make ( mnemonic::fild, MW, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 1 ] = make ( mnemonic::fisttp, MW, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 2 ] = make ( mnemonic::fist, MW, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 3 ] = make ( mnemonic::fistp, MW, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 4 ] = make ( mnemonic::fbld, MT, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 5 ] = make ( mnemonic::fild, MQ, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 6 ] = make ( mnemonic::fbstp, MT, NO, NO, NO, 0 );
		x87_memory [ 7 ] [ 7 ] = make ( mnemonic::fistp, MQ, NO, NO, NO, 0 );

		auto row = [ ] ( int32_t code, int32_t reg, mnemonic name, uint8_t a, uint8_t b ) {

			for ( int32_t i = 0; i < 8; i++ )
				x87_register [ code ] [ reg * 8 + i ] = make ( name, a, b, NO, NO, 0 );

		};

		auto single = [ ] ( int32_t code, int32_t index, mnemonic name ) {

			x87_register [ code ] [ index ] = make ( name, NO, NO, NO, NO, 0 );

		};

		for ( int32_t i = 0; i < 8; i++ ) {

			bool unary = i == 2 || i == 3;

			row ( 0, i, arithmetic [ i ], unary ? STI : ST0_, unary ? NO : STI );

		}

		row ( 1, 0, mnemonic::fld, STI, NO );
		row ( 1, 1, mnemonic::fxch, STI, NO );
		single ( 1, 0x10, mnemonic::fnop );
		single ( 1, 0x20, mnemonic::fchs );
		single ( 1, 0x21, mnemonic::fabs );
		single ( 1, 0x24, mnemonic::ftst );
		single ( 1, 0x25, mnemonic::fxam );

		const mnemonic constants [ 7 ] = { mnemonic::fld1, mnemonic::fldl2t, mnemonic::fldl2e, mnemonic::fldpi, mnemonic::fldlg2, mnemonic::fldln2, mnemonic::fldz };
		const mnemonic transcend [ 16 ] = { mnemonic::f2xm1, mnemonic::fyl2x, mnemonic::fptan, mnemonic::fpatan, mnemonic::fxtract, mnemonic::fprem1, mnemonic::fdecstp, mnemonic::fincstp, mnemonic::fprem, mnemonic::fyl2xp1, mnemonic::fsqrt, mnemonic::fsincos, mnemonic::frndint, mnemonic::fscale, mnemonic::fsin, mnemonic::fcos };

		for ( int32_t i = 0; i < 7; i++ )
			single ( 1, 0x28 + i, constants [ i ] );

		for ( int32_t i = 0; i < 16; i++ )
			single ( 1, 0x30 + i, transcend [ i ] );

		row ( 2, 0, mnemonic::fcmovb, ST0_, STI );
		row ( 2, 1, mnemonic::fcmove, ST0_, STI );
		row ( 2, 2, mnemonic::fcmovbe, ST0_, STI );
		row ( 2, 3, mnemonic::fcmovu, ST0_, STI );
		single ( 2, 0x29, mnemonic::fucompp );
		row ( 3, 0, mnemonic::fcmovnb, ST0_, STI );
		row ( 3, 1, mnemonic::fcmovne, ST0_, STI );
		row ( 3, 2, mnemonic::fcmovnbe, ST0_, STI );
		row ( 3, 3, mnemonic::fcmovnu, ST0_, STI );
		single ( 3, 0x22, mnemonic::fnclex );
		single ( 3, 0x23, mnemonic::fninit );
		row ( 3, 5, mnemonic::fucomi, ST0_, STI );
		row ( 3, 6, mnemonic::fcomi, ST0_, STI );
		row ( 4, 0, mnemonic::fadd, STI, ST0_ );
		row ( 4, 1, mnemonic::fmul, STI, ST0_ );
		row ( 4, 4, mnemonic::fsubr, STI, ST0_ );
		row ( 4, 5, mnemonic::fsub, STI, ST0_ );
		row ( 4, 6, mnemonic::fdivr, STI, ST0_ );
		row ( 4, 7, mnemonic::fdiv, STI, ST0_ );
		row ( 5, 0, mnemonic::ffree, STI, NO );
		row ( 5, 2, mnemonic::fst, STI, NO );
		row ( 5, 3, mnemonic::fstp, STI, NO );
		row ( 5, 4, mnemonic::fucom, STI, NO );
		row ( 5, 5, mnemonic::fucomp, STI, NO );
		row ( 6, 0, mnemonic::faddp, STI, ST0_ );
		row ( 6, 1, mnemonic::fmulp, STI, ST0_ );
		single ( 6, 0x19, mnemonic::fcompp );
		row ( 6, 4, mnemonic::fsubrp, STI, ST0_ );
		row ( 6, 5, mnemonic::fsubp, STI, ST0_ );
		row ( 6, 6, mnemonic::fdivrp, STI, ST0_ );
		row ( 6, 7, mnemonic::fdivp, STI, ST0_ );
		row ( 7, 0, mnemonic::ffreep, STI, NO );
		x87_register [ 7 ] [ 0x20 ] = make ( mnemonic::fnstsw, R_AX16, NO, NO, NO, 0 );
		row ( 7, 5, mnemonic::fucomip, ST0_, STI );
		row ( 7, 6, mnemonic::fcomip, ST0_, STI );

	}

#define PLUMB_NAME( value ) #value,

	static const char* const mnemonic_list [ ] = { PLUMB_MNEMONICS ( PLUMB_NAME ) };

#undef PLUMB_NAME

	static char name_storage [ 40000 ];
	static int16_t evex_first [ 8 ] [ 256 ];
	static int16_t evex_count [ 8 ] [ 256 ];
	static int16_t vex_first [ 4 ] [ 256 ];
	static int16_t vex_count [ 4 ] [ 256 ];

	static void build_names ( ) {

		int32_t used = 0;

		for ( int32_t i = 0; i < static_cast < int32_t > ( mnemonic::COUNT ); i++ ) {

			const char* source = mnemonic_list [ i ];
			char* target = name_storage + used;
			int32_t length = 0;

			while ( source [ length ] )
				length++;

			if ( length && source [ length - 1 ] == '_' )
				length--;

			for ( int32_t k = 0; k < length; k++ )
				target [ k ] = source [ k ];

			target [ length ] = 0;
			names [ i ] = target;
			used += length + 1;

		}

		static const char* const gpr64 [ 16 ] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
		static const char* const gpr32 [ 16 ] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };
		static const char* const gpr16 [ 16 ] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" };
		static const char* const gpr8 [ 20 ] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b", "ah", "ch", "dh", "bh" };
		static const char* const segments [ 6 ] = { "es", "cs", "ss", "ds", "fs", "gs" };
		static const char* const stack [ 8 ] = { "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)" };
		static const char* const mmx [ 8 ] = { "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7" };
		static const char* const control [ 16 ] = { "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7", "cr8", "cr9", "cr10", "cr11", "cr12", "cr13", "cr14", "cr15" };
		static const char* const debug [ 16 ] = { "dr0", "dr1", "dr2", "dr3", "dr4", "dr5", "dr6", "dr7", "dr8", "dr9", "dr10", "dr11", "dr12", "dr13", "dr14", "dr15" };

		register_names [ NONE ] = "";

		for ( int32_t i = 0; i < 16; i++ ) {

			register_names [ RAX + i ] = gpr64 [ i ];
			register_names [ EAX + i ] = gpr32 [ i ];
			register_names [ AX + i ] = gpr16 [ i ];
			register_names [ CR0 + i ] = control [ i ];
			register_names [ DR0 + i ] = debug [ i ];

		}

		for ( int32_t i = 0; i < 20; i++ )
			register_names [ AL + i ] = gpr8 [ i ];

		for ( int32_t i = 0; i < 6; i++ )
			register_names [ ES + i ] = segments [ i ];

		static const char* const tiles [ 8 ] = { "tmm0", "tmm1", "tmm2", "tmm3", "tmm4", "tmm5", "tmm6", "tmm7" };

		for ( int32_t i = 0; i < 8; i++ ) {

			register_names [ ST0 + i ] = stack [ i ];
			register_names [ MM0 + i ] = mmx [ i ];
			register_names [ TMM0 + i ] = tiles [ i ];

		}

		register_names [ RIP ] = "rip";
		register_names [ EIP ] = "eip";

		static char vector_names [ 104 ] [ 6 ];
		const char letters [ 4 ] = { 'x', 'y', 'z', 'k' };

		for ( int32_t bank = 0; bank < 4; bank++ ) {

			int32_t count = bank == 3 ? 8 : 32;

			for ( int32_t i = 0; i < count; i++ ) {

				char* text = vector_names [ bank * 32 + i ];
				int32_t length = 0;

				text [ length++ ] = letters [ bank ];

				if ( bank < 3 ) {

					text [ length++ ] = 'm';
					text [ length++ ] = 'm';

				}

				if ( i >= 10 )
					text [ length++ ] = static_cast < char > ( '0' + i / 10 );

				text [ length++ ] = static_cast < char > ( '0' + i % 10 );
				text [ length ] = 0;
				register_names [ ( bank == 0 ? XMM0 : bank == 1 ? YMM0 : bank == 2 ? ZMM0 : K0 ) + i ] = text;

			}

		}

	}

	void prepare ( ) {

		if ( ready )
			return;

		build_names ( );
		build_one_byte ( );
		build_two_byte ( );
		build_three_byte ( );
		build_x87 ( );

		for ( int32_t i = 0; i < evex_forms_count; i++ ) {

			const simd_form& e = evex_forms [ i ];

			if ( !evex_count [ e.map ] [ e.opcode ]++ )
				evex_first [ e.map ] [ e.opcode ] = static_cast < int16_t > ( i );

		}

		for ( int32_t i = 0; i < vex_forms_count; i++ ) {

			const simd_form& e = vex_forms [ i ];

			if ( !vex_count [ e.map ] [ e.opcode ]++ )
				vex_first [ e.map ] [ e.opcode ] = static_cast < int16_t > ( i );

		}

		ready = true;

	}

	const char* mnemonic_name ( mnemonic value ) {

		int32_t index = static_cast < int32_t > ( value );

		return index >= 0 && index < static_cast < int32_t > ( mnemonic::COUNT ) && names [ index ] ? names [ index ] : "?";

	}

	const char* register_name ( uint8_t value ) {

		return value < REG_COUNT && register_names [ value ] ? register_names [ value ] : "?";

	}

	uint8_t register_size ( uint8_t value ) {

		if ( value >= RAX && value < EAX )
			return 8;

		if ( value >= EAX && value < AX )
			return 4;

		if ( value >= AX && value < AL )
			return 2;

		if ( value >= AL && value < ES )
			return 1;

		if ( value >= ES && value < RIP )
			return 2;

		if ( value == RIP )
			return 8;

		if ( value == EIP )
			return 4;

		if ( value >= ST0 && value < MM0 )
			return 10;

		if ( value >= MM0 && value < XMM0 )
			return 8;

		if ( value >= XMM0 && value < YMM0 )
			return 16;

		if ( value >= YMM0 && value < ZMM0 )
			return 32;

		if ( value >= ZMM0 && value < CR0 )
			return 64;

		if ( value >= TMM0 && value < REG_COUNT )
			return 0;

		return 8;

	}

	int32_t register_family ( uint8_t value ) {

		if ( value >= RAX && value < AH )
			return ( value - RAX ) % 16;

		if ( value >= AH && value < ES )
			return value - AH;

		if ( value >= XMM0 && value < CR0 )
			return 32 + ( value - XMM0 ) % 32;

		return 100 + value;

	}

	bool same_family ( uint8_t a, uint8_t b ) {

		return register_family ( a ) == register_family ( b );

	}

	uint8_t resize_register ( uint8_t value, uint8_t size ) {

		int32_t family = register_family ( value );

		if ( family >= 16 )
			return value;

		switch ( size ) {

			case 8:
				return static_cast < uint8_t > ( RAX + family );

			case 4:
				return static_cast < uint8_t > ( EAX + family );

			case 2:
				return static_cast < uint8_t > ( AX + family );

			default:
				return static_cast < uint8_t > ( AL + family );

		}

	}

	bool is_vector ( uint8_t value ) {

		return value >= XMM0 && value < CR0;

	}

	uint64_t instruction::target ( ) const {

		for ( int32_t i = 0; i < count; i++ ) {

			if ( operands [ i ].type == kind::REL )
				return static_cast < uint64_t > ( operands [ i ].value );

		}

		return 0;

	}

	bool instruction::direct ( ) const {

		return control != flow::NONE && count > 0 && operands [ 0 ].type == kind::REL;

	}

	bool instruction::memory_target ( uint64_t& result ) const {

		for ( int32_t i = 0; i < count; i++ ) {

			const operand& item = operands [ i ];

			if ( item.type != kind::MEM )
				continue;

			if ( item.base == RIP ) {

				result = address + length + static_cast < uint64_t > ( item.value );

				return true;

			}

			if ( item.base == EIP ) {

				result = static_cast < uint32_t > ( address + length + static_cast < uint64_t > ( item.value ) );

				return true;

			}

			if ( item.base == NONE && item.index == NONE ) {

				result = static_cast < uint64_t > ( item.value );

				return true;

			}

		}

		return false;

	}

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

	static int64_t sign_extend ( uint64_t value, int32_t bytes ) {

		int32_t shift = 64 - bytes * 8;

		return shift <= 0 ? static_cast < int64_t > ( value ) : static_cast < int64_t > ( value << shift ) >> shift;

	}

	static uint8_t gpr ( int32_t number, int32_t size, bool rex ) {

		switch ( size ) {

			case 8:
				return static_cast < uint8_t > ( RAX + number );

			case 4:
				return static_cast < uint8_t > ( EAX + number );

			case 2:
				return static_cast < uint8_t > ( AX + number );

			default:
				return static_cast < uint8_t > ( !rex && number >= 4 && number < 8 ? AH + number - 4 : AL + number );

		}

	}

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

	static bool read_memory ( cursor& in, state& s, instruction& out ) {

		operand& m = s.memory;

		m = operand ( );
		m.type = kind::MEM;
		m.scale = 1;
		m.segment = out.segment;

		if ( s.address_size == 2 ) {

			static const uint8_t bases [ 8 ] = { BX, BX, BP, BP, SI, DI, BP, BX };
			static const uint8_t indexes [ 8 ] = { SI, DI, SI, DI, NONE, NONE, NONE, NONE };

			if ( s.mod == 0 && s.rm == 6 ) {

				out.displacement_offset = static_cast < uint8_t > ( in.at );
				out.displacement_size = 2;
				m.value = static_cast < int64_t > ( in.read ( 2 ) );

				return !in.failed;

			}

			m.base = bases [ s.rm ];
			m.index = indexes [ s.rm ];

			if ( s.mod == 1 ) {

				out.displacement_offset = static_cast < uint8_t > ( in.at );
				out.displacement_size = 1;
				m.value = sign_extend ( in.read ( 1 ), 1 );

			} else if ( s.mod == 2 ) {

				out.displacement_offset = static_cast < uint8_t > ( in.at );
				out.displacement_size = 2;
				m.value = sign_extend ( in.read ( 2 ), 2 );

			}

			return !in.failed;

		}

		int32_t size = s.address_size;
		bool absolute = false;

		if ( s.rm == 4 ) {

			uint8_t sib = in.byte ( );
			int32_t index = ( ( sib >> 3 ) & 7 ) | ( s.rex_x ? 8 : 0 );

			s.sib = sib;
			int32_t base = ( sib & 7 ) | ( s.rex_b ? 8 : 0 );

			m.scale = static_cast < uint8_t > ( 1 << ( sib >> 6 ) );

			if ( index != 4 )
				m.index = gpr ( index, size, true );

			if ( ( sib & 7 ) == 5 && s.mod == 0 )
				absolute = true;
			else
				m.base = gpr ( base, size, true );

		} else if ( s.rm == 5 && s.mod == 0 ) {

			if ( s.long_mode )
				m.base = size == 8 ? RIP : EIP;

			absolute = true;

		} else
			m.base = gpr ( s.rm | ( s.rex_b ? 8 : 0 ), size, true );

		if ( absolute || s.mod == 2 ) {

			out.displacement_offset = static_cast < uint8_t > ( in.at );
			out.displacement_size = 4;
			m.value = sign_extend ( in.read ( 4 ), 4 );

			if ( absolute && m.base == NONE && m.index == NONE && !s.long_mode )
				m.value = static_cast < int64_t > ( static_cast < uint32_t > ( m.value ) );

		} else if ( s.mod == 1 ) {

			out.displacement_offset = static_cast < uint8_t > ( in.at );
			out.displacement_size = 1;
			m.value = sign_extend ( in.read ( 1 ), 1 );

		}

		return !in.failed;

	}

	static uint8_t vector ( int32_t number, int32_t width ) {

		int32_t bank = width >= 64 ? ZMM0 : ( width == 32 ? YMM0 : XMM0 );

		return static_cast < uint8_t > ( bank + ( number & 31 ) );

	}

	static bool build_operand ( uint8_t which, cursor& in, state& s, instruction& out, operand& item ) {

		int32_t reg = s.reg | ( s.rex_r ? 8 : 0 );
		int32_t rm = s.rm | ( s.rex_b ? 8 : 0 );
		int32_t vreg = reg | ( s.rex_r2 ? 16 : 0 );
		int32_t vrm = rm | ( s.evex && s.rex_x && s.mod == 3 ? 16 : 0 );
		int32_t w = s.width;
		uint8_t os = s.operand_size;
		uint8_t y = s.long_mode && s.rex_w ? 8 : 4;
		bool rex = s.rex_present;

		auto make_reg = [ & ] ( uint8_t value ) {

			item.type = kind::REG;
			item.reg = value;
			item.size = register_size ( value );

		};

		auto make_mem = [ & ] ( uint8_t size ) {

			item = s.memory;
			item.size = size;

		};

		auto rm_or_mem = [ & ] ( uint8_t reg_value, uint8_t size ) {

			if ( s.mod == 3 )
				make_reg ( reg_value );
			else
				make_mem ( size );

		};

		auto immediate = [ & ] ( int32_t bytes, bool sign, uint8_t size ) {

			if ( !out.immediate_size ) {

				out.immediate_offset = static_cast < uint8_t > ( in.at );
				out.immediate_size = static_cast < uint8_t > ( bytes );

			}

			uint64_t raw = in.read ( bytes );

			item.type = kind::IMM;
			item.size = size;
			item.value = sign ? sign_extend ( raw, bytes ) : static_cast < int64_t > ( raw );

		};

		switch ( which ) {

			case EB:
				rm_or_mem ( gpr ( rm, 1, rex ), 1 );
				break;

			case EW:
				rm_or_mem ( gpr ( rm, 2, rex ), 2 );
				break;

			case ED:
				rm_or_mem ( gpr ( rm, 4, rex ), 4 );
				break;

			case EQ:
				rm_or_mem ( gpr ( rm, 8, rex ), 8 );
				break;

			case EV:
				rm_or_mem ( gpr ( rm, os, rex ), os );
				break;

			case EY:
				rm_or_mem ( gpr ( rm, y, rex ), y );
				break;

			case ER8:
				rm_or_mem ( gpr ( rm, 4, rex ), 1 );
				break;

			case ER16:
				rm_or_mem ( gpr ( rm, 4, rex ), 2 );
				break;

			case GB:
				make_reg ( gpr ( reg, 1, rex ) );
				break;

			case GW:
				make_reg ( gpr ( reg, 2, rex ) );
				break;

			case GD:
				make_reg ( gpr ( reg, 4, rex ) );
				break;

			case GQ:
				make_reg ( gpr ( reg, 8, rex ) );
				break;

			case GV:
				make_reg ( gpr ( reg, os, rex ) );
				break;

			case GY:
				make_reg ( gpr ( reg, y, rex ) );
				break;

			case BY:
				make_reg ( gpr ( s.vvvv & 15, y, true ) );
				break;

			case RY:
				make_reg ( gpr ( rm, s.long_mode ? 8 : 4, true ) );
				break;

			case M0:
			case MW:
			case MD:
			case MQ:
			case MT:
			case MO:
			case MX:
			case MY:
			case MV:
			case MP:
			case MF: {

				if ( s.mod == 3 )
					return false;

				uint8_t size = 0;

				switch ( which ) {

					case MW:
						size = 2;
						break;

					case MD:
						size = 4;
						break;

					case MQ:
						size = 8;
						break;

					case MT:
						size = 10;
						break;

					case MO:
						size = 16;
						break;

					case MX:
						size = static_cast < uint8_t > ( w );
						break;

					case MY:
						size = y;
						break;

					case MV:
						size = os;
						break;

					case MP:
					case MF:
						size = static_cast < uint8_t > ( os == 2 ? 4 : ( os == 8 ? 10 : 6 ) );
						break;

					default:
						break;

				}

				make_mem ( size );

				break;

			}

			case IB:
				immediate ( 1, false, 1 );
				break;

			case IBS:
				immediate ( 1, true, os );
				break;

			case IW:
				immediate ( 2, false, 2 );
				break;

			case IZ:
				immediate ( os == 2 ? 2 : 4, true, os );
				break;

			case IV:
				immediate ( os, os == 8, os );
				break;

			case JB:
			case JZ: {

				int32_t bytes = which == JB ? 1 : ( !s.long_mode && os == 2 ? 2 : 4 );

				if ( !out.immediate_size ) {

					out.immediate_offset = static_cast < uint8_t > ( in.at );
					out.immediate_size = static_cast < uint8_t > ( bytes );

				}

				item.type = kind::REL;
				item.size = static_cast < uint8_t > ( s.long_mode ? 8 : 4 );
				item.value = sign_extend ( in.read ( bytes ), bytes );

				break;

			}

			case OB:
			case OV: {

				item = operand ( );
				item.type = kind::MEM;
				item.scale = 1;
				item.segment = out.segment;
				item.size = which == OB ? 1 : os;
				out.displacement_offset = static_cast < uint8_t > ( in.at );
				out.displacement_size = s.address_size;
				item.value = static_cast < int64_t > ( in.read ( s.address_size ) );

				break;

			}

			case AP: {

				uint64_t offset_part = in.read ( os == 2 ? 2 : 4 );
				uint64_t selector = in.read ( 2 );

				item.type = kind::FAR;
				item.size = static_cast < uint8_t > ( os == 2 ? 4 : 6 );
				item.value = static_cast < int64_t > ( ( selector << 32 ) | offset_part );

				break;

			}

			case R_AL:
				make_reg ( AL );
				break;

			case R_CL:
				make_reg ( CL );
				break;

			case R_DX:
				make_reg ( DX );
				break;

			case R_AX:
				make_reg ( gpr ( 0, os, rex ) );
				break;

			case R_EAX:
				make_reg ( os == 2 ? AX : EAX );
				break;

			case R_ES:
			case R_CS:
			case R_SS:
			case R_DS:
			case R_FS:
			case R_GS:
				make_reg ( static_cast < uint8_t > ( ES + which - R_ES ) );
				break;

			case ONE:
				item.type = kind::IMM;
				item.size = 1;
				item.value = 1;
				break;

			case XMM0_:
				make_reg ( XMM0 );
				break;

			case ZB:
				make_reg ( gpr ( rm, 1, rex ) );
				break;

			case ZV:
				make_reg ( gpr ( rm, os, rex ) );
				break;

			case ZY:
				make_reg ( gpr ( rm, s.rex_w ? 8 : 4, rex ) );
				break;

			case SW:

				if ( s.reg > 5 )
					return false;

				make_reg ( static_cast < uint8_t > ( ES + s.reg ) );

				break;

			case CRX:
				make_reg ( static_cast < uint8_t > ( CR0 + reg ) );
				break;

			case DRX:
				make_reg ( static_cast < uint8_t > ( DR0 + reg ) );
				break;

			case STI:
				make_reg ( static_cast < uint8_t > ( ST0 + s.rm ) );
				break;

			case ST0_:
				make_reg ( ST0 );
				break;

			case VX:
				make_reg ( vector ( vreg, w ) );
				break;

			case VO:
				make_reg ( vector ( vreg, 16 ) );
				break;

			case VY:
				make_reg ( vector ( vreg, 32 ) );
				break;

			case HX:
				make_reg ( vector ( s.vvvv, w ) );
				break;

			case HO:
				make_reg ( vector ( s.vvvv, 16 ) );
				break;

			case HY:
				make_reg ( vector ( s.vvvv, 32 ) );
				break;

			case UX:

				if ( s.mod != 3 )
					return false;

				make_reg ( vector ( vrm, w ) );

				break;

			case UO:

				if ( s.mod != 3 )
					return false;

				make_reg ( vector ( vrm, 16 ) );

				break;

			case WX:
				rm_or_mem ( vector ( vrm, w ), static_cast < uint8_t > ( w ) );
				break;

			case WY:
				rm_or_mem ( vector ( vrm, 32 ), 32 );
				break;

			case WO:
				rm_or_mem ( vector ( vrm, 16 ), 16 );
				break;

			case WQ:
				rm_or_mem ( vector ( vrm, 16 ), 8 );
				break;

			case WD:
				rm_or_mem ( vector ( vrm, 16 ), 4 );
				break;

			case WW:
				rm_or_mem ( vector ( vrm, 16 ), 2 );
				break;

			case WB:
				rm_or_mem ( vector ( vrm, 16 ), 1 );
				break;

			case WH:
				rm_or_mem ( vector ( vrm, base::max ( w / 2, 16 ) ), static_cast < uint8_t > ( w / 2 ) );
				break;

			case WQ4:
				rm_or_mem ( vector ( vrm, base::max ( w / 4, 16 ) ), static_cast < uint8_t > ( w / 4 ) );
				break;

			case WE:
				rm_or_mem ( vector ( vrm, 16 ), static_cast < uint8_t > ( w / 8 ) );
				break;

			case WQX:
				rm_or_mem ( vector ( vrm, w ), static_cast < uint8_t > ( w == 16 ? 8 : w ) );
				break;

			case WS:
				rm_or_mem ( vector ( vrm, 16 ), static_cast < uint8_t > ( s.rex_w ? 8 : 4 ) );
				break;

			case LX: {

				uint8_t value = in.byte ( );

				if ( !out.immediate_size ) {

					out.immediate_offset = static_cast < uint8_t > ( in.at - 1 );
					out.immediate_size = 1;

				}

				make_reg ( vector ( ( value >> 4 ) & ( s.long_mode ? 15 : 7 ), w ) );

				break;

			}

			case PQ:
				make_reg ( static_cast < uint8_t > ( MM0 + s.reg ) );
				break;

			case QQ:
				rm_or_mem ( static_cast < uint8_t > ( MM0 + s.rm ), 8 );
				break;

			case QD:
				rm_or_mem ( static_cast < uint8_t > ( MM0 + s.rm ), 4 );
				break;

			case NQ:

				if ( s.mod != 3 )
					return false;

				make_reg ( static_cast < uint8_t > ( MM0 + s.rm ) );

				break;

			case KR:
				make_reg ( static_cast < uint8_t > ( K0 + ( s.reg & 7 ) ) );
				break;

			case KV:
				make_reg ( static_cast < uint8_t > ( K0 + ( s.vvvv & 7 ) ) );
				break;

			case KB:

				if ( s.mod != 3 )
					return false;

				make_reg ( static_cast < uint8_t > ( K0 + ( s.rm & 7 ) ) );

				break;

			case KRM:
				rm_or_mem ( static_cast < uint8_t > ( K0 + ( s.rm & 7 ) ), os );
				break;

			case KGR:
				make_reg ( gpr ( reg, os == 8 ? 8 : 4, true ) );
				break;

			case KGB:

				if ( s.mod != 3 )
					return false;

				make_reg ( gpr ( rm, os == 8 ? 8 : 4, true ) );

				break;

			case EWV:

				if ( s.mod == 3 )
					make_reg ( gpr ( rm, os, rex ) );
				else
					make_mem ( 2 );

				break;

			case EZ:
				rm_or_mem ( gpr ( rm, s.long_mode ? 8 : 4, rex ), static_cast < uint8_t > ( s.long_mode ? 8 : 4 ) );
				break;

			case GZ:
				make_reg ( gpr ( reg, s.long_mode ? 8 : 4, rex ) );
				break;

			case GA:
				make_reg ( gpr ( reg, s.address_size, rex ) );
				break;

			case MZ:

				if ( s.mod == 3 )
					return false;

				make_mem ( 64 );

				break;

			case RD:
			case RQ:
			case RA:

				if ( s.mod != 3 )
					return false;

				make_reg ( gpr ( rm, which == RD ? 4 : ( which == RQ ? 8 : s.address_size ), rex ) );

				break;

			case R_AX16:
				make_reg ( AX );
				break;

			default:
				return false;

		}

		return !in.failed;

	}

	static mnemonic amd_3dnow ( uint8_t code ) {

		switch ( code ) {

			case 0x0C:
				return mnemonic::pi2fw;

			case 0x0D:
				return mnemonic::pi2fd;

			case 0x1C:
				return mnemonic::pf2iw;

			case 0x1D:
				return mnemonic::pf2id;

			case 0x8A:
				return mnemonic::pfnacc;

			case 0x8E:
				return mnemonic::pfpnacc;

			case 0x90:
				return mnemonic::pfcmpge;

			case 0x94:
				return mnemonic::pfmin;

			case 0x96:
				return mnemonic::pfrcp;

			case 0x97:
				return mnemonic::pfrsqrt;

			case 0x9A:
				return mnemonic::pfsub;

			case 0x9E:
				return mnemonic::pfadd;

			case 0xA0:
				return mnemonic::pfcmpgt;

			case 0xA4:
				return mnemonic::pfmax;

			case 0xA6:
				return mnemonic::pfrcpit1;

			case 0xA7:
				return mnemonic::pfrsqit1;

			case 0xAA:
				return mnemonic::pfsubr;

			case 0xAE:
				return mnemonic::pfacc;

			case 0xB0:
				return mnemonic::pfcmpeq;

			case 0xB4:
				return mnemonic::pfmul;

			case 0xB6:
				return mnemonic::pfrcpit2;

			case 0xB7:
				return mnemonic::pmulhrw;

			case 0xBB:
				return mnemonic::pswapd;

			case 0xBF:
				return mnemonic::pavgusb;

			default:
				return mnemonic::invalid;

		}

	}

	static bool refine ( form& f, state& s, int32_t map, uint8_t opcode, uint8_t mandatory ) {

		if ( map == 1 && ( opcode == 0x12 || opcode == 0x16 ) && mandatory <= 1 && s.mod == 3 ) {

			f.name = opcode == 0x12 ? mnemonic::movhlps : mnemonic::movlhps;
			f.ops [ 0 ] = VO;
			f.ops [ 1 ] = UO;

		}

		if ( map == 1 && ( opcode == 0x78 || opcode == 0x79 ) && ( mandatory == 1 || mandatory == 3 ) && !s.vex && s.mod != 3 ) {

			f.name = opcode == 0x78 ? mnemonic::vmread : mnemonic::vmwrite;
			f.ops [ 0 ] = opcode == 0x78 ? EZ : GZ;
			f.ops [ 1 ] = opcode == 0x78 ? GZ : EZ;
			f.ops [ 2 ] = NO;
			f.ops [ 3 ] = NO;

		}

		if ( map == 2 && !s.vex && mandatory == 2 ) {

			if ( opcode == 0xDC && s.mod == 3 ) {

				f.name = mnemonic::loadiwkey;
				f.ops [ 0 ] = VO;
				f.ops [ 1 ] = UO;

			}

			if ( opcode == 0xD8 ) {

				if ( s.mod == 3 || s.reg > 3 )
					return false;

				const mnemonic wide [ 4 ] = { mnemonic::aesencwide128kl, mnemonic::aesdecwide128kl, mnemonic::aesencwide256kl, mnemonic::aesdecwide256kl };

				f.name = wide [ s.reg ];

			}

		}

		if ( map == 2 && !s.vex && opcode == 0xF8 && ( mandatory == 2 || mandatory == 3 ) && s.mod == 3 ) {

			f.name = mandatory == 2 ? mnemonic::uwrmsr : mnemonic::urdmsr;
			f.ops [ 0 ] = GQ;
			f.ops [ 1 ] = RQ;

		}

		return true;

	}

	static bool resolve_group ( form& f, state& s, uint8_t mandatory, instruction& out ) {

		const form& g = groups [ f.group ] [ s.reg ];
		uint8_t m = s.mod;
		uint8_t r = s.reg;
		uint8_t low = s.rm;

		auto set = [ & ] ( mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint16_t extra ) {

			f.name = name;
			f.ops [ 0 ] = a;
			f.ops [ 1 ] = b;
			f.ops [ 2 ] = c;
			f.ops [ 3 ] = NO;
			f.flags = static_cast < uint16_t > ( ( f.flags & ~NDS ) | extra );

		};

		switch ( f.group ) {

			case G6:

				if ( r == 6 ) {

					if ( mandatory != 3 || !s.long_mode )
						return false;

					set ( mnemonic::lkgs, EW, NO, NO, NOWRITE );
					s.prefix_used = true;

					return true;

				}

				break;

			case G7:

				if ( m == 3 ) {

					static const mnemonic plain [ 8 ] [ 8 ] = {
						{ mnemonic::enclv, mnemonic::vmcall, mnemonic::vmlaunch, mnemonic::vmresume, mnemonic::vmxoff, mnemonic::pconfig, mnemonic::wrmsrns, mnemonic::pbndkb },
						{ mnemonic::monitor, mnemonic::mwait, mnemonic::clac, mnemonic::stac, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::encls },
						{ mnemonic::xgetbv, mnemonic::xsetbv, mnemonic::invalid, mnemonic::invalid, mnemonic::vmfunc, mnemonic::xend, mnemonic::xtest, mnemonic::enclu },
						{ mnemonic::vmrun, mnemonic::vmmcall, mnemonic::vmload, mnemonic::vmsave, mnemonic::stgi, mnemonic::clgi, mnemonic::skinit, mnemonic::invlpga },
						{ mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw },
						{ mnemonic::serialize, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::rdpkru, mnemonic::wrpkru },
						{ mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw },
						{ mnemonic::swapgs, mnemonic::rdtscp, mnemonic::monitorx, mnemonic::mwaitx, mnemonic::clzero, mnemonic::rdpru, mnemonic::invlpgb, mnemonic::tlbsync }
					};

					uint8_t code = static_cast < uint8_t > ( 0xC0 | r << 3 | low );
					mnemonic name = plain [ r ] [ low ];

					if ( !s.long_mode && name == mnemonic::pbndkb )
						return false;

					if ( !s.long_mode && ( code == 0xC6 || code == 0xCA || ( code >= 0xCD && code <= 0xCF ) || ( code >= 0xEC && code <= 0xEF ) || code == 0xFD || code == 0xFE || code == 0xFF ) && mandatory != 0 ) {

						if ( code == 0xFF && mandatory == 3 ) {

							set ( mnemonic::pvalidate, NO, NO, NO, 0 );
							s.prefix_used = true;

							return true;

						}

						if ( code == 0xEC || code == 0xED )
							return false;

					} else if ( mandatory == 1 ) {

						if ( code >= 0xCC && code <= 0xCF ) {

							const mnemonic seam [ 4 ] = { mnemonic::tdcall, mnemonic::seamret, mnemonic::seamops, mnemonic::seamcall };

							name = seam [ code - 0xCC ];
							s.prefix_used = true;

						}

					} else if ( mandatory == 2 ) {

						const uint8_t codes [ 11 ] = { 0xC6, 0xE8, 0xEA, 0xEC, 0xED, 0xEE, 0xEF, 0xFD, 0xFE, 0xFF, 0xCA };
						const mnemonic names_f3 [ 11 ] = { mnemonic::wrmsrlist, mnemonic::setssbsy, mnemonic::saveprevssp, mnemonic::uiret, mnemonic::testui, mnemonic::clui, mnemonic::stui, mnemonic::rmpquery, mnemonic::rmpadjust, mnemonic::psmash, mnemonic::eretu };

						for ( int32_t i = 0; i < 11; i++ ) {

							if ( codes [ i ] == code ) {

								name = names_f3 [ i ];
								s.prefix_used = true;

							}

						}

					} else if ( mandatory == 3 ) {

						const uint8_t codes [ 6 ] = { 0xC6, 0xE8, 0xE9, 0xFE, 0xFF, 0xCA };
						const mnemonic names_f2 [ 6 ] = { mnemonic::rdmsrlist, mnemonic::xsusldtrk, mnemonic::xresldtrk, mnemonic::rmpupdate, mnemonic::pvalidate, mnemonic::erets };

						for ( int32_t i = 0; i < 6; i++ ) {

							if ( codes [ i ] == code ) {

								name = names_f2 [ i ];
								s.prefix_used = true;

							}

						}

					}

					if ( name == mnemonic::smsw )
						set ( name, EV, NO, NO, 0 );
					else if ( name == mnemonic::lmsw )
						set ( name, EW, NO, NO, 0 );
					else
						set ( name, NO, NO, NO, 0 );

					return name != mnemonic::invalid;

				}

				if ( r == 5 ) {

					if ( mandatory != 2 )
						return false;

					set ( mnemonic::rstorssp, MQ, NO, NO, NOWRITE );
					s.prefix_used = true;

					return true;

				}

				break;

			case G9:

				if ( m != 3 ) {

					if ( r == 1 ) {

						set ( s.rex_w ? mnemonic::cmpxchg16b : mnemonic::cmpxchg8b, s.rex_w ? MO : MQ, NO, NO, 0 );

						return true;

					}

					if ( r >= 3 && r <= 5 ) {

						const mnemonic saves [ 3 ] = { mnemonic::xrstors, mnemonic::xsavec, mnemonic::xsaves };

						set ( saves [ r - 3 ], M0, NO, NO, r == 3 ? NOWRITE : 0 );

						return true;

					}

					if ( r == 6 || r == 7 ) {

						mnemonic name = mnemonic::invalid;

						if ( r == 6 )
							name = mandatory == 1 ? mnemonic::vmclear : ( mandatory == 2 ? mnemonic::vmxon : mnemonic::vmptrld );
						else
							name = mnemonic::vmptrst;

						if ( name == mnemonic::invalid )
							return false;

						set ( name, MQ, NO, NO, name == mnemonic::vmptrst ? 0 : NOWRITE );
						s.prefix_used = name == mnemonic::vmclear || name == mnemonic::vmxon;

						return true;

					}

					return false;

				}

				if ( r == 6 ) {

					if ( mandatory == 2 && s.long_mode ) {

						set ( mnemonic::senduipi, RQ, NO, NO, NOWRITE );
						s.prefix_used = true;

						return true;

					}

					set ( mnemonic::rdrand, EV, NO, NO, 0 );

					return true;

				}

				if ( r == 7 ) {

					if ( mandatory == 2 )
						set ( mnemonic::rdpid, RY, NO, NO, 0 );
					else
						set ( mnemonic::rdseed, EV, NO, NO, 0 );

					return true;

				}

				return false;

			case G15:

				if ( m == 3 || ( mandatory == 2 && r == 4 ) ) {

					if ( mandatory == 2 && r == 4 ) {

						set ( mnemonic::ptwrite, EY, NO, NO, NOWRITE );
						s.prefix_used = true;

						return true;

					}

					if ( r == 6 && mandatory != 0 ) {

						if ( mandatory == 1 )
							set ( mnemonic::tpause, RD, NO, NO, NOWRITE );
						else if ( mandatory == 3 )
							set ( mnemonic::umwait, RD, NO, NO, NOWRITE );
						else
							set ( mnemonic::umonitor, RA, NO, NO, NOWRITE );

						s.prefix_used = true;

						return true;

					}

					if ( mandatory == 2 && r < 4 && s.long_mode ) {

						const mnemonic base_ops [ 4 ] = { mnemonic::rdfsbase, mnemonic::rdgsbase, mnemonic::wrfsbase, mnemonic::wrgsbase };

						set ( base_ops [ r ], EY, NO, NO, r >= 2 ? NOWRITE : 0 );

						return true;

					}

					if ( mandatory == 2 && r == 5 ) {

						set ( s.rex_w ? mnemonic::incsspq : mnemonic::incsspd, EY, NO, NO, NOWRITE );

						return true;

					}

					if ( ( r == 5 && mandatory != 2 ) || ( r == 6 && mandatory == 0 ) || r == 7 ) {

						const mnemonic fences [ 3 ] = { mnemonic::lfence, mnemonic::mfence, mnemonic::sfence };

						set ( fences [ r - 5 ], NO, NO, NO, 0 );

						return true;

					}

					return false;

				}

				{

					const mnemonic memory_ops [ 8 ] = { mnemonic::fxsave, mnemonic::fxrstor, mnemonic::ldmxcsr, mnemonic::stmxcsr, mnemonic::xsave, mnemonic::xrstor, mnemonic::xsaveopt, mnemonic::clflush };
					mnemonic name = memory_ops [ r ];

					if ( mandatory == 1 && r == 6 )
						name = mnemonic::clwb;
					else if ( mandatory == 1 && r == 7 )
						name = mnemonic::clflushopt;
					else if ( mandatory == 2 && r == 6 ) {

						set ( mnemonic::clrssbsy, MQ, NO, NO, 0 );
						s.prefix_used = true;

						return true;

					}

					set ( name, r == 2 || r == 3 ? MD : M0, NO, NO, r == 2 || r == 7 || r == 1 || r == 5 ? NOWRITE : 0 );

				}

				return true;

			case G12:
			case G13:
			case G14: {

				if ( m != 3 )
					return false;

				static const mnemonic table [ 3 ] [ 8 ] = {
					{ mnemonic::invalid, mnemonic::invalid, mnemonic::psrlw, mnemonic::invalid, mnemonic::psraw, mnemonic::invalid, mnemonic::psllw, mnemonic::invalid },
					{ mnemonic::invalid, mnemonic::invalid, mnemonic::psrld, mnemonic::invalid, mnemonic::psrad, mnemonic::invalid, mnemonic::pslld, mnemonic::invalid },
					{ mnemonic::invalid, mnemonic::invalid, mnemonic::psrlq, mnemonic::psrldq, mnemonic::invalid, mnemonic::invalid, mnemonic::psllq, mnemonic::pslldq }
				};

				mnemonic name = table [ f.group - G12 ] [ r ];

				if ( name == mnemonic::invalid )
					return false;

				if ( mandatory == 0 ) {

					if ( s.vex || name == mnemonic::psrldq || name == mnemonic::pslldq )
						return false;

					set ( name, NQ, IB, NO, 0 );

				} else if ( s.vex )
					set ( name, HX, UX, IB, 0 );
				else
					set ( name, UX, IB, NO, 0 );

				return true;

			}

			case G16:

				if ( m != 3 && r >= 6 && s.long_mode ) {

					set ( r == 7 ? mnemonic::prefetchit0 : mnemonic::prefetchit1, M0, NO, NO, NOWRITE );

					return true;

				}

				if ( m == 3 || r > 3 ) {

					set ( mnemonic::nop, EV, NO, NO, NOWRITE );

					return true;

				}

				break;

			case G_PREFETCH:

				if ( m == 3 )
					return false;

				set ( r == 1 ? mnemonic::prefetchw : ( r == 2 ? mnemonic::prefetchwt1 : mnemonic::prefetch ), M0, NO, NO, NOWRITE );

				return true;

			case G_HINT:

				if ( s.opcode == 0x1C && m != 3 && r == 0 ) {

					set ( mnemonic::cldemote, M0, NO, NO, NOWRITE );

					return true;

				}

				if ( s.opcode == 0x1E && mandatory == 2 && m == 3 && r == 7 && ( low == 2 || low == 3 ) ) {

					set ( low == 2 ? mnemonic::endbr64 : mnemonic::endbr32, NO, NO, NO, 0 );

					return true;

				}

				if ( s.opcode == 0x1E && mandatory == 2 && m == 3 && r == 1 ) {

					set ( s.rex_w ? mnemonic::rdsspq : mnemonic::rdsspd, EY, NO, NO, 0 );

					return true;

				}

				set ( mnemonic::nop, EV, NO, NO, NOWRITE );

				return true;

			case G11B:
			case G11V:

				if ( m == 3 && r == 7 && low == 0 ) {

					if ( f.group == G11B )
						set ( mnemonic::xabort, IB, NO, NO, NOWRITE );
					else
						set ( mnemonic::xbegin, JZ, NO, NO, NOWRITE );

					return true;

				}

				break;

			default:
				break;

		}

		if ( g.name == mnemonic::invalid )
			return false;

		f.name = g.name;
		f.flags = static_cast < uint16_t > ( f.flags | g.flags );

		if ( g.ops [ 0 ] != NO ) {

			for ( int32_t i = 0; i < 4; i++ )
				f.ops [ i ] = g.ops [ i ];

		}

		return true;

	}

	static bool writes_only ( mnemonic name ) {

		switch ( name ) {

			case mnemonic::mov:
			case mnemonic::movzx:
			case mnemonic::movsx:
			case mnemonic::movsxd:
			case mnemonic::lea:
			case mnemonic::pop:
			case mnemonic::movaps:
			case mnemonic::movapd:
			case mnemonic::movups:
			case mnemonic::movupd:
			case mnemonic::movdqa:
			case mnemonic::movdqu:
			case mnemonic::movd:
			case mnemonic::movq:
			case mnemonic::movss:
			case mnemonic::movsd:
			case mnemonic::movnti:
			case mnemonic::movntdq:
			case mnemonic::movntps:
			case mnemonic::movntpd:
			case mnemonic::lddqu:
			case mnemonic::cvtsi2sd:
			case mnemonic::cvtsi2ss:
			case mnemonic::cvttsd2si:
			case mnemonic::cvttss2si:
			case mnemonic::cvtsd2si:
			case mnemonic::cvtss2si:
			case mnemonic::stmxcsr:
			case mnemonic::fnstcw:
			case mnemonic::fnstsw:
			case mnemonic::fst:
			case mnemonic::fstp:
			case mnemonic::fist:
			case mnemonic::fistp:
			case mnemonic::fisttp:
				return true;

			default:
				return static_cast < int32_t > ( name ) >= static_cast < int32_t > ( mnemonic::seto ) && static_cast < int32_t > ( name ) <= static_cast < int32_t > ( mnemonic::setg );

		}

	}

	static int32_t evex_bytes ( uint8_t size, int32_t width ) {

		switch ( size & 15 ) {

			case 0:
				return width;

			case 1:
				return width / 2;

			case 2:
				return width / 4;

			case 3:
				return width / 8;

			case 4:
				return 1;

			case 5:
				return 2;

			case 6:
			case 11:
				return 4;

			case 7:
			case 12:
				return 8;

			case 8:
				return 16;

			case 9:
				return 32;

			case 10:
				return 64;

			case 14:
				return width == 16 ? 8 : width;

			default:
				return 0;

		}

	}

	static int32_t evex_lanes ( uint8_t size, int32_t width ) {

		switch ( size & 15 ) {

			case 1:
				return base::max ( width / 2, 16 );

			case 2:
				return base::max ( width / 4, 16 );

			default:
				return width;

		}

	}

	static bool decode_vector ( cursor& in, state& s, instruction& out, int32_t map, uint8_t opcode, uint8_t pp, bool addrsize, uint16_t prefixes ) {

		uint8_t modrm = in.byte ( );

		if ( in.failed )
			return false;

		s.has_modrm = true;
		s.mod = modrm >> 6;
		s.reg = ( modrm >> 3 ) & 7;
		s.rm = modrm & 7;
		s.address_size = static_cast < uint8_t > ( s.long_mode ? ( addrsize ? 4 : 8 ) : ( addrsize ? 2 : 4 ) );

		bool round = s.evex && s.broadcast && s.mod == 3;
		int32_t length_code = s.width == 16 ? 0 : ( s.width == 32 ? 1 : ( s.width == 64 ? 2 : 3 ) );
		uint8_t mode = s.mod == 3 ? 2 : 1;
		const simd_form* table = s.evex ? evex_forms : vex_forms;
		const simd_form* form = nullptr;
		int32_t first = s.evex ? evex_first [ map ] [ opcode ] : vex_first [ map & 3 ] [ opcode ];
		int32_t total = s.evex ? evex_count [ map ] [ opcode ] : vex_count [ map & 3 ] [ opcode ];

		bool w_bit = s.rex_w;

		if ( !s.long_mode && w_bit && !s.evex ) {

			for ( int32_t i = first; i < first + total; i++ ) {

				const simd_form& e = table [ i ];

				if ( e.pp != pp || e.w != 1 || ( e.reg >= 0 && e.reg != s.reg ) )
					continue;

				for ( const simd_operand& op : e.ops ) {

					if ( op.kind == 2 && op.size == 12 )
						w_bit = false;

				}

			}

		}

		for ( int32_t i = first; i < first + total && !form; i++ ) {

			const simd_form& e = table [ i ];

			if ( e.pp != pp || e.mode != mode )
				continue;

			if ( e.w != 2 && e.w != ( w_bit ? 1 : 0 ) )
				continue;

			if ( e.reg >= 0 && e.reg != s.reg )
				continue;

			if ( ( e.flags & 32 ) && ( s.rm != 0 || s.rex_b ) )
				continue;

			if ( round ? !( e.flags & 12 ) : !( e.lengths & ( 1 << length_code ) ) )
				continue;

			form = &e;

		}

		if ( !form )
			return false;

		if ( ( out.mask && !( form->flags & 1 ) ) || ( !out.mask && ( form->flags & 16 ) ) || ( out.zeroing && !( form->flags & 2 ) ) )
			return false;

		bool uses_vvvv = false;
		bool vsib = false;

		for ( const simd_operand& op : form->ops ) {

			if ( op.source == 2 )
				uses_vvvv = true;

			if ( op.kind == 5 )
				vsib = true;

		}

		bool index_high = ( s.vvvv & 16 ) != 0;

		if ( !uses_vvvv && ( ( s.vvvv & 15 ) || ( !vsib && index_high ) ) )
			return false;

		if ( s.broadcast && s.mod != 3 && !form->element )
			return false;

		if ( vsib && s.mod != 3 && s.rm != 4 )
			return false;

		int32_t width = round ? 64 : s.width;

		if ( s.mod != 3 && !read_memory ( in, s, out ) )
			return false;

		int32_t reg_number = s.reg | ( s.rex_r ? 8 : 0 ) | ( s.rex_r2 ? 16 : 0 );
		int32_t rm_number = s.rm | ( s.rex_b ? 8 : 0 ) | ( s.evex && s.rex_x && s.mod == 3 ? 16 : 0 );
		int32_t count = 0;
		int32_t suffix = -1;

		auto suffix_byte = [ & ] ( ) {

			if ( suffix < 0 ) {

				if ( !out.immediate_size ) {

					out.immediate_offset = static_cast < uint8_t > ( in.at );
					out.immediate_size = 1;

				}

				suffix = in.byte ( );

			}

			return suffix;

		};

		bool has_is4 = false;

		for ( const simd_operand& op : form->ops )
			has_is4 = has_is4 || op.source == 6;

		for ( int32_t i = 0; i < 5 && form->ops [ i ].source; i++ ) {

			const simd_operand& op = form->ops [ i ];
			operand item;
			int32_t lanes = evex_lanes ( op.size, width );
			uint8_t gpr_size = static_cast < uint8_t > ( op.size == 12 && s.long_mode ? 8 : ( op.size == 15 ? 2 : 4 ) );

			auto make_reg = [ & ] ( uint8_t value ) {

				item.type = kind::REG;
				item.reg = value;
				item.size = register_size ( value );

			};

			switch ( op.source ) {

				case 1:

					if ( op.kind != 0 && op.kind != 2 && ( s.rex_r || s.rex_r2 ) )
						return false;

					if ( op.kind == 0 )
						make_reg ( vector ( reg_number, lanes ) );
					else if ( op.kind == 1 )
						make_reg ( static_cast < uint8_t > ( K0 + ( s.reg & 7 ) ) );
					else if ( op.kind == 4 )
						make_reg ( static_cast < uint8_t > ( K0 + ( s.reg & 6 ) ) );
					else if ( op.kind == 6 )
						make_reg ( static_cast < uint8_t > ( TMM0 + ( s.reg & 7 ) ) );
					else
						make_reg ( gpr ( s.reg | ( s.rex_r ? 8 : 0 ), gpr_size, true ) );

					break;

				case 2:

					if ( op.kind != 0 && s.vvvv > 15 )
						return false;

					if ( op.kind == 0 )
						make_reg ( vector ( s.vvvv, lanes ) );
					else if ( op.kind == 1 )
						make_reg ( static_cast < uint8_t > ( K0 + ( s.vvvv & 7 ) ) );
					else if ( op.kind == 6 )
						make_reg ( static_cast < uint8_t > ( TMM0 + ( s.vvvv & 7 ) ) );
					else
						make_reg ( gpr ( s.vvvv & 15, gpr_size, true ) );

					break;

				case 3:

					if ( s.mod == 3 ) {

						if ( op.kind == 1 && ( s.rex_b || s.rex_x ) )
							return false;

						if ( op.kind == 0 )
							make_reg ( vector ( rm_number, lanes ) );
						else if ( op.kind == 1 )
							make_reg ( static_cast < uint8_t > ( K0 + ( s.rm & 7 ) ) );
						else if ( op.kind == 2 )
							make_reg ( gpr ( s.rm | ( s.rex_b ? 8 : 0 ), gpr_size, true ) );
						else if ( op.kind == 6 )
							make_reg ( static_cast < uint8_t > ( TMM0 + ( s.rm & 7 ) ) );
						else
							return false;

					} else {

						if ( op.kind != 3 && op.kind != 5 )
							return false;

						int32_t bytes = evex_bytes ( op.size, width );

						item = s.memory;
						item.size = static_cast < uint8_t > ( s.broadcast ? form->element : bytes );

						if ( s.broadcast )
							out.broadcast = static_cast < uint8_t > ( bytes / form->element );

						if ( s.evex && out.displacement_size == 1 )
							item.value *= s.broadcast ? form->element : ( form->scale ? form->scale : ( bytes ? bytes : 1 ) );

						if ( op.kind == 5 ) {

							int32_t index = ( ( s.sib >> 3 ) & 7 ) | ( s.rex_x ? 8 : 0 ) | ( s.evex && index_high ? 16 : 0 );

							item.index = vector ( index, evex_lanes ( static_cast < uint8_t > ( op.size >> 4 ), width ) );
							item.scale = static_cast < uint8_t > ( 1 << ( s.sib >> 6 ) );

						}

					}

					break;

				case 4:
					item.type = kind::IMM;
					item.size = 1;
					item.value = static_cast < int64_t > ( has_is4 ? suffix_byte ( ) & 15 : suffix_byte ( ) );
					break;

				case 6:
					make_reg ( vector ( ( suffix_byte ( ) >> 4 ) & ( s.long_mode ? 15 : 7 ), lanes ) );
					break;

				default:
					break;

			}

			item.access = READ;
			out.operands [ count++ ] = item;

			if ( count >= 5 )
				break;

		}

		if ( in.failed )
			return false;

		out.length = static_cast < uint8_t > ( in.at );
		out.count = static_cast < uint8_t > ( count );
		out.name = form->name;
		out.rounding = static_cast < uint8_t > ( round ? ( ( form->flags & 8 ) ? 1 : 2 + length_code ) : 0 );

		if ( count )
			out.operands [ 0 ].access = static_cast < uint8_t > ( writes_only ( form->name ) ? WRITE : READ | WRITE );

		if ( s.rex_w )
			prefixes |= REX_W;

		out.prefixes = prefixes;

		return true;

	}

	bool decode ( const uint8_t* code, size_t available, uint64_t address, bool long_mode, instruction& out ) {

		if ( !ready )
			prepare ( );

		out = instruction ( );
		out.address = address;

		cursor in = { code, available < 15 ? available : 15, 0, false };
		state s = { };
		bool opsize = false;
		bool addrsize = false;
		uint8_t rep = 0;
		uint8_t rex = 0;
		uint16_t prefixes = 0;
		bool notrack = false;

		s.long_mode = long_mode;
		s.width = 16;

		for ( ;; ) {

			if ( in.at >= in.limit )
				return false;

			uint8_t b = code [ in.at ];
			bool legacy = true;

			switch ( b ) {

				case 0xF0:
					prefixes |= LOCK;
					break;

				case 0xF2:
				case 0xF3:
					rep = b;
					break;

				case 0x2E:
					out.segment = CS;
					break;

				case 0x36:
					out.segment = SS;
					break;

				case 0x3E:
					out.segment = DS;
					notrack = true;
					break;

				case 0x26:
					out.segment = ES;
					break;

				case 0x64:
					out.segment = FS;
					break;

				case 0x65:
					out.segment = GS;
					break;

				case 0x66:
					opsize = true;
					break;

				case 0x67:
					addrsize = true;
					break;

				default:
					legacy = false;
					break;

			}

			if ( legacy ) {

				in.at++;
				rex = 0;

				continue;

			}

			if ( long_mode && ( b & 0xF0 ) == 0x40 ) {

				rex = b;
				in.at++;

				continue;

			}

			break;

		}

		if ( long_mode && ( out.segment == CS || out.segment == SS || out.segment == DS || out.segment == ES ) )
			out.segment = 0;

		s.rex_present = rex != 0;
		s.rex_w = ( rex & 8 ) != 0;
		s.rex_r = ( rex & 4 ) != 0;
		s.rex_x = ( rex & 2 ) != 0;
		s.rex_b = ( rex & 1 ) != 0;

		uint8_t first = in.byte ( );
		int32_t map = 0;
		uint8_t opcode = first;
		uint8_t pp = 0;

		if ( first == 0x0F ) {

			opcode = in.byte ( );
			map = 1;

			if ( opcode == 0x38 || opcode == 0x3A ) {

				map = opcode == 0x38 ? 2 : 3;
				opcode = in.byte ( );

			}

		} else if ( ( first == 0xC4 || first == 0xC5 || first == 0x62 ) && in.at < in.limit && ( long_mode || ( code [ in.at ] & 0xC0 ) == 0xC0 ) ) {

			if ( rex || opsize || rep || ( prefixes & LOCK ) )
				return false;

			uint8_t one_vex = in.byte ( );

			s.vex = true;
			s.rex_present = true;
			s.width = 16;

			if ( first == 0x62 ) {

				uint8_t two_vex = in.byte ( );
				uint8_t three_vex = in.byte ( );

				if ( ( one_vex & 0x08 ) || !( two_vex & 0x04 ) )
					return false;

				s.evex = true;
				s.rex_r = long_mode && !( one_vex & 0x80 );
				s.rex_x = long_mode && !( one_vex & 0x40 );
				s.rex_b = long_mode && !( one_vex & 0x20 );
				s.rex_r2 = long_mode && !( one_vex & 0x10 );
				map = one_vex & 7;
				s.rex_w = ( two_vex & 0x80 ) != 0;
				s.vvvv = static_cast < uint8_t > ( ( ( ~two_vex >> 3 ) & 15 ) | ( long_mode && !( three_vex & 0x08 ) ? 16 : 0 ) );
				pp = two_vex & 3;
				out.zeroing = ( three_vex & 0x80 ) != 0;
				out.mask = three_vex & 7;
				s.broadcast = ( three_vex & 0x10 ) != 0;
				s.width = static_cast < uint8_t > ( 16 << ( ( three_vex >> 5 ) & 3 ) );
				prefixes |= EVEX;

				if ( map == 0 || map == 4 || map == 7 )
					return false;

			} else if ( first == 0xC5 ) {

				s.rex_r = long_mode && !( one_vex & 0x80 );
				s.vvvv = static_cast < uint8_t > ( ( ~one_vex >> 3 ) & 15 );
				s.width = static_cast < uint8_t > ( ( one_vex & 4 ) ? 32 : 16 );
				pp = one_vex & 3;
				map = 1;

			} else {

				uint8_t two_vex = in.byte ( );

				s.rex_r = long_mode && !( one_vex & 0x80 );
				s.rex_x = long_mode && !( one_vex & 0x40 );
				s.rex_b = long_mode && !( one_vex & 0x20 );
				map = one_vex & 31;
				s.rex_w = ( two_vex & 0x80 ) != 0;
				s.vvvv = static_cast < uint8_t > ( ( ~two_vex >> 3 ) & 15 );
				s.width = static_cast < uint8_t > ( ( two_vex & 4 ) ? 32 : 16 );
				pp = two_vex & 3;

				if ( map < 1 || map > 3 )
					return false;

			}

			if ( !long_mode )
				s.vvvv &= 7;

			opcode = in.byte ( );
			prefixes |= VEX;

			if ( s.width >= 32 )
				prefixes |= VEX_L;

		}

		if ( in.failed )
			return false;

		if ( s.evex || ( s.vex && !( map == 1 && opcode == 0x77 ) ) )
			return decode_vector ( in, s, out, map, opcode, pp, addrsize, prefixes );

		form f;
		uint8_t mandatory = 0;
		bool handled = false;

		s.opcode = opcode;

		if ( handled ) {

		} else if ( map == 0 )
			f = one_byte [ opcode ];
		else {

			uint8_t slot = 0;

			if ( s.vex )
				slot = pp;
			else if ( rep == 0xF2 )
				slot = 3;
			else if ( rep == 0xF3 )
				slot = 2;
			else if ( opsize )
				slot = 1;

			uint16_t index = extended [ map - 1 ] [ opcode ] [ slot ];

			if ( index ) {

				mandatory = slot;

			} else if ( !s.vex && slot != 0 ) {

				if ( slot >= 2 && opsize && extended [ map - 1 ] [ opcode ] [ 1 ] ) {

					index = extended [ map - 1 ] [ opcode ] [ 1 ];
					mandatory = 1;

				} else
					index = extended [ map - 1 ] [ opcode ] [ 0 ];

			}

			bool usable = index != 0;

			if ( usable ) {

				f = pool [ index ];

				if ( s.vex && ( f.flags & NO_VEX ) )
					usable = false;

				if ( !s.vex && ( f.flags & VEX_ONLY ) )
					usable = false;

			}

			if ( !usable )
				return false;

			if ( s.vex && !s.evex && map == 1 && opcode == 0x77 ) {

				f.name = s.width >= 32 ? mnemonic::vzeroall : mnemonic::vzeroupper;
				f.flags = 0;

			}

		}

		bool distinct = map != 0 && !s.vex && mandatory && extended [ map - 1 ] [ opcode ] [ mandatory ] != extended [ map - 1 ] [ opcode ] [ 0 ];
		bool consumed_66 = mandatory == 1 && distinct;
		bool consumed_rep = ( mandatory == 2 || mandatory == 3 ) && distinct;

		if ( map == 0 && opcode == 0x90 ) {

			if ( rep == 0xF3 ) {

				f.name = mnemonic::pause;
				consumed_rep = true;

			} else if ( s.rex_b ) {

				f.name = mnemonic::xchg;
				f.ops [ 0 ] = R_AX;
				f.ops [ 1 ] = ZV;

			}

		}

		if ( long_mode && ( f.flags & I64 ) )
			return false;

		if ( !long_mode && ( f.flags & O64 ) )
			return false;

		if ( map == 0 && first == 0x63 && long_mode ) {

			f.name = mnemonic::movsxd;
			f.ops [ 0 ] = GV;
			f.ops [ 1 ] = ED;

		}

		bool opsize_active = opsize && !consumed_66;

		if ( long_mode ) {

			if ( s.rex_w && !s.vex )
				s.operand_size = 8;
			else if ( f.flags & F64 )
				s.operand_size = 8;
			else if ( f.flags & D64 )
				s.operand_size = opsize_active ? 2 : 8;
			else
				s.operand_size = opsize_active ? 2 : 4;

			if ( s.vex && s.rex_w )
				s.operand_size = 8;

			s.address_size = addrsize ? 4 : 8;

		} else {

			s.operand_size = opsize_active ? 2 : 4;
			s.address_size = addrsize ? 2 : 4;

		}

		if ( f.flags & MODRM ) {

			uint8_t modrm = in.byte ( );

			s.has_modrm = true;
			s.mod = modrm >> 6;
			s.reg = ( modrm >> 3 ) & 7;
			s.rm = modrm & 7;

			if ( f.flags & REG_ONLY )
				s.mod = 3;

			if ( s.width > 64 )
				return false;

			if ( s.mod != 3 ) {

				if ( !read_memory ( in, s, out ) )
					return false;

				s.memory_ready = true;

			}

		}

		if ( ( f.flags & MODRM ) && !refine ( f, s, map, opcode, mandatory ) )
			return false;

		if ( !( f.flags & MODRM ) )
			s.rm = static_cast < uint8_t > ( opcode & 7 );

		if ( in.failed )
			return false;

		if ( f.flags & X87 ) {

			int32_t index = opcode - 0xD8;

			f = s.mod == 3 ? x87_register [ index ] [ ( s.reg << 3 ) | s.rm ] : x87_memory [ index ] [ s.reg ];

			if ( f.name == mnemonic::invalid )
				return false;

			f.flags |= MODRM;

		} else if ( f.group ) {

			out.address = address;

			if ( !resolve_group ( f, s, mandatory, out ) )
				return false;

			if ( f.flags & D64 ) {

				if ( long_mode && !s.rex_w )
					s.operand_size = opsize_active ? 2 : 8;

			}

			if ( f.flags & F64 ) {

				if ( long_mode )
					s.operand_size = 8;

			}

			switch ( f.name ) {

				case mnemonic::endbr64:
				case mnemonic::endbr32:
				case mnemonic::rdsspd:
				case mnemonic::rdsspq:
				case mnemonic::rdpid:
				case mnemonic::rdfsbase:
				case mnemonic::rdgsbase:
				case mnemonic::wrfsbase:
				case mnemonic::wrgsbase:
				case mnemonic::incsspd:
				case mnemonic::incsspq:
					consumed_rep = true;
					break;

				default:
					break;

			}

		}

		if ( s.ksize )
			s.operand_size = s.ksize;

		if ( s.prefix_used )
			consumed_rep = true;

		if ( f.name == mnemonic::invalid )
			return false;

		if ( f.flags & W_NAME ) {

			if ( s.rex_w && ( s.long_mode || s.vex ) )
				f.name = offset ( f.name, 1 );

		}

		if ( f.flags & SIZE_NAME ) {

			uint8_t size = s.operand_size;
			mnemonic name = f.name;

			if ( name == mnemonic::pusha || name == mnemonic::popa )
				f.name = offset ( name, size == 2 ? 0 : 1 );
			else if ( name == mnemonic::pushf || name == mnemonic::popf )
				f.name = offset ( name, size == 2 ? 0 : ( long_mode ? 2 : 1 ) );
			else if ( name == mnemonic::insw || name == mnemonic::outsw )
				f.name = offset ( name, size == 2 ? 0 : 1 );
			else
				f.name = offset ( name, size == 2 ? 0 : ( size == 4 ? 1 : 2 ) );

		}

		if ( s.rex_w && long_mode && !s.vex ) {

			switch ( f.name ) {

				case mnemonic::fxsave:
					f.name = mnemonic::fxsave64;
					break;

				case mnemonic::fxrstor:
					f.name = mnemonic::fxrstor64;
					break;

				case mnemonic::xsave:
					f.name = mnemonic::xsave64;
					break;

				case mnemonic::xrstor:
					f.name = mnemonic::xrstor64;
					break;

				case mnemonic::xsaveopt:
					f.name = mnemonic::xsaveopt64;
					break;

				case mnemonic::xsavec:
					f.name = mnemonic::xsavec64;
					break;

				case mnemonic::xsaves:
					f.name = mnemonic::xsaves64;
					break;

				case mnemonic::xrstors:
					f.name = mnemonic::xrstors64;
					break;

				case mnemonic::retf:
					f.name = mnemonic::retfq;
					break;

				case mnemonic::sysret:
					f.name = mnemonic::sysretq;
					break;

				case mnemonic::sysexit:
					f.name = mnemonic::sysexitq;
					break;

				default:
					break;

			}

		}

		if ( f.name == mnemonic::jcxz )
			f.name = s.address_size == 2 ? mnemonic::jcxz : ( s.address_size == 4 ? mnemonic::jecxz : mnemonic::jrcxz );

		bool uses_h = false;

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( f.ops [ i ] == HX || f.ops [ i ] == HO || f.ops [ i ] == HY || f.ops [ i ] == BY || f.ops [ i ] == KV )
				uses_h = true;

		}

		bool insert_h = s.vex && ( ( f.flags & NDS ) || ( ( f.flags & NDS_REG ) && s.mod == 3 ) );

		if ( s.vex && !insert_h && !uses_h && s.vvvv != 0 )
			return false;

		int32_t count = 0;

		for ( int32_t i = 0; i < 4 && f.ops [ i ] != NO; i++ ) {

			operand item;

			if ( !build_operand ( f.ops [ i ], in, s, out, item ) )
				return false;

			out.operands [ count++ ] = item;

			if ( i == 0 && insert_h ) {

				operand extra;
				int32_t width = item.type == kind::REG && is_vector ( item.reg ) ? register_size ( item.reg ) : s.width;

				extra.type = kind::REG;
				extra.reg = vector ( s.vvvv, width );
				extra.size = register_size ( extra.reg );
				out.operands [ count++ ] = extra;

			}

			if ( count >= 4 )
				break;

		}

		if ( in.failed )
			return false;

		if ( map == 1 && opcode == 0x0F && !s.vex ) {

			if ( !out.immediate_size ) {

				out.immediate_offset = static_cast < uint8_t > ( in.at );
				out.immediate_size = 1;

			}

			f.name = amd_3dnow ( in.byte ( ) );

			if ( f.name == mnemonic::invalid )
				return false;

		}

		if ( map == 0 && ( opcode == 0x86 || opcode == 0x87 ) && s.mod == 3 && count == 2 ) {

			operand held = out.operands [ 0 ];

			out.operands [ 0 ] = out.operands [ 1 ];
			out.operands [ 1 ] = held;

		}

		if ( map == 0 && opcode == 0x8C && count && out.operands [ 0 ].type == kind::MEM )
			out.operands [ 0 ].size = 2;

		if ( map == 0 && opcode == 0x8E && count > 1 && out.operands [ 1 ].type == kind::REG ) {

			out.operands [ 1 ].reg = resize_register ( out.operands [ 1 ].reg, s.operand_size );
			out.operands [ 1 ].size = s.operand_size;

		}

		out.length = static_cast < uint8_t > ( in.at );
		out.count = static_cast < uint8_t > ( count );
		out.name = f.name;
		out.operand_size = s.operand_size;

		for ( int32_t i = 0; i < count; i++ ) {

			operand& item = out.operands [ i ];

			if ( item.type == kind::REL )
				item.value = static_cast < int64_t > ( long_mode ? address + out.length + static_cast < uint64_t > ( item.value ) : static_cast < uint32_t > ( address + out.length + static_cast < uint64_t > ( item.value ) ) );

			if ( item.type == kind::MEM && item.base == NONE && item.index == NONE && s.address_size == 4 )
				item.value = static_cast < int64_t > ( static_cast < uint32_t > ( item.value ) );

			item.access = READ;

		}

		if ( count > 0 && !( f.flags & NOWRITE ) )
			out.operands [ 0 ].access = static_cast < uint8_t > ( writes_only ( f.name ) ? WRITE : READ | WRITE );

		if ( s.rex_w )
			prefixes |= REX_W;

		if ( rex )
			prefixes |= HAS_REX;

		if ( opsize && !consumed_66 && !s.prefix_used )
			prefixes |= OPSIZE;

		if ( addrsize )
			prefixes |= ADDRSIZE;

		if ( rep && !consumed_rep && !s.vex )
			prefixes |= rep == 0xF3 ? REP : REPNE;

		if ( notrack && ( f.name == mnemonic::jmp || f.name == mnemonic::call ) && count && out.operands [ 0 ].type != kind::REL )
			prefixes |= NOTRACK;

		out.prefixes = prefixes;

		int32_t value = static_cast < int32_t > ( f.name );

		if ( value >= static_cast < int32_t > ( mnemonic::jo ) && value <= static_cast < int32_t > ( mnemonic::jg ) ) {

			out.control = flow::BRANCH;
			out.condition = static_cast < uint8_t > ( value - static_cast < int32_t > ( mnemonic::jo ) );

		} else if ( value >= static_cast < int32_t > ( mnemonic::seto ) && value <= static_cast < int32_t > ( mnemonic::setg ) )
			out.condition = static_cast < uint8_t > ( value - static_cast < int32_t > ( mnemonic::seto ) );
		else if ( value >= static_cast < int32_t > ( mnemonic::cmovo ) && value <= static_cast < int32_t > ( mnemonic::cmovg ) )
			out.condition = static_cast < uint8_t > ( value - static_cast < int32_t > ( mnemonic::cmovo ) );

		switch ( f.name ) {

			case mnemonic::call:
			case mnemonic::callf:
				out.control = flow::CALL;
				break;

			case mnemonic::jmp:
			case mnemonic::jmpf:
				out.control = flow::JUMP;
				break;

			case mnemonic::jcxz:
			case mnemonic::jecxz:
			case mnemonic::jrcxz:
			case mnemonic::loop:
			case mnemonic::loope:
			case mnemonic::loopne:
			case mnemonic::xbegin:
				out.control = flow::BRANCH;
				break;

			case mnemonic::ret:
			case mnemonic::retf:
			case mnemonic::iret:
			case mnemonic::iretd:
			case mnemonic::iretq:
			case mnemonic::sysret:
			case mnemonic::sysexit:
			case mnemonic::retfq:
			case mnemonic::sysretq:
			case mnemonic::sysexitq:
				out.control = flow::RETURN;
				break;

			case mnemonic::int3:
			case mnemonic::hlt:
			case mnemonic::ud0:
			case mnemonic::ud1:
			case mnemonic::ud2:
				out.control = flow::STOP;
				break;

			case mnemonic::int_:

				if ( count && ( out.operands [ 0 ].value == 0x29 || out.operands [ 0 ].value == 0x2C ) )
					out.control = flow::STOP;

				break;

			default:
				break;

		}

		return true;

	}

}