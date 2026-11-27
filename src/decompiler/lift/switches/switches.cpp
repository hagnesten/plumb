#include "decompiler/lift/switches/switches.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::builder::fold_switches ( ) {

	int32_t total = code.count ( );

	if ( total != graph.blocks.count ( ) )
		return;

	base::list < int32_t > uses;

	uses.fill ( vars.size ( ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

		if ( !defines )
			count_vars ( *this, line.left, uses, 0 );

		count_vars ( *this, line.right, uses, 0 );

	}

	for ( const block_code& item : code ) {

		count_vars ( *this, item.condition, uses, 0 );
		count_vars ( *this, item.selector, uses, 0 );
		count_vars ( *this, item.returned, uses, 0 );

	}

	bool changed = false;

	for ( int32_t s = 0; s < total; s++ ) {

		block_code& table = code [ static_cast < size_t > ( s ) ];

		if ( table.end != term::SWITCH || !table.reachable || table.selector < 0 )
			continue;

		bool empty = true;

		for ( int32_t k = table.first; k < table.first + table.count && k < statements.count ( ); k++ )
			empty = empty && statements [ static_cast < size_t > ( k ) ].kind == stmt::NOP;

		if ( !empty )
			continue;

		int32_t guard = -1;
		int32_t incoming = 0;

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to == s ) {

				incoming++;
				guard = link.from;

			}

		}

		if ( incoming != 1 || guard < 0 || guard == s )
			continue;

		block_code& head = code [ static_cast < size_t > ( guard ) ];

		if ( head.end != term::BRANCH || head.condition < 0 || ( head.taken != s && head.other != s ) || head.taken == head.other )
			continue;

		int32_t fallback = head.taken == s ? head.other : head.taken;
		const expr& test = exprs [ static_cast < size_t > ( head.condition ) ];
		int32_t tested = -1;

		if ( head.taken == fallback && ( test.kind == op::GT || test.kind == op::GE ) && ( test.bits & UNSIGNED ) )
			tested = strip_casts ( *this, test.a );
		else if ( head.other == fallback && ( test.kind == op::LE || test.kind == op::LT ) && ( test.bits & UNSIGNED ) )
			tested = strip_casts ( *this, test.a );

		int32_t picked = strip_casts ( *this, table.selector );
		int64_t inline_shift = 0;
		int32_t inline_selector = -1;

		if ( inline_switches && tested >= 0 && picked >= 0 && ( exprs [ static_cast < size_t > ( picked ) ].kind == op::ADD || exprs [ static_cast < size_t > ( picked ) ].kind == op::SUB ) && same ( tested, picked ) ) {

			const expr& offset_expr = exprs [ static_cast < size_t > ( picked ) ];

			if ( offset_expr.a >= 0 && offset_expr.b >= 0 && exprs [ static_cast < size_t > ( offset_expr.b ) ].kind == op::CONST && exprs [ static_cast < size_t > ( strip_casts ( *this, offset_expr.a ) ) ].kind == op::VAR ) {

				int64_t k = exprs [ static_cast < size_t > ( offset_expr.b ) ].value;

				inline_shift = offset_expr.kind == op::SUB ? k : -k;
				inline_selector = offset_expr.a;

			}

		}

		if ( inline_selector < 0 && ( tested < 0 || picked < 0 || exprs [ static_cast < size_t > ( tested ) ].kind != op::VAR || exprs [ static_cast < size_t > ( picked ) ].kind != op::VAR || exprs [ static_cast < size_t > ( tested ) ].value != exprs [ static_cast < size_t > ( picked ) ].value ) )
			continue;

		int64_t v = inline_selector >= 0 ? -1 : exprs [ static_cast < size_t > ( picked ) ].value;
		int32_t selector = inline_selector >= 0 ? inline_selector : table.selector;
		int64_t shift = inline_shift;

		for ( int32_t k = head.first + head.count - 1; k >= head.first && k < statements.count ( ); k-- ) {

			statement& line = statements [ static_cast < size_t > ( k ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != v || line.right < 0 )
				break;

			const expr& source = exprs [ static_cast < size_t > ( line.right ) ];

			if ( source.kind == op::ADD && source.b >= 0 && exprs [ static_cast < size_t > ( source.b ) ].kind == op::CONST && v < uses.count ( ) && uses [ static_cast < size_t > ( v ) ] == 2 ) {

				shift = -exprs [ static_cast < size_t > ( source.b ) ].value;
				selector = source.a;
				line.kind = stmt::NOP;

			} else if ( source.kind == op::SUB && source.b >= 0 && exprs [ static_cast < size_t > ( source.b ) ].kind == op::CONST && v < uses.count ( ) && uses [ static_cast < size_t > ( v ) ] == 2 ) {

				shift = exprs [ static_cast < size_t > ( source.b ) ].value;
				selector = source.a;
				line.kind = stmt::NOP;

			}

			break;

		}

		base::list < analysis::edge > cases;

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.from != s || link.kind != analysis::edge_kind::CASE || link.to == fallback )
				continue;

			analysis::edge moved = link;

			moved.from = guard;
			moved.value = static_cast < int32_t > ( link.value + shift );
			cases.push ( moved );

		}

		if ( cases.empty ( ) )
			continue;

		for ( int32_t i = 1; i < cases.count ( ); i++ ) {

			for ( int32_t j = i; j > 0 && cases [ static_cast < size_t > ( j - 1 ) ].value > cases [ static_cast < size_t > ( j ) ].value; j-- ) {

				analysis::edge swap = cases [ static_cast < size_t > ( j ) ];

				cases [ static_cast < size_t > ( j ) ] = cases [ static_cast < size_t > ( j - 1 ) ];
				cases [ static_cast < size_t > ( j - 1 ) ] = swap;

			}

		}

		int32_t kept = 0;

		for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

			const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];

			if ( link.from == s || link.from == guard )
				continue;

			graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

		}

		while ( graph.edges.count ( ) > kept )
			graph.edges.pop ( );

		for ( const analysis::edge& link : cases )
			graph.edges.push ( link );

		analysis::edge otherwise;

		otherwise.from = guard;
		otherwise.to = fallback;
		otherwise.kind = analysis::edge_kind::JUMP;
		graph.edges.push ( otherwise );

		head.end = term::SWITCH;
		head.selector = selector;
		head.condition = -1;
		head.taken = -1;
		head.other = -1;
		head.last = table.last;

		table.reachable = false;
		table.end = term::STOP;
		table.selector = -1;
		changed = true;

	}

	if ( changed )
		reindex_edges ( );

}
void decompiler::builder::chain_switches ( ) {

	int32_t total = code.count ( );

	if ( total != graph.blocks.count ( ) )
		return;

	base::list < int32_t > preds;

	auto count_preds = [ & ] ( ) {

		preds.clear ( );
		preds.fill ( static_cast < size_t > ( total ), 0 );

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to >= 0 && link.to < total )
				preds [ static_cast < size_t > ( link.to ) ]++;

		}

	};

	auto empty = [ & ] ( int32_t block ) {

		const block_code& item = code [ static_cast < size_t > ( block ) ];

		for ( int32_t s = item.first; s < item.first + item.count && s < statements.count ( ); s++ ) {

			if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
				return false;

		}

		return true;

	};

	auto tested = [ & ] ( int32_t block, int64_t& constant ) -> int32_t {

		const block_code& item = code [ static_cast < size_t > ( block ) ];

		if ( item.end != term::BRANCH || item.condition < 0 || !item.reachable )
			return -1;

		const expr& test = exprs [ static_cast < size_t > ( item.condition ) ];
		bool usable = test.kind == op::EQ || test.kind == op::NE || test.kind == op::LT || test.kind == op::LE || test.kind == op::GT || test.kind == op::GE;

		if ( !usable || test.a < 0 || test.b < 0 || exprs [ static_cast < size_t > ( test.b ) ].kind != op::CONST || ( test.bits & FLOATING ) )
			return -1;

		int32_t subject = strip_casts ( *this, test.a );

		if ( subject < 0 || exprs [ static_cast < size_t > ( subject ) ].kind != op::VAR )
			return -1;

		constant = exprs [ static_cast < size_t > ( test.b ) ].value;

		return static_cast < int32_t > ( exprs [ static_cast < size_t > ( subject ) ].value );

	};

	bool changed = false;

	count_preds ( );

	for ( int32_t root = 0; root < total; root++ ) {

		int64_t first_value = 0;
		int32_t subject = tested ( root, first_value );

		if ( subject < 0 )
			continue;

		base::list < int32_t > nodes;
		base::list < int32_t > work;
		base::list < analysis::edge > cases;
		int32_t fallback = -1;
		bool broken = false;

		work.push ( root );

		while ( work.count ( ) && !broken ) {

			int32_t at = work [ static_cast < size_t > ( work.count ( ) - 1 ) ];

			work.pop ( );

			int64_t value = 0;
			bool inside = at == root || ( at != root && tested ( at, value ) == subject && empty ( at ) && preds [ static_cast < size_t > ( at ) ] == 1 );

			if ( at == root )
				tested ( at, value );

			bool listed = false;

			for ( int32_t n : nodes )
				listed = listed || n == at;

			if ( listed ) {

				broken = true;

				break;

			}

			if ( !inside ) {

				if ( fallback >= 0 && fallback != at )
					broken = true;

				fallback = at;

				continue;

			}

			nodes.push ( at );

			const block_code& item = code [ static_cast < size_t > ( at ) ];
			const expr& test = exprs [ static_cast < size_t > ( item.condition ) ];

			if ( test.kind == op::EQ || test.kind == op::NE ) {

				int32_t hit = test.kind == op::EQ ? item.taken : item.other;
				int32_t miss = test.kind == op::EQ ? item.other : item.taken;
				analysis::edge link;

				link.from = root;
				link.to = hit;
				link.kind = analysis::edge_kind::CASE;
				link.value = static_cast < int32_t > ( value );

				if ( value != static_cast < int64_t > ( link.value ) )
					broken = true;

				cases.push ( link );
				work.push ( miss );

			} else {

				work.push ( item.taken );
				work.push ( item.other );

			}

		}

		if ( broken || fallback < 0 || cases.count ( ) < switch_cases )
			continue;

		int32_t distinct = 0;

		for ( int32_t i = 0; i < cases.count ( ); i++ ) {

			bool again = false;

			for ( int32_t j = 0; j < i; j++ )
				again = again || cases [ static_cast < size_t > ( j ) ].to == cases [ static_cast < size_t > ( i ) ].to;

			if ( !again )
				distinct++;

		}

		if ( distinct < switch_cases && cases.count ( ) < 4 )
			continue;

		bool clash = false;

		for ( int32_t i = 0; i < cases.count ( ); i++ ) {

			if ( cases [ static_cast < size_t > ( i ) ].to == fallback )
				clash = true;

			for ( int32_t n : nodes )
				clash = clash || ( n != root && cases [ static_cast < size_t > ( i ) ].to == n );

			for ( int32_t j = 0; j < i; j++ )
				clash = clash || cases [ static_cast < size_t > ( j ) ].value == cases [ static_cast < size_t > ( i ) ].value;

		}

		if ( clash )
			continue;

		for ( int32_t i = 1; i < cases.count ( ); i++ ) {

			for ( int32_t j = i; j > 0 && cases [ static_cast < size_t > ( j - 1 ) ].value > cases [ static_cast < size_t > ( j ) ].value; j-- ) {

				analysis::edge swap = cases [ static_cast < size_t > ( j ) ];

				cases [ static_cast < size_t > ( j ) ] = cases [ static_cast < size_t > ( j - 1 ) ];
				cases [ static_cast < size_t > ( j - 1 ) ] = swap;

			}

		}

		int32_t kept = 0;

		for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

			const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];
			bool drop = false;

			for ( int32_t n : nodes )
				drop = drop || link.from == n;

			if ( !drop )
				graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

		}

		while ( graph.edges.count ( ) > kept )
			graph.edges.pop ( );

		for ( const analysis::edge& link : cases )
			graph.edges.push ( link );

		analysis::edge otherwise;

		otherwise.from = root;
		otherwise.to = fallback;
		otherwise.kind = analysis::edge_kind::JUMP;
		graph.edges.push ( otherwise );

		block_code& head = code [ static_cast < size_t > ( root ) ];

		head.selector = exprs [ static_cast < size_t > ( head.condition ) ].a;
		head.end = term::SWITCH;
		head.condition = -1;
		head.taken = -1;
		head.other = -1;

		for ( int32_t n : nodes ) {

			if ( n == root )
				continue;

			block_code& dead = code [ static_cast < size_t > ( n ) ];

			dead.reachable = false;
			dead.end = term::STOP;
			dead.condition = -1;

		}

		changed = true;
		count_preds ( );

	}

	if ( changed )
		reindex_edges ( );

}