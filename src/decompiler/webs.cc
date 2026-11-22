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

	static int32_t own_tree ( builder& b, int32_t e, base::list < uint8_t >& visited, int32_t limit, int32_t depth ) {

		if ( e < 0 || e >= visited.count ( ) || depth > 64 )
			return e;

		if ( visited [ static_cast < size_t > ( e ) ] ) {

			if ( b.exprs.count ( ) >= limit )
				return e;

			expr copy = b.exprs [ static_cast < size_t > ( e ) ];

			b.exprs.push ( copy );
			e = b.exprs.count ( ) - 1;
			visited.push ( 1 );

		} else
			visited [ static_cast < size_t > ( e ) ] = 1;

		expr node = b.exprs [ static_cast < size_t > ( e ) ];
		int32_t a = own_tree ( b, node.a, visited, limit, depth + 1 );
		int32_t c = own_tree ( b, node.b, visited, limit, depth + 1 );
		int32_t third = own_tree ( b, node.c, visited, limit, depth + 1 );

		if ( node.arg_count ) {

			base::list < int32_t > list;

			for ( int32_t i = 0; i < node.arg_count; i++ )
				list.push ( own_tree ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], visited, limit, depth + 1 ) );

			node.first_arg = b.args.count ( );

			for ( int32_t item : list )
				b.args.push ( item );

		}

		node.a = a;
		node.b = c;
		node.c = third;
		b.exprs [ static_cast < size_t > ( e ) ] = node;

		return e;

	}

	void builder::split_webs ( ) {

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::ASM )
				return;

		}

		base::list < uint8_t > visited;
		int32_t limit = exprs.count ( ) * 4 + 1024;

		visited.fill ( exprs.size ( ), 0 );

		for ( statement& line : statements ) {

			if ( line.kind == stmt::NOP )
				continue;

			line.left = own_tree ( *this, line.left, visited, limit, 0 );
			line.right = own_tree ( *this, line.right, visited, limit, 0 );

		}

		for ( block_code& block : code ) {

			block.condition = own_tree ( *this, block.condition, visited, limit, 0 );
			block.selector = own_tree ( *this, block.selector, visited, limit, 0 );
			block.returned = own_tree ( *this, block.returned, visited, limit, 0 );

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
		coalesce_selects ( );
		substitute_known ( );
		narrow_webs ( );
		narrow_floats ( );

	}

	void builder::narrow_floats ( ) {

		int32_t total = vars.count ( );
		base::list < uint8_t > blocked;
		base::list < uint8_t > seen;

		blocked.fill ( static_cast < size_t > ( total ), 0 );
		seen.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( item.family < 16 || item.family >= 32 || !( item.bits & FLOATING ) || item.size < 8 || ( item.kind != var_kind::REGISTER && item.kind != var_kind::PARAMETER ) )
				blocked [ static_cast < size_t > ( v ) ] = 1;

		}

		auto var_of = [ & ] ( int32_t e ) -> int32_t {

			if ( e < 0 || exprs [ static_cast < size_t > ( e ) ].kind != op::VAR )
				return -1;

			int64_t v = exprs [ static_cast < size_t > ( e ) ].value;

			return v >= 0 && v < total ? static_cast < int32_t > ( v ) : -1;

		};

		auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::VAR ) {

				int32_t v = var_of ( e );

				if ( v >= 0 ) {

					seen [ static_cast < size_t > ( v ) ] = 1;

					if ( node.size != 4 )
						blocked [ static_cast < size_t > ( v ) ] = 1;

				}

				return;

			}

			if ( node.kind == op::CAST && node.size == 4 && ( node.bits & FLOATING ) && var_of ( node.a ) >= 0 ) {

				seen [ static_cast < size_t > ( var_of ( node.a ) ) ] = 1;

				return;

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

			int32_t target = line.kind == stmt::ASSIGN ? var_of ( line.left ) : -1;

			if ( target >= 0 ) {

				const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

				if ( source.size != 4 || !( source.bits & FLOATING ) )
					blocked [ static_cast < size_t > ( target ) ] = 1;

			} else
				walk ( walk, line.left, 0 );

			walk ( walk, line.right, 0 );

		}

		for ( const block_code& block : code ) {

			if ( !block.reachable )
				continue;

			walk ( walk, block.condition, 0 );
			walk ( walk, block.selector, 0 );
			walk ( walk, block.returned, 0 );

		}

		base::list < uint8_t > chosen;

		chosen.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			if ( blocked [ static_cast < size_t > ( v ) ] || !seen [ static_cast < size_t > ( v ) ] )
				continue;

			chosen [ static_cast < size_t > ( v ) ] = 1;
			vars [ static_cast < size_t > ( v ) ].size = 4;

		}

		for ( expr& node : exprs ) {

			if ( node.kind == op::VAR && node.value >= 0 && node.value < total && chosen [ static_cast < size_t > ( node.value ) ] )
				node.size = 4;

		}

		for ( expr& node : exprs ) {

			if ( node.kind != op::CAST || node.size != 4 || !( node.bits & FLOATING ) || node.a < 0 )
				continue;

			const expr inner = exprs [ static_cast < size_t > ( node.a ) ];

			if ( inner.kind == op::VAR && inner.value >= 0 && inner.value < total && chosen [ static_cast < size_t > ( inner.value ) ] )
				node = inner;

		}

	}

	void builder::narrow_webs ( ) {

		int32_t total = vars.count ( );
		base::list < uint8_t > widest;
		base::list < uint8_t > blocked;
		base::list < uint8_t > defined;

		widest.fill ( static_cast < size_t > ( total ), 0 );
		blocked.fill ( static_cast < size_t > ( total ), 0 );
		defined.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = vars [ static_cast < size_t > ( v ) ];
			bool candidate = ( item.kind == var_kind::REGISTER || item.kind == var_kind::PARAMETER ) && item.family >= 0 && item.family < 16 && !item.cookie && !item.saved && !( item.bits & ( FLOATING | POINTER ) ) && item.size == 8;

			if ( !candidate )
				blocked [ static_cast < size_t > ( v ) ] = 1;

		}

		auto note_use = [ & ] ( int32_t child, int32_t width ) {

			if ( child < 0 || child >= exprs.count ( ) )
				return;

			const expr& node = exprs [ static_cast < size_t > ( child ) ];

			if ( node.kind != op::VAR || node.value < 0 || node.value >= total )
				return;

			size_t v = static_cast < size_t > ( node.value );

			if ( width <= 0 && node.size < vars [ v ].size && node.size <= 4 )
				width = node.size;

			if ( width <= 0 || width > 4 )
				blocked [ v ] = 1;
			else
				widest [ v ] = static_cast < uint8_t > ( base::max ( static_cast < int32_t > ( widest [ v ] ), width ) );

		};

		base::list < uint8_t > visited;

		visited.fill ( static_cast < size_t > ( exprs.count ( ) ), 0 );

		auto walk = [ & ] ( auto& self, int32_t e, int32_t depth, int32_t limit ) -> void {

			if ( e < 0 || e >= exprs.count ( ) || depth > 96 || visited [ static_cast < size_t > ( e ) ] )
				return;

			visited [ static_cast < size_t > ( e ) ] = 1;

			const expr& node = exprs [ static_cast < size_t > ( e ) ];
			bool low_bits = !( node.bits & FLOATING ) && ( node.kind == op::ADD || node.kind == op::SUB || node.kind == op::MUL || node.kind == op::AND || node.kind == op::OR || node.kind == op::XOR || node.kind == op::NOT || node.kind == op::NEG || node.kind == op::SHL );
			int32_t inner = node.kind == op::CAST ? ( node.size <= 4 && !( node.bits & FLOATING ) ? node.size : 0 ) : ( low_bits ? limit : 0 );

			if ( node.kind == op::VAR )
				return;

			if ( node.kind == op::CAST )
				note_use ( node.a, node.size );
			else {

				note_use ( node.a, inner );
				note_use ( node.b, inner );
				note_use ( node.c, inner );

				c_prototype shape;
				bool shaped = false;

				if ( node.kind == op::CALL && node.value && names ) {

					wchar_t callee [ 200 ];
					const wchar_t* api = names->prototype ( static_cast < uint64_t > ( node.value ) );

					if ( !api && names->name ( static_cast < uint64_t > ( node.value ), true, callee, 200 ) )
						api = api_prototype ( callee );

					base::writer problem;

					shaped = api && parse_prototype ( api, long_mode, shape, problem );

				}

				for ( int32_t i = 0; i < node.arg_count; i++ ) {

					int32_t width = 0;

					if ( shaped && i < shape.count && !shape.params [ i ].pointer && !( shape.params [ i ].bits & FLOATING ) && shape.params [ i ].size > 0 && shape.params [ i ].size <= 4 )
						width = shape.params [ i ].size;

					note_use ( args [ static_cast < size_t > ( node.first_arg + i ) ], width );

				}

			}

			self ( self, node.a, depth + 1, inner );
			self ( self, node.b, depth + 1, inner );
			self ( self, node.c, depth + 1, inner );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1, 0 );

		};

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			walk ( walk, line.left, 0, 0 );
			walk ( walk, line.right, 0, 0 );

		}

		for ( const block_code& block : code ) {

			walk ( walk, block.condition, 0, 0 );
			walk ( walk, block.selector, 0, 0 );
			walk ( walk, block.returned, 0, 0 );

		}

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::NOP )
				continue;

			bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

			if ( defines ) {

				int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( v >= 0 && v < total && line.right >= 0 ) {

					const expr& source = exprs [ static_cast < size_t > ( line.right ) ];
					uint8_t size = source.kind == op::CALL || source.kind == op::CONST ? 1 : source.size;

					if ( source.kind == op::CAST && source.a >= 0 && exprs [ static_cast < size_t > ( source.a ) ].size < source.size && !( source.bits & ( FLOATING | POINTER ) ) && !( exprs [ static_cast < size_t > ( source.a ) ].bits & FLOATING ) )
						size = exprs [ static_cast < size_t > ( source.a ) ].size;

					defined [ static_cast < size_t > ( v ) ] = 1;

					if ( source.kind == op::CONST && ( source.value > 0x7FFFFFFFll || source.value < -0x80000000ll ) )
						blocked [ static_cast < size_t > ( v ) ] = 1;

					if ( size > 4 || ( source.bits & ( POINTER | FLOATING ) ) )
						blocked [ static_cast < size_t > ( v ) ] = 1;
					else
						widest [ static_cast < size_t > ( v ) ] = base::max ( widest [ static_cast < size_t > ( v ) ], size );

				}

			} else
				note_use ( line.left, 0 );

			note_use ( line.right, 0 );

		}

		for ( const block_code& block : code ) {

			note_use ( block.condition, 0 );
			note_use ( block.selector, 0 );
			if ( block.reachable )
				note_use ( block.returned, returns_value && !returns_float && return_size && return_size <= 4 ? return_size : 0 );

		}

		base::list < uint8_t > chosen;

		chosen.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			uint8_t width = widest [ static_cast < size_t > ( v ) ];

			if ( blocked [ static_cast < size_t > ( v ) ] || !width )
				continue;

			if ( vars [ static_cast < size_t > ( v ) ].kind == var_kind::REGISTER && !defined [ static_cast < size_t > ( v ) ] )
				continue;

			if ( width == 3 )
				width = 4;

			chosen [ static_cast < size_t > ( v ) ] = width;
			vars [ static_cast < size_t > ( v ) ].size = width;

		}

		for ( expr& node : exprs ) {

			if ( node.kind == op::VAR && node.value >= 0 && node.value < total && chosen [ static_cast < size_t > ( node.value ) ] )
				node.size = chosen [ static_cast < size_t > ( node.value ) ];

		}

		for ( expr& node : exprs ) {

			if ( node.kind != op::CAST || node.a < 0 )
				continue;

			const expr inner = exprs [ static_cast < size_t > ( node.a ) ];

			if ( inner.kind != op::VAR || inner.value < 0 || inner.value >= total || !chosen [ static_cast < size_t > ( inner.value ) ] || inner.size != node.size )
				continue;

			uint8_t bits = node.bits;

			node = inner;

			if ( bits & ( SIGNED | UNSIGNED ) )
				node.bits = static_cast < uint8_t > ( ( node.bits & ~( SIGNED | UNSIGNED ) ) | ( bits & ( SIGNED | UNSIGNED ) ) );

		}

		for ( statement& line : statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
				continue;

			const expr& left = exprs [ static_cast < size_t > ( line.left ) ];

			if ( left.kind != op::VAR || left.value < 0 || left.value >= total || !chosen [ static_cast < size_t > ( left.value ) ] )
				continue;

			const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

			if ( source.kind == op::CAST && source.a >= 0 && exprs [ static_cast < size_t > ( source.a ) ].size == left.size && !( source.bits & ( FLOATING | POINTER ) ) )
				line.right = source.a;

		}

	}

	void builder::coalesce_selects ( ) {

		base::list < int32_t > reads;

		reads.fill ( vars.size ( ), 0 );

		auto count = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = exprs [ static_cast < size_t > ( e ) ];

			if ( ( node.kind == op::VAR || ( node.kind == op::ADDRESS && node.a < 0 ) ) && node.value >= 0 && node.value < reads.count ( ) )
				reads [ static_cast < size_t > ( node.value ) ] += node.kind == op::ADDRESS ? 100 : 1;

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const statement& line : statements ) {

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			if ( !( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
				count ( count, line.left, 0 );

			count ( count, line.right, 0 );

		}

		for ( const block_code& block : code ) {

			count ( count, block.condition, 0 );
			count ( count, block.selector, 0 );
			count ( count, block.returned, 0 );

		}

		for ( const statement& line : statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			const expr& select = exprs [ static_cast < size_t > ( line.right ) ];

			if ( select.kind != op::TERNARY || select.c < 0 )
				continue;

			int32_t arm = select.c;

			while ( arm >= 0 && exprs [ static_cast < size_t > ( arm ) ].kind == op::CAST )
				arm = exprs [ static_cast < size_t > ( arm ) ].a;

			if ( arm < 0 || exprs [ static_cast < size_t > ( arm ) ].kind != op::VAR )
				continue;

			int64_t into = exprs [ static_cast < size_t > ( line.left ) ].value;
			int64_t from = exprs [ static_cast < size_t > ( arm ) ].value;

			if ( into == from || into < 0 || from < 0 || into >= vars.count ( ) || from >= vars.count ( ) || reads [ static_cast < size_t > ( from ) ] != 1 )
				continue;

			const variable& a = vars [ static_cast < size_t > ( into ) ];
			const variable& b = vars [ static_cast < size_t > ( from ) ];

			if ( a.kind != var_kind::REGISTER || b.kind != var_kind::REGISTER || a.family != b.family || a.family < 0 || a.size != b.size || a.cookie || b.cookie )
				continue;

			for ( expr& node : exprs ) {

				if ( node.kind == op::VAR && node.value == from )
					node.value = into;

			}

			reads [ static_cast < size_t > ( from ) ] = 0;

		}

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

			if ( into.family == 0 ) {

				bool returned = false;

				for ( const block_code& block : code ) {

					if ( block.end == term::RETURN && block.returned >= 0 && exprs [ static_cast < size_t > ( block.returned ) ].kind == op::VAR && exprs [ static_cast < size_t > ( block.returned ) ].value == copy )
						returned = true;

				}

				if ( returned )
					continue;

			}

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
