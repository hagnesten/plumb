#pragma once

#include "base/types/types.h"
#include "base/utility/utility.h"

namespace base {

	template < typename type, typename less >
	void insertion_sort ( type* items, int32_t count, less& before ) {

		for ( int32_t i = 1; i < count; i++ ) {

			type held = move ( items [ i ] );
			int32_t j = i - 1;

			while ( j >= 0 && before ( held, items [ j ] ) ) {

				items [ j + 1 ] = move ( items [ j ] );
				j--;

			}

			items [ j + 1 ] = move ( held );

		}

	}

	template < typename type, typename less >
	void sort ( type* items, int32_t count, less before ) {

		constexpr int32_t run = 24;

		if ( count <= run ) {

			insertion_sort ( items, count, before );

			return;

		}

		for ( int32_t at = 0; at < count; at += run )
			insertion_sort ( items + at, min ( run, count - at ), before );

		type* scratch = static_cast < type* > ( runtime_allocate ( sizeof ( type ) * static_cast < size_t > ( count ) ) );

		for ( int32_t i = 0; i < count; i++ )
			new ( &scratch [ i ] ) type ( move ( items [ i ] ) );

		type* from = scratch;
		type* to = items;

		for ( int32_t width = run; width < count; width *= 2 ) {

			for ( int32_t left = 0; left < count; left += width * 2 ) {

				int32_t middle = min ( left + width, count );
				int32_t right = min ( left + width * 2, count );
				int32_t a = left;
				int32_t b = middle;
				int32_t out = left;

				while ( a < middle && b < right )
					to [ out++ ] = move ( before ( from [ b ], from [ a ] ) ? from [ b++ ] : from [ a++ ] );

				while ( a < middle )
					to [ out++ ] = move ( from [ a++ ] );

				while ( b < right )
					to [ out++ ] = move ( from [ b++ ] );

			}

			type* swap_with = from;

			from = to;
			to = swap_with;

		}

		if ( from != items ) {

			for ( int32_t i = 0; i < count; i++ )
				items [ i ] = move ( from [ i ] );

		}

		for ( int32_t i = 0; i < count; i++ )
			scratch [ i ].~type ( );

		runtime_release ( scratch );

	}

}