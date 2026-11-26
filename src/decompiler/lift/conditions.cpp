#include "decompiler/internal.h"

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

		if ( copy.kind == op::MUL && a >= 0 && c >= 0 && b.exprs [ static_cast < size_t > ( c ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( a ) ].kind == op::MUL ) {

			const expr& inner = b.exprs [ static_cast < size_t > ( a ) ];

			if ( inner.b >= 0 && b.exprs [ static_cast < size_t > ( inner.b ) ].kind == op::CONST && inner.size == copy.size )
				return b.binary ( op::MUL, a, c, copy.size, copy.bits );

		}

		if ( copy.arg_count ) {

			copy.first_arg = b.args.count ( );

			for ( int32_t item : list )
				b.args.push ( item );

		}

		b.exprs.push ( copy );

		return b.exprs.count ( ) - 1;

	}

	static bool ghost_folds = false;
	static bool ghost_exprs = false;
	static bool calls_keep_exprs = true;
	static bool ghost_call_copies = false;
	static bool ghost_forwards = false;
	static bool stack_param_copies = true;
	static bool ghost_sinks = false;
	static bool copy_fixed = true;
	static bool copy_into = true;
	static bool calls_forward = true;
	static bool swap_copies = true;
	static bool copy_local = true;
	static bool expr_fixed = true;
	static bool keep_destination = true;
	static bool shared_results = true;
	static bool intact_exprs = true;
	static bool sink_past_moves = true;
	static bool calls_intact = true;
	static bool broken_keep = false;
	static bool origin_numbers = true;

	static bool has_call ( const builder& b, int32_t e, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return false;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::CALL || node.kind == op::SET )
			return true;

		if ( has_call ( b, node.a, depth + 1 ) || has_call ( b, node.b, depth + 1 ) || has_call ( b, node.c, depth + 1 ) )
			return true;

		for ( int32_t i = 0; i < node.arg_count; i++ ) {

			if ( has_call ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 ) )
				return true;

		}

		return false;

	}

	static bool register_only ( const builder& b, int32_t e, int32_t depth ) {

		if ( e < 0 )
			return true;

		if ( depth > 64 )
			return false;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		switch ( node.kind ) {

			case op::CONST:
				return true;

			case op::VAR: {

				if ( node.value < 0 || node.value >= b.vars.count ( ) )
					return false;

				var_kind kind = b.vars [ static_cast < size_t > ( node.value ) ].kind;

				return kind == var_kind::REGISTER || kind == var_kind::PARAMETER;

			}

			case op::LOAD:
			case op::CALL:
			case op::SET:
			case op::ADDRESS:
			case op::GLOBAL:
			case op::INTRINSIC:
				return false;

			default:
				break;

		}

		if ( node.arg_count )
			return false;

		return register_only ( b, node.a, depth + 1 ) && register_only ( b, node.b, depth + 1 ) && register_only ( b, node.c, depth + 1 );

	}

	static int32_t occurrences ( const builder& b, int32_t e, int64_t var, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return 0;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
		int32_t found = node.kind == op::VAR && node.value == var ? 1 : 0;

		found += occurrences ( b, node.a, var, depth + 1 ) + occurrences ( b, node.b, var, depth + 1 ) + occurrences ( b, node.c, var, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			found += occurrences ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], var, depth + 1 );

		return found;

	}

	static bool writes_between ( const builder& b, int32_t block, uint32_t mask, uint64_t from, uint64_t to ) {

		const block_code& item = b.code [ static_cast < size_t > ( block ) ];

		for ( int32_t i = item.writes_first; i < item.writes_first + item.writes_count && i < b.write_at.count ( ); i++ ) {

			uint64_t at = b.write_at [ static_cast < size_t > ( i ) ];

			if ( at > from && at < to && ( b.write_regs [ static_cast < size_t > ( i ) ] & mask ) )
				return true;

		}

		return false;

	}

	static bool holds_through ( const builder& b, uint32_t mask, int32_t from_block, uint64_t from, int32_t to_block, uint64_t to ) {

		if ( from_block == to_block && to > from )
			return !writes_between ( b, from_block, mask, from, to );

		if ( writes_between ( b, to_block, mask, 0, to ) )
			return false;

		int32_t total = b.code.count ( );
		base::list < uint8_t > seen;
		base::list < int32_t > work;

		seen.fill ( static_cast < size_t > ( total ), 0 );
		work.push ( to_block );

		while ( !work.empty ( ) ) {

			int32_t at = work [ work.size ( ) - 1 ];
			int32_t found = 0;

			work.pop ( );

			for ( const analysis::edge& link : b.graph.edges ) {

				if ( link.to != at )
					continue;

				found++;

				if ( link.from < 0 || link.from >= total )
					return false;

				if ( link.from == from_block ) {

					if ( writes_between ( b, from_block, mask, from, ~0ull ) )
						return false;

					continue;

				}

				if ( seen [ static_cast < size_t > ( link.from ) ] )
					continue;

				if ( writes_between ( b, link.from, mask, 0, ~0ull ) )
					return false;

				seen [ static_cast < size_t > ( link.from ) ] = 1;
				work.push ( link.from );

			}

			if ( !found || found != b.code [ static_cast < size_t > ( at ) ].preds )
				return false;

		}

		return true;

	}

	void builder::inline_temps ( ) {

		int32_t total = vars.count ( );

		for ( int32_t round = 0; round < 4; round++ ) {

			base::list < int32_t > defs;
			base::list < int32_t > uses;
			base::list < int32_t > single;
			bool changed = false;

			defs.fill ( static_cast < size_t > ( total ), 0 );
			uses.fill ( static_cast < size_t > ( total ), 0 );
			single.fill ( static_cast < size_t > ( total ), -1 );

			for ( int32_t s = 0; s < statements.count ( ); s++ ) {

				const statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
					continue;

				bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

				if ( defines ) {

					int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

					if ( v >= 0 && v < total ) {

						defs [ static_cast < size_t > ( v ) ]++;
						single [ static_cast < size_t > ( v ) ] = s;

					}

				} else
					count_vars ( *this, line.left, uses, 0 );

				count_vars ( *this, line.right, uses, 0 );

			}

			base::list < int32_t > returned;

			returned.fill ( static_cast < size_t > ( total ), 0 );

			for ( const block_code& block : code ) {

				count_vars ( *this, block.condition, uses, 0 );
				count_vars ( *this, block.selector, uses, 0 );
				count_vars ( *this, block.returned, uses, 0 );
				count_vars ( *this, block.returned, returned, 0 );

			}

			auto plain = [ & ] ( int64_t v ) {

				if ( v < 0 || v >= total )
					return false;

				const variable& item = vars [ static_cast < size_t > ( v ) ];

				return item.kind == var_kind::REGISTER && !item.flags && !item.cookie && !item.saved && uses [ static_cast < size_t > ( v ) ] < 100;

			};

			auto fixed = [ & ] ( int64_t v ) {

				if ( v < 0 || v >= total || uses [ static_cast < size_t > ( v ) ] >= 100 )
					return false;

				const variable& item = vars [ static_cast < size_t > ( v ) ];

				if ( item.kind == var_kind::PARAMETER )
					return defs [ static_cast < size_t > ( v ) ] == 0;

				return item.kind == var_kind::REGISTER && !item.flags && defs [ static_cast < size_t > ( v ) ] == 1;

			};

			base::list < int32_t > owner;
			base::list < int32_t > entering;

			owner.fill ( statements.size ( ), -1 );
			entering.fill ( code.size ( ), 0 );

			for ( int32_t k = 0; k < code.count ( ); k++ ) {

				const block_code& block = code [ static_cast < size_t > ( k ) ];

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ )
					owner [ static_cast < size_t > ( s ) ] = k;

				if ( !block.reachable )
					continue;

				if ( block.taken >= 0 && block.taken < code.count ( ) )
					entering [ static_cast < size_t > ( block.taken ) ]++;

				if ( block.end == term::BRANCH && block.other >= 0 && block.other < code.count ( ) && block.other != block.taken )
					entering [ static_cast < size_t > ( block.other ) ]++;

			}

			auto local_only = [ & ] ( int64_t v, int32_t at, int64_t source_var, int32_t wanted ) -> bool {

				if ( at < 0 || owner [ static_cast < size_t > ( at ) ] < 0 )
					return false;

				const block_code& home = code [ static_cast < size_t > ( owner [ static_cast < size_t > ( at ) ] ) ];
				base::list < int32_t > here;
				int32_t found = 0;
				bool barrier = false;

				here.fill ( static_cast < size_t > ( total ), 0 );

				auto seen = [ & ] ( int32_t e ) {

					here [ static_cast < size_t > ( v ) ] = 0;
					count_vars ( *this, e, here, 0 );

					return here [ static_cast < size_t > ( v ) ];

				};

				auto blocks = [ & ] ( const statement& line ) {

					return line.kind == stmt::STORE || line.kind == stmt::EXPR || has_call ( *this, line.right, 0 ) || ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == source_var );

				};

				for ( int32_t s = at + 1; s < home.first + home.count && s < statements.count ( ); s++ ) {

					const statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::NOP )
						continue;

					if ( line.kind == stmt::ASM )
						return false;

					bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;
					int32_t n = seen ( line.right ) + ( defines ? 0 : seen ( line.left ) );

					if ( defines && exprs [ static_cast < size_t > ( line.left ) ].value == v )
						return false;

					if ( n ) {

						if ( barrier )
							return false;

						found += n;

					}

					if ( blocks ( line ) )
						barrier = true;

				}

				int32_t tail = seen ( home.condition ) + seen ( home.selector ) + seen ( home.returned );

				if ( tail ) {

					if ( barrier )
						return false;

					found += tail;

				}

				int32_t next [ 2 ] = { home.taken, home.end == term::BRANCH && home.other != home.taken ? home.other : -1 };

				for ( int32_t t : next ) {

					if ( t < 0 || t >= code.count ( ) || found == wanted )
						continue;

					const block_code& after = code [ static_cast < size_t > ( t ) ];
					bool first = true;
					bool stopped = barrier;

					for ( int32_t s = after.first; s < after.first + after.count && s < statements.count ( ); s++ ) {

						const statement& line = statements [ static_cast < size_t > ( s ) ];

						if ( line.kind == stmt::NOP )
							continue;

						bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;
						int32_t n = seen ( line.right ) + ( defines ? 0 : seen ( line.left ) );

						if ( n ) {

							if ( !first || stopped || entering [ static_cast < size_t > ( t ) ] != 1 )
								return false;

							found += n;

						}

						first = false;
						stopped = stopped || blocks ( line );

					}

					int32_t end_uses = seen ( after.condition ) + seen ( after.selector ) + seen ( after.returned );

					if ( end_uses ) {

						if ( !first || stopped || entering [ static_cast < size_t > ( t ) ] != 1 )
							return false;

						found += end_uses;

					}

				}

				return found == wanted;

			};

			auto pure_fixed = [ & ] ( auto& self, int32_t e, int32_t depth ) -> bool {

				if ( e < 0 || depth > 16 )
					return false;

				const expr& node = exprs [ static_cast < size_t > ( e ) ];

				switch ( node.kind ) {

					case op::CONST:
						return true;

					case op::VAR:
						return fixed ( node.value ) && !( vars [ static_cast < size_t > ( node.value ) ].bits & FLOATING );

					case op::ADD:
					case op::SUB:
					case op::AND:
					case op::OR:
					case op::XOR:
					case op::SHL:
					case op::SHR:
					case op::MUL:
						return !( node.bits & FLOATING ) && self ( self, node.a, depth + 1 ) && self ( self, node.b, depth + 1 );

					case op::CAST:
						return !( node.bits & FLOATING ) && self ( self, node.a, depth + 1 );

					default:
						return false;

				}

			};

			for ( int32_t v = 0; v < total && expr_fixed; v++ ) {

				if ( !plain ( v ) || defs [ static_cast < size_t > ( v ) ] != 1 || uses [ static_cast < size_t > ( v ) ] != 1 || single [ static_cast < size_t > ( v ) ] < 0 || returned [ static_cast < size_t > ( v ) ] || ( vars [ static_cast < size_t > ( v ) ].bits & FLOATING ) )
					continue;

				statement& line = statements [ static_cast < size_t > ( single [ static_cast < size_t > ( v ) ] ) ];
				const expr source = exprs [ static_cast < size_t > ( line.right ) ];

				if ( source.kind == op::VAR || source.kind == op::CONST || source.size != exprs [ static_cast < size_t > ( line.left ) ].size || !pure_fixed ( pure_fixed, line.right, 0 ) )
					continue;

				int32_t value = line.right;
				bool self_use = false;
				base::list < int32_t > here;

				here.fill ( static_cast < size_t > ( total ), 0 );
				count_vars ( *this, line.right, here, 0 );
				self_use = here [ static_cast < size_t > ( v ) ] != 0;

				if ( self_use )
					continue;

				uint64_t def_at = line.address;
				uint64_t use_at = 0;
				int32_t use_block = -1;
				int32_t def_block = -1;

				for ( int32_t k = 0; k < code.count ( ) && !use_at; k++ ) {

					const block_code& block = code [ static_cast < size_t > ( k ) ];

					for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ) && !use_at; s++ ) {

						const statement& other = statements [ static_cast < size_t > ( s ) ];

						if ( other.kind == stmt::NOP )
							continue;

						base::list < int32_t > seen;

						seen.fill ( static_cast < size_t > ( total ), 0 );
						count_vars ( *this, other.right, seen, 0 );

						if ( !( other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR ) )
							count_vars ( *this, other.left, seen, 0 );

						if ( seen [ static_cast < size_t > ( v ) ] ) {

							use_at = other.address ? other.address : 1;
							use_block = k;

						}

					}

					if ( !use_at ) {

						base::list < int32_t > seen;

						seen.fill ( static_cast < size_t > ( total ), 0 );
						count_vars ( *this, block.condition, seen, 0 );
						count_vars ( *this, block.selector, seen, 0 );
						count_vars ( *this, block.returned, seen, 0 );

						if ( seen [ static_cast < size_t > ( v ) ] && k < graph.blocks.count ( ) ) {

							use_at = graph.blocks [ static_cast < size_t > ( k ) ].end - 1;
							use_block = k;

						}

					}

				}

				for ( int32_t k = 0; k < code.count ( ) && def_block < 0; k++ ) {

					if ( single [ static_cast < size_t > ( v ) ] >= code [ static_cast < size_t > ( k ) ].first && single [ static_cast < size_t > ( v ) ] < code [ static_cast < size_t > ( k ) ].first + code [ static_cast < size_t > ( k ) ].count )
						def_block = k;

				}

				bool intact = intact_exprs && !( line.sources & line.target ) && def_block >= 0 && use_block >= 0 && line.address && use_at > 1 && holds_through ( *this, line.sources, def_block, line.address - 1, use_block, use_at );
				bool hoisted_out = false;

				for ( const analysis::edge& link : graph.edges ) {

					if ( link.from < 0 || link.to < 0 || link.from >= graph.blocks.count ( ) || link.to >= graph.blocks.count ( ) )
						continue;

					uint64_t head = graph.blocks [ static_cast < size_t > ( link.to ) ].start;
					uint64_t tail = graph.blocks [ static_cast < size_t > ( link.from ) ].end;

					if ( graph.blocks [ static_cast < size_t > ( link.from ) ].start < head )
						continue;

					bool use_inside = use_at >= head && use_at < tail;
					bool def_inside = def_at >= head && def_at < tail;

					if ( use_inside && !def_inside && !intact )
						hoisted_out = true;

				}

				if ( hoisted_out || !use_at )
					continue;

				bool crosses_call = false;

				for ( int32_t s = single [ static_cast < size_t > ( v ) ] + 1; s < statements.count ( ) && calls_keep_exprs; s++ ) {

					const statement& other = statements [ static_cast < size_t > ( s ) ];

					if ( other.kind == stmt::NOP || other.kind == stmt::ASM )
						continue;

					if ( other.address && use_at > 1 && other.address > use_at )
						break;

					base::list < int32_t > seen;

					seen.fill ( static_cast < size_t > ( total ), 0 );
					count_vars ( *this, other.right, seen, 0 );

					if ( !( other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR ) )
						count_vars ( *this, other.left, seen, 0 );

					if ( seen [ static_cast < size_t > ( v ) ] )
						break;

					if ( has_call ( *this, other.right, 0 ) || ( other.kind != stmt::ASSIGN && has_call ( *this, other.left, 0 ) ) ) {

						crosses_call = true;

						break;

					}

				}

				if ( crosses_call && !( calls_intact && intact ) )
					continue;

				if ( broken_keep && intact_exprs && line.sources && !intact )
					continue;

				line.kind = stmt::NOP;
				vars [ static_cast < size_t > ( v ) ].ghost = ghost_exprs;
				vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

				for ( statement& other : statements ) {

					if ( other.kind == stmt::NOP || other.kind == stmt::ASM )
						continue;

					bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;

					if ( !defines )
						other.left = substitute ( *this, other.left, v, value, 0 );

					other.right = substitute ( *this, other.right, v, value, 0 );

				}

				for ( block_code& block : code ) {

					block.condition = substitute ( *this, block.condition, v, value, 0 );
					block.selector = substitute ( *this, block.selector, v, value, 0 );
					block.returned = substitute ( *this, block.returned, v, value, 0 );

				}

				changed = true;

			}

			for ( int32_t v = 0; v < total && copy_fixed; v++ ) {

				if ( !plain ( v ) || defs [ static_cast < size_t > ( v ) ] != 1 || single [ static_cast < size_t > ( v ) ] < 0 || returned [ static_cast < size_t > ( v ) ] )
					continue;

				statement& line = statements [ static_cast < size_t > ( single [ static_cast < size_t > ( v ) ] ) ];
				const expr source = exprs [ static_cast < size_t > ( line.right ) ];

				bool va_copy = source.kind == op::ADDRESS && source.a < 0 && va_var >= 0 && source.value == va_var && !returned [ static_cast < size_t > ( v ) ];

				if ( va_copy ) {

					int32_t value = line.right;

					line.kind = stmt::NOP;

					for ( statement& other : statements ) {

						if ( other.kind == stmt::NOP || other.kind == stmt::ASM )
							continue;

						bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;

						if ( !defines )
							other.left = substitute ( *this, other.left, v, value, 0 );

						other.right = substitute ( *this, other.right, v, value, 0 );

					}

					for ( block_code& block : code ) {

						block.condition = substitute ( *this, block.condition, v, value, 0 );
						block.returned = substitute ( *this, block.returned, v, value, 0 );

					}

					changed = true;

					continue;

				}

				if ( source.kind != op::VAR || source.value == v || source.size != exprs [ static_cast < size_t > ( line.left ) ].size || vars [ static_cast < size_t > ( source.value ) ].size != vars [ static_cast < size_t > ( v ) ].size || ( ( vars [ static_cast < size_t > ( source.value ) ].bits ^ vars [ static_cast < size_t > ( v ) ].bits ) & FLOATING ) )
					continue;

				if ( !fixed ( source.value ) && !( copy_local && local_only ( v, single [ static_cast < size_t > ( v ) ], source.value, uses [ static_cast < size_t > ( v ) ] ) ) )
					continue;

				if ( keep_destination && vars [ static_cast < size_t > ( source.value ) ].kind == var_kind::REGISTER && vars [ static_cast < size_t > ( source.value ) ].family == 0 && vars [ static_cast < size_t > ( v ) ].family > 0 && vars [ static_cast < size_t > ( v ) ].family < 16 && defs [ static_cast < size_t > ( source.value ) ] == 1 && single [ static_cast < size_t > ( source.value ) ] >= 0 && !returned [ static_cast < size_t > ( source.value ) ] ) {

					statement& origin = statements [ static_cast < size_t > ( single [ static_cast < size_t > ( source.value ) ] ) ];
					int32_t produced = origin.right >= 0 ? origin.right : -1;

					while ( produced >= 0 && exprs [ static_cast < size_t > ( produced ) ].kind == op::CAST )
						produced = exprs [ static_cast < size_t > ( produced ) ].a;

					if ( produced >= 0 && exprs [ static_cast < size_t > ( produced ) ].kind == op::CALL && origin.address < line.address && shared_results ) {

						int32_t copy_index = single [ static_cast < size_t > ( v ) ];
						base::list < int32_t > owner;

						owner.fill ( static_cast < size_t > ( statements.count ( ) ), -1 );

						for ( int32_t k = 0; k < code.count ( ); k++ ) {

							const block_code& block = code [ static_cast < size_t > ( k ) ];

							for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ )
								owner [ static_cast < size_t > ( s ) ] = k;

						}

						int32_t copy_block = owner [ static_cast < size_t > ( copy_index ) ];
						uint64_t copied_at = line.origin ? line.origin : line.address;
						uint32_t mask = 1u << vars [ static_cast < size_t > ( source.value ) ].family;
						int32_t outside = 0;
						int32_t mine = 0;
						int32_t moved = 0;
						base::list < int32_t > carried_lines;
						base::list < int32_t > carried_blocks;

						for ( int32_t s = 0; s < statements.count ( ) && copy_block >= 0; s++ ) {

							const statement& other = statements [ static_cast < size_t > ( s ) ];

							if ( s == copy_index || other.kind == stmt::NOP || other.kind == stmt::ASM )
								continue;

							bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;
							int32_t here = occurrences ( *this, other.right, v, 0 ) + ( defines ? 0 : occurrences ( *this, other.left, v, 0 ) );
							int32_t theirs = occurrences ( *this, other.right, source.value, 0 ) + ( defines ? 0 : occurrences ( *this, other.left, source.value, 0 ) );
							int32_t at = owner [ static_cast < size_t > ( s ) ];

							if ( theirs && at != copy_block )
								outside += theirs;

							mine += here;

							if ( !here || at < 0 || !holds_through ( *this, mask, copy_block, copied_at, at, other.address ) )
								continue;

							carried_lines.push ( s );
							moved += here;

							if ( at != copy_block )
								outside += here;

						}

						for ( int32_t k = 0; k < code.count ( ) && copy_block >= 0 && k < graph.blocks.count ( ); k++ ) {

							const block_code& block = code [ static_cast < size_t > ( k ) ];
							int32_t here = occurrences ( *this, block.condition, v, 0 ) + occurrences ( *this, block.selector, v, 0 ) + occurrences ( *this, block.returned, v, 0 );
							int32_t theirs = occurrences ( *this, block.condition, source.value, 0 ) + occurrences ( *this, block.selector, source.value, 0 ) + occurrences ( *this, block.returned, source.value, 0 );

							if ( theirs && k != copy_block )
								outside += theirs;

							mine += here;

							if ( !here || !holds_through ( *this, mask, copy_block, copied_at, k, graph.blocks [ static_cast < size_t > ( k ) ].end - 1 ) )
								continue;

							carried_blocks.push ( k );
							moved += here;

							if ( k != copy_block )
								outside += here;

						}

						if ( outside > 0 && mine > moved ) {

							for ( int32_t s : carried_lines ) {

								statement& other = statements [ static_cast < size_t > ( s ) ];
								bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;

								if ( !defines )
									other.left = substitute ( *this, other.left, v, line.right, 0 );

								other.right = substitute ( *this, other.right, v, line.right, 0 );

							}

							for ( int32_t k : carried_blocks ) {

								block_code& block = code [ static_cast < size_t > ( k ) ];

								block.condition = substitute ( *this, block.condition, v, line.right, 0 );
								block.selector = substitute ( *this, block.selector, v, line.right, 0 );
								block.returned = substitute ( *this, block.returned, v, line.right, 0 );

							}

							uses [ static_cast < size_t > ( source.value ) ] += moved;
							uses [ static_cast < size_t > ( v ) ] -= moved;
							changed = changed || moved > 0;

							continue;

						}

					}

					if ( produced >= 0 && exprs [ static_cast < size_t > ( produced ) ].kind == op::CALL && origin.address < line.address ) {

						int64_t from_var = source.value;

						origin.left = line.left;
						line.kind = stmt::NOP;

						for ( expr& node : exprs ) {

							if ( node.kind == op::VAR && node.value == from_var )
								node.value = v;

						}

						uses [ static_cast < size_t > ( v ) ] += uses [ static_cast < size_t > ( from_var ) ] - 1;
						uses [ static_cast < size_t > ( from_var ) ] = 0;
						defs [ static_cast < size_t > ( from_var ) ] = 0;
						single [ static_cast < size_t > ( v ) ] = single [ static_cast < size_t > ( from_var ) ];
						single [ static_cast < size_t > ( from_var ) ] = -1;
						changed = true;

						continue;

					}

				}

				int32_t value = line.right;

				line.kind = stmt::NOP;
				uses [ static_cast < size_t > ( source.value ) ] += uses [ static_cast < size_t > ( v ) ] - 1;
				uses [ static_cast < size_t > ( v ) ] = 0;
				defs [ static_cast < size_t > ( v ) ] = 0;

				const variable& copied = vars [ static_cast < size_t > ( source.value ) ];
				bool stack_param = copied.kind == var_kind::PARAMETER && long_mode && ( copied.position >= 4 || ( copied.bits & FLOATING ) );

				vars [ static_cast < size_t > ( v ) ].ghost = !( stack_param_copies && stack_param );
				vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

				for ( statement& other : statements ) {

					if ( other.kind == stmt::NOP || other.kind == stmt::ASM )
						continue;

					bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;

					if ( !defines )
						other.left = substitute ( *this, other.left, v, value, 0 );

					other.right = substitute ( *this, other.right, v, value, 0 );

				}

				for ( block_code& block : code ) {

					block.condition = substitute ( *this, block.condition, v, value, 0 );
					block.selector = substitute ( *this, block.selector, v, value, 0 );
					block.returned = substitute ( *this, block.returned, v, value, 0 );

				}

				changed = true;

			}

			for ( const block_code& block : code ) {

				if ( !block.reachable )
					continue;

				int32_t previous = -1;

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

					statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::NOP )
						continue;

					if ( previous >= 0 ) {

						statement& head = statements [ static_cast < size_t > ( previous ) ];
						int64_t x = exprs [ static_cast < size_t > ( head.left ) ].value;
						const expr& feed = exprs [ static_cast < size_t > ( head.right ) ];

						if ( copy_into && line.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.right ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.right ) ].value == x && exprs [ static_cast < size_t > ( line.right ) ].size == exprs [ static_cast < size_t > ( head.left ) ].size && exprs [ static_cast < size_t > ( line.left ) ].size == exprs [ static_cast < size_t > ( head.left ) ].size && plain ( exprs [ static_cast < size_t > ( line.left ) ].value ) && !( ( vars [ static_cast < size_t > ( exprs [ static_cast < size_t > ( line.left ) ].value ) ].bits ^ vars [ static_cast < size_t > ( x ) ].bits ) & FLOATING ) ) {

							vars [ static_cast < size_t > ( x ) ].ghost = ghost_call_copies || feed.kind != op::CALL;
							vars [ static_cast < size_t > ( x ) ].born = head.origin ? head.origin : head.address;
							head.left = line.left;
							line.kind = stmt::NOP;
							uses [ static_cast < size_t > ( x ) ] = 0;
							changed = true;
							previous = -1;

							continue;

						}

						if ( calls_forward && swap_copies && feed.kind == op::CALL && line.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && !has_call ( *this, line.right, 0 ) ) {

							int32_t after = -1;

							for ( int32_t t = s + 1; t < block.first + block.count && t < statements.count ( ); t++ ) {

								if ( statements [ static_cast < size_t > ( t ) ].kind != stmt::NOP ) {

									after = t;

									break;

								}

							}

							base::list < int32_t > mine;
							base::list < int32_t > theirs;

							mine.fill ( static_cast < size_t > ( total ), 0 );
							theirs.fill ( static_cast < size_t > ( total ), 0 );
							count_vars ( *this, line.right, mine, 0 );

							if ( after >= 0 && mine [ static_cast < size_t > ( x ) ] == 0 ) {

								statement& next = statements [ static_cast < size_t > ( after ) ];

								if ( next.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( next.left ) ].kind == op::VAR && next.address == line.address && !has_call ( *this, next.right, 0 ) ) {

									count_vars ( *this, next.right, theirs, 0 );

									int64_t a = exprs [ static_cast < size_t > ( line.left ) ].value;
									int64_t c = exprs [ static_cast < size_t > ( next.left ) ].value;

									if ( a != c && theirs [ static_cast < size_t > ( x ) ] == 1 && theirs [ static_cast < size_t > ( a ) ] == 0 && mine [ static_cast < size_t > ( c ) ] == 0 ) {

										statement held = line;

										line = next;
										next = held;

									}

								}

							}

						}

						if ( calls_forward && feed.kind == op::CALL && line.kind != stmt::ASM && !has_call ( *this, line.right, 0 ) && !has_call ( *this, line.left, 0 ) ) {

							base::list < int32_t > here;

							here.fill ( static_cast < size_t > ( total ), 0 );
							count_vars ( *this, line.right, here, 0 );

							if ( !( line.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
								count_vars ( *this, line.left, here, 0 );

							if ( here [ static_cast < size_t > ( x ) ] == 1 ) {

								int32_t value = head.right;

								line.right = substitute ( *this, line.right, x, value, 0 );

								if ( !( line.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
									line.left = substitute ( *this, line.left, x, value, 0 );

								head.kind = stmt::NOP;
								vars [ static_cast < size_t > ( x ) ].ghost = ghost_forwards;
								vars [ static_cast < size_t > ( x ) ].born = head.origin ? head.origin : head.address;
								changed = true;
								previous = -1;

								continue;

							}

						}

					}

					previous = -1;

					if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

						int64_t x = exprs [ static_cast < size_t > ( line.left ) ].value;

						if ( plain ( x ) && defs [ static_cast < size_t > ( x ) ] == 1 && uses [ static_cast < size_t > ( x ) ] == 1 )
							previous = s;

					}

				}

			}

			if ( !changed )
				break;

		}

	}

	static bool fold_pointers = true;
	static bool fold_vector_zero = true;
	static bool ghost_vector_zero = false;
	static bool fold_addresses = true;
	static bool copy_constants = true;
	static bool zero_registers = true;
	static bool zero_stack_slots = true;
	static bool reload_selects = true;
	static bool dead_stack_stores = true;
	static int32_t switch_cases = 3;
	static bool ghost_addresses = false;
	static bool keep_returned_constants = false;

	static bool mentions ( const builder& b, int32_t e, int64_t var, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return false;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( ( node.kind == op::VAR || node.kind == op::ADDRESS ) && node.value == var )
			return true;

		if ( mentions ( b, node.a, var, depth + 1 ) || mentions ( b, node.b, var, depth + 1 ) || mentions ( b, node.c, var, depth + 1 ) )
			return true;

		for ( int32_t i = 0; i < node.arg_count; i++ ) {

			if ( mentions ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], var, depth + 1 ) )
				return true;

		}

		return false;

	}

	void builder::propagate_register_constants ( ) {

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

	static int32_t zero_fold ( builder& b, int32_t e, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return e;

		const expr node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.bits & FLOATING )
			return e;

		int32_t a = zero_fold ( b, node.a, depth + 1 );
		int32_t c = node.kind == op::CALL || node.kind == op::INTRINSIC ? node.b : zero_fold ( b, node.b, depth + 1 );

		auto is_zero = [ & ] ( int32_t x ) {

			return x >= 0 && b.exprs [ static_cast < size_t > ( x ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( x ) ].value == 0 && !( b.exprs [ static_cast < size_t > ( x ) ].bits & POINTER );

		};

		switch ( node.kind ) {

			case op::ADD:
			case op::OR:
			case op::XOR:

				if ( is_zero ( c ) && a >= 0 && b.exprs [ static_cast < size_t > ( a ) ].size == node.size )
					return a;

				if ( is_zero ( a ) && c >= 0 && b.exprs [ static_cast < size_t > ( c ) ].size == node.size )
					return c;

				break;

			case op::SUB:

				if ( is_zero ( c ) && a >= 0 && b.exprs [ static_cast < size_t > ( a ) ].size == node.size )
					return a;

				break;

			case op::MUL:
			case op::AND:

				if ( is_zero ( a ) || is_zero ( c ) )
					return b.constant ( 0, node.size );

				break;

			case op::CAST:

				if ( is_zero ( a ) )
					return b.constant ( 0, node.size );

				break;

			default:
				break;

		}

		if ( a == node.a && c == node.b )
			return e;

		expr copy = node;

		copy.a = a;
		copy.b = c;
		b.exprs.push ( copy );

		return b.exprs.count ( ) - 1;

	}

	void builder::zero_edges ( ) {

		int32_t total = code.count ( );

		if ( total != graph.blocks.count ( ) )
			return;

		base::list < int32_t > preds;

		preds.fill ( static_cast < size_t > ( total ), 0 );

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to >= 0 && link.to < total )
				preds [ static_cast < size_t > ( link.to ) ]++;

		}

		for ( int32_t k = 0; k < total; k++ ) {

			const block_code& head = code [ static_cast < size_t > ( k ) ];

			if ( !head.reachable || head.end != term::BRANCH || head.condition < 0 || head.taken == head.other )
				continue;

			const expr& test = exprs [ static_cast < size_t > ( head.condition ) ];
			int32_t tested = -1;
			int32_t zero_side = -1;

			if ( ( test.kind == op::EQ || test.kind == op::NE ) && test.a >= 0 && test.b >= 0 && exprs [ static_cast < size_t > ( test.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( test.b ) ].value == 0 ) {

				tested = test.a;
				zero_side = test.kind == op::EQ ? head.taken : head.other;

			} else if ( test.kind == op::LNOT && test.a >= 0 ) {

				tested = test.a;
				zero_side = head.taken;

			}

			if ( tested < 0 || zero_side < 0 || zero_side >= total || zero_side == k || preds [ static_cast < size_t > ( zero_side ) ] != 1 )
				continue;

			const expr& probe = exprs [ static_cast < size_t > ( tested ) ];

			if ( probe.kind != op::VAR || probe.value < 0 || probe.value >= vars.count ( ) || ( probe.bits & FLOATING ) )
				continue;

			int64_t v = probe.value;
			const variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( item.kind == var_kind::STACK || ( item.bits & FLOATING ) || item.flags || probe.size != item.size )
				continue;

			int64_t same_vars [ 4 ] = { v, -1, -1, -1 };
			int32_t same_count = 1;

			for ( int32_t s = head.first; s < head.first + head.count && s < statements.count ( ); s++ ) {

				const statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
					continue;

				int64_t w = exprs [ static_cast < size_t > ( line.left ) ].value;
				const expr& source = exprs [ static_cast < size_t > ( line.right ) ];
				bool copy = source.kind == op::VAR && source.size == exprs [ static_cast < size_t > ( line.left ) ].size && w >= 0 && w < vars.count ( ) && vars [ static_cast < size_t > ( w ) ].size == item.size && vars [ static_cast < size_t > ( w ) ].kind != var_kind::STACK && !( vars [ static_cast < size_t > ( w ) ].bits & FLOATING );

				for ( int32_t i = 0; i < same_count; i++ ) {

					if ( same_vars [ i ] == w )
						copy = false;

				}

				if ( !copy )
					continue;

				bool linked = false;

				for ( int32_t i = 0; i < same_count; i++ )
					linked = linked || source.value == same_vars [ i ];

				if ( linked && same_count < 4 )
					same_vars [ same_count++ ] = w;

			}

			base::list < uint8_t > region;

			region.fill ( static_cast < size_t > ( total ), 0 );
			region [ static_cast < size_t > ( zero_side ) ] = 1;

			for ( bool grew = true; grew; ) {

				grew = false;

				for ( int32_t r = 0; r < total; r++ ) {

					if ( region [ static_cast < size_t > ( r ) ] || r == k || !code [ static_cast < size_t > ( r ) ].reachable )
						continue;

					bool inside = false;
					bool outside = false;

					for ( const analysis::edge& link : graph.edges ) {

						if ( link.to != r )
							continue;

						if ( link.from >= 0 && link.from < total && region [ static_cast < size_t > ( link.from ) ] )
							inside = true;
						else
							outside = true;

					}

					if ( inside && !outside ) {

						region [ static_cast < size_t > ( r ) ] = 1;
						grew = true;

					}

				}

			}

			for ( int32_t slot = 0; slot < same_count; slot++ ) {

				int64_t x = same_vars [ slot ];
				int32_t zero = constant ( 0, vars [ static_cast < size_t > ( x ) ].size );
				bool written = false;

				for ( int32_t r = 0; r < total && !written; r++ ) {

					if ( !region [ static_cast < size_t > ( r ) ] )
						continue;

					const block_code& part = code [ static_cast < size_t > ( r ) ];

					for ( int32_t s = part.first; s < part.first + part.count && s < statements.count ( ); s++ ) {

						const statement& line = statements [ static_cast < size_t > ( s ) ];

						if ( line.kind == stmt::ASM || ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == x ) || ( line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::ADDRESS ) )
							written = true;

					}

				}

				if ( written )
					continue;

				for ( int32_t r = 0; r < total; r++ ) {

					if ( !region [ static_cast < size_t > ( r ) ] )
						continue;

					block_code& part = code [ static_cast < size_t > ( r ) ];

					for ( int32_t s = part.first; s < part.first + part.count && s < statements.count ( ); s++ ) {

						statement& line = statements [ static_cast < size_t > ( s ) ];

						if ( line.kind == stmt::NOP )
							continue;

						bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

						if ( line.right >= 0 && mentions ( *this, line.right, x, 0 ) )
							line.right = zero_fold ( *this, substitute ( *this, line.right, x, zero, 0 ), 0 );

						if ( !defines && line.left >= 0 && mentions ( *this, line.left, x, 0 ) )
							line.left = zero_fold ( *this, substitute ( *this, line.left, x, zero, 0 ), 0 );

					}

					if ( part.condition >= 0 && mentions ( *this, part.condition, x, 0 ) )
						part.condition = zero_fold ( *this, substitute ( *this, part.condition, x, zero, 0 ), 0 );

					if ( ( part.end == term::RETURN || part.end == term::TAIL ) && part.returned >= 0 && mentions ( *this, part.returned, x, 0 ) )
						part.returned = zero_fold ( *this, substitute ( *this, part.returned, x, zero, 0 ), 0 );

				}

			}

		}

	}

	void builder::fold_constants ( ) {

		int32_t total = vars.count ( );

		if ( dead_stack_stores ) {

			for ( const block_code& block : code ) {

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

					statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
						continue;

					int64_t slot = exprs [ static_cast < size_t > ( line.left ) ].value;

					if ( slot < 0 || slot >= total || vars [ static_cast < size_t > ( slot ) ].kind != var_kind::STACK || has_call ( *this, line.right, 0 ) )
						continue;

					for ( int32_t t = s + 1; t < block.first + block.count && t < statements.count ( ); t++ ) {

						const statement& next = statements [ static_cast < size_t > ( t ) ];

						if ( next.kind == stmt::NOP )
							continue;

						if ( next.kind == stmt::ASSIGN && next.left >= 0 && exprs [ static_cast < size_t > ( next.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( next.left ) ].value == slot && !mentions ( *this, next.right, slot, 0 ) && exprs [ static_cast < size_t > ( next.left ) ].size >= exprs [ static_cast < size_t > ( line.left ) ].size ) {

							line.kind = stmt::NOP;

							break;

						}

						if ( next.kind != stmt::ASSIGN || has_call ( *this, next.right, 0 ) || mentions ( *this, next.right, slot, 0 ) || mentions ( *this, next.left, slot, 0 ) || exprs [ static_cast < size_t > ( next.left ) ].kind != op::VAR )
							break;

					}

				}

			}

		}

		if ( zero_registers )
			propagate_register_constants ( );

		if ( reload_selects ) {

			base::list < int32_t > def_count;

			def_count.fill ( static_cast < size_t > ( total ), 0 );

			for ( const statement& line : statements ) {

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && exprs [ static_cast < size_t > ( line.left ) ].value < total )
					def_count [ static_cast < size_t > ( exprs [ static_cast < size_t > ( line.left ) ].value ) ]++;

			}

			for ( const block_code& block : code ) {

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

					statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind != stmt::ASSIGN || line.right < 0 || exprs [ static_cast < size_t > ( line.right ) ].kind != op::TERNARY )
						continue;

					expr select = exprs [ static_cast < size_t > ( line.right ) ];
					int32_t arms [ 2 ] = { select.b, select.c };
					bool moved = false;

					for ( int32_t k = 0; k < 2; k++ ) {

						int32_t arm = arms [ k ];
						int32_t bare = arm;

						while ( bare >= 0 && exprs [ static_cast < size_t > ( bare ) ].kind == op::CAST )
							bare = exprs [ static_cast < size_t > ( bare ) ].a;

						if ( bare < 0 || exprs [ static_cast < size_t > ( bare ) ].kind != op::VAR )
							continue;

						int64_t y = exprs [ static_cast < size_t > ( bare ) ].value;

						if ( y < 0 || y >= total || def_count [ static_cast < size_t > ( y ) ] != 1 )
							continue;

						int32_t source = -1;
						base::list < int64_t > changed_vars;

						for ( int32_t t = s - 1; t >= block.first; t-- ) {

							const statement& prior = statements [ static_cast < size_t > ( t ) ];

							if ( prior.kind == stmt::NOP )
								continue;

							if ( prior.kind == stmt::ASSIGN && prior.left >= 0 && exprs [ static_cast < size_t > ( prior.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( prior.left ) ].value == y ) {

								source = prior.right;

								break;

							}

							if ( prior.kind != stmt::ASSIGN || has_call ( *this, prior.right, 0 ) || prior.left < 0 || exprs [ static_cast < size_t > ( prior.left ) ].kind != op::VAR )
								break;

							changed_vars.push ( exprs [ static_cast < size_t > ( prior.left ) ].value );

						}

						while ( source >= 0 && exprs [ static_cast < size_t > ( source ) ].kind == op::CAST )
							source = exprs [ static_cast < size_t > ( source ) ].a;

						bool stale = false;

						for ( int64_t c : changed_vars )
							stale = stale || ( source >= 0 && mentions ( *this, source, c, 0 ) );

						if ( stale )
							continue;

						if ( source < 0 || exprs [ static_cast < size_t > ( source ) ].kind != op::LOAD || exprs [ static_cast < size_t > ( source ) ].size != exprs [ static_cast < size_t > ( arm ) ].size )
							continue;

						arms [ k ] = source;
						moved = true;

					}

					if ( moved ) {

						int32_t made = make ( op::TERNARY, select.size, select.a, arms [ 0 ], 0, select.bits );

						exprs [ static_cast < size_t > ( made ) ].c = arms [ 1 ];
						line.right = made;

					}

				}

			}

		}

		if ( copy_constants ) {

			base::list < int32_t > known;
			base::list < uint64_t > known_at;

			known.fill ( static_cast < size_t > ( total ), -1 );
			known_at.fill ( static_cast < size_t > ( total ), 0 );

			for ( const block_code& block : code ) {

				for ( int32_t v = 0; v < total; v++ )
					known [ static_cast < size_t > ( v ) ] = -1;

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

					statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
						continue;

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR ) {

						if ( line.kind == stmt::EXPR || line.kind == stmt::STORE ) {

							for ( int32_t v = 0; v < total; v++ ) {

								if ( vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER )
									known [ static_cast < size_t > ( v ) ] = -1;

							}

						}

						continue;

					}

					int64_t target = exprs [ static_cast < size_t > ( line.left ) ].value;
					const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

					if ( right.kind == op::VAR && right.value >= 0 && right.value < total && known [ static_cast < size_t > ( right.value ) ] >= 0 && vars [ static_cast < size_t > ( right.value ) ].kind == var_kind::REGISTER && !( right.bits & FLOATING ) ) {

						const expr& constant = exprs [ static_cast < size_t > ( known [ static_cast < size_t > ( right.value ) ] ) ];

						line.right = make ( op::CONST, right.size, -1, -1, constant.value, constant.bits );

						if ( origin_numbers && known_at [ static_cast < size_t > ( right.value ) ] )
							line.origin = known_at [ static_cast < size_t > ( right.value ) ];

					}

					if ( target < 0 || target >= total )
						continue;

					const expr& now = exprs [ static_cast < size_t > ( line.right ) ];

					known [ static_cast < size_t > ( target ) ] = now.kind == op::CONST && !( now.bits & ( FLOATING | POINTER ) ) ? line.right : -1;
					known_at [ static_cast < size_t > ( target ) ] = line.origin ? line.origin : line.address;

				}

			}

		}
		base::list < int32_t > defs;
		base::list < int32_t > single;
		base::list < int32_t > uses;

		defs.fill ( static_cast < size_t > ( total ), 0 );
		single.fill ( static_cast < size_t > ( total ), -1 );
		uses.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t s = 0; s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

			if ( defines ) {

				int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( v >= 0 && v < total ) {

					defs [ static_cast < size_t > ( v ) ]++;
					single [ static_cast < size_t > ( v ) ] = s;

				}

			} else
				count_vars ( *this, line.left, uses, 0 );

			count_vars ( *this, line.right, uses, 0 );

		}

		base::list < int32_t > returned;

		returned.fill ( static_cast < size_t > ( total ), 0 );

		for ( const block_code& block : code ) {

			count_vars ( *this, block.condition, uses, 0 );
			count_vars ( *this, block.selector, uses, 0 );
			count_vars ( *this, block.returned, uses, 0 );
			count_vars ( *this, block.returned, returned, 0 );

		}

		base::list < int32_t > stored;

		stored.fill ( static_cast < size_t > ( total ), 0 );

		for ( const statement& line : statements ) {

			if ( ( line.kind != stmt::ASSIGN && line.kind != stmt::STORE ) || line.left < 0 || line.right < 0 || !( exprs [ static_cast < size_t > ( line.left ) ].kind == op::LOAD || ( zero_stack_slots && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && exprs [ static_cast < size_t > ( line.left ) ].value < total && vars [ static_cast < size_t > ( exprs [ static_cast < size_t > ( line.left ) ].value ) ].kind == var_kind::STACK ) ) || exprs [ static_cast < size_t > ( line.right ) ].kind != op::VAR )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( line.right ) ].value;

			if ( v >= 0 && v < total )
				stored [ static_cast < size_t > ( v ) ]++;

		}

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( fold_vector_zero && ( item.bits & FLOATING ) && item.kind == var_kind::REGISTER && !item.flags && defs [ static_cast < size_t > ( v ) ] == 1 && uses [ static_cast < size_t > ( v ) ] > 0 && uses [ static_cast < size_t > ( v ) ] == stored [ static_cast < size_t > ( v ) ] ) {

				statement& line = statements [ static_cast < size_t > ( single [ static_cast < size_t > ( v ) ] ) ];
				const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

				if ( source.kind != op::CONST || source.value != 0 )
					continue;

				int32_t zero = make ( op::CONST, 8, -1, -1, 0, 0 );

				for ( statement& other : statements ) {

					if ( ( other.kind != stmt::ASSIGN && other.kind != stmt::STORE ) || other.left < 0 || other.right < 0 || exprs [ static_cast < size_t > ( other.right ) ].kind != op::VAR || exprs [ static_cast < size_t > ( other.right ) ].value != v )
						continue;

					if ( exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR ) {

						other.right = make ( op::CONST, exprs [ static_cast < size_t > ( other.left ) ].size, -1, -1, 0, 0 );

						continue;

					}

					if ( exprs [ static_cast < size_t > ( other.left ) ].kind != op::LOAD )
						continue;

					const expr target = exprs [ static_cast < size_t > ( other.left ) ];

					other.left = make ( op::LOAD, target.size, target.a, target.b, target.value, static_cast < uint8_t > ( target.bits & ~FLOATING ) );
					other.right = zero;

				}

				line.kind = stmt::NOP;
				vars [ static_cast < size_t > ( v ) ].ghost = ghost_vector_zero;
				vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

				continue;

			}

			if ( item.kind != var_kind::REGISTER || item.flags || item.cookie || item.saved || ( item.bits & FLOATING ) || defs [ static_cast < size_t > ( v ) ] != 1 || uses [ static_cast < size_t > ( v ) ] == 0 || uses [ static_cast < size_t > ( v ) ] >= 100 )
				continue;

			if ( keep_returned_constants && returned [ static_cast < size_t > ( v ) ] && uses [ static_cast < size_t > ( v ) ] > returned [ static_cast < size_t > ( v ) ] - 0 && item.family == 0 )
				continue;

			statement& line = statements [ static_cast < size_t > ( single [ static_cast < size_t > ( v ) ] ) ];
			const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

			bool address_source = fold_addresses && source.kind == op::ADDRESS && source.a < 0 && source.value >= 0;

			if ( !address_source && ( source.kind != op::CONST || ( source.bits & FLOATING ) || ( ( source.bits & POINTER ) && !fold_pointers ) ) )
				continue;

			if ( !address_source && ( source.bits & POINTER ) ) {

				bool arithmetic = false;

				auto scan = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

					if ( e < 0 || depth > 64 || arithmetic )
						return;

					const expr& node = exprs [ static_cast < size_t > ( e ) ];

					if ( node.kind == op::LOAD && node.a >= 0 && exprs [ static_cast < size_t > ( node.a ) ].kind == op::ADD && exprs [ static_cast < size_t > ( node.a ) ].a >= 0 && exprs [ static_cast < size_t > ( node.a ) ].b >= 0 ) {

						const expr& sum = exprs [ static_cast < size_t > ( node.a ) ];
						const int32_t sides [ 2 ] = { sum.a, sum.b };

						for ( int32_t k = 0; k < 2; k++ ) {

							int32_t inner = sides [ k ];
							int32_t other = sides [ 1 - k ];

							while ( inner >= 0 && exprs [ static_cast < size_t > ( inner ) ].kind == op::CAST )
								inner = exprs [ static_cast < size_t > ( inner ) ].a;

							if ( inner >= 0 && exprs [ static_cast < size_t > ( inner ) ].kind == op::VAR && exprs [ static_cast < size_t > ( inner ) ].value == v && exprs [ static_cast < size_t > ( other ) ].kind != op::CONST )
								arithmetic = true;

						}

					}

					self ( self, node.a, depth + 1 );
					self ( self, node.b, depth + 1 );
					self ( self, node.c, depth + 1 );

					for ( int32_t i = 0; i < node.arg_count; i++ )
						self ( self, args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

				};

				for ( const statement& other : statements ) {

					if ( other.kind == stmt::NOP || other.kind == stmt::ASM )
						continue;

					scan ( scan, other.left, 0 );
					scan ( scan, other.right, 0 );

				}

				for ( const block_code& block : code ) {

					scan ( scan, block.condition, 0 );
					scan ( scan, block.returned, 0 );

				}

				if ( arithmetic )
					continue;

			}

			int32_t value = line.right;

			for ( statement& other : statements ) {

				if ( other.kind == stmt::NOP || other.kind == stmt::ASM || &other == &line )
					continue;

				bool defines = other.kind == stmt::ASSIGN && other.left >= 0 && exprs [ static_cast < size_t > ( other.left ) ].kind == op::VAR;

				if ( origin_numbers && defines && other.right >= 0 && exprs [ static_cast < size_t > ( other.right ) ].kind == op::VAR && exprs [ static_cast < size_t > ( other.right ) ].value == v && !other.origin )
					other.origin = line.origin ? line.origin : line.address;

				if ( !defines )
					other.left = substitute ( *this, other.left, v, value, 0 );

				other.right = substitute ( *this, other.right, v, value, 0 );

			}

			for ( block_code& block : code ) {

				block.condition = substitute ( *this, block.condition, v, value, 0 );
				block.selector = substitute ( *this, block.selector, v, value, 0 );
				block.returned = substitute ( *this, block.returned, v, value, 0 );

			}

			line.kind = stmt::NOP;
			vars [ static_cast < size_t > ( v ) ].ghost = address_source ? ghost_addresses && uses [ static_cast < size_t > ( v ) ] > 1 : ghost_folds;
			vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

		}

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

			int32_t& wanted = test ? item.condition : item.returned;
			base::list < int64_t > passed;

			for ( int32_t s = last; s >= item.first && sink_past_moves; s-- ) {

				const statement& other = statements [ static_cast < size_t > ( s ) ];

				if ( other.kind == stmt::NOP )
					continue;

				if ( other.kind != stmt::ASSIGN || other.left < 0 || other.right < 0 || exprs [ static_cast < size_t > ( other.left ) ].kind != op::VAR ) {

					last = s;

					break;

				}

				int64_t held = exprs [ static_cast < size_t > ( other.left ) ].value;

				if ( held >= 0 && held < vars.count ( ) && uses [ static_cast < size_t > ( held ) ] == 1 && writes [ static_cast < size_t > ( held ) ] == 1 && mentions ( *this, wanted, held, 0 ) ) {

					last = s;

					break;

				}

				if ( !register_only ( *this, other.right, 0 ) ) {

					last = s;

					break;

				}

				passed.push ( held );
				last = s - 1;

			}

			if ( last < item.first )
				continue;

			statement& line = statements [ static_cast < size_t > ( last ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			bool blocked = false;

			for ( int64_t held : passed )
				blocked = blocked || mentions ( *this, line.right, held, 0 );

			if ( blocked )
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

			if ( ghost_sinks && !owner.flags && exprs [ static_cast < size_t > ( line.right ) ].kind == op::CALL ) {

				vars [ static_cast < size_t > ( v ) ].ghost = true;
				vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

			}

		}

	}

	static bool sign_ops = true;
	static bool sign_copies = true;
	static bool widened_returns = true;
	static bool inline_switches = true;
	static bool sign_pointers = true;

	static void note_signs ( const builder& b, int32_t e, base::list < uint8_t >& marks, int32_t depth ) {

		if ( e < 0 || depth > 64 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
		bool ordered = node.kind == op::LT || node.kind == op::LE || node.kind == op::GT || node.kind == op::GE;

		if ( sign_ops && ( node.kind == op::SHR || node.kind == op::DIV || node.kind == op::MOD ) && ( node.bits & ( SIGNED | UNSIGNED ) ) && !( node.bits & FLOATING ) && node.a >= 0 ) {

			const expr& operand = b.exprs [ static_cast < size_t > ( node.a ) ];

			if ( operand.kind == op::VAR && operand.value >= 0 && operand.value < marks.count ( ) && b.vars [ static_cast < size_t > ( operand.value ) ].size == operand.size && operand.size == 8 )
				marks [ static_cast < size_t > ( operand.value ) ] |= ( node.bits & UNSIGNED ) ? 1 : 2;

		}

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

			if ( widened_returns && long_mode && returns_value && !returns_float && !has_prototype && result_width == 0 && result_text.empty ( ) && item.returned >= 0 ) {

				int32_t back = item.returned;

				while ( back >= 0 && exprs [ static_cast < size_t > ( back ) ].kind == op::CAST )
					back = exprs [ static_cast < size_t > ( back ) ].a;

				if ( back >= 0 && exprs [ static_cast < size_t > ( back ) ].kind == op::VAR ) {

					int64_t v = exprs [ static_cast < size_t > ( back ) ].value;

					if ( v >= 0 && v < marks.count ( ) && vars [ static_cast < size_t > ( v ) ].size == 4 && vars [ static_cast < size_t > ( v ) ].kind == var_kind::REGISTER )
						marks [ static_cast < size_t > ( v ) ] |= 1;

				}

			}

		}

		for ( int32_t round = 0; sign_copies && round < 6; round++ ) {

			bool changed = false;

			for ( const statement& line : statements ) {

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
					continue;

				const expr& left = exprs [ static_cast < size_t > ( line.left ) ];
				const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

				if ( left.kind != op::VAR || right.kind != op::VAR || left.size != 8 || right.size != 8 || left.value < 0 || right.value < 0 || left.value >= marks.count ( ) || right.value >= marks.count ( ) )
					continue;

				uint8_t from = marks [ static_cast < size_t > ( right.value ) ];
				uint8_t& to = marks [ static_cast < size_t > ( left.value ) ];

				if ( from == 1 && to == 0 ) {

					to = 1;
					changed = true;

				}

			}

			if ( !changed )
				break;

		}

		for ( int32_t v = 0; v < vars.count ( ); v++ ) {

			variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( marks [ static_cast < size_t > ( v ) ] == 1 && !( item.bits & FLOATING ) && ( sign_pointers || !( item.bits & POINTER ) ) )
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
			int64_t inline_shift = 0;
			int32_t inline_selector = -1;

			if ( inline_switches && tested >= 0 && picked >= 0 && ( exprs [ static_cast < size_t > ( picked ) ].kind == op::ADD || exprs [ static_cast < size_t > ( picked ) ].kind == op::SUB ) && same ( tested, picked ) ) {

				const expr& offset_expr = exprs [ static_cast < size_t > ( picked ) ];

				if ( offset_expr.a >= 0 && offset_expr.b >= 0 && exprs [ static_cast < size_t > ( offset_expr.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( strip_casts ( *this, offset_expr.a ) ) ].kind == op::VAR ) {

					int64_t k = exprs [ static_cast < size_t > ( offset_expr.b ) ].value;

					inline_shift = offset_expr.kind == op::SUB ? k : -k;
					inline_selector = offset_expr.a;

				}

			}

			if ( inline_selector < 0 && ( tested < 0 || picked < 0 || exprs [ static_cast < size_t > ( tested ) ].kind != op::VAR || exprs [ static_cast < size_t > ( picked ) ].kind != op::VAR || exprs [ static_cast < size_t > ( tested ) ].value != exprs [ static_cast < size_t > ( picked ) ].value ) )
				continue;

			int64_t v = inline_selector >= 0 ? -1 : exprs [ static_cast < size_t > ( picked ) ].value;
			int32_t selector = inline_selector >= 0 ? inline_selector : table.selector;
			int64_t shift = inline_shift;

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

			if ( broken || fallback < 0 || cases.count ( ) < switch_cases )
				continue;

			int32_t distinct = 0;

			for ( int32_t i = 0; i < cases.count ( ); i++ ) {

				bool again = false;

				for ( int32_t j = 0; j < i; j++ )
					again = again || cases [ static_cast < size_t > ( j ) ].to == cases [ static_cast < size_t > ( i ) ].to;

				if ( !again )
					distinct++;

			}

			if ( distinct < switch_cases && cases.count ( ) < 4 )
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
	static bool comma_uses = true;

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

						if ( exprs [ static_cast < size_t > ( line.right ) ].kind != op::CALL ) {

							base::list < uint8_t > seen;
							base::list < int32_t > queue;
							bool reaches = false;

							seen.fill ( static_cast < size_t > ( total ), 0 );
							queue.push ( rest );
							seen [ static_cast < size_t > ( rest ) ] = 1;

							for ( int32_t q = 0; q < queue.count ( ) && !reaches; q++ ) {

								const block_code& at = code [ static_cast < size_t > ( queue [ static_cast < size_t > ( q ) ] ) ];
								int32_t next_blocks [ 2 ] = { at.taken, at.other };

								for ( int32_t n : next_blocks ) {

									if ( n < 0 || n >= total || seen [ static_cast < size_t > ( n ) ] || n == a )
										continue;

									if ( n == joined ) {

										reaches = true;

										break;

									}

									seen [ static_cast < size_t > ( n ) ] = 1;
									queue.push ( n );

								}

							}

							if ( reaches )
								continue;

						}
						base::list < int32_t > seen;

						seen.fill ( vars.size ( ), 0 );
						count_vars ( *this, next.condition, seen, 0 );

						if ( v < 0 || v >= seen.count ( ) || seen [ static_cast < size_t > ( v ) ] < 1 || ( seen [ static_cast < size_t > ( v ) ] != 1 && !comma_uses ) )
							continue;

						bool direct = seen [ static_cast < size_t > ( v ) ] == 1 && tests_directly ( *this, next.condition, v );

						if ( !direct && !comma_uses )
							continue;

						if ( !vars [ static_cast < size_t > ( v ) ].born )
							vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

						if ( !direct )
							next.condition = comma_test ( *this, next.condition, v, line.left, line.right );
						else if ( exprs [ static_cast < size_t > ( line.right ) ].kind == op::CALL )
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

	static bool guard_defs = true;

	void builder::guard_expressions ( ) {

		if ( !guard_defs )
			return;

		int32_t total = vars.count ( );
		base::list < int32_t > writes;
		base::list < int32_t > def;

		writes.fill ( static_cast < size_t > ( total ), 0 );
		def.fill ( static_cast < size_t > ( total ), -1 );

		for ( int32_t s = 0; s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v >= 0 && v < total ) {

				writes [ static_cast < size_t > ( v ) ]++;
				def [ static_cast < size_t > ( v ) ] = s;

			}

		}

		auto pure = [ & ] ( auto& self, int32_t e, int32_t depth ) -> bool {

			if ( e < 0 )
				return true;

			if ( depth > 12 )
				return false;

			const expr& node = exprs [ static_cast < size_t > ( e ) ];

			switch ( node.kind ) {

				case op::CONST:
					return !( node.bits & POINTER );

				case op::VAR: {

					if ( node.value < 0 || node.value >= total )
						return false;

					const variable& item = vars [ static_cast < size_t > ( node.value ) ];

					if ( item.kind == var_kind::STACK || ( item.bits & FLOATING ) )
						return false;

					return writes [ static_cast < size_t > ( node.value ) ] == ( item.kind == var_kind::PARAMETER ? 0 : 1 );

				}

				case op::ADD:
				case op::SUB:
				case op::AND:
				case op::OR:
				case op::XOR:
				case op::SHL:
				case op::SHR:
				case op::CAST:
					return node.arg_count == 0 && self ( self, node.a, depth + 1 ) && self ( self, node.b, depth + 1 );

				default:
					return false;

			}

		};

		base::list < base::list < int32_t > > preds;

		preds.resize ( static_cast < size_t > ( code.count ( ) ) );

		for ( int32_t i = 0; i < code.count ( ); i++ ) {

			const block_code& from = code [ static_cast < size_t > ( i ) ];

			if ( !from.reachable || from.end == term::RETURN || from.end == term::TAIL || from.end == term::STOP )
				continue;

			if ( from.taken >= 0 && from.taken < code.count ( ) )
				preds [ static_cast < size_t > ( from.taken ) ].push ( i );

			if ( from.end == term::BRANCH && from.other >= 0 && from.other < code.count ( ) && from.other != from.taken )
				preds [ static_cast < size_t > ( from.other ) ].push ( i );

		}

		auto last_write = [ & ] ( int32_t at, int32_t v ) -> int32_t {

			const block_code& item = code [ static_cast < size_t > ( at ) ];

			for ( int32_t s = item.first + item.count - 1; s >= item.first; s-- ) {

				const statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == v )
					return s;

			}

			return -1;

		};

		auto reaching = [ & ] ( int32_t block, int32_t v ) -> int32_t {

			int32_t own = last_write ( block, v );

			if ( own >= 0 )
				return own;

			base::list < uint8_t > seen;
			base::list < int32_t > work;
			int32_t found = -1;

			seen.fill ( static_cast < size_t > ( code.count ( ) ), 0 );
			seen [ static_cast < size_t > ( block ) ] = 1;

			for ( int32_t p : preds [ static_cast < size_t > ( block ) ] )
				work.push ( p );

			int32_t steps = 0;

			while ( !work.empty ( ) ) {

				int32_t at = work.back ( );

				work.pop ( );

				if ( seen [ static_cast < size_t > ( at ) ] )
					continue;

				seen [ static_cast < size_t > ( at ) ] = 1;

				if ( ++steps > 64 )
					return -1;

				int32_t write = last_write ( at, v );

				if ( write >= 0 ) {

					if ( found >= 0 && found != write )
						return -1;

					found = write;

					continue;

				}

				if ( preds [ static_cast < size_t > ( at ) ].empty ( ) )
					return -1;

				for ( int32_t p : preds [ static_cast < size_t > ( at ) ] )
					work.push ( p );

			}

			return found;

		};

		for ( int32_t index = 0; index < code.count ( ); index++ ) {

			block_code& block = code [ static_cast < size_t > ( index ) ];

			if ( block.end != term::BRANCH || block.condition < 0 )
				continue;

			const expr& test = exprs [ static_cast < size_t > ( block.condition ) ];

			if ( ( test.kind != op::NE && test.kind != op::EQ ) || test.a < 0 || test.b < 0 )
				continue;

			int32_t subject = test.a;

			while ( subject >= 0 && exprs [ static_cast < size_t > ( subject ) ].kind == op::CAST )
				subject = exprs [ static_cast < size_t > ( subject ) ].a;

			if ( subject < 0 )
				continue;

			const expr& left = exprs [ static_cast < size_t > ( subject ) ];
			const expr& right = exprs [ static_cast < size_t > ( test.b ) ];

			if ( left.kind != op::VAR || right.kind != op::CONST || right.value != 0 || left.value < 0 || left.value >= total )
				continue;

			int32_t v = static_cast < int32_t > ( left.value );

			if ( vars [ static_cast < size_t > ( v ) ].kind != var_kind::REGISTER )
				continue;

			int32_t found = reaching ( index, v );

			if ( found < 0 )
				continue;

			const statement& line = statements [ static_cast < size_t > ( found ) ];

			if ( line.right < 0 )
				continue;

			int32_t core = line.right;

			while ( core >= 0 && exprs [ static_cast < size_t > ( core ) ].kind == op::CAST )
				core = exprs [ static_cast < size_t > ( core ) ].a;

			if ( core < 0 || exprs [ static_cast < size_t > ( core ) ].kind != op::AND || !pure ( pure, line.right, 0 ) )
				continue;

			block.condition = substitute ( *this, block.condition, v, line.right, 0 );

		}

	}

}
