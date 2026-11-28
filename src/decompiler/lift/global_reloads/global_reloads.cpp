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

void decompiler::builder::reuse_condition_loads ( ) {

	for ( block_code& block : code ) {

		if ( !block.reachable || block.end != term::BRANCH || block.condition < 0 )
			continue;

		const expr& test = exprs [ static_cast < size_t > ( block.condition ) ];

		if ( test.kind < op::EQ || test.kind > op::GE || test.a < 0 || test.b < 0 || exprs [ static_cast < size_t > ( test.a ) ].kind != op::LOAD || exprs [ static_cast < size_t > ( test.b ) ].kind != op::LOAD )
			continue;

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.right ) ].kind != op::LOAD )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v < 0 || v >= vars.count ( ) || vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER || vars [ static_cast < size_t > ( v ) ].flags || vars [ static_cast < size_t > ( v ) ].size != exprs [ static_cast < size_t > ( line.right ) ].size )
				continue;

			bool clean = true;

			for ( int32_t t = s + 1; t < block.first + block.count && t < statements.count ( ) && clean; t++ ) {

				const statement& later = statements [ static_cast < size_t > ( t ) ];

				if ( later.kind == stmt::NOP )
					continue;

				bool defines = later.kind == stmt::ASSIGN && later.left >= 0 && exprs [ static_cast < size_t > ( later.left ) ].kind == op::VAR;

				if ( !defines || has_call ( *this, later.right, 0 ) || exprs [ static_cast < size_t > ( later.left ) ].value == v || mentions ( *this, line.right, exprs [ static_cast < size_t > ( later.left ) ].value, 0 ) )
					clean = false;

			}

			if ( !clean )
				continue;

			int32_t loaded = line.right;
			int32_t replacement = make ( op::VAR, exprs [ static_cast < size_t > ( line.left ) ].size, -1, -1, v );

			auto swap = [ & ] ( auto& self, int32_t e, int32_t depth ) -> int32_t {

				if ( e < 0 || depth > 32 )
					return e;

				if ( e == loaded || same ( e, loaded ) )
					return replacement;

				expr copy = exprs [ static_cast < size_t > ( e ) ];

				if ( copy.kind == op::CALL )
					return e;

				int32_t na = self ( self, copy.a, depth + 1 );
				int32_t nb = self ( self, copy.b, depth + 1 );
				int32_t nc = self ( self, copy.c, depth + 1 );

				if ( na == copy.a && nb == copy.b && nc == copy.c )
					return e;

				copy.a = na;
				copy.b = nb;
				copy.c = nc;
				exprs.push ( copy );

				return exprs.count ( ) - 1;

			};

			block.condition = swap ( swap, block.condition, 0 );

		}

	}

}

void decompiler::builder::retest_loads ( ) {

	for ( block_code& block : code ) {

		if ( !block.reachable || block.end != term::BRANCH || block.condition < 0 )
			continue;

		const expr& test = exprs [ static_cast < size_t > ( block.condition ) ];
		int32_t tested = -1;

		if ( ( test.kind == op::EQ || test.kind == op::NE ) && test.a >= 0 && test.b >= 0 && exprs [ static_cast < size_t > ( test.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( test.b ) ].value == 0 )
			tested = test.a;
		else if ( test.kind == op::LNOT && test.a >= 0 )
			tested = test.a;
		else if ( test.kind == op::VAR )
			tested = block.condition;

		if ( tested < 0 || exprs [ static_cast < size_t > ( tested ) ].kind != op::VAR )
			continue;

		int64_t v = exprs [ static_cast < size_t > ( tested ) ].value;
		int32_t last = -1;

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::NOP )
				last = s;

		}

		if ( last < 0 )
			continue;

		const statement& line = statements [ static_cast < size_t > ( last ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != v )
			continue;

		const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

		if ( source.kind != op::LOAD || ( source.bits & FLOATING ) || source.size != exprs [ static_cast < size_t > ( tested ) ].size || mentions ( *this, line.right, v, 0 ) || v < 0 || v >= vars.count ( ) || vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER || vars [ static_cast < size_t > ( v ) ].flags )
			continue;

		int32_t defs = 0;

		for ( const statement& other : statements ) {

			if ( other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( other.left ) ].value == v )
				defs++;

		}

		if ( defs < 2 )
			continue;

		bool select = false;

		int32_t nexts [ 2 ] = { block.taken, block.other };

		for ( int32_t next : nexts ) {

			if ( next < 0 || next >= code.count ( ) )
				continue;

			const block_code& after = code [ static_cast < size_t > ( next ) ];

			for ( int32_t s = after.first; s < after.first + after.count && s < statements.count ( ); s++ ) {

				const statement& first = statements [ static_cast < size_t > ( s ) ];

				if ( first.kind == stmt::NOP )
					continue;

				if ( first.kind == stmt::ASSIGN && first.left >= 0 && exprs [ static_cast < size_t > ( first.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( first.left ) ].value == v )
					select = true;

				break;

			}

		}

		if ( select )
			continue;

		block.condition = substitute ( *this, block.condition, v, line.right, 0 );

	}

}

