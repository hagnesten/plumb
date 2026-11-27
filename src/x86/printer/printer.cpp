#include "x86/printer/printer.h"
#include "base/utility/utility.h"

const wchar_t* x86::size_word ( uint8_t size ) {

	switch ( size ) {

		case 1:
			return L"byte";

		case 2:
			return L"word";

		case 4:
			return L"dword";

		case 6:
			return L"fword";

		case 8:
			return L"qword";

		case 10:
			return L"tbyte";

		case 16:
			return L"xmmword";

		case 32:
			return L"ymmword";

		case 64:
			return L"zmmword";

		default:
			return nullptr;

	}

}

const wchar_t* const x86::predicates [ 32 ] = {
	L"eq", L"lt", L"le", L"unord", L"neq", L"nlt", L"nle", L"ord", L"eq_uq", L"nge", L"ngt", L"false", L"neq_oq", L"ge", L"gt", L"true",
	L"eq_os", L"lt_oq", L"le_oq", L"unord_s", L"neq_us", L"nlt_uq", L"nle_uq", L"ord_s", L"eq_us", L"nge_uq", L"ngt_uq", L"false_os", L"neq_os", L"ge_oq", L"gt_oq", L"true_us"
};

int32_t x86::hex_text ( uint64_t value, wchar_t* out, bool suffix ) {

	static const wchar_t digits [ ] = L"0123456789ABCDEF";
	wchar_t reverse [ 20 ];
	int32_t count = 0;
	int32_t length = 0;

	if ( value < 10 ) {

		out [ 0 ] = static_cast < wchar_t > ( L'0' + value );
		out [ 1 ] = 0;

		return 1;

	}

	do {

		reverse [ count++ ] = digits [ value & 15 ];
		value >>= 4;

	} while ( value );

	if ( suffix ) {

		if ( reverse [ count - 1 ] >= L'A' )
			out [ length++ ] = L'0';

	} else {

		out [ length++ ] = L'0';
		out [ length++ ] = L'x';

	}

	while ( count )
		out [ length++ ] = reverse [ --count ];

	if ( suffix )
		out [ length++ ] = L'h';

	out [ length ] = 0;

	return length;

}

bool x86::signed_display ( mnemonic name ) {

	switch ( name ) {

		case mnemonic::add:
		case mnemonic::sub:
		case mnemonic::adc:
		case mnemonic::sbb:
		case mnemonic::cmp:
		case mnemonic::mov:
		case mnemonic::push:
		case mnemonic::imul:
			return true;

		default:
			return false;

	}

}

bool x86::legacy_simd ( mnemonic name ) {

	int32_t value = static_cast < int32_t > ( name );

	return value >= static_cast < int32_t > ( mnemonic::addpd ) && value <= static_cast < int32_t > ( mnemonic::xorps );

}

bool x86::string_op ( mnemonic name ) {

	int32_t value = static_cast < int32_t > ( name );

	return value >= static_cast < int32_t > ( mnemonic::movsb ) && value <= static_cast < int32_t > ( mnemonic::outsd );

}

bool x86::compare_string ( mnemonic name ) {

	int32_t value = static_cast < int32_t > ( name );

	return ( value >= static_cast < int32_t > ( mnemonic::cmpsb ) && value <= static_cast < int32_t > ( mnemonic::cmpsq ) ) || ( value >= static_cast < int32_t > ( mnemonic::scasb ) && value <= static_cast < int32_t > ( mnemonic::scasq ) );

}

int32_t x86::predicate_form ( const instruction& item ) {

	switch ( item.name ) {

		case mnemonic::cmpps:
		case mnemonic::cmppd:
		case mnemonic::cmpss:
		case mnemonic::cmpsd:
			break;

		default:
			return -1;

	}

	const operand& last = item.operands [ item.count - 1 ];

	if ( last.type != kind::IMM )
		return -1;

	int32_t limit = ( item.prefixes & VEX ) ? 32 : 8;

	return last.value >= 0 && last.value < limit ? static_cast < int32_t > ( last.value ) : -1;

}

