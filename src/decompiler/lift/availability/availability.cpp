#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

bool decompiler::writes_between ( const builder& b, int32_t block, uint32_t mask, uint64_t from, uint64_t to ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	for ( int32_t i = item.writes_first; i < item.writes_first + item.writes_count && i < b.write_at.count ( ); i++ ) {

		uint64_t at = b.write_at [ static_cast < size_t > ( i ) ];

		if ( at > from && at < to && ( b.write_regs [ static_cast < size_t > ( i ) ] & mask ) )
			return true;

	}

	return false;

}
bool decompiler::holds_through ( const builder& b, uint32_t mask, int32_t from_block, uint64_t from, int32_t to_block, uint64_t to ) {

	if ( from_block == to_block && to > from )
		return !writes_between ( b, from_block, mask, from, to );

	if ( writes_between ( b, to_block, mask, 0, to ) )
		return false;

	int32_t total = b.code.count ( );
	base::list < uint8_t > seen;
	base::list < int32_t > work;

	seen.fill ( static_cast < size_t > ( total ), 0 );
	work.push ( to_block );

	while ( !work.empty ( ) ) {

		int32_t at = work [ work.size ( ) - 1 ];
		int32_t found = 0;

		work.pop ( );

		for ( const analysis::edge& link : b.graph.edges ) {

			if ( link.to != at )
				continue;

			found++;

			if ( link.from < 0 || link.from >= total )
				return false;

			if ( link.from == from_block ) {

				if ( writes_between ( b, from_block, mask, from, ~0ull ) )
					return false;

				continue;

			}

			if ( seen [ static_cast < size_t > ( link.from ) ] )
				continue;

			if ( writes_between ( b, link.from, mask, 0, ~0ull ) )
				return false;

			seen [ static_cast < size_t > ( link.from ) ] = 1;
			work.push ( link.from );

		}

		if ( !found || found != b.code [ static_cast < size_t > ( at ) ].preds )
			return false;

	}

	return true;

}