void decompiler::builder::shared_exit ( ) {

	if ( !returns_value || code.count ( ) != graph.blocks.count ( ) || code.count ( ) < 3 )
		return;

	int64_t v = -1;
	int32_t found = 0;
	uint64_t edge = 0;

	for ( int32_t k = 0; k < code.count ( ); k++ ) {

		const block_code& block = code [ static_cast < size_t > ( k ) ];

		if ( graph.blocks [ static_cast < size_t > ( k ) ].end > edge )
			edge = graph.blocks [ static_cast < size_t > ( k ) ].end;

		if ( !block.reachable || block.end != term::RETURN )
			continue;

		if ( block.returned < 0 || exprs [ static_cast < size_t > ( block.returned ) ].kind != op::VAR )
			return;

		int64_t returned = exprs [ static_cast < size_t > ( block.returned ) ].value;

		if ( returned < 0 || returned >= vars.count ( ) || vars [ static_cast < size_t > ( returned ) ].family != 0 || ( v >= 0 && returned != v ) )
			return;

		v = returned;
		found++;

	}

	if ( found < 2 || v < 0 )
		return;

	int32_t exit = code.count ( );
	block_code tail;

	tail.first = statements.count ( );
	tail.count = 0;
	tail.end = term::RETURN;
	tail.returned = var_expr ( static_cast < int32_t > ( v ), 0 );
	tail.reachable = true;
	tail.last = edge;

	analysis::block spot;

	spot.start = edge;
	spot.end = edge;
	spot.last = edge;
	spot.returns = true;

	for ( int32_t k = 0; k < exit; k++ ) {

		block_code& block = code [ static_cast < size_t > ( k ) ];

		if ( !block.reachable || block.end != term::RETURN )
			continue;

		block.end = term::FALL;
		block.taken = exit;
		block.returned = -1;

		analysis::edge link;

		link.from = k;
		link.to = exit;
		link.kind = analysis::edge_kind::FALL;
		graph.edges.push ( link );

	}

	code.push ( tail );
	graph.blocks.push ( spot );
	reindex_edges ( );

}