void x86::put_mnemonic ( const instruction& item, line& out, int32_t predicate ) {

	wchar_t text [ 48 ];
	int32_t length = 0;
	const char* name = item.name == mnemonic::callf ? "call" : ( item.name == mnemonic::jmpf ? "jmp" : mnemonic_name ( item.name ) );

	if ( ( item.prefixes & VEX ) && legacy_simd ( item.name ) )
		text [ length++ ] = L'v';

	if ( predicate >= 0 ) {

		text [ length++ ] = L'c';
		text [ length++ ] = L'm';
		text [ length++ ] = L'p';

		for ( const wchar_t* at = predicates [ predicate ]; *at; at++ )
			text [ length++ ] = *at;

		const char* suffix = name + 3;

		while ( *suffix )
			text [ length++ ] = static_cast < wchar_t > ( *suffix++ );

	} else {

		for ( ; *name && length < 46; name++ )
			text [ length++ ] = static_cast < wchar_t > ( *name );

	}

	text [ length ] = 0;
	out.put ( text, token::MNEMONIC );

}

void x86::put_number ( line& out, uint64_t value, uint32_t flags, token type ) {

	wchar_t text [ 24 ];

	hex_text ( value, text, ( flags & HEX_SUFFIX ) != 0 );
	out.put ( text, type, value );

}

bool x86::put_symbol ( line& out, symbolizer* names, uint64_t address, bool code ) {

	if ( !names )
		return false;

	wchar_t text [ 160 ];

	if ( !names->name ( address, code, text, 160 ) )
		return false;

	out.put ( text, token::SYMBOL, address );

	return true;

}

void x86::put_memory ( const instruction& item, const operand& value, line& out, symbolizer* names, uint32_t flags, bool show_size ) {

	if ( show_size && size_word ( value.size ) ) {

		out.put ( size_word ( value.size ), token::SIZE );
		out.put ( L" ptr ", token::SIZE );

	}

	if ( value.segment ) {

		out.put ( register_name ( value.segment ), token::REGISTER );
		out.put ( L":", token::PUNCT );

	}

	out.put ( L"[", token::PUNCT );

	bool rip = value.base == RIP || value.base == EIP;

	if ( rip && !( flags & PLAIN_RIP ) ) {

		uint64_t absolute = item.address + item.length + static_cast < uint64_t > ( value.value );

		if ( value.base == EIP )
			absolute &= 0xFFFFFFFFull;

		if ( !put_symbol ( out, names, absolute, false ) )
			put_number ( out, absolute, flags, token::ADDRESS );

		out.put ( L"]", token::PUNCT );

		return;

	}

	bool any = false;

	if ( value.base ) {

		out.put ( register_name ( value.base ), token::REGISTER );
		any = true;

	}

	if ( value.index ) {

		if ( any )
			out.put ( L"+", token::PUNCT );

		out.put ( register_name ( value.index ), token::REGISTER );

		if ( value.scale > 1 ) {

			wchar_t scale [ 4 ] = { L'*', static_cast < wchar_t > ( L'0' + value.scale ), 0, 0 };

			out.put ( scale, token::PUNCT );

		}

		any = true;

	}

	if ( value.value || !any ) {

		int64_t displacement = value.value;

		if ( !any ) {

			uint64_t absolute = static_cast < uint64_t > ( displacement );

			if ( !put_symbol ( out, names, absolute, false ) )
				put_number ( out, absolute, flags, token::ADDRESS );

		} else if ( displacement > 0x10000 && names && !rip ) {

			out.put ( L"+", token::PUNCT );

			if ( !put_symbol ( out, names, static_cast < uint64_t > ( displacement ), false ) )
				put_number ( out, static_cast < uint64_t > ( displacement ), flags );

		} else if ( displacement < 0 ) {

			out.put ( L"-", token::PUNCT );
			put_number ( out, static_cast < uint64_t > ( -displacement ), flags );

		} else {

			out.put ( L"+", token::PUNCT );
			put_number ( out, static_cast < uint64_t > ( displacement ), flags );

		}

	}

	out.put ( L"]", token::PUNCT );

}

