#include "decompiler/emit/narrowing/narrowing.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

int32_t decompiler::zero_width ( const builder& b, int32_t e, const base::list < uint8_t >& known, int32_t depth ) {

	if ( e < 0 || depth > 8 )
		return 8;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	switch ( node.kind ) {

		case op::CONST: {

			uint64_t v = static_cast < uint64_t > ( node.value );

			if ( node.value < 0 )
				return node.size;

			return v < 0x100 ? 1 : ( v < 0x10000 ? 2 : ( v < 0x100000000ull ? 4 : 8 ) );

		}

		case op::LOAD:
			return node.size;

		case op::CAST: {

			if ( node.bits & ( SIGNED | FLOATING ) )
				return node.size;

			int32_t inner = zero_width ( b, node.a, known, depth + 1 );

			return inner < node.size ? inner : node.size;

		}

		case op::VAR:

			if ( node.value >= 0 && node.value < known.count ( ) && known [ static_cast < size_t > ( node.value ) ] )
				return known [ static_cast < size_t > ( node.value ) ] < node.size ? known [ static_cast < size_t > ( node.value ) ] : node.size;

			return node.size;

		case op::AND: {

			int32_t left = zero_width ( b, node.a, known, depth + 1 );
			int32_t right = zero_width ( b, node.b, known, depth + 1 );

			return left < right ? left : right;

		}

		default:
			return node.size;

	}

}

void decompiler::narrow_values ( builder& b, base::list < uint8_t >& known ) {

	int32_t total = b.vars.count ( );

	known.clear ( );
	known.fill ( static_cast < size_t > ( total ), 0 );

	if ( !zero_ranges )
		return;

	for ( int32_t v = 0; v < total; v++ ) {

		const variable& item = b.vars [ static_cast < size_t > ( v ) ];

		if ( item.kind == var_kind::REGISTER && !( item.bits & FLOATING ) && !item.flags )
			known [ static_cast < size_t > ( v ) ] = 1;

	}

	for ( int32_t round = 0; round < 6; round++ ) {

		bool changed = false;

		for ( const statement& line : b.statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
				continue;

			const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];

			if ( left.kind != op::VAR || left.value < 0 || left.value >= total || !known [ static_cast < size_t > ( left.value ) ] )
				continue;

			int32_t width = zero_width ( b, line.right, known, 0 );

			if ( width > known [ static_cast < size_t > ( left.value ) ] ) {

				known [ static_cast < size_t > ( left.value ) ] = static_cast < uint8_t > ( width > 8 ? 8 : width );
				changed = true;

			}

		}

		if ( !changed )
			break;

	}

	for ( int32_t v = 0; v < total; v++ ) {

		const variable& item = b.vars [ static_cast < size_t > ( v ) ];

		if ( item.kind == var_kind::PARAMETER || known [ static_cast < size_t > ( v ) ] >= item.size )
			known [ static_cast < size_t > ( v ) ] = 0;

	}

	for ( const block_code& block : b.code ) {

		if ( block.returned >= 0 && b.exprs [ static_cast < size_t > ( block.returned ) ].kind == op::VAR ) {

			int64_t v = b.exprs [ static_cast < size_t > ( block.returned ) ].value;

			if ( v >= 0 && v < total )
				known [ static_cast < size_t > ( v ) ] = 0;

		}

	}

}