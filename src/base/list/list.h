#pragma once

#include "base/types/types.h"
#include "base/utility/utility.h"

namespace base {

	template < typename type >
	class list {

	public:

		list ( ) = default;

		list ( const list& other ) {

			copy_from ( other );

		}

		list ( list&& other ) noexcept : items ( other.items ), length ( other.length ), room ( other.room ) {

			other.items = nullptr;
			other.length = 0;
			other.room = 0;

		}

		list& operator= ( const list& other ) {

			if ( this != &other ) {

				clear ( );
				copy_from ( other );

			}

			return *this;

		}

		list& operator= ( list&& other ) noexcept {

			if ( this != &other ) {

				release ( );
				items = other.items;
				length = other.length;
				room = other.room;
				other.items = nullptr;
				other.length = 0;
				other.room = 0;

			}

			return *this;

		}

		~list ( ) {

			release ( );

		}

		void reserve ( size_t wanted ) {

			if ( wanted <= room )
				return;

			type* fresh = static_cast < type* > ( runtime_allocate ( sizeof ( type ) * wanted ) );

			for ( size_t i = 0; i < length; i++ ) {

				new ( &fresh [ i ] ) type ( move ( items [ i ] ) );
				items [ i ].~type ( );

			}

			runtime_release ( items );
			items = fresh;
			room = wanted;

		}

		void push ( const type& value ) {

			if ( length == room ) {

				type held = value;

				grow ( );
				new ( &items [ length++ ] ) type ( move ( held ) );

				return;

			}

			new ( &items [ length++ ] ) type ( value );

		}

		void push ( type&& value ) {

			if ( length == room )
				grow ( );

			new ( &items [ length++ ] ) type ( move ( value ) );

		}

		type& emplace ( ) {

			if ( length == room )
				grow ( );

			new ( &items [ length ] ) type ( );

			return items [ length++ ];

		}

		void resize ( size_t count ) {

			if ( count > length ) {

				reserve ( count );

				for ( size_t i = length; i < count; i++ )
					new ( &items [ i ] ) type ( );

			} else {

				for ( size_t i = count; i < length; i++ )
					items [ i ].~type ( );

			}

			length = count;

		}

		void fill ( size_t count, const type& value ) {

			clear ( );
			reserve ( count );

			for ( size_t i = 0; i < count; i++ )
				new ( &items [ i ] ) type ( value );

			length = count;

		}

		void insert ( size_t index, const type& value ) {

			if ( index > length )
				index = length;

			type held = value;

			push ( type ( ) );

			for ( size_t i = length - 1; i > index; i-- )
				items [ i ] = move ( items [ i - 1 ] );

			items [ index ] = move ( held );

		}

		void remove_at ( size_t index ) {

			if ( index >= length )
				return;

			for ( size_t i = index + 1; i < length; i++ )
				items [ i - 1 ] = move ( items [ i ] );

			items [ --length ].~type ( );

		}

		void pop ( ) {

			if ( length )
				items [ --length ].~type ( );

		}

		void clear ( ) {

			for ( size_t i = 0; i < length; i++ )
				items [ i ].~type ( );

			length = 0;

		}

		type& operator[] ( size_t index ) {

			return items [ index ];

		}

		const type& operator[] ( size_t index ) const {

			return items [ index ];

		}

		type& back ( ) {

			return items [ length - 1 ];

		}

		const type& back ( ) const {

			return items [ length - 1 ];

		}

		type* data ( ) {

			return items;

		}

		const type* data ( ) const {

			return items;

		}

		type* begin ( ) {

			return items;

		}

		type* end ( ) {

			return items + length;

		}

		const type* begin ( ) const {

			return items;

		}

		const type* end ( ) const {

			return items + length;

		}

		size_t size ( ) const {

			return length;

		}

		int32_t count ( ) const {

			return static_cast < int32_t > ( length );

		}

		bool empty ( ) const {

			return length == 0;

		}

	private:

		void grow ( ) {

			reserve ( room < 8 ? 8 : room * 2 );

		}

		void copy_from ( const list& other ) {

			reserve ( other.length );

			for ( size_t i = 0; i < other.length; i++ )
				new ( &items [ i ] ) type ( other.items [ i ] );

			length = other.length;

		}

		void release ( ) {

			clear ( );
			runtime_release ( items );
			items = nullptr;
			room = 0;

		}

		type* items = nullptr;
		size_t length = 0;
		size_t room = 0;

	};

}