#include "decompiler/lift/facts/facts.h"
#include "base/strings/strings.h"

bool decompiler::subject_of ( const builder& b, int32_t e, int32_t& var, uint8_t& width ) {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::VAR ) {

		var = static_cast < int32_t > ( node.value );
		width = node.size;

		return true;

	}

	if ( node.kind == op::CAST && node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR && node.size < b.exprs [ static_cast < size_t > ( node.a ) ].size ) {

		var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( node.a ) ].value );
		width = node.size;

		return true;

	}

	return false;

}

bool decompiler::fact_for ( const builder& b, int32_t condition, bool taken, fact& out ) {

	if ( condition < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( condition ) ];

	if ( node.kind == op::LNOT && taken )
		return subject_of ( b, node.a, out.var, out.width ) && ( out.value = 0, true );

	if ( ( node.kind == op::EQ && taken ) || ( node.kind == op::NE && !taken ) ) {

		if ( node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && subject_of ( b, node.a, out.var, out.width ) ) {

			out.value = b.exprs [ static_cast < size_t > ( node.b ) ].value;

			return true;

		}

		return false;

	}

	if ( !taken && subject_of ( b, condition, out.var, out.width ) ) {

		out.value = 0;

		return true;

	}

	return false;

}

int64_t decompiler::mask_to_size ( int64_t value, uint8_t size ) {

	if ( size >= 8 )
		return value;

	uint64_t mask = ( 1ull << ( size * 8 ) ) - 1;

	return static_cast < int64_t > ( static_cast < uint64_t > ( value ) & mask );

}

bool decompiler::low_bits_only ( op kind ) {

	return kind == op::ADD || kind == op::SUB || kind == op::MUL || kind == op::AND || kind == op::OR || kind == op::XOR || kind == op::NOT || kind == op::NEG || kind == op::SHL || kind == op::CAST;

}

int32_t decompiler::fact_rewriter::constant ( int64_t value, uint8_t size, uint8_t bits ) {

	int32_t made = b.constant ( mask_to_size ( value, size ), size );

	b.exprs [ static_cast < size_t > ( made ) ].bits = static_cast < uint8_t > ( bits & ( SIGNED | UNSIGNED ) );

	return made;

}

int32_t decompiler::fact_rewriter::fold ( int32_t e ) {

	const expr node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::CAST && node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::CONST )
		return constant ( b.exprs [ static_cast < size_t > ( node.a ) ].value, node.size, node.bits );

	if ( ( node.kind == op::NOT || node.kind == op::NEG ) && node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::CONST ) {

		int64_t inner = b.exprs [ static_cast < size_t > ( node.a ) ].value;

		return constant ( node.kind == op::NOT ? ~inner : -inner, node.size, node.bits );

	}

	if ( node.a < 0 || node.b < 0 )
		return e;

	const expr left = b.exprs [ static_cast < size_t > ( node.a ) ];
	const expr right = b.exprs [ static_cast < size_t > ( node.b ) ];

	if ( left.kind != op::CONST || right.kind != op::CONST )
		return e;

	int64_t x = left.value;
	int64_t y = right.value;

	switch ( node.kind ) {

		case op::ADD: return constant ( x + y, node.size, node.bits );
		case op::SUB: return constant ( x - y, node.size, node.bits );
		case op::MUL: return constant ( x * y, node.size, node.bits );
		case op::AND: return constant ( x & y, node.size, node.bits );
		case op::OR: return constant ( x | y, node.size, node.bits );
		case op::XOR: return constant ( x ^ y, node.size, node.bits );
		case op::SHL: return y >= 0 && y < 64 ? constant ( x << y, node.size, node.bits ) : e;
		default: return e;

	}

}

int32_t decompiler::fact_rewriter::rewrite ( int32_t e, uint8_t limit, int32_t depth ) {

	if ( e < 0 || depth > 48 )
		return e;

	const expr node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::VAR && node.value == known.var ) {

		if ( node.size <= known.width || ( limit && limit <= known.width ) ) {

			changed++;

			return constant ( known.value, node.size, node.bits );

		}

		return e;

	}

	uint8_t next_limit = 0;

	if ( node.kind == op::CAST && node.size <= known.width )
		next_limit = node.size;
	else if ( limit && low_bits_only ( node.kind ) )
		next_limit = limit;

	int32_t a = rewrite ( node.a, next_limit, depth + 1 );
	int32_t c = rewrite ( node.b, next_limit, depth + 1 );
	int32_t third = rewrite ( node.c, 0, depth + 1 );
	bool any = a != node.a || c != node.b || third != node.c;
	base::list < int32_t > list;

	for ( int32_t i = 0; i < node.arg_count; i++ ) {

		int32_t old = b.args [ static_cast < size_t > ( node.first_arg + i ) ];
		int32_t made = rewrite ( old, 0, depth + 1 );

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

	return fold ( b.exprs.count ( ) - 1 );

}

