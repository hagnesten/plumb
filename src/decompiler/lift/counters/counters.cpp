#include "decompiler/lift/counters/counters.h"
#include "decompiler/lift/expressions/expressions.h"

bool decompiler::builder::counter_load ( int32_t e, int64_t v, int32_t& global, int32_t& offset ) const {

	global = -1;
	offset = -1;

	if ( e < 0 )
		return false;

	const expr& node = exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::LOAD || node.a < 0 || ( node.bits & FLOATING ) )
		return false;

	int32_t at = node.a;

	if ( exprs [ static_cast < size_t > ( at ) ].kind == op::ADD && exprs [ static_cast < size_t > ( at ) ].b >= 0 && exprs [ static_cast < size_t > ( exprs [ static_cast < size_t > ( at ) ].b ) ].kind == op::CONST && !( exprs [ static_cast < size_t > ( exprs [ static_cast < size_t > ( at ) ].b ) ].bits & POINTER ) && exprs [ static_cast < size_t > ( at ) ].a >= 0 && exprs [ static_cast < size_t > ( exprs [ static_cast < size_t > ( at ) ].a ) ].kind == op::ADD ) {

		offset = exprs [ static_cast < size_t > ( at ) ].b;
		at = exprs [ static_cast < size_t > ( at ) ].a;

	}

	const expr& sum = exprs [ static_cast < size_t > ( at ) ];

	if ( sum.kind != op::ADD || sum.a < 0 || sum.b < 0 )
		return false;

	const expr& left = exprs [ static_cast < size_t > ( sum.a ) ];
	const expr& right = exprs [ static_cast < size_t > ( sum.b ) ];

	auto pointer_base = [ & ] ( int32_t side ) -> int32_t {

		const expr& item = exprs [ static_cast < size_t > ( side ) ];

		if ( item.kind == op::CONST && ( item.bits & POINTER ) )
			return side;

		if ( item.kind == op::VAR && item.value >= 0 && item.value < counter_bases.count ( ) && item.value != v )
			return counter_bases [ static_cast < size_t > ( item.value ) ];

		return -1;

	};

	if ( left.kind == op::VAR && left.value == v && left.size == 8 )
		global = pointer_base ( sum.b );
	else if ( right.kind == op::VAR && right.value == v && right.size == 8 )
		global = pointer_base ( sum.a );

	return global >= 0;

}

