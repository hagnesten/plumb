#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/divisions/divisions.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "decompiler/literals/literals.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"

int32_t decompiler::builder::make ( op kind, uint8_t size, int32_t a, int32_t b, int64_t value, uint8_t bits ) {

	expr& item = exprs.emplace ( );

	item.kind = kind;
	item.size = size;
	item.a = a;
	item.b = b;
	item.value = value;
	item.bits = bits;

	int32_t deepest = 0;

	if ( a >= 0 )
		deepest = exprs [ static_cast < size_t > ( a ) ].depth;

	if ( b >= 0 )
		deepest = base::max ( deepest, exprs [ static_cast < size_t > ( b ) ].depth );

	exprs.back ( ).depth = deepest + 1;

	return exprs.count ( ) - 1;

}

void decompiler::builder::deepen ( int32_t e ) {

	expr& item = exprs [ static_cast < size_t > ( e ) ];
	const int32_t children [ 3 ] = { item.a, item.b, item.c };
	int32_t deepest = 0;

	for ( int32_t child : children ) {

		if ( child >= 0 )
			deepest = base::max ( deepest, exprs [ static_cast < size_t > ( child ) ].depth );

	}

	for ( int32_t i = 0; i < item.arg_count; i++ )
		deepest = base::max ( deepest, exprs [ static_cast < size_t > ( args [ static_cast < size_t > ( item.first_arg + i ) ] ) ].depth );

	item.depth = deepest + 1;

}

int64_t decompiler::truncate ( int64_t value, uint8_t size ) {

	if ( size >= 8 )
		return value;

	int32_t shift = 64 - size * 8;

	return static_cast < int64_t > ( static_cast < uint64_t > ( value ) << shift ) >> shift;

}

int32_t decompiler::builder::constant ( int64_t value, uint8_t size ) {

	return make ( op::CONST, size, -1, -1, truncate ( value, size ) );

}

int32_t decompiler::builder::cast ( int32_t a, uint8_t size, uint8_t bits ) {

	if ( a < 0 )
		return a;

	const expr& source = exprs [ static_cast < size_t > ( a ) ];

	if ( source.kind == op::CONST && !( bits & FLOATING ) ) {

		uint8_t keep = static_cast < uint8_t > ( ( source.bits | bits ) & POINTER );
		int32_t made = constant ( ( bits & SIGNED ) || keep ? truncate ( source.value, source.size ) : ( source.size >= 8 ? source.value : static_cast < int64_t > ( static_cast < uint64_t > ( source.value ) & ( ( 1ull << ( source.size * 8 ) ) - 1 ) ) ), size );

		exprs [ static_cast < size_t > ( made ) ].bits |= keep;

		return made;

	}

	if ( source.size == size && ( source.bits & FLOATING ) == ( bits & FLOATING ) )
		return a;

	if ( source.kind == op::CAST && source.size > size && !( bits & FLOATING ) && !( source.bits & FLOATING ) )
		return cast ( source.a, size, bits );

	return make ( op::CAST, size, a, -1, 0, bits );

}

bool decompiler::same_tree ( const builder& k, int32_t x, int32_t y, int32_t depth ) {

	if ( x == y )
		return true;

	if ( x < 0 || y < 0 || depth > 24 )
		return false;

	const expr& p = k.exprs [ static_cast < size_t > ( x ) ];
	const expr& q = k.exprs [ static_cast < size_t > ( y ) ];

	if ( p.kind != q.kind || p.size != q.size || p.value != q.value || p.arg_count != q.arg_count || p.kind == op::CALL || p.kind == op::INTRINSIC )
		return false;

	return same_tree ( k, p.a, q.a, depth + 1 ) && same_tree ( k, p.b, q.b, depth + 1 ) && same_tree ( k, p.c, q.c, depth + 1 );

}

bool decompiler::builder::same ( int32_t x, int32_t y ) const {

	return same_tree ( *this, x, y, 0 );

}

