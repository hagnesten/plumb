#include "decompiler/emit/increments/increments.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

int64_t decompiler::emitter::var_step ( int32_t v ) const {

	if ( v < 0 || v >= element.count ( ) || !element [ static_cast < size_t > ( v ) ] )
		return 1;

	uint8_t shape = element [ static_cast < size_t > ( v ) ];

	if ( shape == 0x10 )
		return 1;

	if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) )
		return 8;

	return shape & 0x0F ? shape & 0x0F : 1;

}
int32_t decompiler::emitter::count_var ( int32_t e, int64_t v, int32_t depth ) const {

	if ( e < 0 || depth > 64 )
		return 0;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
	int32_t n = node.kind == op::VAR && node.value == v ? 1 : 0;

	n += count_var ( node.a, v, depth + 1 ) + count_var ( node.b, v, depth + 1 ) + count_var ( node.c, v, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		n += count_var ( b.args [ static_cast < size_t > ( node.first_arg + i ) ], v, depth + 1 );

	return n;

}
bool decompiler::emitter::count_call ( int32_t e, int32_t depth ) const {

	if ( e < 0 || depth > 64 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::CALL || node.kind == op::INTRINSIC )
		return true;

	return count_call ( node.a, depth + 1 ) || count_call ( node.b, depth + 1 ) || count_call ( node.c, depth + 1 );

}
void decompiler::emitter::plan_increments ( ) {

	fold_at.clear ( );
	fold_how.clear ( );
	fold_cond.clear ( );
	fold_cond_var.clear ( );
	fold_cond_how.clear ( );
	fold_at.fill ( b.statements.size ( ), -1 );
	fold_how.fill ( b.statements.size ( ), 0 );
	fold_inc.clear ( );
	fold_done.clear ( );
	fold_cond_inc.clear ( );
	fold_cond_done.clear ( );
	fold_inc.fill ( b.statements.size ( ), -1 );
	fold_done.fill ( b.statements.size ( ), 0 );
	fold_unit.clear ( );
	fold_cond_unit.clear ( );
	fold_unit.fill ( b.statements.size ( ), 1 );

	if ( !fold_increments )
		return;

	if ( hidden.count ( ) < b.statements.count ( ) )
		hidden.fill ( b.statements.size ( ) - hidden.size ( ), 0 );

	base::list < uint8_t > flag_read;

	flag_read.fill ( b.vars.size ( ), 0 );

	auto mark_reads = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 64 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::VAR && node.value >= 0 && node.value < b.vars.count ( ) )
			flag_read [ static_cast < size_t > ( node.value ) ] = 1;

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const statement& line : b.statements ) {

		if ( line.kind == stmt::NOP )
			continue;

		mark_reads ( mark_reads, line.right, 0 );

		if ( !( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
			mark_reads ( mark_reads, line.left, 0 );

	}

	for ( const block_code& block : b.code ) {

		mark_reads ( mark_reads, block.condition, 0 );
		mark_reads ( mark_reads, block.selector, 0 );
		mark_reads ( mark_reads, block.returned, 0 );

	}

	for ( int32_t k = 0; k < count; k++ ) {

		const block_code& block = b.code [ static_cast < size_t > ( k ) ];

		if ( !block.reachable )
			continue;

		base::list < int32_t > lines;

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ].flags && !flag_read [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ] )
				continue;

			lines.push ( s );

		}

		bool latch = false;
		int32_t next_blocks [ 2 ] = { block.taken, block.end == term::BRANCH ? block.other : -1 };

		for ( int32_t n : next_blocks ) {

			if ( n >= 0 && n < count && header [ static_cast < size_t > ( n ) ] && b.graph.blocks [ static_cast < size_t > ( n ) ].start <= b.graph.blocks [ static_cast < size_t > ( k ) ].start )
				latch = true;

		}

		for ( int32_t t = 0; t < lines.count ( ); t++ ) {

			int32_t at = lines [ static_cast < size_t > ( t ) ];
			const statement& line = b.statements [ static_cast < size_t > ( at ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int64_t x = b.exprs [ static_cast < size_t > ( line.left ) ].value;
			const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

			if ( x < 0 || x >= b.vars.count ( ) || b.vars [ static_cast < size_t > ( x ) ].kind != var_kind::REGISTER || ( right.bits & FLOATING ) || right.kind != op::ADD || right.a < 0 || right.b < 0 )
				continue;

			const expr& self_node = b.exprs [ static_cast < size_t > ( right.a ) ];
			const expr& amount = b.exprs [ static_cast < size_t > ( right.b ) ];

			if ( self_node.kind != op::VAR || self_node.value != x || amount.kind != op::CONST )
				continue;

			int64_t unit = amount.value < 0 ? -amount.value : amount.value;

			if ( unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16 )
				continue;

			bool up = amount.value > 0;
			bool last_line = t == lines.count ( ) - 1;

			int32_t back = t - 1;

			while ( skip_increments && back > 0 && back >= t - 2 ) {

				const statement& gap = b.statements [ static_cast < size_t > ( lines [ static_cast < size_t > ( back ) ] ) ];

				if ( gap.kind != stmt::ASSIGN || gap.left < 0 || gap.right < 0 || b.exprs [ static_cast < size_t > ( gap.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( gap.left ) ].value == x || count_var ( gap.right, x, 0 ) || count_call ( gap.right, 0 ) || fold_at [ static_cast < size_t > ( lines [ static_cast < size_t > ( back ) ] ) ] >= 0 )
					break;

				back--;

			}

			if ( back != t - 1 && b.statements [ static_cast < size_t > ( lines [ static_cast < size_t > ( back ) ] ) ].kind != stmt::STORE )
				back = t - 1;

			if ( t > 0 ) {

				int32_t before = lines [ static_cast < size_t > ( back ) ];
				const statement& prior = b.statements [ static_cast < size_t > ( before ) ];
				bool defines = prior.kind == stmt::ASSIGN && prior.left >= 0 && b.exprs [ static_cast < size_t > ( prior.left ) ].kind == op::VAR;
				int32_t uses = count_var ( prior.right, x, 0 ) + ( defines ? 0 : count_var ( prior.left, x, 0 ) );
				int32_t loaded = prior.right;

				while ( loaded >= 0 && b.exprs [ static_cast < size_t > ( loaded ) ].kind == op::CAST )
					loaded = b.exprs [ static_cast < size_t > ( loaded ) ].a;

				bool memory = !defines || ( loaded >= 0 && b.exprs [ static_cast < size_t > ( loaded ) ].kind == op::LOAD );
				bool calls = prior.right < 0 || count_call ( prior.right, 0 ) || ( !defines && count_call ( prior.left, 0 ) );

				auto offset_use = [ & ] ( auto& self, int32_t e, int32_t depth ) -> bool {

					if ( e < 0 || depth > 64 )
						return false;

					const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

					if ( node.kind == op::LOAD && node.a >= 0 ) {

						const expr& where = b.exprs [ static_cast < size_t > ( node.a ) ];

						if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.a ) ].value == x && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( where.b ) ].value )
							return true;

					}

					return self ( self, node.a, depth + 1 ) || self ( self, node.b, depth + 1 ) || self ( self, node.c, depth + 1 );

				};

				if ( offset_folds && ( offset_use ( offset_use, prior.right, 0 ) || ( !defines && offset_use ( offset_use, prior.left, 0 ) ) ) )
					calls = true;

				if ( ( !( last_line && latch ) || ( memory && latch_folds ) ) && !calls && ( prior.kind == stmt::ASSIGN || prior.kind == stmt::STORE ) && uses == 1 && fold_at [ static_cast < size_t > ( before ) ] < 0 && !hidden [ static_cast < size_t > ( before ) ] && !( defines && b.exprs [ static_cast < size_t > ( prior.left ) ].value == x ) ) {

					bool pre = false;

					if ( pre_index_folds && defines && up ) {

						int32_t found = -1;
						int32_t inner = -1;

						auto scaled = [ & ] ( int32_t e ) -> int64_t {

							if ( e < 0 )
								return 0;

							const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

							if ( node.kind == op::VAR && node.value == x )
								return 1;

							if ( node.kind == op::MUL && node.a >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( node.a ) ].value == x && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST )
								return b.exprs [ static_cast < size_t > ( node.b ) ].value;

							if ( node.kind == op::MUL && node.a >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( node.b ) ].value == x && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::CONST )
								return b.exprs [ static_cast < size_t > ( node.a ) ].value;

							if ( node.kind == op::SHL && node.a >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( node.a ) ].value == x && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( node.b ) ].value < 5 )
								return 1ll << b.exprs [ static_cast < size_t > ( node.b ) ].value;

							return 0;

						};

						auto seek = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

							if ( e < 0 || depth > 32 || found >= 0 )
								return;

							const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

							if ( node.kind == op::LOAD && node.a >= 0 ) {

								const expr& where = b.exprs [ static_cast < size_t > ( node.a ) ];

								if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

									const expr& sum = b.exprs [ static_cast < size_t > ( where.a ) ];
									int64_t step = sum.kind == op::ADD && sum.b >= 0 ? scaled ( sum.b ) : 0;

									if ( !step && sum.kind == op::ADD && sum.a >= 0 )
										step = scaled ( sum.a );

									if ( step > 0 && step == node.size && b.exprs [ static_cast < size_t > ( where.b ) ].value == step * unit ) {

										found = e;
										inner = where.a;

										return;

									}

								}

								if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::ADD ) {

									const expr& head = b.exprs [ static_cast < size_t > ( where.a ) ];
									int64_t step = scaled ( where.b );

									if ( step > 0 && step == node.size && head.a >= 0 && head.b >= 0 && b.exprs [ static_cast < size_t > ( head.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( head.b ) ].value == step * unit ) {

										expr joined = where;

										joined.a = head.a;
										b.exprs.push ( joined );
										found = e;
										inner = b.exprs.count ( ) - 1;

										return;

									}

								}

								if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::ADD ) {

									const expr& tail = b.exprs [ static_cast < size_t > ( where.b ) ];
									int64_t step = tail.a >= 0 ? scaled ( tail.a ) : 0;

									if ( step > 0 && step == node.size && tail.b >= 0 && b.exprs [ static_cast < size_t > ( tail.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( tail.b ) ].value == step * unit ) {

										expr joined = where;

										joined.b = tail.a;
										b.exprs.push ( joined );
										found = e;
										inner = b.exprs.count ( ) - 1;

										return;

									}

								}

							}

							self ( self, node.a, depth + 1 );
							self ( self, node.b, depth + 1 );
							self ( self, node.c, depth + 1 );

						};

						seek ( seek, prior.right, 0 );

						if ( found >= 0 ) {

							expr moved = b.exprs [ static_cast < size_t > ( found ) ];

							moved.a = inner;
							b.exprs.push ( moved );

							int32_t replacement = b.exprs.count ( ) - 1;

							auto rebuild = [ & ] ( auto& self, int32_t e, int32_t depth ) -> int32_t {

								if ( e < 0 || depth > 32 )
									return e;

								if ( e == found )
									return replacement;

								expr copy = b.exprs [ static_cast < size_t > ( e ) ];
								int32_t na = self ( self, copy.a, depth + 1 );
								int32_t nb = self ( self, copy.b, depth + 1 );
								int32_t nc = self ( self, copy.c, depth + 1 );

								if ( na == copy.a && nb == copy.b && nc == copy.c )
									return e;

								copy.a = na;
								copy.b = nb;
								copy.c = nc;
								b.exprs.push ( copy );

								return b.exprs.count ( ) - 1;

							};

							b.statements [ static_cast < size_t > ( before ) ].right = rebuild ( rebuild, prior.right, 0 );
							pre = true;

						}

					}

					fold_at [ static_cast < size_t > ( before ) ] = static_cast < int32_t > ( x );
					fold_how [ static_cast < size_t > ( before ) ] = pre ? 1 : ( up ? 3 : 4 );
					fold_inc [ static_cast < size_t > ( before ) ] = at;
					fold_unit [ static_cast < size_t > ( before ) ] = unit;
					hidden [ static_cast < size_t > ( at ) ] = 1;

					continue;

				}

			}

			if ( last_line && !latch && block.end == term::BRANCH && block.condition >= 0 && count_var ( block.condition, x, 0 ) == 1 ) {

				fold_cond.push ( block.condition );
				fold_cond_var.push ( static_cast < int32_t > ( x ) );
				fold_cond_how.push ( up ? 1 : 2 );
				fold_cond_inc.push ( at );
				fold_cond_unit.push ( unit );
				fold_cond_done.push ( 0 );
				hidden [ static_cast < size_t > ( at ) ] = 1;

			}

		}

	}

}
bool decompiler::emitter::unfold_missed ( ) {

	bool missed = false;

	for ( int32_t s = 0; s < fold_at.count ( ); s++ ) {

		if ( fold_at [ static_cast < size_t > ( s ) ] < 0 || fold_done [ static_cast < size_t > ( s ) ] )
			continue;

		hidden [ static_cast < size_t > ( fold_inc [ static_cast < size_t > ( s ) ] ) ] = 0;
		fold_at [ static_cast < size_t > ( s ) ] = -1;
		missed = true;

	}

	for ( int32_t k = 0; k < fold_cond.count ( ); k++ ) {

		if ( fold_cond [ static_cast < size_t > ( k ) ] < 0 || fold_cond_done [ static_cast < size_t > ( k ) ] )
			continue;

		hidden [ static_cast < size_t > ( fold_cond_inc [ static_cast < size_t > ( k ) ] ) ] = 0;
		fold_cond [ static_cast < size_t > ( k ) ] = -1;
		missed = true;

	}

	return missed;

}