#include "decompiler/emit/arrays/arrays.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

void decompiler::stack_arrays ( builder& b, base::list < int32_t >& array_of, base::list < int32_t >& array_step, base::list < int32_t >& array_count ) {

	int32_t total = b.vars.count ( );
	base::list < int32_t > reads;
	base::list < uint8_t > addressed;
	base::list < uint8_t > accessed;

	array_of.clear ( );
	array_step.clear ( );
	array_count.clear ( );
	array_of.fill ( static_cast < size_t > ( total ), -1 );
	array_step.fill ( static_cast < size_t > ( total ), 0 );
	array_count.fill ( static_cast < size_t > ( total ), 0 );

	if ( !array_vars )
		return;

	reads.fill ( static_cast < size_t > ( total ), 0 );
	addressed.fill ( static_cast < size_t > ( total ), 0 );
	accessed.fill ( static_cast < size_t > ( total ), 0 );

	auto slot_of = [ & ] ( int32_t e ) -> int32_t {

		if ( e < 0 )
			return -1;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind != op::ADDRESS )
			return -1;

		int64_t v = node.a < 0 ? node.value : ( b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( node.a ) ].value : -1 );

		return v >= 0 && v < total ? static_cast < int32_t > ( v ) : -1;

	};

	base::list < int32_t > index_step;

	index_step.fill ( static_cast < size_t > ( total ), 0 );

	auto note_index = [ & ] ( int32_t load ) {

		if ( !indexed_arrays || load < 0 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( load ) ];

		if ( node.kind != op::LOAD || node.a < 0 || ( node.bits & FLOATING ) )
			return;

		const expr& sum = b.exprs [ static_cast < size_t > ( node.a ) ];

		if ( sum.kind != op::ADD || sum.a < 0 || sum.b < 0 )
			return;

		for ( int32_t k = 0; k < 2; k++ ) {

			int32_t base_side = k ? sum.b : sum.a;
			int32_t scaled_side = k ? sum.a : sum.b;
			int32_t v = slot_of ( base_side );

			if ( v < 0 )
				continue;

			const expr& scaled = b.exprs [ static_cast < size_t > ( scaled_side ) ];
			int64_t scale = 0;

			if ( scaled.kind == op::MUL && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST )
				scale = b.exprs [ static_cast < size_t > ( scaled.b ) ].value;
			else if ( scaled.kind == op::SHL && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value < 4 )
				scale = 1ll << b.exprs [ static_cast < size_t > ( scaled.b ) ].value;
			else if ( scaled.kind != op::CONST && node.size == 1 )
				scale = 1;

			int32_t& slot = index_step [ static_cast < size_t > ( v ) ];

			if ( scale != node.size || ( scale != 1 && scale != 2 && scale != 4 && scale != 8 ) )
				slot = -1;
			else if ( slot == 0 )
				slot = static_cast < int32_t > ( scale );
			else if ( slot != scale )
				slot = -1;

		}

	};

	auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 96 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::VAR && node.value >= 0 && node.value < total ) {

			reads [ static_cast < size_t > ( node.value ) ]++;
			accessed [ static_cast < size_t > ( node.value ) ] = 1;

			return;

		}

		if ( node.kind == op::LOAD && slot_of ( node.a ) >= 0 ) {

			reads [ static_cast < size_t > ( slot_of ( node.a ) ) ]++;
			accessed [ static_cast < size_t > ( slot_of ( node.a ) ) ] = 1;

			return;

		}

		if ( node.kind == op::LOAD )
			note_index ( e );

		if ( node.kind == op::ADDRESS && slot_of ( e ) >= 0 ) {

			addressed [ static_cast < size_t > ( slot_of ( e ) ) ] = 1;

			return;

		}

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const block_code& block : b.code ) {

		if ( !block.reachable )
			continue;

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			walk ( walk, line.right, 0 );

			if ( line.left < 0 )
				continue;

			const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];

			if ( left.kind == op::VAR && left.value >= 0 && left.value < total )
				accessed [ static_cast < size_t > ( left.value ) ] = 1;
			else if ( left.kind == op::LOAD && slot_of ( left.a ) >= 0 )
				accessed [ static_cast < size_t > ( slot_of ( left.a ) ) ] = 1;
			else {

				if ( left.kind == op::LOAD )
					note_index ( line.left );

				walk ( walk, line.left, 0 );

			}

		}

		walk ( walk, block.condition, 0 );
		walk ( walk, block.selector, 0 );
		walk ( walk, block.returned, 0 );

	}

	base::list < int32_t > stack;

	for ( int32_t v = 0; v < total; v++ ) {

		if ( b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::STACK && ( accessed [ static_cast < size_t > ( v ) ] || addressed [ static_cast < size_t > ( v ) ] ) )
			stack.push ( v );

	}

	base::sort ( stack.data ( ), stack.count ( ), [ & ] ( int32_t x, int32_t y ) { return b.vars [ static_cast < size_t > ( x ) ].offset < b.vars [ static_cast < size_t > ( y ) ].offset; } );

	for ( int32_t i = 0; i < stack.count ( ); i++ ) {

		int32_t head = stack [ static_cast < size_t > ( i ) ];
		const variable& item = b.vars [ static_cast < size_t > ( head ) ];

		if ( !addressed [ static_cast < size_t > ( head ) ] || item.cookie || item.saved || head == b.va_var || array_of [ static_cast < size_t > ( head ) ] >= 0 || ( head < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( head ) ].empty ( ) ) )
			continue;

		int64_t from_rbp = frame_deepest ? ( b.has_frame ? item.offset - b.frame_at : item.offset ) : item.offset + b.pushed;
		int64_t end = item.offset - from_rbp;

		if ( from_rbp >= 0 )
			continue;

		int32_t last = i;

		for ( int32_t j = i + 1; j < stack.count ( ); j++ ) {

			const variable& other = b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ];

			if ( other.offset >= end )
				break;

			if ( reads [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ] || addressed [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ] || other.offset + other.size > end ) {

				end = other.offset;

				break;

			}

			last = j;

		}

		int64_t span = end - item.offset;

		if ( span <= item.size || span > 0x10000 )
			continue;

		int32_t step = accessed [ static_cast < size_t > ( head ) ] ? item.size : 0;
		bool uniform = true;

		for ( int32_t j = i + 1; j <= last; j++ ) {

			const variable& other = b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ];

			if ( !step )
				step = other.size;

			if ( other.size != step || ( other.offset - item.offset ) % step != 0 )
				uniform = false;

		}

		if ( !uniform && accessed [ static_cast < size_t > ( head ) ] )
			continue;

		if ( last == i && accessed [ static_cast < size_t > ( head ) ] && item.size == 16 )
			continue;

		int32_t text_step = 0;

		if ( head < b.call_names.count ( ) && !b.call_names [ static_cast < size_t > ( head ) ].empty ( ) && !( buffer_arrays && head < b.call_types.count ( ) && buffer_element ( b.call_types [ static_cast < size_t > ( head ) ].get ( ), text_step ) ) )
			continue;

		if ( indexed_arrays && index_step [ static_cast < size_t > ( head ) ] > 1 && ( !step || !accessed [ static_cast < size_t > ( head ) ] ) ) {

			int32_t wanted = index_step [ static_cast < size_t > ( head ) ];
			bool fits = !accessed [ static_cast < size_t > ( head ) ] || item.size % wanted == 0;

			for ( int32_t j = i + 1; j <= last && fits; j++ ) {

				const variable& other = b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ];

				if ( ( other.offset - item.offset ) % wanted != 0 || other.size % wanted != 0 )
					fits = false;

			}

			if ( fits ) {

				step = wanted;
				uniform = true;

			}

		}

		if ( !uniform && array_min_step ) {

			int32_t smallest = accessed [ static_cast < size_t > ( head ) ] ? item.size : 64;

			for ( int32_t j = i + 1; j <= last; j++ )
				smallest = base::min ( smallest, static_cast < int32_t > ( b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ].size ) );

			bool aligned = smallest == 2 || smallest == 4 || smallest == 8;

			for ( int32_t j = i + 1; j <= last && aligned; j++ ) {

				const variable& other = b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ];

				if ( ( other.offset - item.offset ) % smallest != 0 || other.size % smallest != 0 )
					aligned = false;

			}

			if ( aligned ) {

				step = smallest;
				uniform = true;

			}

		}

		if ( text_step ) {

			step = text_step;
			uniform = true;

		}

		if ( !uniform || !step || ( step != 1 && step != 2 && step != 4 && step != 8 && step != 16 ) || ( ( item.bits & FLOATING ) && step != 16 ) )
			step = 1;

		span -= span % step;

		if ( text_step && head < b.call_counts.count ( ) && b.call_counts [ static_cast < size_t > ( head ) ] > 0 && static_cast < int64_t > ( b.call_counts [ static_cast < size_t > ( head ) ] ) * step <= span )
			span = static_cast < int64_t > ( b.call_counts [ static_cast < size_t > ( head ) ] ) * step;

		array_of [ static_cast < size_t > ( head ) ] = head;
		array_step [ static_cast < size_t > ( head ) ] = step;
		array_count [ static_cast < size_t > ( head ) ] = static_cast < int32_t > ( span / step );

		for ( int32_t j = i + 1; j <= last; j++ )
			array_of [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ] = head;

	}

}