int32_t decompiler::builder::fold_arithmetic ( op kind, int32_t a, int32_t b, uint8_t size ) {

	const expr left = exprs [ static_cast < size_t > ( a ) ];
	const expr right = exprs [ static_cast < size_t > ( b ) ];

	if ( ( kind == op::SHR || kind == op::INTRINSIC ) && right.kind == op::CONST && left.kind == op::INTRINSIC && left.value == I_UMULH && left.arg_count == 2 ) {

		int32_t first = args [ static_cast < size_t > ( left.first_arg ) ];
		int32_t second = args [ static_cast < size_t > ( left.first_arg + 1 ) ];
		int32_t magic = exprs [ static_cast < size_t > ( first ) ].kind == op::CONST ? first : second;
		int32_t subject = magic == first ? second : first;

		if ( exprs [ static_cast < size_t > ( magic ) ].kind == op::CONST ) {

			uint64_t divisor = magic_divisor ( static_cast < uint64_t > ( exprs [ static_cast < size_t > ( magic ) ].value ), static_cast < int32_t > ( right.value ) );

			if ( divisor )
				return make ( op::DIV, exprs [ static_cast < size_t > ( subject ) ].size, subject, constant ( static_cast < int64_t > ( divisor ), exprs [ static_cast < size_t > ( subject ) ].size ), 0, UNSIGNED );

		}

	}

	if ( kind == op::ADD && same_tree ( *this, a, b, 0 ) )
		return binary ( op::MUL, a, constant ( 2, size ), size );

	if ( kind == op::MUL && right.kind == op::CONST && left.kind == op::SHL && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( left.b ) ].value < 32 )
		return binary ( op::MUL, left.a, constant ( right.value << exprs [ static_cast < size_t > ( left.b ) ].value, size ), size );

	if ( fold_scaled_shifts && kind == op::SHL && right.kind == op::CONST && right.value > 0 && right.value < 16 && left.kind == op::MUL && left.size == size && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( left.b ) ].value > 0 && exprs [ static_cast < size_t > ( left.b ) ].value < 16 )
		return binary ( op::MUL, left.a, constant ( exprs [ static_cast < size_t > ( left.b ) ].value << right.value, size ), size );

	if ( kind == op::ADD ) {

		const int32_t sides [ 2 ] = { a, b };

		for ( int32_t s = 0; s < 2; s++ ) {

			const expr& scaled = exprs [ static_cast < size_t > ( sides [ s ] ) ];
			int32_t plain = sides [ 1 - s ];

			if ( scaled.kind == op::MUL && scaled.b >= 0 && exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && same_tree ( *this, scaled.a, plain, 0 ) )
				return binary ( op::MUL, plain, constant ( exprs [ static_cast < size_t > ( scaled.b ) ].value + 1, size ), size );

		}

		auto scale_of = [ & ] ( const expr& node, int32_t self, int32_t& base_out ) -> int64_t {

			if ( ( node.kind == op::MUL || node.kind == op::SHL ) && node.b >= 0 && exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST ) {

				int64_t k = exprs [ static_cast < size_t > ( node.b ) ].value;

				if ( node.kind == op::SHL && ( k < 0 || k > 31 ) )
					return 0;

				base_out = node.a;

				return node.kind == op::MUL ? k : ( 1ll << k );

			}

			base_out = self;

			return 1;

		};

		int32_t left_base = -1;
		int32_t right_base = -1;
		int64_t left_scale = scale_of ( left, a, left_base );
		int64_t right_scale = scale_of ( right, b, right_base );

		if ( left_scale && right_scale && ( left_scale != 1 || right_scale != 1 ) && left_base >= 0 && right_base >= 0 && same_tree ( *this, left_base, right_base, 0 ) )
			return binary ( op::MUL, left_base, constant ( left_scale + right_scale, size ), size );

	}

	if ( kind == op::MUL && right.kind == op::CONST && left.kind == op::MUL && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST )
		return binary ( op::MUL, left.a, constant ( exprs [ static_cast < size_t > ( left.b ) ].value * right.value, size ), size );

	if ( kind == op::SUB && right.kind == op::MUL && right.b >= 0 && exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST && right.a >= 0 ) {

		const expr& quotient = exprs [ static_cast < size_t > ( right.a ) ];

		if ( quotient.kind == op::DIV && quotient.b >= 0 && exprs [ static_cast < size_t > ( quotient.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( quotient.b ) ].value == exprs [ static_cast < size_t > ( right.b ) ].value && same_tree ( *this, quotient.a, a, 0 ) )
			return make ( op::MOD, size, a, quotient.b, 0, quotient.bits );

	}

	return -1;

}

