#include "decompiler/lift/prune/prune.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "decompiler/literals/literals.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"

void decompiler::count_reads ( const builder& b, int32_t e, base::list < int32_t >& reads ) {

	if ( e < 0 )
		return;

	const expr& item = b.exprs [ static_cast < size_t > ( e ) ];

	if ( ( item.kind == op::VAR || ( item.kind == op::ADDRESS && item.a < 0 ) ) && item.value >= 0 && item.value < reads.count ( ) )
		reads [ static_cast < size_t > ( item.value ) ]++;

	count_reads ( b, item.a, reads );
	count_reads ( b, item.b, reads );
	count_reads ( b, item.c, reads );

	for ( int32_t i = 0; i < item.arg_count; i++ )
		count_reads ( b, b.args [ static_cast < size_t > ( item.first_arg + i ) ], reads );

}

bool decompiler::pure_intrinsic ( int32_t id ) {

	switch ( id ) {

		case I_TZCNT:
		case I_LZCNT:
		case I_POPCNT:
		case I_BYTESWAP:
		case I_ROTL:
		case I_ROTR:
		case I_UMULH:
		case I_MULH:
		case I_SQRTF:
		case I_SQRT:
		case I_FMINF:
		case I_FMAXF:
		case I_FMIN:
		case I_FMAX:
		case I_OVERFLOW:
		case I_PARITY:
		case I_UNORDERED:
		case I_BITTEST:
		case I_AND:
		case I_OR:
		case I_XOR:
		case I_ANDNOT:
		case I_SHLD:
		case I_SHRD:
		case I_LOADL_PI:
		case I_LOADH_PI:
		case I_LOADL_PD:
		case I_LOADH_PD:
		case I_XOR_PS:
		case I_XOR_PD:
		case I_XOR_SI128:
		case I_AND_PS:
		case I_AND_PD:
		case I_OR_PS:
		case I_OR_PD:
			return true;

		default:
			return false;

	}

}

bool decompiler::has_effects ( const builder& b, int32_t e ) {

	if ( e < 0 )
		return false;

	const expr& item = b.exprs [ static_cast < size_t > ( e ) ];

	if ( item.kind == op::CALL || ( item.kind == op::INTRINSIC && item.value < packed_base && !( pure_intrinsics && pure_intrinsic ( static_cast < int32_t > ( item.value ) ) ) ) )
		return true;

	if ( has_effects ( b, item.a ) || has_effects ( b, item.b ) || has_effects ( b, item.c ) )
		return true;

	for ( int32_t i = 0; i < item.arg_count; i++ ) {

		if ( has_effects ( b, b.args [ static_cast < size_t > ( item.first_arg + i ) ] ) )
			return true;

	}

	return false;

}

void decompiler::release_reads ( const builder& b, int32_t e, base::list < int32_t >& pending ) {

	if ( e < 0 )
		return;

	const expr& item = b.exprs [ static_cast < size_t > ( e ) ];

	if ( ( item.kind == op::VAR || item.kind == op::ADDRESS ) && item.value >= 0 && item.value < pending.count ( ) )
		pending [ static_cast < size_t > ( item.value ) ] = -1;

	release_reads ( b, item.a, pending );
	release_reads ( b, item.b, pending );
	release_reads ( b, item.c, pending );

	for ( int32_t i = 0; i < item.arg_count; i++ )
		release_reads ( b, b.args [ static_cast < size_t > ( item.first_arg + i ) ], pending );

}

void decompiler::builder::prune ( ) {

	base::list < int32_t > reads;
	base::list < int32_t > pending;

	pending.fill ( vars.size ( ), -1 );

	for ( const block_code& block : code ) {

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind == stmt::ASM ) {

				for ( int32_t& slot : pending )
					slot = -1;

				continue;

			}

			release_reads ( *this, line.right, pending );

			bool direct = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

			if ( !direct ) {

				release_reads ( *this, line.left, pending );

				continue;

			}

			int32_t target = static_cast < int32_t > ( exprs [ static_cast < size_t > ( line.left ) ].value );

			if ( target < 0 || target >= vars.count ( ) )
				continue;

			const variable& item = vars [ static_cast < size_t > ( target ) ];

			if ( item.kind == var_kind::STACK || ( item.kind == var_kind::PARAMETER && item.family < 0 ) || item.cookie || item.saved )
				continue;

			int32_t earlier = pending [ static_cast < size_t > ( target ) ];

			if ( earlier >= 0 ) {

				statement& dead = statements [ static_cast < size_t > ( earlier ) ];

				if ( has_effects ( *this, dead.right ) ) {

					dead.kind = stmt::EXPR;
					dead.left = -1;

				} else
					dead.kind = stmt::NOP;

			}

			pending [ static_cast < size_t > ( target ) ] = s;

		}

		for ( int32_t& slot : pending )
			slot = -1;

	}

	bool escaped = false;

	for ( const expr& node : exprs ) {

		if ( node.kind == op::ADDRESS && node.a < 0 && node.value >= 0 && node.value < vars.count ( ) && vars [ static_cast < size_t > ( node.value ) ].kind == var_kind::STACK )
			escaped = true;

		if ( node.kind == op::ADDRESS && node.a >= 0 && node.a < exprs.count ( ) && exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR && exprs [ static_cast < size_t > ( node.a ) ].value >= 0 && exprs [ static_cast < size_t > ( node.a ) ].value < vars.count ( ) && vars [ static_cast < size_t > ( exprs [ static_cast < size_t > ( node.a ) ].value ) ].kind == var_kind::STACK )
			escaped = true;

	}

	for ( int32_t round = 0; round < 8; round++ ) {

		reads.clear ( );
		reads.fill ( vars.size ( ), 0 );

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				count_reads ( *this, line.left, reads );

			count_reads ( *this, line.right, reads );

		}

		for ( const block_code& item : code ) {

			if ( !item.reachable )
				continue;

			count_reads ( *this, item.condition, reads );
			count_reads ( *this, item.selector, reads );

			if ( returns_value || item.end == term::TAIL )
				count_reads ( *this, item.returned, reads );

		}

		bool changed = false;

		for ( statement& line : statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 )
				continue;

			const expr& target = exprs [ static_cast < size_t > ( line.left ) ];

			if ( target.kind != op::VAR || target.value < 0 || target.value >= vars.count ( ) )
				continue;

			const variable& item = vars [ static_cast < size_t > ( target.value ) ];

			if ( ( item.kind == var_kind::PARAMETER && item.family < 0 ) || item.cookie || item.saved || reads [ static_cast < size_t > ( target.value ) ] )
				continue;

			if ( item.kind == var_kind::STACK ) {

				if ( escaped )
					continue;

				bool overlapped = false;

				for ( int32_t other = 0; other < vars.count ( ) && !overlapped; other++ ) {

					const variable& near = vars [ static_cast < size_t > ( other ) ];

					if ( near.kind == var_kind::STACK && reads [ static_cast < size_t > ( other ) ] && near.offset < item.offset + item.size && item.offset < near.offset + near.size )
						overlapped = true;

				}

				if ( overlapped )
					continue;

			}

			if ( has_effects ( *this, line.right ) ) {

				line.kind = stmt::EXPR;
				line.left = -1;

			} else
				line.kind = stmt::NOP;

			changed = true;

		}

		if ( !changed )
			break;

	}

}