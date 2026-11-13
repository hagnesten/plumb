#include "analysis/database.hh"
#include <windows.h>

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

	static void atomic_set ( uint64_t* words, uint64_t index ) {

		__atomic_fetch_or ( &words [ index >> 6 ], 1ull << ( index & 63 ), __ATOMIC_RELAXED );

	}

	static bool atomic_test ( const uint64_t* words, uint64_t index ) {

		return ( __atomic_load_n ( &words [ index >> 6 ], __ATOMIC_RELAXED ) >> ( index & 63 ) ) & 1;

	}

	static void atomic_cover ( uint64_t* words, uint64_t from, uint64_t count ) {

		uint64_t end = from + count;

		while ( from < end ) {

			uint64_t shift = from & 63;
			uint64_t take = base::min ( 64 - shift, end - from );
			uint64_t mask = ( take == 64 ? ~0ull : ( ( 1ull << take ) - 1 ) ) << shift;

			__atomic_fetch_or ( &words [ from >> 6 ], mask, __ATOMIC_RELAXED );
			from += take;

		}

	}

	static bool any_covered ( const uint64_t* words, uint64_t from, uint64_t count ) {

		for ( uint64_t i = 0; i < count; i++ ) {

			if ( atomic_test ( words, from + i ) )
				return true;

		}

		return false;

	}

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

	static void unique_addresses ( base::list < uint64_t >& items ) {

		radix_sort ( items, [ ] ( uint64_t value ) { return value; } );

		size_t kept = 0;

		for ( size_t i = 0; i < items.size ( ); i++ ) {

			if ( !kept || items [ kept - 1 ] != items [ i ] )
				items [ kept++ ] = items [ i ];

		}

		items.resize ( kept );

	}

	static int32_t slot_import ( const engine& e, uint64_t address ) {

		int32_t low = 0;
		int32_t high = e.slots.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = e.slots [ static_cast < size_t > ( middle ) ].address;

			if ( value == address )
				return e.slots [ static_cast < size_t > ( middle ) ].import;

			if ( value < address )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return -1;

	}

	static bool in_image ( const engine& e, uint64_t address ) {

		if ( address < e.image_low || address >= e.image_high )
			return false;

		return e.file->section_at ( address ) > 0;

	}

	static void note_ref ( worker& w, uint64_t target, uint64_t source, ref type ) {

		uint64_t base_address = w.owner->file->base;
		xref item;

		item.target = static_cast < uint32_t > ( target - base_address );
		item.source = static_cast < uint32_t > ( source - base_address );
		item.type = type;
		w.refs.push ( item );

	}

	static void mark_label ( engine& e, uint64_t target ) {

		int32_t index = e.db->map_of ( target );

		if ( index < 0 )
			return;

		code_map& map = e.db->maps [ static_cast < size_t > ( index ) ];

		atomic_set ( map.labels, target - map.start );

	}

	static void data_refs ( worker& w, const x86::instruction& ins ) {

		engine& e = *w.owner;
		uint64_t target = 0;

		if ( ins.memory_target ( target ) && in_image ( e, target ) ) {

			ref type = ref::READ;

			if ( ins.name == x86::mnemonic::lea )
				type = ref::ADDRESS;
			else {

				for ( int32_t i = 0; i < ins.count; i++ ) {

					if ( ins.operands [ i ].type == x86::kind::MEM && ( ins.operands [ i ].access & x86::WRITE ) )
						type = ref::WRITE;

				}

			}

			if ( ( ins.control == x86::flow::CALL || ins.control == x86::flow::JUMP ) && slot_import ( e, target ) >= 0 )
				type = ins.control == x86::flow::CALL ? ref::CALL : ref::JUMP;

			note_ref ( w, target, ins.address, type );

		}

		for ( int32_t i = 0; i < ins.count; i++ ) {

			const x86::operand& item = ins.operands [ i ];

			if ( item.type != x86::kind::IMM || item.size < 4 )
				continue;

			uint64_t value = static_cast < uint64_t > ( item.value );

			if ( !e.long_mode )
				value &= 0xFFFFFFFFull;

			if ( in_image ( e, value ) )
				note_ref ( w, value, ins.address, ref::ADDRESS );

		}

	}

	static bool lea_value ( const x86::instruction& ins, uint64_t& value ) {

		if ( ins.name != x86::mnemonic::lea || ins.count < 2 || ins.operands [ 0 ].type != x86::kind::REG )
			return false;

		const x86::operand& memory = ins.operands [ 1 ];

		if ( !memory.rip_relative ( ) )
			return false;

		value = ins.next ( ) + static_cast < uint64_t > ( memory.value );

		return true;

	}

	static int32_t find_def ( const worker& w, int32_t before, uint8_t reg ) {

		for ( int32_t i = before - 1; i >= 0; i-- ) {

			const x86::instruction& ins = w.ring [ i ];

			if ( ins.count && ins.operands [ 0 ].type == x86::kind::REG && x86::same_family ( ins.operands [ 0 ].reg, reg ) && ( ins.operands [ 0 ].access & x86::WRITE ) )
				return i;

		}

		return -1;

	}

	static bool register_value ( const worker& w, int32_t before, uint8_t reg, uint64_t& value ) {

		int32_t at = find_def ( w, before, reg );

		return at >= 0 && lea_value ( w.ring [ at ], value );

	}

	static bool read_entry ( const loader::binary& file, uint64_t address, int32_t size, uint64_t& value ) {

		uint64_t offset = 0;

		if ( !file.offset_of ( address, offset ) || offset + static_cast < uint64_t > ( size ) > file.size )
			return false;

		value = size == 8 ? file.read64 ( offset ) : ( size == 4 ? file.read32 ( offset ) : file.byte ( offset ) );

		return true;

	}

	static uint64_t entry_target ( const jump_table& table, uint64_t base_address, uint64_t raw ) {

		switch ( table.type ) {

			case table_kind::IMAGE_BASED:
				return base_address + static_cast < uint32_t > ( raw );

			case table_kind::TABLE_BASED:
				return table.anchor + static_cast < uint64_t > ( static_cast < int64_t > ( static_cast < int32_t > ( raw ) ) );

			default:
				return raw;

		}

	}

	static void resolve_table ( worker& w, const x86::instruction& jump ) {

		engine& e = *w.owner;
		const loader::binary& file = *e.file;
		int32_t last = w.ring_count - 1;
		jump_table table;
		uint8_t index_reg = 0;
		int32_t load_at = -1;
		const x86::operand& target = jump.operands [ 0 ];

		table.jump = jump.address;

		if ( target.type == x86::kind::MEM ) {

			uint8_t pointer = static_cast < uint8_t > ( e.long_mode ? 8 : 4 );

			if ( target.base != x86::NONE || target.index == x86::NONE || target.scale != pointer )
				return;

			table.table = e.long_mode ? static_cast < uint64_t > ( target.value ) : static_cast < uint32_t > ( target.value );
			table.type = table_kind::POINTERS;
			table.entry_size = pointer;
			index_reg = target.index;
			load_at = last;

		} else if ( target.type == x86::kind::REG ) {

			int32_t add_at = find_def ( w, last, target.reg );

			if ( add_at < 0 )
				return;

			const x86::instruction& add = w.ring [ add_at ];

			if ( add.name != x86::mnemonic::add || add.count != 2 || add.operands [ 1 ].type != x86::kind::REG )
				return;

			uint8_t other = add.operands [ 1 ].reg;
			uint64_t base_value = 0;

			load_at = find_def ( w, add_at, target.reg );

			if ( load_at < 0 )
				return;

			const x86::instruction& load = w.ring [ load_at ];

			if ( ( load.name != x86::mnemonic::mov && load.name != x86::mnemonic::movsxd ) || load.count != 2 || load.operands [ 1 ].type != x86::kind::MEM )
				return;

			const x86::operand& memory = load.operands [ 1 ];

			if ( memory.index == x86::NONE || memory.scale != 4 || memory.size != 4 || memory.base == x86::NONE )
				return;

			bool same_base = x86::same_family ( memory.base, other );

			if ( !register_value ( w, add_at, other, base_value ) ) {

				if ( !same_base || !e.long_mode || memory.value <= 0 || !in_image ( e, file.base + static_cast < uint64_t > ( memory.value ) ) )
					return;

				base_value = file.base;

			}

			uint64_t table_base = 0;

			if ( same_base )
				table_base = base_value;
			else if ( !register_value ( w, load_at, memory.base, table_base ) )
				return;

			table.table = table_base + static_cast < uint64_t > ( memory.value );
			table.anchor = base_value;
			table.entry_size = 4;
			index_reg = memory.index;

			if ( base_value == file.base )
				table.type = table_kind::IMAGE_BASED;
			else if ( base_value == table.table )
				table.type = table_kind::TABLE_BASED;
			else
				return;

		} else
			return;

		uint8_t bound_reg = index_reg;
		int32_t search_from = load_at;
		int32_t index_def = find_def ( w, load_at, index_reg );

		if ( index_def >= 0 ) {

			const x86::instruction& d = w.ring [ index_def ];

			if ( d.name == x86::mnemonic::movzx && d.count == 2 && d.operands [ 1 ].type == x86::kind::MEM && d.operands [ 1 ].size == 1 ) {

				const x86::operand& memory = d.operands [ 1 ];

				if ( memory.index != x86::NONE && memory.base != x86::NONE ) {

					uint64_t byte_base = 0;

					if ( !register_value ( w, index_def, memory.base, byte_base ) ) {

						if ( table.type != table_kind::IMAGE_BASED )
							return;

						byte_base = file.base;

					}

					table.index_table = byte_base + static_cast < uint64_t > ( memory.value );
					bound_reg = memory.index;

				} else if ( memory.base != x86::NONE && memory.index == x86::NONE && !e.long_mode ) {

					table.index_table = static_cast < uint32_t > ( memory.value );
					bound_reg = memory.base;

				}

				search_from = index_def;

			}

		}

		uint32_t bound = 0;
		bool bounded = false;

		for ( int32_t i = search_from - 1; i >= 0 && !bounded; i-- ) {

			const x86::instruction& c = w.ring [ i ];

			if ( c.name == x86::mnemonic::cmp && c.count == 2 && c.operands [ 0 ].type == x86::kind::REG && x86::same_family ( c.operands [ 0 ].reg, bound_reg ) && c.operands [ 1 ].type == x86::kind::IMM ) {

				uint64_t limit = static_cast < uint64_t > ( c.operands [ 1 ].value );

				for ( int32_t k = i + 1; k < search_from; k++ ) {

					x86::mnemonic name = w.ring [ k ].name;

					if ( name == x86::mnemonic::ja || name == x86::mnemonic::jg ) {

						bound = static_cast < uint32_t > ( limit + 1 );
						bounded = true;

						break;

					}

					if ( name == x86::mnemonic::jae || name == x86::mnemonic::jge ) {

						bound = static_cast < uint32_t > ( limit );
						bounded = true;

						break;

					}

				}

				break;

			}

			const x86::instruction& d = w.ring [ i ];

			if ( d.count && d.operands [ 0 ].type == x86::kind::REG && x86::same_family ( d.operands [ 0 ].reg, bound_reg ) && ( d.operands [ 0 ].access & x86::WRITE ) && d.name != x86::mnemonic::mov && d.name != x86::mnemonic::movsxd && d.name != x86::mnemonic::movzx )
				break;

		}

		if ( bounded && ( bound == 0 || bound > 8192 ) )
			return;

		if ( table.index_table ) {

			uint32_t limit = 0;

			if ( !bounded && table.index_table > table.table && ( table.index_table - table.table ) % table.entry_size == 0 )
				limit = static_cast < uint32_t > ( ( table.index_table - table.table ) / table.entry_size );

			if ( !bounded && ( !limit || limit > 4096 ) )
				return;

			uint32_t entries = bounded ? bound : 1024;
			uint32_t widest = 0;
			uint32_t taken = 0;

			for ( uint32_t k = 0; k < entries; k++ ) {

				uint64_t value = 0;

				if ( !read_entry ( file, table.index_table + k, 1, value ) )
					break;

				if ( !bounded && value >= limit )
					break;

				widest = base::max ( widest, static_cast < uint32_t > ( value ) );
				taken++;

			}

			if ( !taken )
				return;

			table.index_count = taken;
			table.count = bounded ? widest + 1 : limit;
			bounded = true;

		} else
			table.count = bounded ? bound : 512;

		uint32_t valid = 0;

		for ( uint32_t k = 0; k < table.count; k++ ) {

			uint64_t raw = 0;

			if ( !read_entry ( file, table.table + static_cast < uint64_t > ( k ) * table.entry_size, table.entry_size, raw ) )
				break;

			uint64_t destination = entry_target ( table, file.base, raw );

			if ( e.db->map_of ( destination ) < 0 )
				break;

			valid++;

		}

		if ( !valid || ( bounded && valid < table.count && table.index_table ) )
			return;

		table.count = valid;

		for ( uint32_t k = 0; k < table.count; k++ ) {

			uint64_t raw = 0;

			read_entry ( file, table.table + static_cast < uint64_t > ( k ) * table.entry_size, table.entry_size, raw );

			uint64_t destination = entry_target ( table, file.base, raw );

			mark_label ( e, destination );
			note_ref ( w, destination, jump.address, ref::TABLE );
			w.stack.push ( destination );

		}

		w.found.push ( table );

	}

	static void walk ( worker& w, uint64_t seed ) {

		engine& e = *w.owner;
		database& db = *e.db;

		w.stack.clear ( );
		w.stack.push ( seed );

		while ( !w.stack.empty ( ) ) {

			uint64_t at = w.stack.back ( );

			w.stack.pop ( );
			w.ring_count = 0;

			for ( ;; ) {

				int32_t index = db.map_of ( at );

				if ( index < 0 )
					break;

				code_map& map = db.maps [ static_cast < size_t > ( index ) ];
				uint64_t offset = at - map.start;

				if ( offset >= map.decodable || atomic_test ( map.covered, offset ) )
					break;

				x86::instruction ins;

				if ( !x86::decode ( e.file->data + map.offset + offset, static_cast < size_t > ( map.decodable - offset ), at, e.long_mode, ins ) )
					break;

				if ( any_covered ( map.covered, offset + 1, static_cast < uint64_t > ( ins.length - 1 ) ) )
					break;

				atomic_set ( map.starts, offset );
				atomic_cover ( map.covered, offset, ins.length );
				w.decoded++;

				if ( w.ring_count == 24 ) {

					for ( int32_t i = 1; i < 24; i++ )
						w.ring [ i - 1 ] = w.ring [ i ];

					w.ring_count = 23;

				}

				w.ring [ w.ring_count++ ] = ins;
				data_refs ( w, ins );

				bool stop = false;

				switch ( ins.control ) {

					case x86::flow::CALL:

						if ( ins.direct ( ) ) {

							uint64_t target = ins.target ( );

							if ( db.map_of ( target ) >= 0 ) {

								note_ref ( w, target, at, ref::CALL );
								w.calls.push ( target );

							}

						} else {

							uint64_t slot = 0;

							if ( ins.memory_target ( slot ) ) {

								int32_t import = slot_import ( e, slot );

								if ( import >= 0 && is_noreturn_name ( e.file->imports [ static_cast < size_t > ( import ) ].name.get ( ) ) )
									stop = true;

							}

						}

						break;

					case x86::flow::JUMP:

						if ( ins.direct ( ) ) {

							uint64_t target = ins.target ( );

							if ( db.map_of ( target ) >= 0 ) {

								note_ref ( w, target, at, ref::JUMP );
								mark_label ( e, target );
								w.stack.push ( target );

							}

						} else if ( ins.count && ( ins.operands [ 0 ].type == x86::kind::REG || ( ins.operands [ 0 ].type == x86::kind::MEM && !ins.operands [ 0 ].rip_relative ( ) ) ) )
							resolve_table ( w, ins );

						stop = true;

						break;

					case x86::flow::BRANCH:

						if ( ins.direct ( ) ) {

							uint64_t target = ins.target ( );

							if ( db.map_of ( target ) >= 0 ) {

								note_ref ( w, target, at, ref::BRANCH );
								mark_label ( e, target );
								w.stack.push ( target );

							}

						}

						break;

					case x86::flow::RETURN:
					case x86::flow::STOP:
						stop = true;
						break;

					default:
						break;

				}

				if ( stop )
					break;

				at = ins.next ( );

			}

		}

	}

	static unsigned long __stdcall thread_entry ( void* parameter ) {

		worker& w = *static_cast < worker* > ( parameter );
		engine& e = *w.owner;
		long total = e.queue.count ( );

		for ( ;; ) {

			long index = __atomic_fetch_add ( &e.next, 1, __ATOMIC_RELAXED );

			if ( index >= total )
				break;

			walk ( w, e.queue [ static_cast < size_t > ( index ) ] );

			if ( e.report )
				__atomic_fetch_add ( &e.report->done, 1, __ATOMIC_RELAXED );

		}

		return 0;

	}

	static void run_round ( engine& e, base::list < worker >& workers ) {

		if ( e.queue.empty ( ) )
			return;

		e.next = 0;

		if ( e.report ) {

			__atomic_store_n ( &e.report->done, 0, __ATOMIC_RELAXED );
			__atomic_store_n ( &e.report->total, static_cast < long > ( e.queue.count ( ) ), __ATOMIC_RELAXED );

		}

		HANDLE handles [ 64 ];
		int32_t count = 0;
		int32_t wanted = e.queue.count ( ) < 64 ? 1 : workers.count ( );

		for ( int32_t i = 1; i < wanted; i++ ) {

			HANDLE handle = CreateThread ( nullptr, 0, thread_entry, &workers [ static_cast < size_t > ( i ) ], 0, nullptr );

			if ( handle )
				handles [ count++ ] = handle;

		}

		thread_entry ( &workers [ 0 ] );

		if ( count )
			WaitForMultipleObjects ( static_cast < DWORD > ( count ), handles, TRUE, INFINITE );

		for ( int32_t i = 0; i < count; i++ )
			CloseHandle ( handles [ i ] );

	}

	static void follow_calls ( engine& e, base::list < worker >& workers ) {

		for ( int32_t round = 0; round < 32; round++ ) {

			base::list < uint64_t > fresh;

			for ( worker& w : workers ) {

				for ( uint64_t target : w.calls ) {

					int32_t index = e.db->map_of ( target );

					if ( index >= 0 && !atomic_test ( e.db->maps [ static_cast < size_t > ( index ) ].covered, target - e.db->maps [ static_cast < size_t > ( index ) ].start ) )
						fresh.push ( target );

				}

				w.calls.clear ( );

			}

			if ( fresh.empty ( ) )
				break;

			unique_addresses ( fresh );
			e.queue = base::move ( fresh );
			run_round ( e, workers );

		}

	}

	static bool padding_byte ( uint8_t value ) {

		return value == 0xCC || value == 0x90 || value == 0x00;

	}

	static bool looks_like_entry ( const engine& e, const code_map& map, uint64_t offset ) {

		const uint8_t* at = e.file->data + map.offset + offset;
		uint64_t room = map.decodable - offset;

		if ( room < 4 )
			return false;

		if ( !e.long_mode ) {

			if ( at [ 0 ] == 0x55 && at [ 1 ] == 0x8B && at [ 2 ] == 0xEC )
				return true;

			if ( at [ 0 ] == 0x8B && at [ 1 ] == 0xFF && at [ 2 ] == 0x55 && room >= 5 && at [ 3 ] == 0x8B && at [ 4 ] == 0xEC )
				return true;

			return false;

		}

		if ( at [ 0 ] == 0x48 && at [ 1 ] == 0x89 && ( at [ 2 ] == 0x5C || at [ 2 ] == 0x4C || at [ 2 ] == 0x54 || at [ 2 ] == 0x6C || at [ 2 ] == 0x74 ) && at [ 3 ] == 0x24 )
			return true;

		if ( at [ 0 ] == 0x48 && at [ 1 ] == 0x83 && at [ 2 ] == 0xEC )
			return true;

		if ( at [ 0 ] == 0x48 && at [ 1 ] == 0x81 && at [ 2 ] == 0xEC )
			return true;

		if ( at [ 0 ] == 0x40 && ( at [ 1 ] == 0x53 || at [ 1 ] == 0x55 || at [ 1 ] == 0x56 || at [ 1 ] == 0x57 ) )
			return true;

		if ( ( at [ 0 ] == 0x48 || at [ 0 ] == 0x4C ) && at [ 1 ] == 0x8B && ( at [ 2 ] == 0xC4 || at [ 2 ] == 0xDC ) )
			return true;

		if ( at [ 0 ] == 0x55 && at [ 1 ] == 0x48 && at [ 2 ] == 0x8B && at [ 3 ] == 0xEC )
			return true;

		return false;

	}

	static void scan_prologues ( engine& e, base::list < uint64_t >& out ) {

		database& db = *e.db;

		for ( const code_map& map : db.maps ) {

			uint64_t offset = 0;

			while ( offset < map.decodable ) {

				if ( atomic_test ( map.covered, offset ) ) {

					uint64_t end = db.covered_end ( map.start + offset, map.start + map.decodable );

					offset = end - map.start;

					continue;

				}

				uint64_t gap_end = db.uncovered_end ( map.start + offset, map.start + map.decodable ) - map.start;
				bool fresh = true;

				for ( uint64_t at = offset; at < gap_end; at++ ) {

					uint8_t value = e.file->data [ map.offset + at ];

					if ( padding_byte ( value ) ) {

						fresh = true;

						continue;

					}

					if ( fresh && looks_like_entry ( e, map, at ) )
						out.push ( map.start + at );

					fresh = false;

				}

				offset = gap_end;

			}

		}

	}

	static void scan_pointers ( engine& e, base::list < uint64_t >& seeds, bool record, base::list < xref >& found ) {

		const loader::binary& file = *e.file;
		database& db = *e.db;
		uint32_t step = e.long_mode ? 8 : 4;

		for ( int32_t s = 1; s < file.sections.count ( ); s++ ) {

			const loader::section& item = file.sections [ static_cast < size_t > ( s ) ];

			if ( item.type == loader::kind::CODE || !item.raw )
				continue;

			uint64_t raw = base::min ( item.raw, file.size - item.offset );

			for ( uint64_t p = 0; p + step <= raw; p += step ) {

				uint64_t value = step == 8 ? file.read64 ( item.offset + p ) : file.read32 ( item.offset + p );
				int32_t index = db.map_of ( value );

				if ( index < 0 )
					continue;

				const code_map& map = db.maps [ static_cast < size_t > ( index ) ];
				uint64_t offset = value - map.start;

				if ( offset >= map.decodable )
					continue;

				if ( record ) {

					if ( atomic_test ( map.starts, offset ) ) {

						xref entry;

						entry.target = static_cast < uint32_t > ( value - file.base );
						entry.source = static_cast < uint32_t > ( item.address + p - file.base );
						entry.type = ref::POINTER;
						found.push ( entry );

					}

					continue;

				}

				if ( atomic_test ( map.covered, offset ) )
					continue;

				uint8_t before = offset ? file.data [ map.offset + offset - 1 ] : 0xCC;
				bool boundary = !offset || before == 0xCC || before == 0x90 || before == 0xC3 || atomic_test ( map.covered, offset - 1 );

				if ( !boundary ) {

					for ( int32_t side = -1; side <= 1 && !boundary; side += 2 ) {

						int64_t neighbor = static_cast < int64_t > ( p ) + side * static_cast < int64_t > ( step );

						if ( neighbor < 0 || static_cast < uint64_t > ( neighbor ) + step > raw )
							continue;

						uint64_t other = step == 8 ? file.read64 ( item.offset + static_cast < uint64_t > ( neighbor ) ) : file.read32 ( item.offset + static_cast < uint64_t > ( neighbor ) );
						int32_t other_map = db.map_of ( other );

						if ( other_map >= 0 && other != value && other - db.maps [ static_cast < size_t > ( other_map ) ].start < db.maps [ static_cast < size_t > ( other_map ) ].decodable )
							boundary = true;

					}

				}

				if ( boundary )
					seeds.push ( value );

			}

		}

	}

	static void read_pdata ( engine& e, base::list < seed_entry >& seeds, base::list < chunk_info >& chunks ) {

		const loader::binary& file = *e.file;

		if ( !e.long_mode || file.directory_count < 4 || !file.directories [ 3 ].address )
			return;

		uint64_t at = 0;

		if ( !file.offset_of ( file.base + file.directories [ 3 ].address, at ) )
			return;

		uint32_t count = file.directories [ 3 ].size / 12;

		for ( uint32_t i = 0; i < count; i++ ) {

			uint64_t entry = at + static_cast < uint64_t > ( i ) * 12;

			if ( entry + 12 > file.size )
				break;

			uint32_t begin = file.read32 ( entry );
			uint32_t end = file.read32 ( entry + 4 );
			uint32_t unwind = file.read32 ( entry + 8 );

			if ( !begin || end <= begin )
				continue;

			uint32_t root = begin;
			bool chained = false;
			uint32_t walk = unwind;

			for ( int32_t depth = 0; depth < 32; depth++ ) {

				uint64_t info = 0;

				if ( ( walk & 1 ) || !file.offset_of ( file.base + walk, info ) || info + 4 > file.size )
					break;

				uint8_t flags = static_cast < uint8_t > ( file.byte ( info ) >> 3 );

				if ( !( flags & 4 ) )
					break;

				uint32_t codes = file.byte ( info + 2 );
				uint64_t parent = info + 4 + static_cast < uint64_t > ( ( codes + 1 ) & ~1u ) * 2;

				if ( parent + 12 > file.size )
					break;

				chained = true;
				root = file.read32 ( parent );
				walk = file.read32 ( parent + 8 );

			}

			if ( chained && root != begin ) {

				chunk_info chunk;

				chunk.start = file.base + begin;
				chunk.end = file.base + end;
				chunk.owner = file.base + root;
				chunks.push ( chunk );

				continue;

			}

			seed_entry seed;

			seed.address = file.base + begin;
			seed.end = file.base + end;
			seed.flags = FROM_PDATA;
			seeds.push ( seed );

		}

	}

	static void build_ranks ( code_map& map ) {

		uint64_t blocks = map.words / 8 + 2;

		map.start_rank = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * blocks ) );
		map.label_rank = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * blocks ) );

		uint32_t starts = 0;
		uint32_t labels = 0;

		for ( uint64_t w = 0; w < map.words; w++ ) {

			if ( ( w & 7 ) == 0 ) {

				map.start_rank [ w >> 3 ] = starts;
				map.label_rank [ w >> 3 ] = labels;

			}

			starts += static_cast < uint32_t > ( __builtin_popcountll ( map.starts [ w ] ) );
			labels += static_cast < uint32_t > ( __builtin_popcountll ( map.labels [ w ] ) );

		}

		map.start_rank [ ( map.words + 7 ) >> 3 ] = starts;
		map.label_rank [ ( map.words + 7 ) >> 3 ] = labels;

	}

	bool database::build ( const loader::binary& source, progress* report ) {

		clear ( );

		double started = base::seconds ( );

		file = &source;
		x86::prepare ( );

		if ( ( source.type != loader::format::PE32 && source.type != loader::format::PE64 ) || ( source.arch != loader::machine::X86 && source.arch != loader::machine::X64 ) ) {

			ready = true;

			return false;

		}

		long_mode = source.arch == loader::machine::X64;

		for ( int32_t i = 1; i < source.sections.count ( ); i++ ) {

			const loader::section& item = source.sections [ static_cast < size_t > ( i ) ];

			if ( item.type != loader::kind::CODE || !item.raw )
				continue;

			code_map& map = maps.emplace ( );

			map.start = item.address;
			map.size = base::max ( item.size, item.raw );
			map.decodable = base::min ( item.size ? item.size : item.raw, item.raw );
			map.offset = item.offset;
			map.section = i;
			map.words = ( map.size + 63 ) / 64 + 1;
			map.starts = static_cast < uint64_t* > ( runtime_allocate_zero ( sizeof ( uint64_t ) * map.words ) );
			map.covered = static_cast < uint64_t* > ( runtime_allocate_zero ( sizeof ( uint64_t ) * map.words ) );
			map.labels = static_cast < uint64_t* > ( runtime_allocate_zero ( sizeof ( uint64_t ) * map.words ) );
			code_bytes += map.decodable;

		}

		engine e;

		e.file = &source;
		e.db = this;
		e.report = report;
		e.long_mode = long_mode;
		e.image_low = source.low ( );
		e.image_high = source.high ( );

		for ( int32_t i = 0; i < source.imports.count ( ); i++ ) {

			slot_entry item = { source.imports [ static_cast < size_t > ( i ) ].slot, i };

			e.slots.push ( item );

		}

		base::sort ( e.slots.data ( ), e.slots.count ( ), [ ] ( const slot_entry& a, const slot_entry& b ) { return a.address < b.address; } );

		SYSTEM_INFO system = { };

		GetSystemInfo ( &system );
		threads = base::clamp ( static_cast < int32_t > ( system.dwNumberOfProcessors ), 1, 32 );

		base::list < worker > workers;

		workers.resize ( static_cast < size_t > ( threads ) );

		for ( worker& w : workers )
			w.owner = &e;

		base::list < seed_entry > seeds;

		for ( const loader::symbol& item : source.functions ) {

			seed_entry seed;

			seed.address = item.address;
			seed.end = 0;
			seed.flags = item.type == loader::symbol_kind::ENTRY ? FROM_ENTRY : ( item.type == loader::symbol_kind::TLS ? FROM_TLS : ( item.type == loader::symbol_kind::DEBUG ? FROM_SYMBOLS : FROM_EXPORT ) );
			seeds.push ( seed );

		}

		read_pdata ( e, seeds, chunks );

		if ( report )
			report->stage = 1;

		for ( const seed_entry& seed : seeds )
			e.queue.push ( seed.address );

		for ( const chunk_info& chunk : chunks )
			e.queue.push ( chunk.start );

		unique_addresses ( e.queue );
		run_round ( e, workers );
		follow_calls ( e, workers );

		base::list < uint64_t > pointer_seeds;

		scan_pointers ( e, pointer_seeds, false, refs );
		unique_addresses ( pointer_seeds );
		e.queue = pointer_seeds;
		run_round ( e, workers );
		follow_calls ( e, workers );

		bool has_pdata = false;

		for ( const seed_entry& seed : seeds ) {

			if ( seed.flags & FROM_PDATA )
				has_pdata = true;

		}

		base::list < uint64_t > prologue_seeds;

		if ( !has_pdata ) {

			for ( int32_t pass = 0; pass < 4; pass++ ) {

				base::list < uint64_t > found;

				scan_prologues ( e, found );

				if ( found.empty ( ) )
					break;

				for ( uint64_t value : found )
					prologue_seeds.push ( value );

				e.queue = base::move ( found );
				run_round ( e, workers );
				follow_calls ( e, workers );

			}

		}

		if ( report )
			report->stage = 2;

		size_t total_refs = 0;

		for ( worker& w : workers ) {

			total_refs += w.refs.size ( );
			decoded += w.decoded;

		}

		base::list < xref > pointer_refs;

		scan_pointers ( e, pointer_seeds, true, pointer_refs );
		total_refs += pointer_refs.size ( );

		base::list < keyed_ref > keyed;

		keyed.reserve ( total_refs );

		auto add_keys = [ & ] ( const base::list < xref >& list ) {

			for ( const xref& item : list ) {

				keyed_ref entry;

				entry.key = ( static_cast < uint64_t > ( item.target ) << 32 ) | item.source;
				entry.type = static_cast < uint8_t > ( item.type );
				keyed.push ( entry );

			}

		};

		for ( worker& w : workers ) {

			add_keys ( w.refs );
			w.refs.clear ( );

		}

		add_keys ( pointer_refs );
		pointer_refs.clear ( );
		radix_sort ( keyed, [ ] ( const keyed_ref& item ) { return item.key; } );
		refs.reserve ( keyed.size ( ) );

		for ( size_t i = 0; i < keyed.size ( ); i++ ) {

			if ( i && keyed [ i ].key == keyed [ i - 1 ].key && keyed [ i ].type == keyed [ i - 1 ].type )
				continue;

			xref item;

			item.target = static_cast < uint32_t > ( keyed [ i ].key >> 32 );
			item.source = static_cast < uint32_t > ( keyed [ i ].key );
			item.type = static_cast < ref > ( keyed [ i ].type );
			refs.push ( item );

		}

		keyed.clear ( );

		for ( worker& w : workers ) {

			for ( const jump_table& table : w.found ) {

				bool duplicate = false;

				for ( const jump_table& existing : tables ) {

					if ( existing.jump == table.jump )
						duplicate = true;

				}

				if ( !duplicate )
					tables.push ( table );

			}

		}

		base::sort ( tables.data ( ), tables.count ( ), [ ] ( const jump_table& a, const jump_table& b ) { return a.table < b.table; } );

		if ( report )
			report->stage = 3;

		for ( const xref& item : refs ) {

			if ( item.type == ref::CALL ) {

				seed_entry seed;

				seed.address = source.base + item.target;
				seed.end = 0;
				seed.flags = FROM_CALL;
				seeds.push ( seed );

			}

		}

		for ( uint64_t value : pointer_seeds ) {

			seed_entry seed = { value, 0, FROM_POINTER };

			seeds.push ( seed );

		}

		for ( uint64_t value : prologue_seeds ) {

			seed_entry seed = { value, 0, FROM_PROLOGUE };

			seeds.push ( seed );

		}

		base::list < uint64_t > order;

		for ( size_t i = 0; i < seeds.size ( ); i++ )
			order.push ( ( static_cast < uint64_t > ( seeds [ i ].address - source.base ) << 24 ) | i );

		radix_sort ( order, [ ] ( uint64_t value ) { return value; } );

		for ( size_t k = 0; k < order.size ( ); k++ ) {

			const seed_entry& seed = seeds [ static_cast < size_t > ( order [ k ] & 0xFFFFFF ) ];

			if ( !is_start ( seed.address ) )
				continue;

			if ( !functions.empty ( ) && functions.back ( ).start == seed.address ) {

				function_info& last = functions.back ( );

				last.flags |= seed.flags;

				if ( seed.end )
					last.end = seed.end;

				continue;

			}

			if ( !functions.empty ( ) && ( functions.back ( ).flags & FROM_PDATA ) && seed.address < functions.back ( ).end && !( seed.flags & ( FROM_PDATA | FROM_EXPORT | FROM_ENTRY ) ) )
				continue;

			function_info& item = functions.emplace ( );

			item.start = seed.address;
			item.end = seed.end;
			item.flags = seed.flags;

		}

		auto settle = [ & ] ( int32_t i ) {


			function_info& item = functions [ static_cast < size_t > ( i ) ];
			uint64_t next = i + 1 < functions.count ( ) ? functions [ static_cast < size_t > ( i + 1 ) ].start : ~0ull;

			if ( !item.end || item.end > next ) {

				int32_t index = map_of ( item.start );
				uint64_t limit = index >= 0 ? maps [ static_cast < size_t > ( index ) ].start + maps [ static_cast < size_t > ( index ) ].decodable : item.start + 1;

				item.end = base::min ( covered_end ( item.start, base::min ( next, limit ) ), next );

				if ( item.end <= item.start )
					item.end = item.start + 1;

			}

			x86::instruction first;

			if ( decode ( item.start, first ) && first.control == x86::flow::JUMP && first.count && first.operands [ 0 ].type == x86::kind::MEM ) {

				uint64_t slot = 0;

				if ( first.memory_target ( slot ) ) {

					int32_t import = slot_import ( e, slot );

					if ( import >= 0 ) {

						item.flags |= THUNK;
						item.import = import;

					}

				}

			}

		};

		for ( int32_t i = 0; i < functions.count ( ); i++ )
			settle ( i );

		base::list < function_info > orphans;

		for ( const code_map& map : maps ) {

			uint64_t pos = map.start;
			uint64_t stop = map.start + map.decodable;

			while ( pos < stop ) {

				if ( !is_covered ( pos ) ) {

					pos = uncovered_end ( pos, stop );

					continue;

				}

				uint64_t run_end = covered_end ( pos, stop );

				while ( pos < run_end ) {

					int32_t owner = function_at ( pos );

					if ( owner >= 0 ) {

						pos = base::max ( functions [ static_cast < size_t > ( owner ) ].end, pos + 1 );

						continue;

					}

					if ( !is_start ( pos ) ) {

						pos++;

						continue;

					}

					function_info item;

					item.start = pos;
					item.flags = FROM_JUMP;
					orphans.push ( item );

					int32_t low = 0;
					int32_t high = functions.count ( );

					while ( low < high ) {

						int32_t middle = ( low + high ) / 2;

						if ( functions [ static_cast < size_t > ( middle ) ].start <= pos )
							low = middle + 1;
						else
							high = middle;

					}

					uint64_t next = low < functions.count ( ) ? functions [ static_cast < size_t > ( low ) ].start : run_end;

					pos = base::min ( base::max ( next, pos + 1 ), run_end );

				}

			}

		}

		if ( !orphans.empty ( ) ) {

			base::list < function_info > merged;
			int32_t a = 0;
			int32_t b = 0;

			merged.reserve ( functions.size ( ) + orphans.size ( ) );

			while ( a < functions.count ( ) || b < orphans.count ( ) ) {

				if ( b >= orphans.count ( ) || ( a < functions.count ( ) && functions [ static_cast < size_t > ( a ) ].start < orphans [ static_cast < size_t > ( b ) ].start ) )
					merged.push ( functions [ static_cast < size_t > ( a++ ) ] );
				else
					merged.push ( orphans [ static_cast < size_t > ( b++ ) ] );

			}

			functions = base::move ( merged );

			for ( int32_t i = 0; i < functions.count ( ); i++ ) {

				if ( !functions [ static_cast < size_t > ( i ) ].end )
					settle ( i );

			}

		}

		for ( const loader::symbol& item : source.functions ) {

			int32_t index = function_starting ( item.address );

			if ( index < 0 || functions [ static_cast < size_t > ( index ) ].name >= 0 )
				continue;

			functions [ static_cast < size_t > ( index ) ].name = names.count ( );
			functions [ static_cast < size_t > ( index ) ].flags |= NAMED;
			names.push ( item.name );

		}

		for ( code_map& map : maps ) {

			for ( uint64_t w = 0; w < map.words; w++ )
				map.labels [ w ] &= map.starts [ w ];

		}

		for ( const function_info& item : functions ) {

			int32_t index = map_of ( item.start );

			if ( index >= 0 ) {

				code_map& map = maps [ static_cast < size_t > ( index ) ];
				uint64_t offset = item.start - map.start;

				map.labels [ offset >> 6 ] &= ~( 1ull << ( offset & 63 ) );

			}

		}

		for ( const chunk_info& item : chunks ) {

			int32_t index = map_of ( item.start );

			if ( index >= 0 ) {

				code_map& map = maps [ static_cast < size_t > ( index ) ];
				uint64_t offset = item.start - map.start;

				map.labels [ offset >> 6 ] &= ~( 1ull << ( offset & 63 ) );

			}

		}

		base::sort ( chunks.data ( ), chunks.count ( ), [ ] ( const chunk_info& a, const chunk_info& b ) { return a.start < b.start; } );

		for ( code_map& map : maps )
			build_ranks ( map );

		for ( function_info& item : functions ) {

			int32_t index = map_of ( item.start );

			if ( index < 0 )
				continue;

			const code_map& map = maps [ static_cast < size_t > ( index ) ];
			uint64_t low = item.start - map.start;
			uint64_t high = base::min ( item.end, map.start + map.size ) - map.start;

			item.instructions = static_cast < uint32_t > ( rank ( map.starts, map.start_rank, high ) - rank ( map.starts, map.start_rank, low ) );
			item.blocks = static_cast < uint32_t > ( rank ( map.labels, map.label_rank, high ) - rank ( map.labels, map.label_rank, low ) ) + 1;

			int32_t first = 0;
			int32_t count = 0;

			refs_to ( item.start, first, count );

			for ( int32_t k = 0; k < count; k++ ) {

				if ( refs [ static_cast < size_t > ( first + k ) ].type == ref::CALL )
					item.callers++;

			}

		}

		find_classes ( );
		seconds = base::seconds ( ) - started;
		ready = true;

		return true;

	}

}