void decompiler::stack_memsets ( builder& b, const base::list < int32_t >& array_of ) {

	if ( !array_memsets )
		return;

	int32_t total = b.vars.count ( );

	auto target_of = [ & ] ( const statement& line, int32_t& var, uint8_t& size ) -> bool {

		if ( ( line.kind != stmt::ASSIGN && line.kind != stmt::STORE ) || line.left < 0 || line.right < 0 )
			return false;

		const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

		if ( right.kind != op::CONST || right.value != 0 || ( right.bits & POINTER ) )
			return false;

		const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];

		if ( left.kind == op::VAR ) {

			var = static_cast < int32_t > ( left.value );
			size = static_cast < uint8_t > ( left.size );

		} else if ( left.kind == op::LOAD && left.a >= 0 && b.exprs [ static_cast < size_t > ( left.a ) ].kind == op::ADDRESS ) {

			const expr& where = b.exprs [ static_cast < size_t > ( left.a ) ];

			var = static_cast < int32_t > ( where.a < 0 ? where.value : ( b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( where.a ) ].value : -1 ) );
			size = static_cast < uint8_t > ( left.size );

		} else
			return false;

		return var >= 0 && var < total && var < array_of.count ( ) && array_of [ static_cast < size_t > ( var ) ] >= 0;

	};

	for ( const block_code& block : b.code ) {

		if ( !block.reachable )
			continue;

		int32_t head = -1;
		int64_t low = 0;
		int64_t high = 0;
		int32_t first_line = -1;
		int32_t low_var = -1;
		base::list < int32_t > run;

		auto close = [ & ] ( ) {

			if ( run.count ( ) >= 2 && high - low >= 24 && first_line >= 0 ) {

				int32_t place = b.make ( op::ADDRESS, 8, -1, -1, low_var, POINTER );
				int32_t zero = b.make ( op::CONST, 4, -1, -1, 0, 0 );
				int32_t amount = b.make ( op::CONST, 4, -1, -1, high - low, 0 );
				int32_t call = b.make ( op::INTRINSIC, 8, -1, -1, 1, 0 );
				int32_t first = b.args.count ( );

				b.args.push ( place );
				b.args.push ( zero );
				b.args.push ( amount );
				b.exprs [ static_cast < size_t > ( call ) ].first_arg = first;
				b.exprs [ static_cast < size_t > ( call ) ].arg_count = 3;

				for ( int32_t s : run )
					b.statements [ static_cast < size_t > ( s ) ].kind = stmt::NOP;

				statement& kept = b.statements [ static_cast < size_t > ( first_line ) ];

				kept.kind = stmt::EXPR;
				kept.left = -1;
				kept.right = call;

			}

			run.clear ( );
			head = -1;
			first_line = -1;

		};

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			int32_t var = -1;
			uint8_t size = 0;

			if ( !target_of ( line, var, size ) ) {

				close ( );

				continue;

			}

			int32_t owner = array_of [ static_cast < size_t > ( var ) ];
			int64_t from = b.vars [ static_cast < size_t > ( var ) ].offset - b.vars [ static_cast < size_t > ( owner ) ].offset;

			if ( head == owner && from == high ) {

				high += size;
				run.push ( s );

				continue;

			}

			if ( head == owner && from + size == low ) {

				low = from;
				low_var = var;
				run.push ( s );

				continue;

			}

			close ( );
			head = owner;
			low = from;
			high = from + size;
			low_var = var;
			first_line = s;
			run.push ( s );

		}

		close ( );

	}

}