void decompiler::builder::inline_pre_increments ( ) {

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

	for ( block_code& block : code ) {

		if ( !block.reachable )
			continue;

		base::list < int32_t > live;

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
				live.push ( s );

		}

		for ( int32_t i = 0; i + 1 < live.count ( ); i++ ) {

			statement& first = statements [ static_cast < size_t > ( live [ static_cast < size_t > ( i ) ] ) ];
			statement& second = statements [ static_cast < size_t > ( live [ static_cast < size_t > ( i + 1 ) ] ) ];

			if ( first.kind != stmt::ASSIGN || first.left < 0 || first.right < 0 || exprs [ static_cast < size_t > ( first.left ) ].kind != op::VAR || ( second.kind != stmt::STORE && second.kind != stmt::ASSIGN ) || second.left < 0 || second.right < 0 )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( first.left ) ].value;
			const expr step = exprs [ static_cast < size_t > ( first.right ) ];

			if ( v < 0 || v >= vars.count ( ) || defs [ static_cast < size_t > ( v ) ] != 1 || reads [ static_cast < size_t > ( v ) ] != 1 || vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER )
				continue;

			if ( step.kind != op::ADD || step.a < 0 || step.b < 0 || exprs [ static_cast < size_t > ( step.b ) ].kind != op::CONST || ( exprs [ static_cast < size_t > ( step.b ) ].value != 1 && exprs [ static_cast < size_t > ( step.b ) ].value != -1 ) || ( step.bits & FLOATING ) )
				continue;

			const expr& place = exprs [ static_cast < size_t > ( second.left ) ];
			bool global = place.kind == op::GLOBAL || ( place.kind == op::LOAD && place.a >= 0 && exprs [ static_cast < size_t > ( place.a ) ].kind == op::CONST );

			if ( !global || !same ( step.a, second.left ) )
				continue;

			bool stored_var = exprs [ static_cast < size_t > ( second.right ) ].kind == op::VAR && exprs [ static_cast < size_t > ( second.right ) ].value == v;

			if ( !stored_var && !same ( second.right, first.right ) )
				continue;

			int32_t increment = make ( op::SET, exprs [ static_cast < size_t > ( second.left ) ].size, second.left, first.right, 2 );
			bool placed = false;

			if ( i + 2 < live.count ( ) ) {

				statement& third = statements [ static_cast < size_t > ( live [ static_cast < size_t > ( i + 2 ) ] ) ];

				if ( third.kind != stmt::ASM && third.right >= 0 && mentions ( *this, third.right, v, 0 ) ) {

					third.right = substitute ( *this, third.right, v, increment, 0 );
					placed = true;

				}

			} else {

				if ( block.returned >= 0 && mentions ( *this, block.returned, v, 0 ) ) {

					block.returned = substitute ( *this, block.returned, v, increment, 0 );
					placed = true;

				} else if ( block.condition >= 0 && mentions ( *this, block.condition, v, 0 ) ) {

					block.condition = substitute ( *this, block.condition, v, increment, 0 );
					placed = true;

				}

			}

			if ( !placed )
				continue;

			first.kind = stmt::NOP;
			second.kind = stmt::NOP;
			i++;

		}

	}

}

