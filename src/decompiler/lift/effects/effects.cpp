#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/flags/flags.h"
#include "x86/decoder/decoder.h"

int32_t decompiler::slot_of ( uint8_t reg ) {

	if ( !reg || reg == x86::RIP || reg == x86::EIP )
		return -1;

	int32_t family = x86::register_family ( reg );

	if ( family < 16 )
		return family;

	if ( family >= 32 && family < 48 )
		return 16 + family - 32;

	return -1;

}

bool decompiler::same_registers ( const x86::instruction& ins ) {

	return ins.count >= 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 0 ].reg == ins.operands [ 1 ].reg;

}

void decompiler::effects ( const x86::instruction& ins, bool long_mode, uint32_t& use, uint32_t& def, bool partial ) {

	use = 0;
	def = 0;

	if ( ins.name == mnemonic::nop && quiet_nops )
		return;

	for ( int32_t i = 0; i < ins.count; i++ ) {

		const x86::operand& item = ins.operands [ i ];

		if ( item.type == x86::kind::REG ) {

			int32_t slot = slot_of ( item.reg );

			if ( slot < 0 )
				continue;

			if ( item.access & x86::WRITE )
				def |= 1u << slot;

			if ( ( item.access & x86::READ ) || ( partial && i == 0 && item.size < 4 && slot < 16 ) )
				use |= 1u << slot;

		} else if ( item.type == x86::kind::MEM ) {

			int32_t base_slot = slot_of ( item.base );
			int32_t index_slot = slot_of ( item.index );

			if ( base_slot >= 0 )
				use |= 1u << base_slot;

			if ( index_slot >= 0 )
				use |= 1u << index_slot;

		}

	}

	switch ( ins.name ) {

		case mnemonic::xor_:
		case mnemonic::sub:
		case mnemonic::pxor:
		case mnemonic::xorps:
		case mnemonic::xorpd:

			if ( same_registers ( ins ) )
				use &= ~( 1u << slot_of ( ins.operands [ 0 ].reg ) );

			break;

		case mnemonic::call:

			def |= long_mode ? ( ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 8 ) | ( 1u << 9 ) | ( 1u << 10 ) | ( 1u << 11 ) | ( 0x3Fu << 16 ) ) : 7u;

			break;

		case mnemonic::ret:
			use |= 1u | ( 1u << 16 );
			break;

		case mnemonic::div:
		case mnemonic::idiv:
			use |= 5u;
			def |= 5u;
			break;

		case mnemonic::mul:
		case mnemonic::imul:

			if ( ins.count == 1 ) {

				use |= 1u;
				def |= 5u;

			}

			if ( ins.count == 3 && ins.operands [ 0 ].type == x86::kind::REG ) {

				int32_t slot = slot_of ( ins.operands [ 0 ].reg );
				const x86::operand& source = ins.operands [ 1 ];
				bool reads = source.type == x86::kind::REG ? slot_of ( source.reg ) == slot : ( source.type == x86::kind::MEM && ( slot_of ( source.base ) == slot || slot_of ( source.index ) == slot ) );

				if ( slot >= 0 && !reads )
					use &= ~( 1u << slot );

			}

			break;

		case mnemonic::cdq:
		case mnemonic::cqo:
		case mnemonic::cwd:
			use |= 1u;
			def |= 4u;
			break;

		case mnemonic::cdqe:
		case mnemonic::cwde:
		case mnemonic::cbw:
			use |= 1u;
			def |= 1u;
			break;

		case mnemonic::stosb:
		case mnemonic::stosw:
		case mnemonic::stosd:
		case mnemonic::stosq:
			use |= ( 1u << 7 ) | ( 1u << 1 ) | 1u;
			def |= ( 1u << 7 ) | ( 1u << 1 );
			break;

		case mnemonic::movsb:
		case mnemonic::movsw:
		case mnemonic::movsd_:
		case mnemonic::movsq:
			use |= ( 1u << 6 ) | ( 1u << 7 ) | ( 1u << 1 );
			def |= ( 1u << 6 ) | ( 1u << 7 ) | ( 1u << 1 );
			break;

		case mnemonic::cpuid:
			use |= 3u;
			def |= 0xFu;
			break;

		case mnemonic::rdtsc:
			def |= 5u;
			break;

		case mnemonic::xchg:
		case mnemonic::xadd:

			if ( ins.count == 2 && ins.operands [ 1 ].type == x86::kind::REG && slot_of ( ins.operands [ 1 ].reg ) >= 0 )
				def |= 1u << slot_of ( ins.operands [ 1 ].reg );

			break;

		case mnemonic::cmpxchg:
			use |= 1u;
			def |= 1u;
			break;

		default:
			break;

	}

	use &= ~( 1u << 4 );
	def &= ~( 1u << 4 );

}