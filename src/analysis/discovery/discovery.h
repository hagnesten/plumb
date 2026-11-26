#pragma once

#include "analysis/database/database.h"

namespace analysis {

	struct slot_entry {

		uint64_t address;
		int32_t import;

	};

	struct seed_entry {

		uint64_t address;
		uint64_t end;
		uint32_t flags;

	};

	struct keyed_ref {

		uint64_t key;
		uint8_t type;

	};

	struct engine;

	struct worker {

		engine* owner = nullptr;
		base::list < xref > refs;
		base::list < uint64_t > calls;
		base::list < jump_table > found;
		base::list < uint64_t > stack;
		x86::instruction ring [ 24 ];
		int32_t ring_count = 0;
		uint64_t decoded = 0;

	};

	struct engine {

		const loader::binary* file = nullptr;
		database* db = nullptr;
		progress* report = nullptr;
		bool long_mode = true;
		uint64_t image_low = 0;
		uint64_t image_high = 0;
		base::list < slot_entry > slots;
		base::list < uint64_t > queue;
		volatile long next = 0;

	};

	void atomic_set ( uint64_t* words, uint64_t index );

	bool atomic_test ( const uint64_t* words, uint64_t index );

	void atomic_cover ( uint64_t* words, uint64_t from, uint64_t count );

	bool any_covered ( const uint64_t* words, uint64_t from, uint64_t count );

	template < typename record, typename key_of >
	static void radix_sort ( base::list < record >& items, key_of key ) {

		size_t count = items.size ( );

		if ( count < 2 )
			return;

		base::list < record > scratch;
		uint32_t* counts = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * 65536 ) );

		scratch.resize ( count );

		record* from = items.data ( );
		record* to = scratch.data ( );

		for ( int32_t pass = 0; pass < 4; pass++ ) {

			int32_t shift = pass * 16;
			bool needed = false;
			uint64_t first = ( key ( from [ 0 ] ) >> shift ) & 0xFFFF;

			for ( size_t i = 0; i < 65536; i++ )
				counts [ i ] = 0;

			for ( size_t i = 0; i < count; i++ ) {

				uint64_t digit = ( key ( from [ i ] ) >> shift ) & 0xFFFF;

				counts [ digit ]++;

				if ( digit != first )
					needed = true;

			}

			if ( !needed )
				continue;

			uint32_t total = 0;

			for ( size_t i = 0; i < 65536; i++ ) {

				uint32_t held = counts [ i ];

				counts [ i ] = total;
				total += held;

			}

			for ( size_t i = 0; i < count; i++ )
				to [ counts [ ( key ( from [ i ] ) >> shift ) & 0xFFFF ]++ ] = from [ i ];

			record* held = from;

			from = to;
			to = held;

		}

		if ( from != items.data ( ) ) {

			for ( size_t i = 0; i < count; i++ )
				items [ i ] = from [ i ];

		}

		runtime_release ( counts );

	}

	void unique_addresses ( base::list < uint64_t >& items );

	int32_t slot_import ( const engine& e, uint64_t address );

	bool in_image ( const engine& e, uint64_t address );

	void note_ref ( worker& w, uint64_t target, uint64_t source, ref type );

	void mark_label ( engine& e, uint64_t target );

	void data_refs ( worker& w, const x86::instruction& ins );

	bool lea_value ( const x86::instruction& ins, uint64_t& value );

	int32_t find_def ( const worker& w, int32_t before, uint8_t reg );

	bool register_value ( const worker& w, int32_t before, uint8_t reg, uint64_t& value );

	bool read_entry ( const loader::binary& file, uint64_t address, int32_t size, uint64_t& value );

	uint64_t entry_target ( const jump_table& table, uint64_t base_address, uint64_t raw );

	void resolve_table ( worker& w, const x86::instruction& jump );

	void walk ( worker& w, uint64_t seed );

	unsigned long __stdcall thread_entry ( void* parameter );

	void run_round ( engine& e, base::list < worker >& workers );

	void follow_calls ( engine& e, base::list < worker >& workers );

	bool padding_byte ( uint8_t value );

	bool looks_like_entry ( const engine& e, const code_map& map, uint64_t offset );

	void scan_prologues ( engine& e, base::list < uint64_t >& out );

	void scan_pointers ( engine& e, base::list < uint64_t >& seeds, bool record, base::list < xref >& found );

	void read_pdata ( engine& e, base::list < seed_entry >& seeds, base::list < chunk_info >& chunks );

	void build_ranks ( code_map& map );

}