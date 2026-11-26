#include "x86/mnemonics/mnemonics.h"

#define PLUMB_NAME( value ) #value,

const char* const x86::mnemonic_list [ ] = { PLUMB_MNEMONICS ( PLUMB_NAME ) };

#undef PLUMB_NAME

const char* x86::names [ static_cast < int32_t > ( mnemonic::COUNT ) ];

char x86::name_storage [ 40000 ];

void x86::build_mnemonic_names ( ) {

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

}

const char* x86::mnemonic_name ( mnemonic value ) {

	int32_t index = static_cast < int32_t > ( value );

	return index >= 0 && index < static_cast < int32_t > ( mnemonic::COUNT ) && names [ index ] ? names [ index ] : "?";

}