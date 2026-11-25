#include "analysis/database.hh"

namespace analysis {

	static void demangle ( const char* mangled, wchar_t* out, int32_t room ) {

		const char* at = mangled;
		int32_t length = 0;

		auto put = [ & ] ( const char* from, int32_t count ) {

			for ( int32_t i = 0; i < count && length < room - 1; i++ )
				out [ length++ ] = static_cast < wchar_t > ( from [ i ] );

		};

		auto identifier = [ ] ( const char* from, int32_t count ) {

			if ( count <= 0 || from [ 0 ] == '?' || ( from [ 0 ] >= '0' && from [ 0 ] <= '9' ) )
				return false;

			for ( int32_t i = 0; i < count; i++ ) {

				char c = from [ i ];

				if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
					return false;

			}

			return true;

		};

		if ( at [ 0 ] == '.' && at [ 1 ] == '?' && at [ 2 ] == 'A' && ( at [ 3 ] == 'V' || at [ 3 ] == 'U' ) )
			at += 4;

		int32_t total = 0;

		while ( at [ total ] )
			total++;

		if ( at [ 0 ] == '?' && at [ 1 ] == '$' ) {

			const char* name = at + 2;
			int32_t name_length = 0;

			while ( name [ name_length ] && name [ name_length ] != '@' )
				name_length++;

			if ( total >= 4 && at [ total - 1 ] == '@' && at [ total - 2 ] == '@' ) {

				int32_t scope_end = total - 2;
				int32_t scope_start = scope_end;

				while ( scope_start > 0 && at [ scope_start - 1 ] != '@' )
					scope_start--;

				if ( scope_start > name_length + 2 && identifier ( at + scope_start, scope_end - scope_start ) ) {

					put ( at + scope_start, scope_end - scope_start );
					put ( "::", 2 );

				}

			}

			put ( name, name_length );

			if ( length < room - 3 ) {

				out [ length++ ] = L'<';
				out [ length++ ] = L'>';

			}

			out [ length ] = 0;

			return;

		}

		const char* parts [ 16 ];
		int32_t lengths [ 16 ];
		int32_t count = 0;

		while ( *at && *at != '@' && count < 16 ) {

			parts [ count ] = at;

			while ( *at && *at != '@' )
				at++;

			lengths [ count ] = static_cast < int32_t > ( at - parts [ count ] );
			count++;

			if ( *at == '@' )
				at++;

		}

		for ( int32_t i = count - 1; i >= 0; i-- ) {

			if ( i != count - 1 )
				put ( "::", 2 );

			put ( parts [ i ], lengths [ i ] );

		}

		if ( !length )
			put ( mangled, total );

		out [ length ] = 0;

	}

	static bool read_name ( const loader::binary& file, uint64_t address, char* out, int32_t room ) {

		uint64_t offset = 0;

		if ( !file.offset_of ( address, offset ) )
			return false;

		int32_t length = 0;

		while ( length < room - 1 && offset + static_cast < uint64_t > ( length ) < file.size ) {

			char c = static_cast < char > ( file.byte ( offset + static_cast < uint64_t > ( length ) ) );

			if ( !c )
				break;

			if ( c < 0x20 || c > 0x7E )
				return false;

			out [ length++ ] = c;

		}

		out [ length ] = 0;

		return length > 4 && out [ 0 ] == '.' && out [ 1 ] == '?' && out [ 2 ] == 'A';

	}

	int32_t database::class_at_vtable ( uint64_t address ) const {

		for ( int32_t i = 0; i < classes.count ( ); i++ ) {

			if ( classes [ static_cast < size_t > ( i ) ].vtable == address )
				return i;

		}

		return -1;

	}

	int32_t database::virtual_of ( uint64_t function ) const {

		int32_t low = 0;
		int32_t high = virtuals.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = virtuals [ static_cast < size_t > ( middle ) ].function;

			if ( value == function )
				return middle;

			if ( value < function )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return -1;

	}

