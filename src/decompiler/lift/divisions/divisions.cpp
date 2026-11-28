#include "decompiler/lift/divisions/divisions.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/lift/prune/prune.h"
#include "decompiler/apis/apis.h"
#include "decompiler/literals/literals.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"

uint64_t decompiler::high_product ( uint64_t x, uint64_t y ) {

	uint64_t xl = x & 0xFFFFFFFFull;
	uint64_t xh = x >> 32;
	uint64_t yl = y & 0xFFFFFFFFull;
	uint64_t yh = y >> 32;
	uint64_t low = xl * yl;
	uint64_t mid1 = xh * yl + ( low >> 32 );
	uint64_t mid2 = xl * yh + ( mid1 & 0xFFFFFFFFull );

	return xh * yh + ( mid1 >> 32 ) + ( mid2 >> 32 );

}

uint64_t decompiler::magic_divisor ( uint64_t magic, int32_t shift ) {

	if ( !magic || shift < 0 || shift > 63 )
		return 0;

	double scale = 18446744073709551616.0;

	for ( int32_t i = 0; i < shift; i++ )
		scale *= 2.0;

	double estimate = scale / static_cast < double > ( magic );

	if ( estimate < 2.0 || estimate > 4294967296.0 )
		return 0;

	uint64_t divisor = static_cast < uint64_t > ( estimate + 0.5 );
	const uint64_t probes [ ] = { 1, 7, 99, 12345, divisor - 1, divisor, divisor + 1, divisor * 2 - 1, divisor * 2, divisor * 1000 + 999, 0xFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0x123456789ABCDEFull };

	for ( uint64_t x : probes ) {

		if ( ( high_product ( x, magic ) >> shift ) != x / divisor )
			return 0;

	}

	return divisor;

}

uint64_t decompiler::magic_divisor32 ( uint64_t magic, int32_t shift ) {

	if ( !magic || magic > 0xFFFFFFFFull || shift < 32 || shift > 63 )
		return 0;

	double estimate = 1.0;

	for ( int32_t i = 0; i < shift; i++ )
		estimate *= 2.0;

	estimate /= static_cast < double > ( magic );

	if ( estimate < 2.0 || estimate > 4294967295.0 )
		return 0;

	uint64_t divisor = static_cast < uint64_t > ( estimate + 0.5 );
	const uint64_t probes [ ] = { 1, 7, 99, 12345, divisor - 1, divisor, divisor + 1, divisor * 2 - 1, divisor * 2, 0x7FFFFFFFull, 0xFFFFFFFFull, 0x89ABCDEFull };

	for ( uint64_t x : probes ) {

		x &= 0xFFFFFFFFull;

		if ( ( ( x * magic ) >> shift ) != x / divisor )
			return 0;

	}

	return divisor;

}

void decompiler::builder::split_divisions ( ) {

	base::list < int32_t > defs;
	base::list < int32_t > reads;

	defs.fill ( vars.size ( ), 0 );
	reads.fill ( vars.size ( ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		bool direct = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

		if ( direct ) {

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v >= 0 && v < defs.count ( ) )
				defs [ static_cast < size_t > ( v ) ]++;

		} else
			count_reads ( *this, line.left, reads );

		count_reads ( *this, line.right, reads );

	}

	for ( const block_code& item : code ) {

		count_reads ( *this, item.condition, reads );
		count_reads ( *this, item.selector, reads );
		count_reads ( *this, item.returned, reads );

	}

	for ( statement& line : statements ) {

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
			continue;

		int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

		if ( v < 0 || v >= vars.count ( ) || defs [ static_cast < size_t > ( v ) ] != 1 || reads [ static_cast < size_t > ( v ) ] != 1 || vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER )
			continue;

		int32_t top = line.right;

		while ( top >= 0 && exprs [ static_cast < size_t > ( top ) ].kind == op::CAST && !( exprs [ static_cast < size_t > ( top ) ].bits & ( SIGNED | FLOATING ) ) )
			top = exprs [ static_cast < size_t > ( top ) ].a;

		if ( top < 0 )
			continue;

		const expr high = exprs [ static_cast < size_t > ( top ) ];

		if ( high.kind != op::SHR || high.a < 0 || high.b < 0 || exprs [ static_cast < size_t > ( high.b ) ].kind != op::CONST || exprs [ static_cast < size_t > ( high.b ) ].value != 32 )
			continue;

		const expr product = exprs [ static_cast < size_t > ( high.a ) ];

		if ( product.kind != op::MUL || product.a < 0 || product.b < 0 )
			continue;

		int32_t magic = exprs [ static_cast < size_t > ( product.a ) ].kind == op::CONST ? product.a : ( exprs [ static_cast < size_t > ( product.b ) ].kind == op::CONST ? product.b : -1 );

		if ( magic < 0 )
			continue;

		int32_t subject = magic == product.a ? product.b : product.a;

		while ( subject >= 0 && exprs [ static_cast < size_t > ( subject ) ].kind == op::CAST && exprs [ static_cast < size_t > ( subject ) ].size == 8 && !( exprs [ static_cast < size_t > ( subject ) ].bits & ( SIGNED | FLOATING ) ) )
			subject = exprs [ static_cast < size_t > ( subject ) ].a;

		if ( subject < 0 || exprs [ static_cast < size_t > ( subject ) ].size != 4 )
			continue;

		for ( int32_t e = 0; e < exprs.count ( ); e++ ) {

			const expr use = exprs [ static_cast < size_t > ( e ) ];

			if ( use.kind != op::SHR || use.a < 0 || use.b < 0 || exprs [ static_cast < size_t > ( use.b ) ].kind != op::CONST )
				continue;

			int32_t read = use.a;

			while ( read >= 0 && exprs [ static_cast < size_t > ( read ) ].kind == op::CAST && !( exprs [ static_cast < size_t > ( read ) ].bits & ( SIGNED | FLOATING ) ) )
				read = exprs [ static_cast < size_t > ( read ) ].a;

			if ( read < 0 || exprs [ static_cast < size_t > ( read ) ].kind != op::VAR || exprs [ static_cast < size_t > ( read ) ].value != v )
				continue;

			uint64_t divisor = magic_divisor32 ( static_cast < uint64_t > ( exprs [ static_cast < size_t > ( magic ) ].value ), 32 + static_cast < int32_t > ( exprs [ static_cast < size_t > ( use.b ) ].value ) );

			if ( !divisor )
				break;

			int32_t amount = constant ( static_cast < int64_t > ( divisor ), 4 );
			int32_t quotient = make ( op::DIV, 4, subject, amount, 0, UNSIGNED );
			expr& target = exprs [ static_cast < size_t > ( e ) ];

			target.kind = op::CAST;
			target.a = quotient;
			target.b = -1;
			target.bits = 0;
			line.kind = stmt::NOP;

			break;

		}

	}

}