void x86::format ( const instruction& item, printer& printer_out, symbolizer* names, uint32_t flags ) {

	line out = { printer_out };

	if ( !item.valid ( ) ) {

		out.put ( L"(bad)", token::MNEMONIC );

		return;

	}

	bool memory_write = item.count && item.operands [ 0 ].type == kind::MEM;
	bool elision = ( item.prefixes & LOCK ) || ( item.name == mnemonic::xchg && ( memory_write || ( item.count > 1 && item.operands [ 1 ].type == kind::MEM ) ) ) || ( item.name == mnemonic::mov && memory_write );
	bool near_branch = item.name == mnemonic::call || item.name == mnemonic::jmp || item.name == mnemonic::ret || ( item.control == flow::BRANCH && item.condition != 0xFF );

	if ( ( item.prefixes & REPNE ) && !string_op ( item.name ) && elision && ( ( item.prefixes & LOCK ) || item.name == mnemonic::xchg ) )
		out.put ( L"xacquire ", token::PREFIX );

	if ( ( item.prefixes & REP ) && !string_op ( item.name ) && elision )
		out.put ( L"xrelease ", token::PREFIX );

	if ( item.prefixes & LOCK )
		out.put ( L"lock ", token::PREFIX );

	if ( item.prefixes & REP ) {

		if ( string_op ( item.name ) )
			out.put ( compare_string ( item.name ) ? L"repe " : L"rep ", token::PREFIX );
		else if ( item.name == mnemonic::ret )
			out.put ( L"rep ", token::PREFIX );

	}

	if ( item.prefixes & REPNE ) {

		if ( string_op ( item.name ) )
			out.put ( L"repne ", token::PREFIX );
		else if ( near_branch )
			out.put ( L"bnd ", token::PREFIX );

	}

	if ( item.prefixes & NOTRACK )
		out.put ( L"notrack ", token::PREFIX );

	int32_t predicate = predicate_form ( item );
	int32_t shown = predicate >= 0 ? item.count - 1 : item.count;

	put_mnemonic ( item, out, predicate );

	if ( !shown )
		return;

	out.put ( L" ", token::TEXT );

	uint8_t widest_reg = 0;
	bool mixed = false;

	for ( int32_t i = 0; i < shown; i++ ) {

		const operand& value = item.operands [ i ];

		if ( value.type == kind::REG ) {

			if ( widest_reg && widest_reg != value.size )
				mixed = true;

			widest_reg = base::max ( widest_reg, value.size );

		}

	}

	static const wchar_t* const round_modes [ 5 ] = { L"{sae}", L"{rn-sae}", L"{rd-sae}", L"{ru-sae}", L"{rz-sae}" };
	int32_t round_before = -1;

	for ( int32_t i = 0; item.rounding && i < shown; i++ ) {

		if ( item.operands [ i ].type == kind::REG && is_vector ( item.operands [ i ].reg ) )
			round_before = i + 1;

	}

	for ( int32_t i = 0; i < shown; i++ ) {

		const operand& value = item.operands [ i ];

		if ( i )
			out.put ( L", ", token::PUNCT );

		if ( i == round_before ) {

			out.put ( round_modes [ item.rounding - 1 ], token::PUNCT );
			out.put ( L", ", token::PUNCT );

		}

		switch ( value.type ) {

			case kind::REG:
				out.put ( register_name ( value.reg ), token::REGISTER );
				break;

			case kind::MEM: {

				bool show = ( flags & ALWAYS_SIZE ) != 0;

				if ( !show ) {

					bool implied = false;

					for ( int32_t k = 0; k < shown; k++ ) {

						if ( k != i && item.operands [ k ].type == kind::REG && item.operands [ k ].size == value.size && !mixed )
							implied = true;

					}

					if ( ( item.control == flow::CALL || item.control == flow::JUMP ) && value.size == ( item.operand_size ? item.operand_size : 8 ) )
						implied = true;

					show = !implied;

				}

				put_memory ( item, value, out, names, flags, show );

				if ( item.broadcast ) {

					wchar_t text [ 12 ] = { L'{', L'1', L't', L'o', 0 };
					int32_t length = 4;

					if ( item.broadcast >= 10 )
						text [ length++ ] = static_cast < wchar_t > ( L'0' + item.broadcast / 10 );

					text [ length++ ] = static_cast < wchar_t > ( L'0' + item.broadcast % 10 );
					text [ length++ ] = L'}';
					text [ length ] = 0;
					out.put ( text, token::PUNCT );

				}

				break;

			}

			case kind::IMM: {

				uint64_t mask = value.size >= 8 ? ~0ull : ( 1ull << ( value.size * 8 ) ) - 1;
				uint64_t raw = static_cast < uint64_t > ( value.value ) & mask;
				int64_t as_signed = value.size >= 8 ? value.value : static_cast < int64_t > ( raw << ( 64 - value.size * 8 ) ) >> ( 64 - value.size * 8 );
				wchar_t custom [ 96 ];

				if ( names && names->number ( item.address, raw, value.size, custom, 96 ) ) {

					out.put ( custom, token::NUMBER, raw );

					break;

				}

				if ( value.size >= 4 && raw > 0x10000 && names && put_symbol ( out, names, raw, false ) )
					break;

				if ( ( flags & SIGNED_IMM ) ? as_signed < 0 && ( item.immediate_size < value.size || value.size == 1 || value.size == 8 ) : ( !( flags & ALWAYS_SIZE ) && as_signed < 0 && as_signed > -0x10000 && signed_display ( item.name ) ) ) {

					out.put ( L"-", token::NUMBER );
					put_number ( out, static_cast < uint64_t > ( -as_signed ), flags );

				} else
					put_number ( out, raw, flags );

				break;

			}

			case kind::REL: {

				uint64_t target = static_cast < uint64_t > ( value.value );

				if ( !put_symbol ( out, names, target, true ) )
					put_number ( out, target, flags, token::ADDRESS );

				break;

			}

			case kind::PTR: {

				out.put ( L"far ", token::SIZE );
				put_number ( out, static_cast < uint64_t > ( value.value ) >> 32, flags );
				out.put ( L":", token::PUNCT );
				put_number ( out, static_cast < uint64_t > ( value.value ) & 0xFFFFFFFFull, flags, token::ADDRESS );

				break;

			}

			default:
				break;

		}

		if ( i == 0 && item.mask ) {

			wchar_t text [ 8 ] = { L' ', L'{', L'k', static_cast < wchar_t > ( L'0' + item.mask ), L'}', 0 };

			out.put ( text, token::PUNCT );

		}

		if ( i == 0 && item.zeroing )
			out.put ( L" {z}", token::PUNCT );

	}

	if ( item.rounding && round_before == shown ) {

		out.put ( L", ", token::PUNCT );
		out.put ( round_modes [ item.rounding - 1 ], token::PUNCT );

	}

}

int32_t x86::format_text ( const instruction& item, wchar_t* out, int32_t room, symbolizer* names, uint32_t flags ) {

	buffer_printer sink;

	sink.out = out;
	sink.room = room;
	sink.length = 0;
	out [ 0 ] = 0;
	format ( item, sink, names, flags );

	return sink.length;

}

int32_t x86::mnemonic_text ( const instruction& item, wchar_t* out, int32_t room ) {

	buffer_printer sink;
	line writer = { sink };

	sink.out = out;
	sink.room = room;
	sink.length = 0;
	out [ 0 ] = 0;
	put_mnemonic ( item, writer, predicate_form ( item ) );

	return sink.length;

}