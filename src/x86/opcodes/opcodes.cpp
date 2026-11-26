#include "x86/opcodes/opcodes.h"

x86::form x86::one_byte [ 256 ];

x86::form x86::groups [ GROUP_COUNT ] [ 8 ];

x86::form x86::pool [ 1400 ];

int32_t x86::pool_count = 1;

uint16_t x86::extended [ 3 ] [ 256 ] [ 4 ];

x86::form x86::x87_memory [ 8 ] [ 8 ];

x86::form x86::x87_register [ 8 ] [ 64 ];

int16_t x86::evex_first [ 8 ] [ 256 ];

int16_t x86::evex_count [ 8 ] [ 256 ];

int16_t x86::vex_first [ 4 ] [ 256 ];

int16_t x86::vex_count [ 4 ] [ 256 ];

x86::form x86::make ( mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t flags ) {

	form item;

	item.name = name;
	item.ops [ 0 ] = a;
	item.ops [ 1 ] = b;
	item.ops [ 2 ] = c;
	item.ops [ 3 ] = d;
	item.flags = flags;

	return item;

}

void x86::one ( int32_t code, mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint16_t flags ) {

	one_byte [ code ] = make ( name, a, b, c, NO, flags );

}

void x86::one_group ( int32_t code, uint8_t group, uint8_t a, uint8_t b, uint16_t flags ) {

	form item = make ( mnemonic::invalid, a, b, NO, NO, flags | MODRM );

	item.group = group;
	one_byte [ code ] = item;

}

void x86::put ( int32_t map, int32_t code, uint8_t prefixes, mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t flags ) {

	int32_t index = pool_count++;

	pool [ index ] = make ( name, a, b, c, d, flags );

	for ( int32_t slot = 0; slot < 4; slot++ ) {

		if ( prefixes & ( 1 << slot ) )
			extended [ map - 1 ] [ code ] [ slot ] = static_cast < uint16_t > ( index );

	}

}

void x86::put_group ( int32_t map, int32_t code, uint8_t prefixes, uint8_t group, uint8_t a, uint8_t b, uint16_t flags ) {

	int32_t index = pool_count++;
	form item = make ( mnemonic::invalid, a, b, NO, NO, flags | MODRM );

	item.group = group;
	pool [ index ] = item;

	for ( int32_t slot = 0; slot < 4; slot++ ) {

		if ( prefixes & ( 1 << slot ) )
			extended [ map - 1 ] [ code ] [ slot ] = static_cast < uint16_t > ( index );

	}

}

void x86::in_group ( uint8_t group, int32_t reg, mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint16_t flags ) {

	groups [ group ] [ reg ] = make ( name, a, b, c, NO, flags );

}

x86::mnemonic x86::offset ( mnemonic base, int32_t delta ) {

	return static_cast < mnemonic > ( static_cast < int32_t > ( base ) + delta );

}

void x86::simd ( int32_t code, mnemonic name, bool mmx ) {

	if ( mmx )
		put ( 1, code, PN, name, PQ, QQ, NO, NO, MODRM | NO_VEX );

	put ( 1, code, P66, name, VX, WX, NO, NO, MODRM | NDS );

}

void x86::arith ( int32_t code, mnemonic packed_single, bool scalar ) {

	put ( 1, code, PN, packed_single, VX, WX, NO, NO, MODRM | NDS );
	put ( 1, code, P66, offset ( packed_single, -1 ), VX, WX, NO, NO, MODRM | NDS );

	if ( scalar ) {

		put ( 1, code, PF3, offset ( packed_single, 2 ), VO, WD, NO, NO, MODRM | NDS );
		put ( 1, code, PF2, offset ( packed_single, 1 ), VO, WQ, NO, NO, MODRM | NDS );

	}

}

void x86::build_one_byte ( ) {

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

void x86::build_two_byte ( ) {

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

void x86::build_three_byte ( ) {

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

void x86::build_x87 ( ) {

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