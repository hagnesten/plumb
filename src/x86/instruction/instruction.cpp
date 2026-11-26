#include "x86/instruction/instruction.h"

uint64_t x86::instruction::target ( ) const {

	for ( int32_t i = 0; i < count; i++ ) {

		if ( operands [ i ].type == kind::REL )
			return static_cast < uint64_t > ( operands [ i ].value );

	}

	return 0;

}

bool x86::instruction::direct ( ) const {

	return control != flow::NONE && count > 0 && operands [ 0 ].type == kind::REL;

}

bool x86::instruction::memory_target ( uint64_t& result ) const {

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