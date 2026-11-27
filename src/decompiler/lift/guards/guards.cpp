#include "decompiler/lift/guards/guards.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::builder::guard_expressions ( ) {

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