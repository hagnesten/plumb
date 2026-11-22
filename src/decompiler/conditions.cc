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

	static int32_t strip_casts ( const builder& b, int32_t e ) {

		while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
			e = b.exprs [ static_cast < size_t > ( e ) ].a;

		return e;

	}

	void builder::fold_switches ( ) {

		int32_t total = code.count ( );

		if ( total != graph.blocks.count ( ) )
			return;

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

		bool changed = false;

		for ( int32_t s = 0; s < total; s++ ) {

			block_code& table = code [ static_cast < size_t > ( s ) ];

			if ( table.end != term::SWITCH || !table.reachable || table.selector < 0 )
				continue;

			bool empty = true;

			for ( int32_t k = table.first; k < table.first + table.count && k < statements.count ( ); k++ )
				empty = empty && statements [ static_cast < size_t > ( k ) ].kind == stmt::NOP;

			if ( !empty )
				continue;

			int32_t guard = -1;
			int32_t incoming = 0;

			for ( const analysis::edge& link : graph.edges ) {

				if ( link.to == s ) {

					incoming++;
					guard = link.from;

				}

			}

			if ( incoming != 1 || guard < 0 || guard == s )
				continue;

			block_code& head = code [ static_cast < size_t > ( guard ) ];

			if ( head.end != term::BRANCH || head.condition < 0 || ( head.taken != s && head.other != s ) || head.taken == head.other )
				continue;

			int32_t fallback = head.taken == s ? head.other : head.taken;
			const expr& test = exprs [ static_cast < size_t > ( head.condition ) ];
			int32_t tested = -1;

			if ( head.taken == fallback && ( test.kind == op::GT || test.kind == op::GE ) && ( test.bits & UNSIGNED ) )
				tested = strip_casts ( *this, test.a );
			else if ( head.other == fallback && ( test.kind == op::LE || test.kind == op::LT ) && ( test.bits & UNSIGNED ) )
				tested = strip_casts ( *this, test.a );

			int32_t picked = strip_casts ( *this, table.selector );

			if ( tested < 0 || picked < 0 || exprs [ static_cast < size_t > ( tested ) ].kind != op::VAR || exprs [ static_cast < size_t > ( picked ) ].kind != op::VAR || exprs [ static_cast < size_t > ( tested ) ].value != exprs [ static_cast < size_t > ( picked ) ].value )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( picked ) ].value;
			int32_t selector = table.selector;
			int64_t shift = 0;

			for ( int32_t k = head.first + head.count - 1; k >= head.first && k < statements.count ( ); k-- ) {

				statement& line = statements [ static_cast < size_t > ( k ) ];

				if ( line.kind == stmt::NOP )
					continue;

				if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != v || line.right < 0 )
					break;

				const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

				if ( source.kind == op::ADD && source.b >= 0 && exprs [ static_cast < size_t > ( source.b ) ].kind == op::CONST && v < uses.count ( ) && uses [ static_cast < size_t > ( v ) ] == 2 ) {

					shift = -exprs [ static_cast < size_t > ( source.b ) ].value;
					selector = source.a;
					line.kind = stmt::NOP;

				} else if ( source.kind == op::SUB && source.b >= 0 && exprs [ static_cast < size_t > ( source.b ) ].kind == op::CONST && v < uses.count ( ) && uses [ static_cast < size_t > ( v ) ] == 2 ) {

					shift = exprs [ static_cast < size_t > ( source.b ) ].value;
					selector = source.a;
					line.kind = stmt::NOP;

				}

				break;

			}

			base::list < analysis::edge > cases;

			for ( const analysis::edge& link : graph.edges ) {

				if ( link.from != s || link.kind != analysis::edge_kind::CASE || link.to == fallback )
					continue;

				analysis::edge moved = link;

				moved.from = guard;
				moved.value = static_cast < int32_t > ( link.value + shift );
				cases.push ( moved );

			}

			if ( cases.empty ( ) )
				continue;

			for ( int32_t i = 1; i < cases.count ( ); i++ ) {

				for ( int32_t j = i; j > 0 && cases [ static_cast < size_t > ( j - 1 ) ].value > cases [ static_cast < size_t > ( j ) ].value; j-- ) {

					analysis::edge swap = cases [ static_cast < size_t > ( j ) ];

					cases [ static_cast < size_t > ( j ) ] = cases [ static_cast < size_t > ( j - 1 ) ];
					cases [ static_cast < size_t > ( j - 1 ) ] = swap;

				}

			}

			int32_t kept = 0;

			for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

				const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];

				if ( link.from == s || link.from == guard )
					continue;

				graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

			}

			while ( graph.edges.count ( ) > kept )
				graph.edges.pop ( );

			for ( const analysis::edge& link : cases )
				graph.edges.push ( link );

			analysis::edge otherwise;

			otherwise.from = guard;
			otherwise.to = fallback;
			otherwise.kind = analysis::edge_kind::JUMP;
			graph.edges.push ( otherwise );

			head.end = term::SWITCH;
			head.selector = selector;
			head.condition = -1;
			head.taken = -1;
			head.other = -1;
			head.last = table.last;

			table.reachable = false;
			table.end = term::STOP;
			table.selector = -1;
			changed = true;

		}

		if ( changed )
			reindex_edges ( );

	}

	void builder::chain_switches ( ) {

		int32_t total = code.count ( );

		if ( total != graph.blocks.count ( ) )
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

		auto tested = [ & ] ( int32_t block, int64_t& constant ) -> int32_t {

			const block_code& item = code [ static_cast < size_t > ( block ) ];

			if ( item.end != term::BRANCH || item.condition < 0 || !item.reachable )
				return -1;

			const expr& test = exprs [ static_cast < size_t > ( item.condition ) ];
			bool usable = test.kind == op::EQ || test.kind == op::NE || test.kind == op::LT || test.kind == op::LE || test.kind == op::GT || test.kind == op::GE;

			if ( !usable || test.a < 0 || test.b < 0 || exprs [ static_cast < size_t > ( test.b ) ].kind != op::CONST || ( test.bits & FLOATING ) )
				return -1;

			int32_t subject = strip_casts ( *this, test.a );

			if ( subject < 0 || exprs [ static_cast < size_t > ( subject ) ].kind != op::VAR )
				return -1;

			constant = exprs [ static_cast < size_t > ( test.b ) ].value;

			return static_cast < int32_t > ( exprs [ static_cast < size_t > ( subject ) ].value );

		};

		bool changed = false;

		count_preds ( );

		for ( int32_t root = 0; root < total; root++ ) {

			int64_t first_value = 0;
			int32_t subject = tested ( root, first_value );

			if ( subject < 0 )
				continue;

			base::list < int32_t > nodes;
			base::list < int32_t > work;
			base::list < analysis::edge > cases;
			int32_t fallback = -1;
			bool broken = false;

			work.push ( root );

			while ( work.count ( ) && !broken ) {

				int32_t at = work [ static_cast < size_t > ( work.count ( ) - 1 ) ];

				work.pop ( );

				int64_t value = 0;
				bool inside = at == root || ( at != root && tested ( at, value ) == subject && empty ( at ) && preds [ static_cast < size_t > ( at ) ] == 1 );

				if ( at == root )
					tested ( at, value );

				bool listed = false;

				for ( int32_t n : nodes )
					listed = listed || n == at;

				if ( listed ) {

					broken = true;

					break;

				}

				if ( !inside ) {

					if ( fallback >= 0 && fallback != at )
						broken = true;

					fallback = at;

					continue;

				}

				nodes.push ( at );

				const block_code& item = code [ static_cast < size_t > ( at ) ];
				const expr& test = exprs [ static_cast < size_t > ( item.condition ) ];

				if ( test.kind == op::EQ || test.kind == op::NE ) {

					int32_t hit = test.kind == op::EQ ? item.taken : item.other;
					int32_t miss = test.kind == op::EQ ? item.other : item.taken;
					analysis::edge link;

					link.from = root;
					link.to = hit;
					link.kind = analysis::edge_kind::CASE;
					link.value = static_cast < int32_t > ( value );

					if ( value != static_cast < int64_t > ( link.value ) )
						broken = true;

					cases.push ( link );
					work.push ( miss );

				} else {

					work.push ( item.taken );
					work.push ( item.other );

				}

			}

			if ( broken || fallback < 0 || cases.count ( ) < 4 )
				continue;

			bool clash = false;

			for ( int32_t i = 0; i < cases.count ( ); i++ ) {

				if ( cases [ static_cast < size_t > ( i ) ].to == fallback )
					clash = true;

				for ( int32_t n : nodes )
					clash = clash || ( n != root && cases [ static_cast < size_t > ( i ) ].to == n );

				for ( int32_t j = 0; j < i; j++ )
					clash = clash || cases [ static_cast < size_t > ( j ) ].value == cases [ static_cast < size_t > ( i ) ].value;

			}

			if ( clash )
				continue;

			for ( int32_t i = 1; i < cases.count ( ); i++ ) {

				for ( int32_t j = i; j > 0 && cases [ static_cast < size_t > ( j - 1 ) ].value > cases [ static_cast < size_t > ( j ) ].value; j-- ) {

					analysis::edge swap = cases [ static_cast < size_t > ( j ) ];

					cases [ static_cast < size_t > ( j ) ] = cases [ static_cast < size_t > ( j - 1 ) ];
					cases [ static_cast < size_t > ( j - 1 ) ] = swap;

				}

			}

			int32_t kept = 0;

			for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

				const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];
				bool drop = false;

				for ( int32_t n : nodes )
					drop = drop || link.from == n;

				if ( !drop )
					graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

			}

			while ( graph.edges.count ( ) > kept )
				graph.edges.pop ( );

			for ( const analysis::edge& link : cases )
				graph.edges.push ( link );

			analysis::edge otherwise;

			otherwise.from = root;
			otherwise.to = fallback;
			otherwise.kind = analysis::edge_kind::JUMP;
			graph.edges.push ( otherwise );

			block_code& head = code [ static_cast < size_t > ( root ) ];

			head.selector = exprs [ static_cast < size_t > ( head.condition ) ].a;
			head.end = term::SWITCH;
			head.condition = -1;
			head.taken = -1;
			head.other = -1;

			for ( int32_t n : nodes ) {

				if ( n == root )
					continue;

				block_code& dead = code [ static_cast < size_t > ( n ) ];

				dead.reachable = false;
				dead.end = term::STOP;
				dead.condition = -1;

			}

			changed = true;
			count_preds ( );

		}

		if ( changed )
			reindex_edges ( );

	}

	bool builder::api_result_type ( ) {

		if ( has_prototype || returns_float || result_width || !names )
			return false;

		base::text chosen;
		uint8_t chosen_size = 0;
		bool any = false;

		auto call_type = [ & ] ( int32_t e ) -> bool {

			const expr& call = exprs [ static_cast < size_t > ( e ) ];
			wchar_t callee [ 200 ];

			if ( call.kind != op::CALL || !call.value || !names->name ( static_cast < uint64_t > ( call.value ), true, callee, 200 ) )
				return false;

			const wchar_t* api = api_prototype ( callee );
			c_prototype shape;
			base::writer problem;

			if ( !api || !parse_prototype ( api, long_mode, shape, problem ) || shape.result_type.empty || shape.result_type.size == 0 || ( shape.result_type.bits & FLOATING ) )
				return false;

			if ( !any ) {

				chosen.set ( shape.result );
				chosen_size = shape.result_type.size;
				any = true;

				return true;

			}

			return base::same_text ( chosen.get ( ), shape.result );

		};

		for ( const block_code& item : code ) {

			if ( !item.reachable || ( item.end != term::RETURN && item.end != term::TAIL ) || item.returned < 0 )
				continue;

			int32_t e = strip_casts ( *this, item.returned );
			const expr& back = exprs [ static_cast < size_t > ( e ) ];

			if ( back.kind == op::CONST )
				continue;

			if ( back.kind == op::CALL ) {

				if ( !call_type ( e ) )
					return false;

				continue;

			}

			if ( back.kind != op::VAR )
				return false;

			bool assigned = false;

			for ( const statement& line : statements ) {

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != back.value )
					continue;

				int32_t source = strip_casts ( *this, line.right );

				if ( exprs [ static_cast < size_t > ( source ) ].kind == op::CONST )
					continue;

				if ( !call_type ( source ) )
					return false;

				assigned = true;

			}

			if ( !assigned )
				return false;

		}

		if ( !any )
			return false;

		result_text = chosen;

		if ( chosen_size <= 4 ) {

			return_size = chosen_size;

			return true;

		}

		return false;

	}

	static bool assign_in_tests = true;

	static bool bare_var ( const builder& b, int32_t e, int64_t v ) {

		return e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( e ) ].value == v && !( b.exprs [ static_cast < size_t > ( e ) ].bits & FLOATING ) && !( b.vars [ static_cast < size_t > ( v ) ].bits & FLOATING );

	}

	static int32_t explicit_test ( builder& b, int32_t e, int64_t v ) {

		if ( e < 0 )
			return e;

		expr node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::LAND || node.kind == op::LOR ) {

			int32_t left = explicit_test ( b, node.a, v );
			int32_t right = explicit_test ( b, node.b, v );

			if ( left == node.a && right == node.b )
				return e;

			return b.make ( node.kind, node.size, left, right );

		}

		if ( node.kind == op::LNOT && bare_var ( b, node.a, v ) ) {

			uint8_t size = b.exprs [ static_cast < size_t > ( node.a ) ].size;

			return b.binary ( op::EQ, node.a, b.constant ( 0, size ), 1 );

		}

		if ( bare_var ( b, e, v ) )
			return b.binary ( op::NE, e, b.constant ( 0, node.size ), 1 );

		return e;

	}

	static int32_t comma_test ( builder& b, int32_t e, int64_t v, int32_t target, int32_t source ) {

		if ( e < 0 )
			return e;

		expr node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::LAND || node.kind == op::LOR ) {

			int32_t left = comma_test ( b, node.a, v, target, source );
			int32_t right = comma_test ( b, node.b, v, target, source );

			if ( left == node.a && right == node.b )
				return e;

			return b.make ( node.kind, node.size, left, right );

		}

		if ( !b.references ( e, static_cast < int32_t > ( v ), -1 ) )
			return e;

		int32_t made = b.make ( op::SET, 1, target, source, 1 );

		b.exprs [ static_cast < size_t > ( made ) ].c = e;

		return made;

	}

	static bool tests_directly ( const builder& b, int32_t e, int64_t v ) {

		if ( e < 0 )
			return false;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		switch ( node.kind ) {

			case op::LAND:
			case op::LOR:
				return tests_directly ( b, node.a, v ) || tests_directly ( b, node.b, v );

			case op::LNOT:
				return bare_var ( b, node.a, v ) || tests_directly ( b, node.a, v );

			case op::EQ:
			case op::NE:
			case op::LT:
			case op::LE:
			case op::GT:
			case op::GE:
				return bare_var ( b, node.a, v ) || bare_var ( b, node.b, v );

			default:
				return bare_var ( b, e, v );

		}

	}

	void builder::merge_conditions ( ) {

		sink_into_tests ( );
		chain_switches ( );

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

					block_code next = code [ static_cast < size_t > ( inner ) ];

					if ( next.end != term::BRANCH || next.condition < 0 || next.taken == next.other || next.taken == inner || next.other == inner )
						continue;

					int32_t assigned = -1;

					if ( !empty ( inner ) ) {

						if ( !assign_in_tests )
							continue;

						for ( int32_t s = next.first; s < next.first + next.count && s < statements.count ( ); s++ ) {

							if ( statements [ static_cast < size_t > ( s ) ].kind == stmt::NOP )
								continue;

							assigned = assigned < 0 ? s : -2;

						}

						if ( assigned < 0 )
							continue;

						const statement& line = statements [ static_cast < size_t > ( assigned ) ];

						if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
							continue;

						int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;
						int32_t joined = side == 0 ? head.taken : head.other;
						int32_t rest = next.taken == joined ? next.other : ( next.other == joined ? next.taken : -1 );

						if ( rest < 0 || rest >= total || rest <= inner || joined <= inner )
							continue;

						if ( code [ static_cast < size_t > ( joined ) ].end == term::RETURN && empty ( joined ) )
							continue;

						const block_code& after = code [ static_cast < size_t > ( rest ) ];

						if ( after.end == term::FALL && ( after.taken == joined || after.other == joined ) )
							continue;

						if ( after.end == term::BRANCH && ( after.taken == joined || after.other == joined ) )
							continue;
						base::list < int32_t > seen;

						seen.fill ( vars.size ( ), 0 );
						count_vars ( *this, next.condition, seen, 0 );

						if ( v < 0 || v >= seen.count ( ) || seen [ static_cast < size_t > ( v ) ] != 1 )
							continue;

						if ( !tests_directly ( *this, next.condition, v ) )
							continue;

						if ( exprs [ static_cast < size_t > ( line.right ) ].kind == op::CALL )
							next.condition = comma_test ( *this, explicit_test ( *this, next.condition, v ), v, line.left, line.right );
						else {

							int32_t setter = make ( op::SET, exprs [ static_cast < size_t > ( line.left ) ].size, line.left, line.right );

							next.condition = substitute ( *this, explicit_test ( *this, next.condition, v ), v, setter, 0 );

						}

					}

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

					if ( assigned >= 0 )
						statements [ static_cast < size_t > ( assigned ) ].kind = stmt::NOP;

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
