#include "decompiler/lift/returns/returns.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"

void decompiler::builder::reindex_edges ( ) {

	int32_t total = graph.blocks.count ( );
	base::list < analysis::edge > sorted;

	for ( analysis::block& item : graph.blocks ) {

		item.first_out = 0;
		item.out_count = 0;

	}

	for ( int32_t from = 0; from < total; from++ ) {

		graph.blocks [ static_cast < size_t > ( from ) ].first_out = sorted.count ( );

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.from == from )
				sorted.push ( link );

		}

		graph.blocks [ static_cast < size_t > ( from ) ].out_count = sorted.count ( ) - graph.blocks [ static_cast < size_t > ( from ) ].first_out;

	}

	graph.edges = sorted;

}

bool decompiler::lone_return_folds = true;

void decompiler::builder::fold_returns ( ) {

	int32_t total = code.count ( );

	for ( int32_t r = 0; r < total && total == graph.blocks.count ( ); r++ ) {

		const block_code tail = code [ static_cast < size_t > ( r ) ];

		if ( tail.end != term::RETURN || tail.returned < 0 || exprs [ static_cast < size_t > ( tail.returned ) ].kind != op::VAR )
			continue;

		bool empty = true;

		for ( int32_t s = tail.first; s < tail.first + tail.count && s < statements.count ( ); s++ )
			empty = empty && statements [ static_cast < size_t > ( s ) ].kind == stmt::NOP;

		if ( !empty )
			continue;

		int64_t var = exprs [ static_cast < size_t > ( tail.returned ) ].value;

		for ( int32_t p = 0; p < total; p++ ) {

			block_code& owner = code [ static_cast < size_t > ( p ) ];

			if ( p == r || owner.end != term::FALL || owner.taken != r )
				continue;

			int32_t last = -1;

			for ( int32_t s = owner.first; s < owner.first + owner.count && s < statements.count ( ); s++ ) {

				if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
					last = s;

			}

			if ( last < 0 )
				continue;

			statement& line = statements [ static_cast < size_t > ( last ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != var || exprs [ static_cast < size_t > ( line.right ) ].kind != op::CONST )
				continue;

			owner.end = term::RETURN;
			owner.returned = line.right;
			owner.taken = -1;
			owner.last = tail.last;
			line.kind = stmt::NOP;

			int32_t kept = 0;

			for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

				const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];

				if ( link.from == p && link.to == r )
					continue;

				graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

			}

			while ( graph.edges.count ( ) > kept )
				graph.edges.pop ( );

		}

		if ( !lone_return_folds )
			continue;

		int32_t sources = 0;
		int32_t lone = -1;

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to == r ) {

				sources++;
				lone = link.from;

			}

		}

		if ( sources != 1 || lone < 0 || lone >= total || lone == r )
			continue;

		block_code& owner = code [ static_cast < size_t > ( lone ) ];

		if ( owner.end != term::FALL || owner.taken != r )
			continue;

		int32_t last = -1;

		for ( int32_t s = owner.first; s < owner.first + owner.count && s < statements.count ( ); s++ ) {

			if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
				last = s;

		}

		if ( last < 0 )
			continue;

		statement& line = statements [ static_cast < size_t > ( last ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || exprs [ static_cast < size_t > ( line.left ) ].value != var || references ( line.right, static_cast < int32_t > ( var ), -1 ) )
			continue;

		bool folded_before = false;

		for ( int32_t p = 0; p < total; p++ ) {

			const block_code& other = code [ static_cast < size_t > ( p ) ];

			if ( p != r && other.end == term::RETURN && other.last == tail.last && other.returned >= 0 && exprs [ static_cast < size_t > ( other.returned ) ].kind == op::CONST )
				folded_before = true;

		}

		if ( !folded_before )
			continue;

		owner.end = term::RETURN;
		owner.returned = line.right;
		owner.taken = -1;
		owner.last = tail.last;
		line.kind = stmt::NOP;

		int32_t kept = 0;

		for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

			const analysis::edge link = graph.edges [ static_cast < size_t > ( e ) ];

			if ( link.from == lone && link.to == r )
				continue;

			graph.edges [ static_cast < size_t > ( kept++ ) ] = link;

		}

		while ( graph.edges.count ( ) > kept )
			graph.edges.pop ( );

	}

	for ( block_code& tail : code ) {

		if ( tail.end != term::RETURN || tail.returned < 0 || exprs [ static_cast < size_t > ( tail.returned ) ].kind != op::VAR )
			continue;

		int32_t last = -1;

		for ( int32_t s = tail.first; s < tail.first + tail.count && s < statements.count ( ); s++ ) {

			if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
				last = s;

		}

		if ( last < 0 )
			continue;

		statement& line = statements [ static_cast < size_t > ( last ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
			continue;

		const expr& left = exprs [ static_cast < size_t > ( line.left ) ];
		const expr& right = exprs [ static_cast < size_t > ( line.right ) ];

		if ( left.kind != op::VAR || left.value != exprs [ static_cast < size_t > ( tail.returned ) ].value || right.kind != op::CONST )
			continue;

		tail.returned = line.right;
		line.kind = stmt::NOP;

	}

	if ( total != graph.blocks.count ( ) || total <= 0 )
		return;

	reindex_edges ( );
	sweep_unreachable ( *this );

}

void decompiler::sweep_unreachable ( builder& b ) {

	int32_t total = b.code.count ( );
	base::list < uint8_t > seen;
	base::list < int32_t > work;

	if ( total <= 0 || total != b.graph.blocks.count ( ) )
		return;

	seen.fill ( static_cast < size_t > ( total ), 0 );
	seen [ 0 ] = 1;
	work.push ( 0 );

	while ( work.count ( ) ) {

		int32_t at = work [ static_cast < size_t > ( work.count ( ) - 1 ) ];

		work.pop ( );

		for ( const analysis::edge& link : b.graph.edges ) {

			if ( link.from != at || link.to < 0 || link.to >= total || seen [ static_cast < size_t > ( link.to ) ] )
				continue;

			seen [ static_cast < size_t > ( link.to ) ] = 1;
			work.push ( link.to );

		}

	}

	for ( int32_t i = 0; i < total; i++ ) {

		if ( !seen [ static_cast < size_t > ( i ) ] )
			b.code [ static_cast < size_t > ( i ) ].reachable = false;

	}

}