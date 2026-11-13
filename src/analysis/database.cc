#include "analysis/database.hh"

namespace analysis {

	static int32_t ones ( uint64_t value ) {

		return __builtin_popcountll ( value );

	}

	static int32_t lowest ( uint64_t value ) {

		return __builtin_ctzll ( value );

	}

	static bool bit ( const uint64_t* words, uint64_t index ) {

		return ( words [ index >> 6 ] >> ( index & 63 ) ) & 1;

	}

	uint64_t rank ( const uint64_t* words, const uint32_t* directory, uint64_t index ) {

		uint64_t block = index >> 9;
		uint64_t word = index >> 6;
		uint64_t total = directory [ block ];

		for ( uint64_t w = block << 3; w < word; w++ )
			total += static_cast < uint64_t > ( ones ( words [ w ] ) );

		if ( index & 63 )
			total += static_cast < uint64_t > ( ones ( words [ word ] & ( ( 1ull << ( index & 63 ) ) - 1 ) ) );

		return total;

	}

	database::~database ( ) {

		clear ( );

	}

	void database::clear ( ) {

		for ( code_map& map : maps ) {

			runtime_release ( map.starts );
			runtime_release ( map.covered );
			runtime_release ( map.labels );
			runtime_release ( map.start_rank );
			runtime_release ( map.label_rank );

		}

		maps.clear ( );
		functions.clear ( );
		chunks.clear ( );
		refs.clear ( );
		tables.clear ( );
		names.clear ( );
		file = nullptr;
		ready = false;
		decoded = 0;
		code_bytes = 0;
		seconds = 0.0;

	}

	int32_t database::map_of ( uint64_t address ) const {

		for ( int32_t i = 0; i < maps.count ( ); i++ ) {

			const code_map& map = maps [ static_cast < size_t > ( i ) ];

			if ( address >= map.start && address < map.start + map.size )
				return i;

		}

		return -1;

	}

	bool database::is_start ( uint64_t address ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return false;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];