int32_t decompiler::builder::binary ( op kind, int32_t a, int32_t b, uint8_t size, uint8_t bits ) {

	if ( a < 0 || b < 0 )
		return a < 0 ? b : a;

	const expr left = exprs [ static_cast < size_t > ( a ) ];
	const expr right = exprs [ static_cast < size_t > ( b ) ];
	bool floating = ( bits & FLOATING ) != 0;

	if ( !floating && left.kind == op::CONST && right.kind == op::CONST ) {

		int64_t x = left.value;
		int64_t y = right.value;

		switch ( kind ) {

			case op::ADD:
				return constant ( x + y, size );

			case op::SUB:
				return constant ( x - y, size );

			case op::MUL:
				return constant ( x * y, size );

			case op::AND:
				return constant ( x & y, size );

			case op::OR:
				return constant ( x | y, size );

			case op::XOR:
				return constant ( x ^ y, size );

			case op::SHL:
				return constant ( static_cast < int64_t > ( static_cast < uint64_t > ( x ) << ( y & 63 ) ), size );

			default:
				break;

		}

	}

	if ( !floating && right.kind == op::CONST ) {

		if ( ( kind == op::AND || kind == op::MUL ) && right.value == 0 )
			return constant ( 0, size );

		if ( kind == op::OR && truncate ( right.value, size ) == -1 )
			return constant ( -1, size );

		if ( kind == op::AND && truncate ( right.value, size ) == -1 )
			return a;

		if ( ( kind == op::ADD || kind == op::SUB || kind == op::OR || kind == op::XOR || kind == op::SHL || kind == op::SHR ) && right.value == 0 )
			return left.size == size ? a : cast ( a, size, 0 );

		if ( kind == op::MUL && right.value == 1 )
			return a;

		if ( ( kind == op::ADD || kind == op::SUB ) && left.kind == op::ADD && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST ) {

			int64_t merged = exprs [ static_cast < size_t > ( left.b ) ].value + ( kind == op::ADD ? right.value : -right.value );

			return binary ( op::ADD, left.a, constant ( merged, size ), size, bits );

		}

		if ( kind == op::SUB )
			return binary ( op::ADD, a, constant ( -right.value, size ), size, bits );

	}

	if ( !floating && left.kind == op::CONST && ( kind == op::ADD || kind == op::MUL || kind == op::AND || kind == op::OR || kind == op::XOR ) )
		return binary ( kind, b, a, size, bits );

	if ( !floating && ( bits & UNSIGNED ) && right.kind == op::CONST ) {

		if ( ( kind == op::GE && right.value == 1 ) || ( kind == op::GT && right.value == 0 ) )
			return make ( op::NE, 1, a, constant ( 0, left.size ), 0, 0 );

		if ( ( kind == op::LT && right.value == 1 ) || ( kind == op::LE && right.value == 0 ) )
			return make ( op::EQ, 1, a, constant ( 0, left.size ), 0, 0 );

	}

	if ( !floating ) {

		int32_t folded = fold_arithmetic ( kind, a, b, size );

		if ( folded >= 0 )
			return folded;

	}

	uint8_t result_size = size;

	if ( kind >= op::EQ && kind <= op::LOR )
		result_size = 1;

	int32_t made = make ( kind, result_size, a, b, 0, bits );

	return made;

}

