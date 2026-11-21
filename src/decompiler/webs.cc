#include "decompiler/internal.hh"

namespace decompiler {

	struct web_state {

		builder& b;
		int32_t var = 0;
		base::list < int32_t > defs;
		base::list < int32_t > parent;
		base::list < int32_t > use_expr;
		base::list < int32_t > use_def;
		base::list < int32_t > def_of_statement;
		int32_t words = 0;

		explicit web_state ( builder& owner ) : b ( owner ) {

		}

		int32_t find ( int32_t x ) {

			while ( parent [ static_cast < size_t > ( x ) ] != x ) {

				parent [ static_cast < size_t > ( x ) ] = parent [ static_cast < size_t > ( parent [ static_cast < size_t > ( x ) ] ) ];
				x = parent [ static_cast < size_t > ( x ) ];

			}

			return x;

		}

		void join ( int32_t a, int32_t c ) {

			a = find ( a );
			c = find ( c );

			if ( a != c )
				parent [ static_cast < size_t > ( c > a ? c : a ) ] = c > a ? a : c;

		}

		void uses_in ( int32_t e, const uint64_t* reaching, base::list < int32_t >& seen ) {

			if ( e < 0 || e >= b.exprs.count ( ) )
				return;

			for ( int32_t item : seen ) {

				if ( item == e )
					return;

			}

			seen.push ( e );

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::VAR && node.value == var ) {

				int32_t first = -1;

				for ( int32_t d = 0; d < defs.count ( ) + 1; d++ ) {

					if ( !( reaching [ d / 64 ] & ( 1ull << ( d % 64 ) ) ) )
						continue;

					if ( first < 0 )
						first = d;
					else
						join ( first, d );

				}

				if ( first < 0 )
					first = 0;

				for ( int32_t i = 0; i < use_expr.count ( ); i++ ) {

					if ( use_expr [ static_cast < size_t > ( i ) ] == e ) {

						join ( use_def [ static_cast < size_t > ( i ) ], first );

						return;

					}

				}

				use_expr.push ( e );
				use_def.push ( first );

				return;

			}

			uses_in ( node.a, reaching, seen );
			uses_in ( node.b, reaching, seen );
			uses_in ( node.c, reaching, seen );

			if ( node.kind == op::CALL || node.kind == op::INTRINSIC ) {

				for ( int32_t i = 0; i < node.arg_count; i++ )
					uses_in ( b.args [ static_cast < size_t > ( node.first_arg + i ) ], reaching, seen );

			}

		}

		bool defines ( const statement& line ) const {

			return line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == var;

		}