	void database::find_classes ( ) {

		classes.clear ( );
		class_bases.clear ( );
		virtuals.clear ( );

		if ( !file )
			return;

		const loader::binary& image = *file;
		uint32_t pointer = long_mode ? 8 : 4;
		char mangled [ 512 ];
		wchar_t name [ 512 ];

		auto read_pointer = [ & ] ( uint64_t address, uint64_t& value ) {

			uint64_t offset = 0;

			if ( !image.offset_of ( address, offset ) || offset + pointer > image.size )
				return false;

			value = pointer == 8 ? image.read64 ( offset ) : image.read32 ( offset );

			return true;

		};

		auto read_u32 = [ & ] ( uint64_t address, uint32_t& value ) {

			uint64_t offset = 0;

			if ( !image.offset_of ( address, offset ) || offset + 4 > image.size )
				return false;

			value = image.read32 ( offset );

			return true;

		};

		auto resolve = [ & ] ( uint32_t raw ) {

			return long_mode ? image.base + raw : static_cast < uint64_t > ( raw );

		};

		auto locator_valid = [ & ] ( uint64_t locator, uint64_t& descriptor, int32_t& offset_value ) {

			uint32_t signature = 0;
			uint32_t offset = 0;
			uint32_t type = 0;
			uint32_t self = 0;

			if ( !read_u32 ( locator, signature ) || signature != ( long_mode ? 1u : 0u ) )
				return false;

			read_u32 ( locator + 4, offset );
			read_u32 ( locator + 12, type );

			if ( long_mode && ( !read_u32 ( locator + 20, self ) || image.base + self != locator ) )
				return false;

			descriptor = resolve ( type );
			offset_value = static_cast < int32_t > ( offset );

			return read_name ( image, descriptor + pointer * 2, mangled, 512 );

		};

		for ( int32_t s = 1; s < image.sections.count ( ); s++ ) {

			const loader::section& section = image.sections [ static_cast < size_t > ( s ) ];

			if ( section.type == loader::kind::CODE || !section.raw )
				continue;

			uint64_t raw = base::min ( section.raw, image.size - section.offset );

			for ( uint64_t p = 0; p + pointer * 2 <= raw; p += pointer ) {

				uint64_t value = pointer == 8 ? image.read64 ( section.offset + p ) : image.read32 ( section.offset + p );
				int32_t target_section = image.section_at ( value );

				if ( target_section <= 0 || image.sections [ static_cast < size_t > ( target_section ) ].type == loader::kind::CODE )
					continue;

				uint64_t descriptor = 0;
				int32_t offset_value = 0;

				if ( !locator_valid ( value, descriptor, offset_value ) )
					continue;

				uint64_t vtable = section.address + p + pointer;
				uint64_t first = 0;

				if ( !read_pointer ( vtable, first ) || map_of ( first ) < 0 )
					continue;

				class_info& item = classes.emplace ( );

				demangle ( mangled, name, 512 );
				item.name.set ( name );
				item.locator = value;
				item.vtable = vtable;
				item.offset = offset_value;
				item.first_base = class_bases.count ( );

				for ( int32_t k = 0; k < 4096; k++ ) {

					uint64_t entry = 0;

					if ( !read_pointer ( vtable + static_cast < uint64_t > ( k ) * pointer, entry ) || map_of ( entry ) < 0 )
						break;

					if ( k > 0 && image.section_at ( vtable + static_cast < uint64_t > ( k ) * pointer ) != image.section_at ( vtable ) )
						break;

					if ( k > 0 ) {

						int32_t first_ref = 0;
						int32_t ref_count = 0;

						refs_to ( vtable + static_cast < uint64_t > ( k ) * pointer, first_ref, ref_count );

						if ( ref_count )
							break;

					}

					item.methods++;

				}

				uint32_t hierarchy = 0;
				uint32_t count = 0;
				uint32_t array = 0;

				if ( read_u32 ( value + 16, hierarchy ) && read_u32 ( resolve ( hierarchy ) + 8, count ) && read_u32 ( resolve ( hierarchy ) + 12, array ) ) {

					for ( uint32_t b = 1; b < count && b < 64; b++ ) {

						uint32_t descriptor_ref = 0;
						uint32_t type = 0;

						if ( !read_u32 ( resolve ( array ) + b * 4, descriptor_ref ) || !read_u32 ( resolve ( descriptor_ref ), type ) )
							break;

						if ( !read_name ( image, resolve ( type ) + pointer * 2, mangled, 512 ) )
							break;

						demangle ( mangled, name, 512 );
						class_bases.push ( base::text ( name ) );
						item.base_count++;

					}

				}

			}

		}

		for ( int32_t c = 0; c < classes.count ( ); c++ ) {

			class_info& item = classes [ static_cast < size_t > ( c ) ];

			for ( int32_t other = 0; other < classes.count ( ); other++ ) {

				uint64_t next = classes [ static_cast < size_t > ( other ) ].vtable;

				if ( next > item.vtable && next - pointer < item.vtable + static_cast < uint64_t > ( item.methods ) * pointer )
					item.methods = static_cast < int32_t > ( ( next - pointer - item.vtable ) / pointer );

			}

		}

		for ( int32_t c = 0; c < classes.count ( ); c++ ) {

			const class_info& item = classes [ static_cast < size_t > ( c ) ];

			for ( int32_t k = 0; k < item.methods; k++ ) {

				uint64_t entry = 0;

				if ( !read_pointer ( item.vtable + static_cast < uint64_t > ( k ) * pointer, entry ) || function_starting ( entry ) < 0 )
					continue;

				virtual_slot slot;

				slot.function = entry;
				slot.owner = c;
				slot.slot = k;
				virtuals.push ( slot );

			}

		}

		size_t count = virtuals.size ( );
		base::list < virtual_slot > scratch;

		scratch.resize ( count );

		virtual_slot* from = virtuals.data ( );
		virtual_slot* to = scratch.data ( );

		for ( size_t width = 1; width < count; width *= 2 ) {

			for ( size_t low = 0; low < count; low += width * 2 ) {

				size_t middle = base::min ( low + width, count );
				size_t high = base::min ( low + width * 2, count );
				size_t a = low;
				size_t b = middle;
				size_t out_index = low;

				while ( a < middle && b < high )
					to [ out_index++ ] = from [ a ].function <= from [ b ].function ? from [ a++ ] : from [ b++ ];

				while ( a < middle )
					to [ out_index++ ] = from [ a++ ];

				while ( b < high )
					to [ out_index++ ] = from [ b++ ];

			}

			virtual_slot* held = from;

			from = to;
			to = held;

		}

		if ( from != virtuals.data ( ) ) {

			for ( size_t i = 0; i < count; i++ )
				virtuals [ i ] = from [ i ];

		}

		size_t kept = 0;

		for ( size_t i = 0; i < virtuals.size ( ); i++ ) {

			if ( kept && virtuals [ kept - 1 ].function == virtuals [ i ].function ) {

				const class_info& current = classes [ static_cast < size_t > ( virtuals [ kept - 1 ].owner ) ];
				const class_info& candidate = classes [ static_cast < size_t > ( virtuals [ i ].owner ) ];

				if ( candidate.offset < current.offset || ( candidate.offset == current.offset && candidate.base_count < current.base_count ) )
					virtuals [ kept - 1 ] = virtuals [ i ];

				continue;

			}

			virtuals [ kept++ ] = virtuals [ i ];

		}

		virtuals.resize ( kept );

	}

}