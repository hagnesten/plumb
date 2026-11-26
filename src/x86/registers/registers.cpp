#include "x86/registers/registers.h"

const char* x86::register_names [ REG_COUNT ];

char x86::vector_names [ 104 ] [ 6 ];

void x86::build_register_names ( ) {

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

const char* x86::register_name ( uint8_t value ) {

	return value < REG_COUNT && register_names [ value ] ? register_names [ value ] : "?";

}

uint8_t x86::register_size ( uint8_t value ) {

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

int32_t x86::register_family ( uint8_t value ) {

	if ( value >= RAX && value < AH )
		return ( value - RAX ) % 16;

	if ( value >= AH && value < ES )
		return value - AH;

	if ( value >= XMM0 && value < CR0 )
		return 32 + ( value - XMM0 ) % 32;

	return 100 + value;

}

bool x86::same_family ( uint8_t a, uint8_t b ) {

	return register_family ( a ) == register_family ( b );

}

uint8_t x86::resize_register ( uint8_t value, uint8_t size ) {

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

bool x86::is_vector ( uint8_t value ) {

	return value >= XMM0 && value < CR0;

}