#include "decompiler/lift/inline_temps/inline_temps.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::builder::inline_temps ( ) {

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

			bool cast_param = false;

			if ( param_copies && source.kind == op::CAST && source.a >= 0 && !( source.bits & FLOATING ) && exprs [ static_cast < size_t > ( source.a ) ].kind == op::VAR ) {

				int64_t p = exprs [ static_cast < size_t > ( source.a ) ].value;

				cast_param = p >= 0 && p < total && p != v && vars [ static_cast < size_t > ( p ) ].kind == var_kind::PARAMETER && fixed ( p ) && !( vars [ static_cast < size_t > ( p ) ].bits & FLOATING ) && !( vars [ static_cast < size_t > ( v ) ].bits & FLOATING ) && ( !long_mode || vars [ static_cast < size_t > ( p ) ].position < 4 );

			}

			if ( !cast_param && ( source.kind != op::VAR || source.value == v || source.size != exprs [ static_cast < size_t > ( line.left ) ].size || vars [ static_cast < size_t > ( source.value ) ].size != vars [ static_cast < size_t > ( v ) ].size || ( ( vars [ static_cast < size_t > ( source.value ) ].bits ^ vars [ static_cast < size_t > ( v ) ].bits ) & FLOATING ) ) )
				continue;

			if ( cast_param ) {

				int32_t value = line.right;
				int64_t p = exprs [ static_cast < size_t > ( source.a ) ].value;

				line.kind = stmt::NOP;
				uses [ static_cast < size_t > ( p ) ] += uses [ static_cast < size_t > ( v ) ] - 1;
				uses [ static_cast < size_t > ( v ) ] = 0;
				defs [ static_cast < size_t > ( v ) ] = 0;
				vars [ static_cast < size_t > ( v ) ].ghost = true;
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

				continue;

			}

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