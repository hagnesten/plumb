#include "decompiler/lift/global_reloads/global_reloads.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/prune/prune.h"

bool decompiler::global_slot_load ( const builder& b, int32_t e ) {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::GLOBAL && node.size == 8 && !( node.bits & FLOATING ) )
		return true;

	if ( node.kind != op::LOAD || node.size != 8 || ( node.bits & FLOATING ) || node.a < 0 )
		return false;

	const expr& where = b.exprs [ static_cast < size_t > ( node.a ) ];

	return where.kind == op::CONST && ( where.bits & POINTER );

}

bool decompiler::quiet_statement ( const builder& b, const statement& line ) {

	if ( line.kind == stmt::NOP )
		return true;

	if ( line.kind != stmt::ASSIGN || line.left < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
		return false;

	return !has_call ( b, line.right, 0 );

}

void decompiler::builder::global_reloads ( ) {

	int32_t total = code.count ( );

	if ( total != graph.blocks.count ( ) )
		return;

	base::list < int32_t > defs;
	base::list < int32_t > reads;
	base::list < int32_t > preds;

	defs.fill ( vars.size ( ), 0 );
	reads.fill ( vars.size ( ), 0 );
	preds.fill ( static_cast < size_t > ( total ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		bool direct = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

		if ( direct ) {

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v >= 0 && v < vars.count ( ) )
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

	for ( const analysis::edge& link : graph.edges ) {

		if ( link.to >= 0 && link.to < total )
			preds [ static_cast < size_t > ( link.to ) ]++;

	}

	for ( int32_t k = 0; k < total; k++ ) {

		block_code& head = code [ static_cast < size_t > ( k ) ];

		if ( !head.reachable || head.end != term::BRANCH || head.condition < 0 || head.taken == head.other )
			continue;

		int32_t def_at = -1;
		int64_t v = -1;
		bool quiet = true;

		for ( int32_t s = head.first; s < head.first + head.count && s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && global_slot_load ( *this, line.right ) ) {

				def_at = s;
				v = exprs [ static_cast < size_t > ( line.left ) ].value;
				quiet = true;

				continue;

			}

			if ( def_at >= 0 && ( !quiet_statement ( *this, line ) || mentions ( *this, line.right, v, 0 ) ) )
				quiet = false;

		}

		if ( def_at < 0 || !quiet || v < 0 || v >= vars.count ( ) || defs [ static_cast < size_t > ( v ) ] != 1 || reads [ static_cast < size_t > ( v ) ] != 2 )
			continue;

		const variable& item = vars [ static_cast < size_t > ( v ) ];

		if ( item.kind != var_kind::REGISTER || item.flags || item.cookie || item.saved || occurrences ( *this, head.condition, v, 0 ) != 1 )
			continue;

		int32_t call_at = -1;

		for ( int32_t side = 0; side < 2 && call_at < 0; side++ ) {

			int32_t next = side ? head.other : head.taken;

			if ( next < 0 || next >= total || next == k || preds [ static_cast < size_t > ( next ) ] != 1 )
				continue;

			const block_code& after = code [ static_cast < size_t > ( next ) ];

			for ( int32_t s = after.first; s < after.first + after.count && s < statements.count ( ); s++ ) {

				const statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				if ( line.right >= 0 && mentions ( *this, line.right, v, 0 ) ) {

					const expr& root = exprs [ static_cast < size_t > ( line.right ) ];
					int32_t called = strip_casts ( *this, line.right );

					if ( root.kind == op::CALL || ( called >= 0 && exprs [ static_cast < size_t > ( called ) ].kind == op::CALL ) ) {

						const expr& call = exprs [ static_cast < size_t > ( called >= 0 ? called : line.right ) ];

						if ( call.value == 0 && bare_var ( *this, call.a, v ) )
							call_at = s;

					}

					break;

				}

				if ( !quiet_statement ( *this, line ) )
					break;

			}

			if ( call_at < 0 ) {

				int32_t returned = after.end == term::RETURN || after.end == term::TAIL ? after.returned : -1;

				if ( returned >= 0 && mentions ( *this, returned, v, 0 ) ) {

					bool clean = true;

					for ( int32_t s = after.first; s < after.first + after.count && s < statements.count ( ) && clean; s++ )
						clean = quiet_statement ( *this, statements [ static_cast < size_t > ( s ) ] );

					int32_t called = strip_casts ( *this, returned );

					if ( clean && called >= 0 && exprs [ static_cast < size_t > ( called ) ].kind == op::CALL && exprs [ static_cast < size_t > ( called ) ].value == 0 && bare_var ( *this, exprs [ static_cast < size_t > ( called ) ].a, v ) ) {

						int32_t loaded = statements [ static_cast < size_t > ( def_at ) ].right;

						code [ static_cast < size_t > ( next ) ].returned = substitute ( *this, returned, v, loaded, 0 );
						head.condition = substitute ( *this, head.condition, v, loaded, 0 );
						statements [ static_cast < size_t > ( def_at ) ].kind = stmt::NOP;

						break;

					}

				}

			}

		}

		if ( call_at < 0 )
			continue;

		int32_t loaded = statements [ static_cast < size_t > ( def_at ) ].right;
		statement& use = statements [ static_cast < size_t > ( call_at ) ];

		use.right = substitute ( *this, use.right, v, loaded, 0 );
		head.condition = substitute ( *this, head.condition, v, loaded, 0 );
		statements [ static_cast < size_t > ( def_at ) ].kind = stmt::NOP;

	}

}
