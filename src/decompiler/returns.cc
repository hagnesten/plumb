#include "decompiler/internal.hh"

namespace decompiler {

	void builder::fold_returns ( ) {

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

	}

}
