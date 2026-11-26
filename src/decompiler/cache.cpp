#include "decompiler/decompiler.h"

namespace decompiler {

	cache::~cache ( ) {

		clear ( );

	}

	result* cache::find ( uint64_t function, uint32_t revision ) {

		for ( int32_t i = 0; i < entries.count ( ); i++ ) {

			result* item = entries [ static_cast < size_t > ( i ) ];

			if ( item->function != function )
				continue;

			if ( item->revision != revision ) {

				delete item;
				entries.remove_at ( static_cast < size_t > ( i ) );

				return nullptr;

			}

			if ( i + 1 < entries.count ( ) ) {

				entries.remove_at ( static_cast < size_t > ( i ) );
				entries.push ( item );

			}

			return item;

		}

		return nullptr;

	}

	result* cache::make ( uint64_t function ) {

		if ( entries.count ( ) >= 48 ) {

			delete entries [ 0 ];
			entries.remove_at ( 0 );

		}

		result* item = new result ( );

		item->function = function;
		entries.push ( item );

		return item;

	}

	void cache::clear ( ) {

		for ( result* item : entries )
			delete item;

		entries.clear ( );

	}

}