void decompiler::builder::reuse_store_loads ( ) {

	for ( const block_code& block : code ) {

		if ( !block.reachable )
			continue;

		int32_t prior = -1;

		for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

			statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind == stmt::STORE && prior >= 0 && line.right >= 0 ) {

				const statement& made = statements [ static_cast < size_t > ( prior ) ];

				if ( made.kind == stmt::ASSIGN && made.left >= 0 && made.right >= 0 && exprs [ static_cast < size_t > ( made.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( made.right ) ].kind == op::LOAD && !( exprs [ static_cast < size_t > ( made.right ) ].bits & FLOATING ) ) {

					int64_t v = exprs [ static_cast < size_t > ( made.left ) ].value;

					const expr& root = exprs [ static_cast < size_t > ( line.right ) ];
					bool direct = same ( line.right, made.right ) || ( root.kind != op::LOAD && root.kind != op::CALL && ( ( root.a >= 0 && same ( root.a, made.right ) ) || ( root.b >= 0 && same ( root.b, made.right ) ) ) );

					if ( direct && v >= 0 && v < vars.count ( ) && vars [ static_cast < size_t > ( v ) ].kind == var_kind::REGISTER && !vars [ static_cast < size_t > ( v ) ].flags && vars [ static_cast < size_t > ( v ) ].size == exprs [ static_cast < size_t > ( made.right ) ].size && !mentions ( *this, made.right, v, 0 ) ) {

						int32_t loaded = made.right;
						int32_t replacement = make ( op::VAR, exprs [ static_cast < size_t > ( made.left ) ].size, -1, -1, v );

						auto swap = [ & ] ( auto& self, int32_t e, int32_t depth ) -> int32_t {

							if ( e < 0 || depth > 32 )
								return e;

							if ( e == loaded || same ( e, loaded ) )
								return replacement;

							expr copy = exprs [ static_cast < size_t > ( e ) ];

							if ( copy.kind == op::CALL )
								return e;

							int32_t na = self ( self, copy.a, depth + 1 );
							int32_t nb = self ( self, copy.b, depth + 1 );
							int32_t nc = self ( self, copy.c, depth + 1 );

							if ( na == copy.a && nb == copy.b && nc == copy.c )
								return e;

							copy.a = na;
							copy.b = nb;
							copy.c = nc;
							exprs.push ( copy );

							return exprs.count ( ) - 1;

						};

						line.right = swap ( swap, line.right, 0 );

					}

				}

			}

			prior = s;

		}

	}

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

	if ( next_reloads ) {

		for ( const block_code& head : code ) {

			if ( !head.reachable )
				continue;

			for ( int32_t s = head.first; s < head.first + head.count && s < statements.count ( ); s++ ) {

				const statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || !global_slot_load ( *this, line.right ) )
					continue;

				int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( v < 0 || v >= vars.count ( ) || vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER || vars [ static_cast < size_t > ( v ) ].flags || vars [ static_cast < size_t > ( v ) ].cookie || reads [ static_cast < size_t > ( v ) ] < 3 || defs [ static_cast < size_t > ( v ) ] != 1 )
					continue;

				int32_t t = s + 1;

				while ( t < head.first + head.count && t < statements.count ( ) && statements [ static_cast < size_t > ( t ) ].kind == stmt::NOP )
					t++;

				if ( t >= head.first + head.count || t >= statements.count ( ) )
					continue;

				statement& use = statements [ static_cast < size_t > ( t ) ];

				if ( use.kind == stmt::ASM )
					continue;

				bool defines = use.kind == stmt::ASSIGN && use.left >= 0 && exprs [ static_cast < size_t > ( use.left ) ].kind == op::VAR;
				bool in_left = !defines && use.left >= 0 && mentions ( *this, use.left, v, 0 );
				bool in_right = use.right >= 0 && mentions ( *this, use.right, v, 0 );

				if ( !in_left && !in_right )
					continue;

				if ( in_left )
					use.left = substitute ( *this, use.left, v, line.right, 0 );

				if ( in_right )
					use.right = substitute ( *this, use.right, v, line.right, 0 );

			}

		}

	}

	if ( !import_callees || !file )
		return;

	base::list < int32_t > callee_reads;

	callee_reads.fill ( vars.size ( ), 0 );

	auto count_callees = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 64 )
			return;

		const expr& node = exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::CALL && node.value == 0 && node.a >= 0 && exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR ) {

			int64_t v = exprs [ static_cast < size_t > ( node.a ) ].value;

			if ( v >= 0 && v < callee_reads.count ( ) )
				callee_reads [ static_cast < size_t > ( v ) ]++;

		}

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		count_callees ( count_callees, line.left, 0 );
		count_callees ( count_callees, line.right, 0 );

	}

	for ( const block_code& item : code ) {

		count_callees ( count_callees, item.condition, 0 );
		count_callees ( count_callees, item.returned, 0 );

	}

	for ( int32_t s = 0; s < statements.count ( ); s++ ) {

		statement& line = statements [ static_cast < size_t > ( s ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
			continue;

		int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;
		const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

		if ( v < 0 || v >= vars.count ( ) || defs [ static_cast < size_t > ( v ) ] != 1 || !reads [ static_cast < size_t > ( v ) ] || reads [ static_cast < size_t > ( v ) ] != callee_reads [ static_cast < size_t > ( v ) ] || vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER )
			continue;

		if ( !global_slot_load ( *this, line.right ) )
			continue;

		uint64_t slot = static_cast < uint64_t > ( source.kind == op::GLOBAL ? source.value : exprs [ static_cast < size_t > ( source.a ) ].value );
		bool imported = false;

		for ( const loader::import& entry : file->imports )
			imported = imported || entry.slot == slot;

		if ( !imported )
			continue;

		int32_t loaded = line.right;

		line.kind = stmt::NOP;

		for ( statement& other : statements ) {

			if ( other.kind == stmt::NOP || other.kind == stmt::ASM )
				continue;

			bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;

			if ( !defines )
				other.left = substitute ( *this, other.left, v, loaded, 0 );

			other.right = substitute ( *this, other.right, v, loaded, 0 );

		}

		for ( block_code& item : code ) {

			item.condition = substitute ( *this, item.condition, v, loaded, 0 );
			item.returned = substitute ( *this, item.returned, v, loaded, 0 );

		}

	}

}
