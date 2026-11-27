#include "decompiler/lift/fold_constants/fold_constants.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::builder::fold_constants ( ) {

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