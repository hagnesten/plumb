#include "decompiler/internal.hh"

namespace decompiler {

	static void count_vars ( const builder& b, int32_t e, base::list < int32_t >& uses, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( ( node.kind == op::VAR || ( node.kind == op::ADDRESS && node.a < 0 ) ) && node.value >= 0 && node.value < uses.count ( ) )
			uses [ static_cast < size_t > ( node.value ) ] += node.kind == op::ADDRESS ? 100 : 1;

		count_vars ( b, node.a, uses, depth + 1 );
		count_vars ( b, node.b, uses, depth + 1 );
		count_vars ( b, node.c, uses, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			count_vars ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], uses, depth + 1 );

	}

	static int32_t substitute ( builder& b, int32_t e, int64_t var, int32_t value, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return e;

		const expr node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::VAR && node.value == var ) {

			uint8_t size = b.exprs [ static_cast < size_t > ( value ) ].size;

			return node.size < size ? b.cast ( value, node.size, 0 ) : value;

		}

		int32_t a = substitute ( b, node.a, var, value, depth + 1 );
		int32_t c = substitute ( b, node.b, var, value, depth + 1 );
		int32_t third = substitute ( b, node.c, var, value, depth + 1 );
		bool any = a != node.a || c != node.b || third != node.c;
		base::list < int32_t > list;

		for ( int32_t i = 0; i < node.arg_count; i++ ) {

			int32_t old = b.args [ static_cast < size_t > ( node.first_arg + i ) ];
			int32_t made = substitute ( b, old, var, value, depth + 1 );

			any = any || made != old;
			list.push ( made );

		}

		if ( !any )
			return e;

		expr copy = node;

		copy.a = a;
		copy.b = c;
		copy.c = third;

		if ( copy.arg_count ) {

			copy.first_arg = b.args.count ( );

			for ( int32_t item : list )
				b.args.push ( item );

		}

		b.exprs.push ( copy );

		return b.exprs.count ( ) - 1;

	}

	void builder::sink_into_tests ( ) {

		base::list < int32_t > uses;

		uses.fill ( vars.size ( ), 0 );

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

			if ( !defines )
				count_vars ( *this, line.left, uses, 0 );

			count_vars ( *this, line.right, uses, 0 );

		}

		for ( const block_code& item : code ) {

			count_vars ( *this, item.condition, uses, 0 );
			count_vars ( *this, item.selector, uses, 0 );
			count_vars ( *this, item.returned, uses, 0 );

		}

		base::list < int32_t > writes;

		writes.fill ( vars.size ( ), 0 );

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

				int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( v >= 0 && v < writes.count ( ) )
					writes [ static_cast < size_t > ( v ) ]++;

			}

		}

		for ( block_code& item : code ) {

			bool test = item.end == term::BRANCH && item.condition >= 0;
			bool back = item.end == term::RETURN && item.returned >= 0;

			if ( !test && !back )
				continue;

			int32_t last = -1;

			for ( int32_t s = item.first; s < item.first + item.count && s < statements.count ( ); s++ ) {

				if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
					last = s;

			}

			if ( last < 0 )
				continue;

			statement& line = statements [ static_cast < size_t > ( last ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v < 0 || v >= vars.count ( ) || uses [ static_cast < size_t > ( v ) ] != 1 || writes [ static_cast < size_t > ( v ) ] != 1 )
				continue;

			const variable& owner = vars [ static_cast < size_t > ( v ) ];

			if ( owner.kind != var_kind::REGISTER || owner.cookie || owner.saved || ( owner.bits & FLOATING ) )
				continue;

			if ( back && exprs [ static_cast < size_t > ( line.right ) ].kind != op::CALL )
				continue;

			int32_t& slot = test ? item.condition : item.returned;
			int32_t made = substitute ( *this, slot, v, line.right, 0 );

			if ( made == slot )
				continue;

			slot = made;
			line.kind = stmt::NOP;

		}

	}

	static void note_signs ( const builder& b, int32_t e, base::list < uint8_t >& marks, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
		bool ordered = node.kind == op::LT || node.kind == op::LE || node.kind == op::GT || node.kind == op::GE;

		if ( ordered && ( node.bits & ( SIGNED | UNSIGNED ) ) ) {

			const int32_t sides [ 2 ] = { node.a, node.b };

			for ( int32_t side : sides ) {

				if ( side < 0 )
					continue;

				const expr& operand = b.exprs [ static_cast < size_t > ( side ) ];

				if ( operand.kind != op::VAR || operand.value < 0 || operand.value >= marks.count ( ) )
					continue;

				if ( b.vars [ static_cast < size_t > ( operand.value ) ].size != operand.size )
					continue;

				marks [ static_cast < size_t > ( operand.value ) ] |= ( node.bits & UNSIGNED ) ? 1 : 2;

			}

		}

		note_signs ( b, node.a, marks, depth + 1 );
		note_signs ( b, node.b, marks, depth + 1 );
		note_signs ( b, node.c, marks, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			note_signs ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], marks, depth + 1 );

	}

	void builder::infer_signs ( ) {

		base::list < uint8_t > marks;

		marks.fill ( vars.size ( ), 0 );

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			note_signs ( *this, line.left, marks, 0 );
			note_signs ( *this, line.right, marks, 0 );

		}

		for ( const block_code& item : code ) {

			if ( !item.reachable )
				continue;

			note_signs ( *this, item.condition, marks, 0 );
			note_signs ( *this, item.returned, marks, 0 );

		}

		for ( int32_t v = 0; v < vars.count ( ); v++ ) {

			variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( marks [ static_cast < size_t > ( v ) ] == 1 && !( item.bits & ( FLOATING | POINTER ) ) )
				item.bits = static_cast < uint8_t > ( ( item.bits & ~SIGNED ) | UNSIGNED );

		}

	}

	void builder::merge_conditions ( ) {

		sink_into_tests ( );

		int32_t total = code.count ( );

		if ( total != graph.blocks.count ( ) || total < 3 )
			return;

		base::list < int32_t > preds;

		auto count_preds = [ & ] ( ) {

			preds.clear ( );
			preds.fill ( static_cast < size_t > ( total ), 0 );

			for ( const analysis::edge& link : graph.edges ) {

				if ( link.to >= 0 && link.to < total )
					preds [ static_cast < size_t > ( link.to ) ]++;

			}

		};

		auto empty = [ & ] ( int32_t block ) {

			const block_code& item = code [ static_cast < size_t > ( block ) ];

			for ( int32_t s = item.first; s < item.first + item.count && s < statements.count ( ); s++ ) {

				if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
					return false;

			}

			return true;

		};

		auto rewire = [ & ] ( int32_t a, int32_t gone, int32_t taken, int32_t other ) {

			int32_t kept = 0;

			for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

				const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];

				if ( link.from == a || link.from == gone )
					continue;

				graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

			}

			while ( graph.edges.count ( ) > kept )
				graph.edges.pop ( );

			analysis::edge yes;

			yes.from = a;
			yes.to = taken;
			yes.kind = analysis::edge_kind::TAKEN;
			graph.edges.push ( yes );

			analysis::edge no;

			no.from = a;
			no.to = other;
			no.kind = analysis::edge_kind::NOT_TAKEN;
			graph.edges.push ( no );

			block_code& dead = code [ static_cast < size_t > ( gone ) ];

			dead.reachable = false;
			dead.end = term::STOP;
			dead.taken = -1;
			dead.other = -1;

		};

		for ( int32_t round = 0; round < 64; round++ ) {

			bool changed = false;

			count_preds ( );

			for ( int32_t a = 0; a < total; a++ ) {

				block_code& head = code [ static_cast < size_t > ( a ) ];

				if ( head.end != term::BRANCH || head.condition < 0 || !head.reachable || head.taken == head.other )
					continue;

				for ( int32_t side = 0; side < 2 && !changed; side++ ) {

					int32_t inner = side == 0 ? head.other : head.taken;

					if ( inner <= 0 || inner >= total || inner == a || preds [ static_cast < size_t > ( inner ) ] != 1 )
						continue;

					const block_code next = code [ static_cast < size_t > ( inner ) ];

					if ( next.end != term::BRANCH || next.condition < 0 || next.taken == next.other || next.taken == inner || next.other == inner || !empty ( inner ) )
						continue;

					int32_t shared = side == 0 ? head.taken : head.other;
					int32_t condition = -1;
					int32_t taken = -1;
					int32_t other = -1;

					if ( side == 0 ) {

						if ( next.taken == shared ) {

							condition = binary ( op::LOR, head.condition, next.condition, 1 );
							taken = shared;
							other = next.other;

						} else if ( next.other == shared ) {

							condition = binary ( op::LOR, head.condition, negate ( next.condition ), 1 );
							taken = shared;
							other = next.taken;

						}

					} else {

						if ( next.other == shared ) {

							condition = binary ( op::LAND, head.condition, next.condition, 1 );
							taken = next.taken;
							other = shared;

						} else if ( next.taken == shared ) {

							condition = binary ( op::LAND, head.condition, negate ( next.condition ), 1 );
							taken = next.other;
							other = shared;

						}

					}

					if ( condition < 0 || taken == other )
						continue;

					head.condition = condition;
					head.taken = taken;
					head.other = other;
					head.last = next.last;
					rewire ( a, inner, taken, other );
					changed = true;

				}

				if ( changed )
					break;

			}

			if ( !changed )
				break;

		}

		reindex_edges ( );

	}

}
