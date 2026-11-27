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

bool decompiler::text_has ( const wchar_t* text, const wchar_t* part ) {

	for ( int32_t i = 0; text [ i ]; i++ ) {

		int32_t k = 0;

		while ( part [ k ] && text [ i + k ] == part [ k ] )
			k++;

		if ( !part [ k ] )
			return true;

	}

	return false;

}

bool decompiler::text_ends ( const wchar_t* text, const wchar_t* part ) {

	int32_t n = 0;
	int32_t m = 0;

	while ( text [ n ] )
		n++;

	while ( part [ m ] )
		m++;

	if ( m > n )
		return false;

	for ( int32_t k = 0; k < m; k++ ) {

		if ( text [ n - m + k ] != part [ k ] )
			return false;

	}

	return true;

}

int32_t decompiler::packed_flavor ( const wchar_t* name, bool result ) {

	if ( !( name [ 0 ] == L'_' && name [ 1 ] == L'm' && name [ 2 ] == L'm' && name [ 3 ] == L'_' ) )
		return 0;

	bool scalar = text_has ( name, L"movemask" ) || text_has ( name, L"extract" ) || text_has ( name, L"cvtsi128" );

	if ( result && scalar )
		return 0;

	if ( text_has ( name, L"cvt" ) ) {

		if ( !result )
			return 0;

	}

	if ( text_ends ( name, L"_ps" ) )
		return 3;

	if ( text_ends ( name, L"_pd" ) )
		return 4;

	if ( text_has ( name, L"epi" ) || text_has ( name, L"epu" ) || text_has ( name, L"si128" ) )
		return 2;

	return 0;

}

void decompiler::simd_vars ( builder& b, base::list < uint8_t >& vector, int32_t& si128 ) {

	si128 = -1;

	if ( !simd_types )
		return;

	int32_t total = b.vars.count ( );

	auto mark = [ & ] ( int32_t e, int32_t flavor ) {

		if ( e < 0 || !flavor )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind != op::VAR || node.value < 0 || node.value >= total || node.value >= vector.count ( ) )
			return;

		const variable& item = b.vars [ static_cast < size_t > ( node.value ) ];

		if ( item.kind != var_kind::REGISTER || !( item.bits & FLOATING ) || ( item.family < 16 && item.family >= 0 ) )
			return;

		uint8_t& slot = vector [ static_cast < size_t > ( node.value ) ];

		if ( !slot )
			slot = static_cast < uint8_t > ( flavor );

	};

	auto scan = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 96 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::INTRINSIC ) {

			int32_t flavor = packed_flavor ( intrinsic_name ( static_cast < int32_t > ( node.value ) ), false );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				mark ( b.args [ static_cast < size_t > ( node.first_arg + i ) ], flavor );

		}

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const statement& line : b.statements ) {

		if ( line.kind == stmt::NOP )
			continue;

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.right ) ].kind == op::INTRINSIC )
			mark ( line.left, packed_flavor ( intrinsic_name ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.right ) ].value ) ), true ) );

		scan ( scan, line.left, 0 );
		scan ( scan, line.right, 0 );

	}

	for ( const block_code& block : b.code ) {

		scan ( scan, block.condition, 0 );
		scan ( scan, block.returned, 0 );

	}

	for ( const statement& line : b.statements ) {

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
			continue;

		const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];
		const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

		if ( left.kind == op::VAR && right.kind == op::GLOBAL && right.size == 16 && left.value >= 0 && left.value < vector.count ( ) && vector [ static_cast < size_t > ( left.value ) ] == 2 ) {

			si128 = static_cast < int32_t > ( left.value );

			break;

		}

	}

}

void decompiler::vector_vars ( builder& b, base::list < uint8_t >& vector ) {

	int32_t total = b.vars.count ( );
	base::list < uint8_t > bad;
	base::list < int32_t > loads;
	base::list < int32_t > stores;

	vector.clear ( );
	vector.fill ( static_cast < size_t > ( total ), 0 );

	if ( !vector_copies )
		return;

	bad.fill ( static_cast < size_t > ( total ), 0 );
	loads.fill ( static_cast < size_t > ( total ), 0 );
	stores.fill ( static_cast < size_t > ( total ), 0 );

	for ( int32_t v = 0; v < total; v++ ) {

		const variable& item = b.vars [ static_cast < size_t > ( v ) ];

		if ( item.kind != var_kind::REGISTER || item.size < 8 || !( item.bits & FLOATING ) || item.flags || ( v < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( v ) ].empty ( ) ) )
			bad [ static_cast < size_t > ( v ) ] = 1;

	}

	auto taint = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 96 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::VAR && node.value >= 0 && node.value < total )
			bad [ static_cast < size_t > ( node.value ) ] = 1;

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const block_code& block : b.code ) {

		if ( !block.reachable )
			continue;

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			const expr* left = line.left >= 0 ? &b.exprs [ static_cast < size_t > ( line.left ) ] : nullptr;
			const expr* right = line.right >= 0 ? &b.exprs [ static_cast < size_t > ( line.right ) ] : nullptr;

			if ( line.kind == stmt::ASSIGN && left && left->kind == op::VAR && left->value >= 0 && left->value < total ) {

				if ( right && right->kind == op::LOAD && right->size == 16 ) {

					loads [ static_cast < size_t > ( left->value ) ]++;
					taint ( taint, right->a, 0 );

				} else {

					bad [ static_cast < size_t > ( left->value ) ] = 1;
					taint ( taint, line.right, 0 );

				}

				continue;

			}

			if ( ( line.kind == stmt::STORE || line.kind == stmt::ASSIGN ) && left && left->kind == op::LOAD && left->size == 16 && right && right->kind == op::VAR && right->value >= 0 && right->value < total ) {

				stores [ static_cast < size_t > ( right->value ) ]++;
				taint ( taint, left->a, 0 );

				continue;

			}

			taint ( taint, line.left, 0 );
			taint ( taint, line.right, 0 );

		}

		taint ( taint, block.condition, 0 );
		taint ( taint, block.selector, 0 );
		taint ( taint, block.returned, 0 );

	}

	for ( int32_t v = 0; v < total; v++ ) {

		if ( !bad [ static_cast < size_t > ( v ) ] && loads [ static_cast < size_t > ( v ) ] > 0 && stores [ static_cast < size_t > ( v ) ] > 0 )
			vector [ static_cast < size_t > ( v ) ] = 1;

	}

}