void decompiler::builder::fold_divisions ( ) {

	if ( !late_divisions )
		return;

	if ( split_magic_divisions )
		split_divisions ( );

	for ( int32_t e = 0; e < exprs.count ( ); e++ ) {

		const expr node = exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind != op::SHR || node.a < 0 || node.b < 0 || exprs [ static_cast < size_t > ( node.b ) ].kind != op::CONST )
			continue;

		const expr product = exprs [ static_cast < size_t > ( node.a ) ];

		if ( product.kind != op::INTRINSIC || product.value != I_UMULH || product.arg_count != 2 )
			continue;

		int32_t first = args [ static_cast < size_t > ( product.first_arg ) ];
		int32_t second = args [ static_cast < size_t > ( product.first_arg + 1 ) ];
		int32_t magic = exprs [ static_cast < size_t > ( first ) ].kind == op::CONST ? first : second;
		int32_t subject = magic == first ? second : first;

		if ( exprs [ static_cast < size_t > ( magic ) ].kind != op::CONST )
			continue;

		uint64_t divisor = magic_divisor ( static_cast < uint64_t > ( exprs [ static_cast < size_t > ( magic ) ].value ), static_cast < int32_t > ( exprs [ static_cast < size_t > ( node.b ) ].value ) );

		if ( !divisor )
			continue;

		uint8_t size = exprs [ static_cast < size_t > ( subject ) ].size;
		int32_t amount = constant ( static_cast < int64_t > ( divisor ), size );
		expr& target = exprs [ static_cast < size_t > ( e ) ];

		target.kind = op::DIV;
		target.a = subject;
		target.b = amount;
		target.bits = UNSIGNED;

	}

	auto bare = [ & ] ( int32_t e ) {

		while ( e >= 0 && exprs [ static_cast < size_t > ( e ) ].kind == op::CAST && !( exprs [ static_cast < size_t > ( e ) ].bits & ( SIGNED | FLOATING ) ) )
			e = exprs [ static_cast < size_t > ( e ) ].a;

		return e;

	};

	for ( int32_t e = 0; e < exprs.count ( ); e++ ) {

		const expr node = exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind != op::SUB || node.a < 0 || node.b < 0 || ( node.bits & FLOATING ) )
			continue;

		int32_t whole = bare ( node.a );
		int32_t scaled = bare ( node.b );

		if ( whole < 0 || scaled < 0 || exprs [ static_cast < size_t > ( scaled ) ].kind != op::MUL )
			continue;

		const expr product = exprs [ static_cast < size_t > ( scaled ) ];
		int32_t factor = product.b >= 0 && exprs [ static_cast < size_t > ( product.b ) ].kind == op::CONST ? product.b : ( product.a >= 0 && exprs [ static_cast < size_t > ( product.a ) ].kind == op::CONST ? product.a : -1 );

		if ( factor < 0 )
			continue;

		int32_t quotient = bare ( factor == product.b ? product.a : product.b );

		if ( quotient < 0 || exprs [ static_cast < size_t > ( quotient ) ].kind != op::DIV )
			continue;

		const expr division = exprs [ static_cast < size_t > ( quotient ) ];

		if ( division.b < 0 || exprs [ static_cast < size_t > ( division.b ) ].kind != op::CONST || exprs [ static_cast < size_t > ( division.b ) ].value != exprs [ static_cast < size_t > ( factor ) ].value )
			continue;

		int32_t dividend = division.a;

		if ( !same_tree ( *this, bare ( division.a ), whole, 0 ) ) {

			if ( !split_magic_divisions || exprs [ static_cast < size_t > ( whole ) ].kind != op::VAR )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( whole ) ].value;
			int32_t source = -1;
			int32_t found = 0;

			for ( const statement& line : statements ) {

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == v ) {

					source = line.right;
					found++;

				}

			}

			if ( found != 1 || source < 0 || !same_tree ( *this, bare ( source ), bare ( division.a ), 0 ) )
				continue;

			dividend = whole;

		}

		int32_t remainder = make ( op::MOD, division.size, dividend, division.b, 0, division.bits );
		expr& target = exprs [ static_cast < size_t > ( e ) ];

		if ( node.size == division.size ) {

			target = exprs [ static_cast < size_t > ( remainder ) ];

		} else {

			target.kind = op::CAST;
			target.a = remainder;
			target.b = -1;
			target.bits = 0;

		}

	}

}