void decompiler::builder::counter_walk ( int32_t e, counter_scan& scan, int32_t depth ) const {

	if ( e < 0 || depth > 64 || scan.bad )
		return;

	const expr& node = exprs [ static_cast < size_t > ( e ) ];
	int32_t global = -1;
	int32_t offset = -1;

	if ( counter_load ( e, scan.var, global, offset ) ) {

		if ( node.size != 2 && node.size != 4 && node.size != 8 ) {

			scan.bad = true;

			return;

		}

		if ( scan.element && scan.element != node.size )
			scan.bad = true;

		scan.element = node.size;
		scan.matched++;

		if ( offset >= 0 && exprs [ static_cast < size_t > ( offset ) ].value % node.size )
			scan.bad = true;

		return;

	}

	if ( node.kind >= op::EQ && node.kind <= op::GE && node.a >= 0 && node.b >= 0 ) {

		const expr& left = exprs [ static_cast < size_t > ( node.a ) ];
		const expr& right = exprs [ static_cast < size_t > ( node.b ) ];

		if ( left.kind == op::VAR && left.value == scan.var && right.kind == op::CONST && !( right.bits & POINTER ) ) {

			scan.matched++;

			return;

		}

	}

	counter_walk ( node.a, scan, depth + 1 );
	counter_walk ( node.b, scan, depth + 1 );
	counter_walk ( node.c, scan, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		counter_walk ( args [ static_cast < size_t > ( node.first_arg + i ) ], scan, depth + 1 );

}

int32_t decompiler::builder::counter_rewrite ( int32_t e, int64_t v, int32_t element, int32_t depth ) {

	if ( e < 0 || depth > 64 )
		return e;

	expr node = exprs [ static_cast < size_t > ( e ) ];
	int32_t global = -1;
	int32_t offset = -1;

	if ( counter_load ( e, v, global, offset ) ) {

		int32_t scaled = make ( op::MUL, 8, var_expr ( static_cast < int32_t > ( v ), 8 ), constant ( element, 8 ), 0, 0 );
		int32_t address = make ( op::ADD, 8, global, scaled, 0, POINTER );

		if ( offset >= 0 )
			address = make ( op::ADD, 8, address, offset, 0, POINTER );

		node.a = address;
		exprs.push ( node );

		return exprs.count ( ) - 1;

	}

	if ( node.kind >= op::EQ && node.kind <= op::GE && node.a >= 0 && node.b >= 0 && exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR && exprs [ static_cast < size_t > ( node.a ) ].value == v && exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST ) {

		node.b = constant ( exprs [ static_cast < size_t > ( node.b ) ].value / element, exprs [ static_cast < size_t > ( node.b ) ].size );
		exprs.push ( node );

		return exprs.count ( ) - 1;

	}

	int32_t a = counter_rewrite ( node.a, v, element, depth + 1 );
	int32_t b = counter_rewrite ( node.b, v, element, depth + 1 );
	int32_t c = counter_rewrite ( node.c, v, element, depth + 1 );
	bool changed = a != node.a || b != node.b || c != node.c;
	base::list < int32_t > moved;

	for ( int32_t i = 0; i < node.arg_count; i++ ) {

		int32_t old = args [ static_cast < size_t > ( node.first_arg + i ) ];
		int32_t fresh = counter_rewrite ( old, v, element, depth + 1 );

		moved.push ( fresh );
		changed = changed || fresh != old;

	}

	if ( !changed )
		return e;

	node.a = a;
	node.b = b;
	node.c = c;

	if ( node.arg_count ) {

		node.first_arg = args.count ( );

		for ( int32_t item : moved )
			args.push ( item );

	}

	exprs.push ( node );

	return exprs.count ( ) - 1;

}

void decompiler::builder::scale_counters ( ) {

	if ( !scaled_counters || !long_mode )
		return;

	int32_t total = vars.count ( );
	bool changed_bases = false;
	base::list < int32_t > writes;

	counter_bases.clear ( );
	counter_bases.fill ( static_cast < size_t > ( total ), -1 );
	writes.fill ( static_cast < size_t > ( total ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
			continue;

		int64_t w = exprs [ static_cast < size_t > ( line.left ) ].value;

		if ( w < 0 || w >= total )
			continue;

		writes [ static_cast < size_t > ( w ) ]++;

		const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

		if ( right.kind == op::CONST && ( right.bits & POINTER ) && vars [ static_cast < size_t > ( w ) ].kind == var_kind::REGISTER )
			counter_bases [ static_cast < size_t > ( w ) ] = line.right;

	}

	for ( int32_t w = 0; w < total; w++ ) {

		if ( writes [ static_cast < size_t > ( w ) ] != 1 )
			counter_bases [ static_cast < size_t > ( w ) ] = -1;

	}

	for ( int32_t v = 0; v < total; v++ ) {

		const variable& item = vars [ static_cast < size_t > ( v ) ];

		if ( item.kind != var_kind::REGISTER || item.size != 8 || ( item.bits & FLOATING ) || item.flags )
			continue;

		counter_scan scan;
		base::list < int32_t > defs;
		int64_t steps = 0;
		bool shaped = true;

		scan.var = v;

		for ( int32_t s = 0; s < statements.count ( ) && shaped; s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == v;

			if ( defines ) {

				const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

				if ( right.kind == op::CONST && !( right.bits & POINTER ) )
					defs.push ( s );
				else if ( right.kind == op::ADD && right.a >= 0 && right.b >= 0 && exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && exprs [ static_cast < size_t > ( right.a ) ].value == v && exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( right.b ) ].value > 0 ) {

					defs.push ( s );
					steps++;

				} else
					shaped = false;

				continue;

			}

			scan.uses += occurrences ( *this, line.right, v, 0 );
			counter_walk ( line.right, scan, 0 );

			if ( !( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) ) {

				scan.uses += occurrences ( *this, line.left, v, 0 );
				counter_walk ( line.left, scan, 0 );

			}

		}

		for ( const block_code& block : code ) {

			scan.uses += occurrences ( *this, block.condition, v, 0 ) + occurrences ( *this, block.selector, v, 0 ) + occurrences ( *this, block.returned, v, 0 );
			counter_walk ( block.condition, scan, 0 );
			counter_walk ( block.selector, scan, 0 );
			counter_walk ( block.returned, scan, 0 );

		}

		if ( !shaped || scan.bad || !steps || !scan.element || scan.uses != scan.matched || scan.matched < 2 )
			continue;

		int32_t element = scan.element;
		bool divisible = true;

		for ( int32_t s : defs ) {

			const expr& right = exprs [ static_cast < size_t > ( statements [ static_cast < size_t > ( s ) ].right ) ];
			int64_t value = right.kind == op::CONST ? right.value : exprs [ static_cast < size_t > ( right.b ) ].value;

			if ( value % element )
				divisible = false;

		}

		auto compare_ok = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 64 || !divisible )
				return;

			const expr& node = exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind >= op::EQ && node.kind <= op::GE && node.a >= 0 && node.b >= 0 && exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR && exprs [ static_cast < size_t > ( node.a ) ].value == v && exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( node.b ) ].value % element )
				divisible = false;

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );

		};

		for ( const block_code& block : code )
			compare_ok ( compare_ok, block.condition, 0 );

		for ( const statement& line : statements ) {

			if ( line.kind != stmt::NOP )
				compare_ok ( compare_ok, line.right, 0 );

		}

		if ( !divisible )
			continue;

		for ( int32_t s : defs ) {

			statement& line = statements [ static_cast < size_t > ( s ) ];
			const expr right = exprs [ static_cast < size_t > ( line.right ) ];

			if ( right.kind == op::CONST )
				line.right = constant ( right.value / element, right.size );
			else
				line.right = make ( op::ADD, right.size, right.a, constant ( exprs [ static_cast < size_t > ( right.b ) ].value / element, exprs [ static_cast < size_t > ( right.b ) ].size ), 0, right.bits );

		}

		for ( int32_t s = 0; s < statements.count ( ); s++ ) {

			statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == v;

			if ( defines )
				continue;

			line.right = counter_rewrite ( line.right, v, element, 0 );

			if ( !( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
				line.left = counter_rewrite ( line.left, v, element, 0 );

		}

		for ( block_code& block : code ) {

			block.condition = counter_rewrite ( block.condition, v, element, 0 );
			block.selector = counter_rewrite ( block.selector, v, element, 0 );
			block.returned = counter_rewrite ( block.returned, v, element, 0 );

		}

		changed_bases = true;

	}

	if ( !changed_bases )
		return;

	for ( int32_t w = 0; w < total; w++ ) {

		if ( counter_bases [ static_cast < size_t > ( w ) ] < 0 )
			continue;

		int32_t left_over = 0;
		int32_t def_at = -1;

		for ( int32_t s = 0; s < statements.count ( ); s++ ) {

			const statement& line = statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == w;

			if ( defines )
				def_at = s;
			else
				left_over += occurrences ( *this, line.left, w, 0 );

			left_over += occurrences ( *this, line.right, w, 0 );

		}

		for ( const block_code& block : code )
			left_over += occurrences ( *this, block.condition, w, 0 ) + occurrences ( *this, block.selector, w, 0 ) + occurrences ( *this, block.returned, w, 0 );

		if ( !left_over && def_at >= 0 )
			statements [ static_cast < size_t > ( def_at ) ].kind = stmt::NOP;

	}

}
