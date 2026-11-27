#include "decompiler/lift/zero_edges/zero_edges.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

int32_t decompiler::zero_fold ( builder& b, int32_t e, int32_t depth ) {

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
void decompiler::builder::zero_edges ( ) {

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