		return bit ( map.starts, address - map.start );

	}

	bool database::is_label ( uint64_t address ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return false;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];

		return bit ( map.labels, address - map.start );

	}

	bool database::is_covered ( uint64_t address ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return false;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];

		return bit ( map.covered, address - map.start );

	}

	uint64_t database::start_rank_at ( uint64_t address ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return 0;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];

		return rank ( map.starts, map.start_rank, address - map.start );

	}

	uint64_t database::row_rank ( uint64_t address ) const {

		int32_t index = map_of ( address );

		if ( index < 0 ) {

			if ( !maps.empty ( ) && address >= maps.back ( ).start + maps.back ( ).size ) {

				const code_map& map = maps.back ( );

				return rank ( map.starts, map.start_rank, map.size ) + rank ( map.labels, map.label_rank, map.size );

			}

			return 0;

		}

		const code_map& map = maps [ static_cast < size_t > ( index ) ];
		uint64_t offset = address - map.start;

		return rank ( map.starts, map.start_rank, offset ) + rank ( map.labels, map.label_rank, offset );

	}

	uint64_t database::row_address ( uint64_t from, uint64_t to, uint64_t row, bool& label ) const {

		int32_t index = map_of ( from );

		label = false;

		if ( index < 0 )
			return from;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];
		uint64_t low = from - map.start;
		uint64_t high = base::min ( to, map.start + map.size ) - map.start;
		uint64_t origin = rank ( map.starts, map.start_rank, low ) + rank ( map.labels, map.label_rank, low );
		uint64_t wanted = origin + row;
		uint64_t a = low;
		uint64_t b = high;

		while ( a + 1 < b ) {

			uint64_t middle = a + ( b - a ) / 2;
			uint64_t count = rank ( map.starts, map.start_rank, middle ) + rank ( map.labels, map.label_rank, middle );

			if ( count <= wanted )
				a = middle;
			else
				b = middle;

		}

		uint64_t before = rank ( map.starts, map.start_rank, a ) + rank ( map.labels, map.label_rank, a );

		label = bit ( map.labels, a ) && before == wanted;

		return map.start + a;

	}

	uint64_t database::instruction_at ( uint64_t address ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return address;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];
		uint64_t offset = address - map.start;

		if ( !bit ( map.covered, offset ) )
			return address;

		for ( uint64_t back = 0; back < 16 && back <= offset; back++ ) {

			if ( bit ( map.starts, offset - back ) )
				return address - back;

		}

		return address;

	}

	uint64_t database::covered_end ( uint64_t address, uint64_t limit ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return address;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];
		uint64_t offset = address - map.start;
		uint64_t stop = base::min ( limit, map.start + map.size ) - map.start;

		while ( offset < stop ) {

			uint64_t word = map.covered [ offset >> 6 ] >> ( offset & 63 );
			uint64_t inverse = ~word;

			if ( ( offset & 63 ) )
				inverse &= ( 1ull << ( 64 - ( offset & 63 ) ) ) - 1;

			if ( inverse ) {

				offset += static_cast < uint64_t > ( lowest ( inverse ) );

				break;

			}

			offset = ( offset | 63 ) + 1;

		}

		return map.start + base::min ( offset, stop );

	}

	uint64_t database::uncovered_end ( uint64_t address, uint64_t limit ) const {

		int32_t index = map_of ( address );

		if ( index < 0 )
			return limit;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];
		uint64_t offset = address - map.start;
		uint64_t stop = base::min ( limit, map.start + map.size ) - map.start;

		while ( offset < stop ) {

			uint64_t word = map.covered [ offset >> 6 ] >> ( offset & 63 );

			if ( word ) {

				offset += static_cast < uint64_t > ( lowest ( word ) );

				break;

			}

			offset = ( offset | 63 ) + 1;

		}

		return map.start + base::min ( offset, stop );

	}

	int32_t database::function_at ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = functions.count ( ) - 1;

		if ( high < 0 || functions [ 0 ].start > address )
			return -1;

		while ( low < high ) {

			int32_t middle = ( low + high + 1 ) / 2;

			if ( functions [ static_cast < size_t > ( middle ) ].start <= address )
				low = middle;
			else
				high = middle - 1;

		}

		const function_info& item = functions [ static_cast < size_t > ( low ) ];

		return address < item.end || address == item.start ? low : -1;

	}

	int32_t database::function_starting ( uint64_t address ) const {

		int32_t found = function_at ( address );

		return found >= 0 && functions [ static_cast < size_t > ( found ) ].start == address ? found : -1;

	}

	int32_t database::chunk_at ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = chunks.count ( ) - 1;

		if ( high < 0 || chunks [ 0 ].start > address )
			return -1;

		while ( low < high ) {

			int32_t middle = ( low + high + 1 ) / 2;

			if ( chunks [ static_cast < size_t > ( middle ) ].start <= address )
				low = middle;
			else
				high = middle - 1;

		}

		return address < chunks [ static_cast < size_t > ( low ) ].end ? low : -1;

	}

	int32_t database::table_at ( uint64_t address ) const {

		for ( int32_t i = 0; i < tables.count ( ); i++ ) {

			const jump_table& item = tables [ static_cast < size_t > ( i ) ];

			if ( address >= item.table && address < item.table + static_cast < uint64_t > ( item.count ) * item.entry_size )
				return i;

			if ( item.index_table && address >= item.index_table && address < item.index_table + item.index_count )
				return i;

		}

		return -1;

	}

	int32_t database::table_for_jump ( uint64_t address ) const {

		for ( int32_t i = 0; i < tables.count ( ); i++ ) {

			if ( tables [ static_cast < size_t > ( i ) ].jump == address )
				return i;

		}

		return -1;

	}

	void database::refs_to ( uint64_t target, int32_t& first, int32_t& count ) const {

		first = 0;
		count = 0;

		if ( !file || target < file->base || target - file->base > 0xFFFFFFFFull )
			return;

		uint32_t key = static_cast < uint32_t > ( target - file->base );
		int32_t low = 0;
		int32_t high = refs.count ( );

		while ( low < high ) {

			int32_t middle = ( low + high ) / 2;

			if ( refs [ static_cast < size_t > ( middle ) ].target < key )
				low = middle + 1;
			else
				high = middle;

		}

		first = low;

		while ( low + count < refs.count ( ) && refs [ static_cast < size_t > ( low + count ) ].target == key )
			count++;

	}

	bool database::decode ( uint64_t address, x86::instruction& out ) const {

		int32_t index = map_of ( address );

		if ( index < 0 || !file )
			return false;

		const code_map& map = maps [ static_cast < size_t > ( index ) ];
		uint64_t offset = address - map.start;

		if ( offset >= map.decodable )
			return false;

		return x86::decode ( file->data + map.offset + offset, static_cast < size_t > ( map.decodable - offset ), address, long_mode, out );

	}

	bool is_noreturn_name ( const wchar_t* name ) {

		static const wchar_t* const list [ ] = {
			L"ExitProcess", L"ExitThread", L"FreeLibraryAndExitThread", L"RtlExitUserThread", L"RtlExitUserProcess", L"NtTerminateProcess",
			L"exit", L"_exit", L"_Exit", L"abort", L"quick_exit", L"_invalid_parameter_noinfo_noreturn", L"__std_terminate", L"terminate",
			L"_CxxThrowException", L"__CxxFrameHandler_noreturn", L"RtlRaiseStatus", L"__fastfail", L"_amsg_exit", L"__report_gsfailure",
			L"__report_rangecheckfailure", L"__security_check_cookie_failure", L"longjmp", L"RaiseFailFastException"
		};

		if ( !name || !name [ 0 ] )
			return false;

		for ( const wchar_t* item : list ) {

			if ( base::same_text ( item, name ) )
				return true;

		}

		return false;

	}

	const wchar_t* ref_name ( ref type ) {

		switch ( type ) {

			case ref::CALL:
				return L"call";

			case ref::JUMP:
				return L"jump";

			case ref::BRANCH:
				return L"branch";

			case ref::READ:
				return L"read";

			case ref::WRITE:
				return L"write";

			case ref::ADDRESS:
				return L"address";

			case ref::POINTER:
				return L"pointer";

			default:
				return L"case";

		}

	}

}