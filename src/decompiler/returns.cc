#include "decompiler/internal.hh"

namespace decompiler {

	void builder::fold_returns ( ) {

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