void decompiler::builder::substitute_known ( ) {

	int32_t count = code.count ( );

	if ( count < 2 || count > 3000 || count != graph.blocks.count ( ) )
		return;

	base::list < base::list < int32_t > > preds;

	preds.resize ( static_cast < size_t > ( count ) );

	for ( const analysis::edge& link : graph.edges ) {

		if ( link.from >= 0 && link.from < count && link.to >= 0 && link.to < count )
			preds [ static_cast < size_t > ( link.to ) ].push ( link.from );

	}

	int32_t words = ( count + 63 ) / 64;
	base::list < uint64_t > dom;
	base::list < uint64_t > scratch;

	dom.fill ( static_cast < size_t > ( count * words ), ~0ull );
	scratch.fill ( static_cast < size_t > ( words ), 0 );

	for ( int32_t w = 0; w < words; w++ )
		dom [ static_cast < size_t > ( w ) ] = 0;

	dom [ 0 ] = 1;

	for ( int32_t round = 0; round < count + 2; round++ ) {

		bool moved = false;

		for ( int32_t v = 1; v < count; v++ ) {

			for ( int32_t w = 0; w < words; w++ )
				scratch [ static_cast < size_t > ( w ) ] = preds [ static_cast < size_t > ( v ) ].empty ( ) ? 0 : ~0ull;

			for ( int32_t p : preds [ static_cast < size_t > ( v ) ] ) {

				for ( int32_t w = 0; w < words; w++ )
					scratch [ static_cast < size_t > ( w ) ] &= dom [ static_cast < size_t > ( p * words + w ) ];

			}

			scratch [ static_cast < size_t > ( v / 64 ) ] |= 1ull << ( v % 64 );

			for ( int32_t w = 0; w < words; w++ ) {

				uint64_t& slot = dom [ static_cast < size_t > ( v * words + w ) ];

				if ( slot != scratch [ static_cast < size_t > ( w ) ] ) {

					slot = scratch [ static_cast < size_t > ( w ) ];
					moved = true;

				}

			}

		}

		if ( !moved )
			break;

	}

	auto dominated = [ & ] ( int32_t top, int32_t v ) {

		return ( dom [ static_cast < size_t > ( v * words + top / 64 ) ] >> ( top % 64 ) ) & 1ull;

	};

	for ( int32_t k = 0; k < count; k++ ) {

		const block_code head = code [ static_cast < size_t > ( k ) ];

		if ( head.end != term::BRANCH || head.condition < 0 || !head.reachable )
			continue;

		for ( int32_t side = 0; side < 2; side++ ) {

			bool taken = side == 0;
			int32_t target = taken ? head.taken : head.other;
			fact known;

			if ( target <= 0 || target >= count || head.taken == head.other || preds [ static_cast < size_t > ( target ) ].count ( ) != 1 || !fact_for ( *this, head.condition, taken, known ) )
				continue;

			if ( known.var < 0 || known.var >= vars.count ( ) || vars [ static_cast < size_t > ( known.var ) ].kind == var_kind::STACK )
				continue;

			bool written = false;

			for ( int32_t v = 0; v < count && !written; v++ ) {

				if ( !dominated ( target, v ) )
					continue;

				const block_code& block = code [ static_cast < size_t > ( v ) ];

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

					const statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::ASM )
						written = true;

					if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == known.var )
						written = true;

				}

			}

			if ( written )
				continue;

			fact_rewriter writer { *this, known };

			for ( int32_t v = 0; v < count; v++ ) {

				if ( !dominated ( target, v ) )
					continue;

				block_code& block = code [ static_cast < size_t > ( v ) ];

				for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

					statement& line = statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::NOP )
						continue;

					bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

					line.right = writer.rewrite ( line.right, 0, 0 );

					if ( !defines )
						line.left = writer.rewrite ( line.left, 0, 0 );

				}

				block.condition = writer.rewrite ( block.condition, 0, 0 );
				block.returned = writer.rewrite ( block.returned, 0, 0 );
				block.selector = writer.rewrite ( block.selector, 0, 0 );

			}

		}

	}

}