		void run ( ) {

			defs.clear ( );
			def_of_statement.clear ( );
			def_of_statement.fill ( b.statements.size ( ), -1 );

			for ( int32_t s = 0; s < b.statements.count ( ); s++ ) {

				if ( defines ( b.statements [ static_cast < size_t > ( s ) ] ) ) {

					def_of_statement [ static_cast < size_t > ( s ) ] = defs.count ( ) + 1;
					defs.push ( s );

				}

			}

			bool parameter = b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER;

			if ( defs.count ( ) < ( parameter ? 1 : 2 ) || defs.count ( ) > 4000 )
				return;

			int32_t total = defs.count ( ) + 1;
			int32_t blocks = b.code.count ( );

			words = ( total + 63 ) / 64;
			parent.clear ( );

			for ( int32_t i = 0; i < total; i++ )
				parent.push ( i );

			base::list < uint64_t > in;
			base::list < uint64_t > out;
			base::list < int32_t > last_def;

			in.fill ( static_cast < size_t > ( blocks * words ), 0 );
			out.fill ( static_cast < size_t > ( blocks * words ), 0 );
			last_def.fill ( static_cast < size_t > ( blocks ), -1 );

			for ( int32_t k = 0; k < blocks; k++ ) {

				const block_code& block = b.code [ static_cast < size_t > ( k ) ];

				for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

					if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP && def_of_statement [ static_cast < size_t > ( s ) ] > 0 )
						last_def [ static_cast < size_t > ( k ) ] = def_of_statement [ static_cast < size_t > ( s ) ];

				}

			}

			if ( blocks > 0 )
				in [ 0 ] |= 1ull;

			for ( int32_t round = 0; round < blocks + 4; round++ ) {

				bool changed = false;

				for ( int32_t k = 0; k < blocks; k++ ) {

					uint64_t* block_in = in.data ( ) + k * words;
					uint64_t* block_out = out.data ( ) + k * words;

					for ( const analysis::edge& link : b.graph.edges ) {

						if ( link.to != k || link.from < 0 || link.from >= blocks )
							continue;

						const uint64_t* from_out = out.data ( ) + link.from * words;

						for ( int32_t w = 0; w < words; w++ ) {

							uint64_t merged = block_in [ w ] | from_out [ w ];

							if ( merged != block_in [ w ] ) {

								block_in [ w ] = merged;
								changed = true;

							}

						}

					}

					int32_t gen = last_def [ static_cast < size_t > ( k ) ];

					for ( int32_t w = 0; w < words; w++ ) {

						uint64_t next = gen >= 0 ? ( w == gen / 64 ? 1ull << ( gen % 64 ) : 0 ) : block_in [ w ];

						if ( next != block_out [ w ] ) {

							block_out [ w ] = next;
							changed = true;

						}

					}

				}

				if ( !changed )
					break;

			}

			use_expr.clear ( );
			use_def.clear ( );

			base::list < uint64_t > reaching;
			base::list < int32_t > seen;

			reaching.fill ( static_cast < size_t > ( words ), 0 );

			for ( int32_t k = 0; k < blocks; k++ ) {

				const block_code& block = b.code [ static_cast < size_t > ( k ) ];

				for ( int32_t w = 0; w < words; w++ )
					reaching [ static_cast < size_t > ( w ) ] = in [ static_cast < size_t > ( k * words + w ) ];

				for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

					const statement& line = b.statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
						continue;

					seen.clear ( );
					uses_in ( line.right, reaching.data ( ), seen );

					if ( !defines ( line ) )
						uses_in ( line.left, reaching.data ( ), seen );
					else {

						int32_t made = def_of_statement [ static_cast < size_t > ( s ) ];

						for ( int32_t w = 0; w < words; w++ )
							reaching [ static_cast < size_t > ( w ) ] = 0;

						reaching [ static_cast < size_t > ( made / 64 ) ] = 1ull << ( made % 64 );

					}

				}

				seen.clear ( );
				uses_in ( block.condition, reaching.data ( ), seen );
				uses_in ( block.selector, reaching.data ( ), seen );
				uses_in ( block.returned, reaching.data ( ), seen );
				uses_in ( block.flag_a, reaching.data ( ), seen );
				uses_in ( block.flag_b, reaching.data ( ), seen );

			}

			base::list < int32_t > class_var;
			bool entry_used = parameter;

			class_var.fill ( static_cast < size_t > ( total ), -1 );

			for ( int32_t d : use_def )
				entry_used = entry_used || find ( d ) == find ( 0 );

			bool kept = false;

			if ( entry_used ) {

				class_var [ static_cast < size_t > ( find ( 0 ) ) ] = var;
				kept = true;

			}

			for ( int32_t d = 1; d < total; d++ ) {

				int32_t root = find ( d );

				if ( class_var [ static_cast < size_t > ( root ) ] >= 0 )
					continue;

				if ( !kept ) {

					class_var [ static_cast < size_t > ( root ) ] = var;
					kept = true;

					continue;

				}

				variable copy = b.vars [ static_cast < size_t > ( var ) ];

				copy.uses = 0;

				if ( copy.kind == var_kind::PARAMETER ) {

					copy.kind = var_kind::REGISTER;
					copy.position = -1;

				}
				class_var [ static_cast < size_t > ( root ) ] = b.vars.count ( );
				b.vars.push ( copy );

			}

			for ( int32_t d = 1; d < total; d++ ) {

				int32_t target = class_var [ static_cast < size_t > ( find ( d ) ) ];
				const statement& line = b.statements [ static_cast < size_t > ( defs [ static_cast < size_t > ( d - 1 ) ] ) ];

				b.exprs [ static_cast < size_t > ( line.left ) ].value = target;

			}

			for ( int32_t i = 0; i < use_expr.count ( ); i++ ) {

				int32_t target = class_var [ static_cast < size_t > ( find ( use_def [ static_cast < size_t > ( i ) ] ) ) ];

				if ( target < 0 )
					target = var;

				b.exprs [ static_cast < size_t > ( use_expr [ static_cast < size_t > ( i ) ] ) ].value = target;

				if ( target != var ) {

					b.vars [ static_cast < size_t > ( target ) ].uses++;

					if ( b.vars [ static_cast < size_t > ( var ) ].uses > 0 )
						b.vars [ static_cast < size_t > ( var ) ].uses--;

				}

			}

		}

	};

	void builder::split_webs ( ) {

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::ASM )
				return;

		}

		int32_t original = vars.count ( );
		web_state state ( *this );

		for ( int32_t v = 0; v < original; v++ ) {

			const variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( ( item.kind != var_kind::REGISTER && item.kind != var_kind::PARAMETER ) || item.family < 0 || item.cookie || item.saved )
				continue;

			state.var = v;
			state.run ( );

		}

		coalesce_copies ( );

	}

	void builder::coalesce_copies ( ) {

		base::list < int32_t > writes;

		writes.fill ( vars.size ( ), 0 );

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

				int64_t target = exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( target >= 0 && target < writes.count ( ) )
					writes [ static_cast < size_t > ( target ) ]++;

			}

		}

		for ( statement& line : statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
				continue;

			const expr& left = exprs [ static_cast < size_t > ( line.left ) ];
			const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

			if ( left.kind != op::VAR || right.kind != op::VAR || left.value == right.value )
				continue;

			int32_t copy = static_cast < int32_t > ( left.value );
			int32_t source = static_cast < int32_t > ( right.value );

			if ( copy < 0 || source < 0 || copy >= vars.count ( ) || source >= vars.count ( ) )
				continue;

			const variable& into = vars [ static_cast < size_t > ( copy ) ];
			const variable& from = vars [ static_cast < size_t > ( source ) ];

			if ( into.kind != var_kind::REGISTER || into.cookie || into.saved || writes [ static_cast < size_t > ( copy ) ] != 1 )
				continue;

			if ( from.kind != var_kind::PARAMETER || writes [ static_cast < size_t > ( source ) ] != 0 || into.size != from.size || ( into.bits & FLOATING ) != ( from.bits & FLOATING ) )
				continue;

			line.kind = stmt::NOP;

			for ( expr& node : exprs ) {

				if ( node.kind == op::VAR && node.value == copy )
					node.value = source;

			}

			vars [ static_cast < size_t > ( source ) ].uses += vars [ static_cast < size_t > ( copy ) ].uses;
			vars [ static_cast < size_t > ( copy ) ].uses = 0;
			writes [ static_cast < size_t > ( copy ) ] = 0;

		}

	}

}
