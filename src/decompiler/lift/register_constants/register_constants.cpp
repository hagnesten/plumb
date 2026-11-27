#include "decompiler/lift/register_constants/register_constants.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::builder::propagate_register_constants ( ) {

	int32_t total = vars.count ( );
	int32_t blocks = code.count ( );
	base::list < int32_t > slot;
	base::list < int32_t > picked;
	base::list < int32_t > defs;

	slot.fill ( static_cast < size_t > ( total ), -1 );
	defs.fill ( static_cast < size_t > ( total ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && exprs [ static_cast < size_t > ( line.left ) ].value < total )
			defs [ static_cast < size_t > ( exprs [ static_cast < size_t > ( line.left ) ].value ) ]++;

	}

	for ( const statement& line : statements ) {

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.right ) ].kind != op::CONST || ( exprs [ static_cast < size_t > ( line.right ) ].bits & ( POINTER | FLOATING ) ) )
			continue;

		int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

		if ( v < 0 || v >= total || slot [ static_cast < size_t > ( v ) ] >= 0 || picked.count ( ) >= 48 )
			continue;

		const variable& item = vars [ static_cast < size_t > ( v ) ];

		if ( item.kind != var_kind::REGISTER || item.flags || ( item.bits & FLOATING ) || defs [ static_cast < size_t > ( v ) ] < 2 || defs [ static_cast < size_t > ( v ) ] > 8 )
			continue;

		slot [ static_cast < size_t > ( v ) ] = picked.count ( );
		picked.push ( static_cast < int32_t > ( v ) );

	}

	int32_t width = picked.count ( );

	if ( !width || blocks == 0 || blocks > 4000 )
		return;

	base::list < uint8_t > state;
	base::list < int64_t > held;
	base::list < uint8_t > out_state;
	base::list < int64_t > out_held;

	state.fill ( static_cast < size_t > ( blocks * width ), 0 );
	held.fill ( static_cast < size_t > ( blocks * width ), 0 );
	out_state.fill ( static_cast < size_t > ( blocks * width ), 0 );
	out_held.fill ( static_cast < size_t > ( blocks * width ), 0 );

	base::list < uint8_t > cur;
	base::list < int64_t > cur_held;

	cur.fill ( static_cast < size_t > ( width ), 0 );
	cur_held.fill ( static_cast < size_t > ( width ), 0 );

	auto meet = [ & ] ( size_t into, uint8_t s, int64_t h ) {

		if ( s == 0 )
			return;

		if ( cur [ into ] == 0 ) {

			cur [ into ] = s;
			cur_held [ into ] = h;

		} else if ( cur [ into ] != s || ( s == 1 && cur_held [ into ] != h ) )
			cur [ into ] = 2;

	};

	auto transfer = [ & ] ( int32_t k ) {

		const block_code& block = code [ static_cast < size_t > ( k ) ];

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v < 0 || v >= total || slot [ static_cast < size_t > ( v ) ] < 0 )
				continue;

			size_t c = static_cast < size_t > ( slot [ static_cast < size_t > ( v ) ] );
			const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

			if ( right.kind == op::CONST && !( right.bits & ( POINTER | FLOATING ) ) ) {

				cur [ c ] = 1;
				cur_held [ c ] = right.value;

			} else
				cur [ c ] = 2;

		}

	};

	base::list < base::list < int32_t > > preds;

	preds.resize ( static_cast < size_t > ( blocks ) );

	for ( int32_t k = 0; k < blocks; k++ ) {

		const block_code& block = code [ static_cast < size_t > ( k ) ];

		if ( !block.reachable )
			continue;

		if ( block.taken >= 0 && block.taken < blocks )
			preds [ static_cast < size_t > ( block.taken ) ].push ( k );

		if ( block.end == term::BRANCH && block.other >= 0 && block.other < blocks && block.other != block.taken )
			preds [ static_cast < size_t > ( block.other ) ].push ( k );

	}

	for ( int32_t round = 0; round < 32; round++ ) {

		bool moved = false;

		for ( int32_t k = 0; k < blocks; k++ ) {

			if ( !code [ static_cast < size_t > ( k ) ].reachable )
				continue;

			for ( int32_t c = 0; c < width; c++ ) {

				cur [ static_cast < size_t > ( c ) ] = k == 0 ? 2 : 0;
				cur_held [ static_cast < size_t > ( c ) ] = 0;

			}

			if ( k != 0 && preds [ static_cast < size_t > ( k ) ].empty ( ) ) {

				for ( int32_t c = 0; c < width; c++ )
					cur [ static_cast < size_t > ( c ) ] = 2;

			}

			for ( int32_t p : preds [ static_cast < size_t > ( k ) ] ) {

				for ( int32_t c = 0; c < width; c++ )
					meet ( static_cast < size_t > ( c ), out_state [ static_cast < size_t > ( p * width + c ) ], out_held [ static_cast < size_t > ( p * width + c ) ] );

			}

			for ( int32_t c = 0; c < width; c++ ) {

				state [ static_cast < size_t > ( k * width + c ) ] = cur [ static_cast < size_t > ( c ) ];
				held [ static_cast < size_t > ( k * width + c ) ] = cur_held [ static_cast < size_t > ( c ) ];

			}

			transfer ( k );

			for ( int32_t c = 0; c < width; c++ ) {

				size_t at = static_cast < size_t > ( k * width + c );

				if ( out_state [ at ] != cur [ static_cast < size_t > ( c ) ] || out_held [ at ] != cur_held [ static_cast < size_t > ( c ) ] ) {

					out_state [ at ] = cur [ static_cast < size_t > ( c ) ];
					out_held [ at ] = cur_held [ static_cast < size_t > ( c ) ];
					moved = true;

				}

			}

		}

		if ( !moved )
			break;

	}

	for ( int32_t k = 0; k < blocks; k++ ) {

		const block_code& block = code [ static_cast < size_t > ( k ) ];

		if ( !block.reachable )
			continue;

		for ( int32_t c = 0; c < width; c++ ) {

			cur [ static_cast < size_t > ( c ) ] = state [ static_cast < size_t > ( k * width + c ) ];
			cur_held [ static_cast < size_t > ( c ) ] = held [ static_cast < size_t > ( k * width + c ) ];

		}

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM || line.right < 0 )
				continue;

			const expr& right = exprs [ static_cast < size_t > ( line.right ) ];
			int32_t stored = line.right;

			while ( stored >= 0 && exprs [ static_cast < size_t > ( stored ) ].kind == op::CAST && !( exprs [ static_cast < size_t > ( stored ) ].bits & FLOATING ) )
				stored = exprs [ static_cast < size_t > ( stored ) ].a;

			if ( line.kind == stmt::STORE && stored >= 0 && exprs [ static_cast < size_t > ( stored ) ].kind == op::VAR && exprs [ static_cast < size_t > ( stored ) ].value >= 0 && exprs [ static_cast < size_t > ( stored ) ].value < total && slot [ static_cast < size_t > ( exprs [ static_cast < size_t > ( stored ) ].value ) ] >= 0 ) {

				size_t c = static_cast < size_t > ( slot [ static_cast < size_t > ( exprs [ static_cast < size_t > ( stored ) ].value ) ] );

				if ( cur [ c ] == 1 ) {

					int64_t shown = cur_held [ c ];

					if ( right.size < 8 )
						shown = static_cast < int64_t > ( static_cast < uint64_t > ( shown ) & ( ( 1ull << ( right.size * 8 ) ) - 1 ) );

					line.right = make ( op::CONST, right.size, -1, -1, shown, 0 );

				}

			} else if ( right.kind == op::CALL ) {

				for ( int32_t i = 0; i < right.arg_count; i++ ) {

					int32_t arg = args [ static_cast < size_t > ( right.first_arg + i ) ];

					if ( arg < 0 || exprs [ static_cast < size_t > ( arg ) ].kind != op::VAR )
						continue;

					int64_t v = exprs [ static_cast < size_t > ( arg ) ].value;

					if ( v < 0 || v >= total || slot [ static_cast < size_t > ( v ) ] < 0 )
						continue;

					size_t c = static_cast < size_t > ( slot [ static_cast < size_t > ( v ) ] );

					if ( cur [ c ] == 1 )
						args [ static_cast < size_t > ( right.first_arg + i ) ] = make ( op::CONST, exprs [ static_cast < size_t > ( arg ) ].size, -1, -1, cur_held [ c ], 0 );

				}

			}

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

				int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( v >= 0 && v < total && slot [ static_cast < size_t > ( v ) ] >= 0 ) {

					size_t c = static_cast < size_t > ( slot [ static_cast < size_t > ( v ) ] );
					const expr& now = exprs [ static_cast < size_t > ( line.right ) ];

					if ( now.kind == op::CONST && !( now.bits & ( POINTER | FLOATING ) ) ) {

						cur [ c ] = 1;
						cur_held [ c ] = now.value;

					} else
						cur [ c ] = 2;

				}

			}

		}

	}

}