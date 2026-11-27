#include "decompiler/emit/numbers/numbers.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

void decompiler::emitter::number ( int64_t value, uint8_t size, bool hex_form, bool is_unsigned ) {

	wchar_t text [ 80 ];
	int32_t length = 0;
	bool negative = value < 0 && !( size < 8 && value > 0 );
	uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

	bool wrapped = false;

	if ( negative && ( is_unsigned || bare_unsigned || ( magnitude > 0x100000000ull && hex_form ) || ( wide_negatives && size >= 8 && magnitude > 0x80000000ull ) ) ) {

		wrapped = !is_unsigned && !bare_unsigned && size >= 8;
		negative = false;
		magnitude = size >= 8 ? static_cast < uint64_t > ( value ) : static_cast < uint64_t > ( value ) & ( ( 1ull << ( size * 8 ) ) - 1 );

	}

	if ( bare_unsigned && magnitude > 9 )
		hex_form = true;

	bool fits = negative ? magnitude <= 0x80000000ull : magnitude <= ( is_unsigned || bare_unsigned ? 0xFFFFFFFFull : 0x7FFFFFFFull );
	bool unsigned_suffix = ( is_unsigned && !negative && !bare_unsigned ) || ( wrapped && wrapped_unsigned );
	int32_t occurrence = next_occurrence ( value );
	wchar_t form = b.names && !dry ? b.names->number_form ( out.open ? out.current.address : 0, value, occurrence ) : 0;

	hex_form = hex_form || unsigned_suffix || magnitude > 0xFFFFFFFFull;

	if ( power_hex && magnitude >= 0x2000 && ( magnitude & ( magnitude - 1 ) ) == 0 )
		hex_form = true;

	if ( plain_negatives && negative )
		hex_form = false;

	if ( form == L'h' )
		hex_form = true;
	else if ( form == L'd' )
		hex_form = false;
	else if ( form == L'c' && char_literal ( value, text, 40 ) ) {

		put_number ( text, value );

		return;

	} else if ( form == L'b' ) {

		uint64_t bits = negative ? static_cast < uint64_t > ( value ) & ( size >= 8 ? ~0ull : ( ( 1ull << ( size * 8 ) ) - 1 ) ) : magnitude;
		int32_t top = 63;

		while ( top > 0 && !( ( bits >> top ) & 1 ) )
			top--;

		text [ length++ ] = L'0';
		text [ length++ ] = L'b';

		for ( int32_t bit = top; bit >= 0 && length < 78; bit-- )
			text [ length++ ] = ( bits >> bit ) & 1 ? L'1' : L'0';

		text [ length ] = 0;
		put_number ( text, value );

		return;

	}

	if ( negative )
		text [ length++ ] = L'-';

	if ( hex_form && ( magnitude >= 10 || form == L'h' ) ) {

		wchar_t digits [ 20 ];

		hex ( magnitude, digits );
		text [ length++ ] = L'0';
		text [ length++ ] = L'x';

		for ( int32_t i = 0; digits [ i ]; i++ )
			text [ length++ ] = digits [ i ];

	} else {

		wchar_t reverse [ 24 ];
		int32_t n = 0;

		do {

			reverse [ n++ ] = static_cast < wchar_t > ( L'0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		while ( n )
			text [ length++ ] = reverse [ --n ];

	}

	if ( size == 8 && !( operand && fits ) ) {

		if ( unsigned_suffix )
			text [ length++ ] = L'u';

		text [ length++ ] = L'i';
		text [ length++ ] = L'6';
		text [ length++ ] = L'4';

	} else if ( unsigned_suffix )
		text [ length++ ] = L'u';

	text [ length ] = 0;
	put_number ( text, value );

}
void decompiler::emitter::put_number ( const wchar_t* text, int64_t value ) {

	int32_t before = out.r->spans.count ( );

	out.put ( text, syntax::NUMBER );

	if ( out.r->spans.count ( ) > before ) {

		out.r->spans.back ( ).value = value;
		out.r->spans.back ( ).occurrence = last_occurrence;

	}

}
int32_t decompiler::emitter::next_occurrence ( int64_t value ) {

	uint64_t address = out.open ? out.current.address : 0;
	int32_t count = 0;

	if ( dry )
		return 0;

	for ( int32_t i = 0; i < number_addresses.count ( ); i++ ) {

		if ( number_addresses [ static_cast < size_t > ( i ) ] == address && number_values [ static_cast < size_t > ( i ) ] == value )
			count++;

	}

	number_addresses.push ( address );
	number_values.push ( value );
	last_occurrence = count;

	return count;

}
void decompiler::emitter::real ( int64_t bits, uint8_t size ) {

	double value = 0.0;

	if ( size == 4 ) {

		uint32_t raw = static_cast < uint32_t > ( bits );

		value = static_cast < double > ( __builtin_bit_cast ( float, raw ) );

	} else
		value = __builtin_bit_cast ( double, bits );

	base::writer text;

	if ( ida_reals && value == value && value < 1e300 && value > -1e300 ) {

		wchar_t shown [ 64 ];
		int32_t n = 0;

		if ( value < 0 ) {

			shown [ n++ ] = L'-';
			value = -value;

		}

		if ( value == 0.0 ) {

			shown [ n++ ] = L'0';
			shown [ n++ ] = L'.';
			shown [ n++ ] = L'0';

		} else {

			int32_t significant = size == 4 ? 8 : 16;
			int32_t exponent = 0;
			double power = 1.0;

			if ( value >= 1.0 ) {

				while ( value >= power * 10.0 && exponent < 300 ) {

					power *= 10.0;
					exponent++;

				}

			} else {

				while ( value < power && exponent > -300 ) {

					power /= 10.0;
					exponent--;

				}

			}

			double mantissa = value / power;
			double scale = 1.0;

			for ( int32_t i = 1; i < significant; i++ )
				scale *= 10.0;

			uint64_t digits = static_cast < uint64_t > ( mantissa * scale + 0.5 );
			uint64_t limit = static_cast < uint64_t > ( scale * 10.0 );
			int32_t shift = significant - 1 - exponent;

			if ( exact_reals && shift >= -22 && shift <= 22 ) {

				double exact = 1.0;

				for ( int32_t i = 0; i < ( shift < 0 ? -shift : shift ); i++ )
					exact *= 10.0;

				digits = static_cast < uint64_t > ( ( shift >= 0 ? value * exact : value / exact ) + 0.5 );

			}

			if ( digits >= limit ) {

				digits /= 10;
				exponent++;

			}

			wchar_t text_digits [ 24 ];
			int32_t count = significant;

			for ( int32_t i = significant - 1; i >= 0; i-- ) {

				text_digits [ i ] = static_cast < wchar_t > ( L'0' + digits % 10 );
				digits /= 10;

			}

			while ( count > 1 && text_digits [ count - 1 ] == L'0' )
				count--;

			if ( exponent >= significant || exponent < -4 ) {

				shown [ n++ ] = text_digits [ 0 ];
				shown [ n++ ] = L'.';

				if ( count > 1 ) {

					for ( int32_t i = 1; i < count; i++ )
						shown [ n++ ] = text_digits [ i ];

				} else
					shown [ n++ ] = L'0';

				shown [ n++ ] = L'e';

				if ( exponent < 0 ) {

					shown [ n++ ] = L'-';
					exponent = -exponent;

				}

				wchar_t reverse [ 8 ];
				int32_t r = 0;

				do {

					reverse [ r++ ] = static_cast < wchar_t > ( L'0' + exponent % 10 );
					exponent /= 10;

				} while ( exponent && r < 7 );

				while ( r )
					shown [ n++ ] = reverse [ --r ];

			} else if ( exponent >= 0 ) {

				for ( int32_t i = 0; i <= exponent; i++ )
					shown [ n++ ] = i < count ? text_digits [ i ] : L'0';

				shown [ n++ ] = L'.';

				if ( count > exponent + 1 ) {

					for ( int32_t i = exponent + 1; i < count; i++ )
						shown [ n++ ] = text_digits [ i ];

				} else
					shown [ n++ ] = L'0';

			} else {

				shown [ n++ ] = L'0';
				shown [ n++ ] = L'.';

				for ( int32_t i = 0; i < -exponent - 1 && n < 40; i++ )
					shown [ n++ ] = L'0';

				for ( int32_t i = 0; i < count; i++ )
					shown [ n++ ] = text_digits [ i ];

			}

		}

		shown [ n ] = 0;
		out.put ( shown, syntax::NUMBER );

		return;

	}

	if ( value != value )
		text.add ( L"NAN" );
	else if ( value > 1e300 || value < -1e300 )
		text.add ( value < 0 ? L"-INFINITY" : L"INFINITY" );
	else {

		int32_t decimals = 1;

		for ( ; decimals < 9; decimals++ ) {

			double scale = 1.0;

			for ( int32_t i = 0; i < decimals; i++ )
				scale *= 10.0;

			double rounded = static_cast < double > ( static_cast < int64_t > ( value * scale + ( value < 0 ? -0.5 : 0.5 ) ) ) / scale;

			if ( size == 4 ? static_cast < float > ( rounded ) == static_cast < float > ( value ) : rounded == value )
				break;

		}

		if ( value > 1e15 || value < -1e15 )
			text.fixed ( value, 0 );
		else
			text.fixed ( value, decimals );

	}

	if ( size == 4 )
		text.add ( L"f" );

	out.put ( text.text ( ), syntax::NUMBER );

}
bool decompiler::emitter::simple_constant ( int64_t value ) {

	uint64_t magnitude = static_cast < uint64_t > ( value < 0 ? -value : value );

	return magnitude < 0x80000000ull;

}
void decompiler::emitter::pointer_constant ( uint64_t address ) {

	wchar_t text [ 300 ];

	if ( b.names->literal ( address, text, 290 ) ) {

		out.put ( text, syntax::STRING, address );

		return;

	}

	if ( address != b.file->base && b.names->name ( address, true, text, 200 ) ) {

		short_symbol ( text );

		bool code = b.db->function_starting ( address ) >= 0;

		if ( code ) {

			const analysis::function_info& info = b.db->functions [ static_cast < size_t > ( b.db->function_starting ( address ) ) ];

			out.put ( text, ( info.flags & analysis::THUNK ) ? syntax::IMPORT : syntax::FUNCTION, address );

		} else {

			out.put ( L"&" );
			out.put ( text, syntax::GLOBAL, address );

		}

		return;

	}

	if ( address != b.file->base && b.names->name ( address, false, text, 200 ) ) {

		out.put ( L"&" );
		out.put ( text, syntax::GLOBAL, address );

		return;

	}

	number ( static_cast < int64_t > ( address ), 8, true, false );

}
bool decompiler::emitter::put_folded ( int32_t var, const wchar_t* name ) {

	if ( active_var < 0 || var != active_var || var_step ( var ) != active_unit )
		return false;

	if ( active_how <= 2 )
		out.put ( active_how == 1 ? L"++" : L"--", syntax::OPERATOR );

	out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

	if ( active_how >= 3 )
		out.put ( active_how == 3 ? L"++" : L"--", syntax::OPERATOR );

	if ( active_cond && active_slot >= 0 && active_slot < fold_cond_done.count ( ) )
		fold_cond_done [ static_cast < size_t > ( active_slot ) ] = 1;
	else if ( !active_cond && active_slot >= 0 && active_slot < fold_done.count ( ) )
		fold_done [ static_cast < size_t > ( active_slot ) ] = 1;

	active_var = -1;

	return true;

}
void decompiler::emitter::index_number ( int64_t amount ) {

	if ( amount < 0 ) {

		out.put ( L"-", syntax::NUMBER );
		amount = -amount;

	}

	out.put ( base::writer ( ).number ( amount ).text ( ), syntax::NUMBER );

}