int32_t decompiler::builder::negate ( int32_t condition ) {

	const expr item = exprs [ static_cast < size_t > ( condition ) ];

	if ( negate_pairs && ( item.kind == op::EQ || item.kind == op::NE ) && item.a >= 0 && item.b >= 0 && exprs [ static_cast < size_t > ( item.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( item.b ) ].value == 0 ) {

		const expr& inner = exprs [ static_cast < size_t > ( item.a ) ];

		if ( ( inner.kind == op::OR || inner.kind == op::AND ) && inner.size == 1 && inner.a >= 0 && inner.b >= 0 ) {

			op left = exprs [ static_cast < size_t > ( inner.a ) ].kind;
			op right = exprs [ static_cast < size_t > ( inner.b ) ].kind;
			bool pair = ( ( left >= op::EQ && left <= op::GE ) || left == op::LNOT ) && ( ( right >= op::EQ && right <= op::GE ) || right == op::LNOT );

			if ( pair )
				return item.kind == op::NE ? negate ( item.a ) : item.a;

		}

	}

	switch ( item.kind ) {

		case op::EQ:
			return make ( op::NE, 1, item.a, item.b, 0, item.bits );

		case op::NE:
			return make ( op::EQ, 1, item.a, item.b, 0, item.bits );

		case op::LT:
			return ( item.bits & FLOATING ) && !float_flips ? make ( op::LNOT, 1, condition ) : make ( op::GE, 1, item.a, item.b, 0, item.bits );

		case op::GE:
			return ( item.bits & FLOATING ) && !float_flips ? make ( op::LNOT, 1, condition ) : make ( op::LT, 1, item.a, item.b, 0, item.bits );

		case op::LE:
			return ( item.bits & FLOATING ) && !float_flips ? make ( op::LNOT, 1, condition ) : make ( op::GT, 1, item.a, item.b, 0, item.bits );

		case op::GT:
			return ( item.bits & FLOATING ) && !float_flips ? make ( op::LNOT, 1, condition ) : make ( op::LE, 1, item.a, item.b, 0, item.bits );

		case op::LNOT:
			return item.a;

		case op::LAND:
			return make ( op::LOR, 1, negate ( item.a ), negate ( item.b ) );

		case op::LOR:
			return make ( op::LAND, 1, negate ( item.a ), negate ( item.b ) );

		case op::OR:
		case op::AND: {

			auto boolean = [ & ] ( int32_t e ) {

				if ( e < 0 )
					return false;

				op kind = exprs [ static_cast < size_t > ( e ) ].kind;

				return ( kind >= op::EQ && kind <= op::GE ) || kind == op::LNOT || kind == op::LAND || kind == op::LOR;

			};

			if ( negate_pairs && item.size == 1 && boolean ( item.a ) && boolean ( item.b ) )
				return make ( item.kind == op::OR ? op::LAND : op::LOR, 1, negate ( item.a ), negate ( item.b ) );

			return make ( op::LNOT, 1, condition );

		}

		case op::CONST:
			return constant ( item.value ? 0 : 1, 1 );

		case op::SET:

			if ( item.value == 1 && item.c >= 0 ) {

				int32_t flipped = negate ( item.c );
				int32_t made = make ( op::SET, 1, item.a, item.b, 1 );

				exprs [ static_cast < size_t > ( made ) ].c = flipped;

				return made;

			}

			return make ( op::LNOT, 1, condition );

		default:
			return make ( op::LNOT, 1, condition );

	}

}

int32_t decompiler::builder::var_expr ( int32_t index, uint8_t size ) {

	variable& item = vars [ static_cast < size_t > ( index ) ];
	int32_t made = make ( op::VAR, item.size, -1, -1, index, item.bits );

	item.uses++;

	if ( size && size < item.size )
		return cast ( made, size, 0 );

	return made;

}

int32_t decompiler::builder::register_var ( int32_t family ) {

	if ( family_var [ family ] >= 0 )
		return family_var [ family ];

	variable& item = vars.emplace ( );

	item.kind = var_kind::REGISTER;
	item.family = family;
	item.size = static_cast < uint8_t > ( family >= 16 ? 8 : ( long_mode ? 8 : 4 ) );

	if ( family >= 16 )
		item.bits = FLOATING;

	family_var [ family ] = vars.count ( ) - 1;

	return family_var [ family ];

}

bool decompiler::builder::contains_load ( int32_t e ) const {

	if ( e < 0 )
		return false;

	const expr& item = exprs [ static_cast < size_t > ( e ) ];

	if ( item.kind == op::LOAD || item.kind == op::CALL || item.kind == op::GLOBAL || item.kind == op::INTRINSIC )
		return true;

	if ( item.kind == op::VAR && vars [ static_cast < size_t > ( item.value ) ].kind != var_kind::REGISTER )
		return true;

	if ( contains_load ( item.a ) || contains_load ( item.b ) || contains_load ( item.c ) )
		return true;

	for ( int32_t i = 0; i < item.arg_count; i++ ) {

		if ( contains_load ( args [ static_cast < size_t > ( item.first_arg + i ) ] ) )
			return true;

	}

	return false;

}

bool decompiler::builder::references ( int32_t e, int32_t var, int32_t skip ) const {

	if ( e < 0 || e == skip )
		return false;

	const expr& item = exprs [ static_cast < size_t > ( e ) ];

	if ( ( item.kind == op::VAR || ( item.kind == op::ADDRESS && item.a < 0 ) ) && item.value == var )
		return true;

	if ( references ( item.a, var, skip ) || references ( item.b, var, skip ) || references ( item.c, var, skip ) )
		return true;

	for ( int32_t i = 0; i < item.arg_count; i++ ) {

		if ( references ( args [ static_cast < size_t > ( item.first_arg + i ) ], var, skip ) )
			return true;

	}

	return false;

}

int32_t decompiler::builder::replace ( int32_t e, int32_t from, int32_t to ) {

	if ( e < 0 )
		return e;

	if ( e == from )
		return to;

	expr item = exprs [ static_cast < size_t > ( e ) ];
	int32_t a = replace ( item.a, from, to );
	int32_t b = replace ( item.b, from, to );
	int32_t c = replace ( item.c, from, to );
	bool changed = a != item.a || b != item.b || c != item.c;
	base::list < int32_t > list;

	for ( int32_t i = 0; i < item.arg_count; i++ ) {

		int32_t original = args [ static_cast < size_t > ( item.first_arg + i ) ];
		int32_t value = replace ( original, from, to );

		list.push ( value );

		if ( value != original )
			changed = true;

	}

	if ( !changed )
		return e;

	item.a = a;
	item.b = b;
	item.c = c;

	if ( item.arg_count ) {

		item.first_arg = args.count ( );

		for ( int32_t value : list )
			args.push ( value );

	}

	exprs.push ( item );

	return exprs.count ( ) - 1;

}