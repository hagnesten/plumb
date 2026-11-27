#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::count_vars ( const builder& b, int32_t e, base::list < int32_t >& uses, int32_t depth ) {

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
int32_t decompiler::substitute ( builder& b, int32_t e, int64_t var, int32_t value, int32_t depth ) {

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
bool decompiler::has_call ( const builder& b, int32_t e, int32_t depth ) {

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
bool decompiler::register_only ( const builder& b, int32_t e, int32_t depth ) {

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
int32_t decompiler::occurrences ( const builder& b, int32_t e, int64_t var, int32_t depth ) {

	if ( e < 0 || depth > 64 )
		return 0;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
	int32_t found = node.kind == op::VAR && node.value == var ? 1 : 0;

	found += occurrences ( b, node.a, var, depth + 1 ) + occurrences ( b, node.b, var, depth + 1 ) + occurrences ( b, node.c, var, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		found += occurrences ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], var, depth + 1 );

	return found;

}
bool decompiler::mentions ( const builder& b, int32_t e, int64_t var, int32_t depth ) {

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
int32_t decompiler::strip_casts ( const builder& b, int32_t e ) {

	while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
		e = b.exprs [ static_cast < size_t > ( e ) ].a;

	return e;

}
bool decompiler::bare_var ( const builder& b, int32_t e, int64_t v ) {

	return e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( e ) ].value == v && !( b.exprs [ static_cast < size_t > ( e ) ].bits & FLOATING ) && !( b.vars [ static_cast < size_t > ( v ) ].bits & FLOATING );

}