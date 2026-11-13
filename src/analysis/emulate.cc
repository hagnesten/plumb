#include "analysis/emulate.hh"

namespace analysis {

	using x86::mnemonic;

	struct memory_page {

		uint64_t index = 0;
		uint8_t* data = nullptr;
		uint8_t* written = nullptr;
		uint64_t* writers = nullptr;

	};

	struct machine {

		const database* db = nullptr;
		const loader::binary* file = nullptr;
		bool wide = true;
		uint64_t r [ 16 ] = { };
		uint64_t rip = 0;
		uint64_t current = 0;
		bool cf = false;
		bool zf = false;
		bool sf = false;
		bool of = false;
		bool pf = false;
		bool df = false;
		bool fault = false;
		base::text reason;
		base::list < memory_page > pages;

		~machine ( ) {

			for ( memory_page& item : pages ) {

				runtime_release ( item.data );
				runtime_release ( item.written );
				runtime_release ( item.writers );

			}

		}

		void stop ( const wchar_t* why ) {

			if ( fault )
				return;

			fault = true;
			reason.set ( why );

		}

		memory_page* page ( uint64_t index ) {

			int32_t low = 0;
			int32_t high = pages.count ( ) - 1;

			while ( low <= high ) {

				int32_t middle = ( low + high ) / 2;
				uint64_t value = pages [ static_cast < size_t > ( middle ) ].index;

				if ( value == index )
					return &pages [ static_cast < size_t > ( middle ) ];

				if ( value < index )
					low = middle + 1;
				else
					high = middle - 1;

			}

			if ( pages.count ( ) >= 16384 ) {

				stop ( L"used more than 64 MB of memory" );

				return nullptr;

			}

			memory_page fresh;

			fresh.index = index;
			fresh.data = static_cast < uint8_t* > ( runtime_allocate_zero ( 4096 ) );
			fresh.written = static_cast < uint8_t* > ( runtime_allocate_zero ( 512 ) );

			uint64_t start = index * 4096;

			if ( start + 4096 > file->base && start < file->high ( ) ) {

				for ( uint64_t i = 0; i < 4096; i++ ) {

					uint64_t offset = 0;

					if ( file->offset_of ( start + i, offset ) && offset < file->size )
						fresh.data [ i ] = file->data [ offset ];

				}

			}

			pages.push ( fresh );

			for ( int32_t i = pages.count ( ) - 1; i > low; i-- )
				pages [ static_cast < size_t > ( i ) ] = pages [ static_cast < size_t > ( i - 1 ) ];

			pages [ static_cast < size_t > ( low ) ] = fresh;

			return &pages [ static_cast < size_t > ( low ) ];

		}

		uint8_t read8 ( uint64_t address ) {

			memory_page* item = page ( address >> 12 );

			return item ? item->data [ address & 4095 ] : 0;

		}

		void write8 ( uint64_t address, uint8_t value ) {

			memory_page* item = page ( address >> 12 );

			if ( !item )
				return;

			uint64_t at = address & 4095;

			if ( !item->writers )
				item->writers = static_cast < uint64_t* > ( runtime_allocate_zero ( 4096 * sizeof ( uint64_t ) ) );

			item->data [ at ] = value;
			item->written [ at >> 3 ] |= static_cast < uint8_t > ( 1 << ( at & 7 ) );
			item->writers [ at ] = current;

		}

		uint64_t read ( uint64_t address, int32_t size ) {

			uint64_t value = 0;

			for ( int32_t i = size - 1; i >= 0; i-- )
				value = ( value << 8 ) | read8 ( address + static_cast < uint64_t > ( i ) );

			return value;

		}

		void write ( uint64_t address, int32_t size, uint64_t value ) {

			for ( int32_t i = 0; i < size; i++ )
				write8 ( address + static_cast < uint64_t > ( i ), static_cast < uint8_t > ( value >> ( i * 8 ) ) );

		}

		static uint64_t mask ( int32_t size ) {

			return size >= 8 ? ~0ull : ( 1ull << ( size * 8 ) ) - 1;

		}

		static uint64_t sign ( int32_t size ) {

			return 1ull << ( size * 8 - 1 );

		}

		static int64_t extend ( uint64_t value, int32_t size ) {

			if ( size >= 8 )
				return static_cast < int64_t > ( value );

			int32_t shift = 64 - size * 8;

			return static_cast < int64_t > ( value << shift ) >> shift;

		}

		uint64_t reg ( uint8_t id ) {

			if ( id >= x86::AH && id < x86::ES )
				return ( r [ id - x86::AH ] >> 8 ) & 0xFF;

			int32_t family = x86::register_family ( id );

			if ( family < 0 || family >= 16 ) {

				stop ( L"uses a register the emulator doesn't model" );

				return 0;

			}

			return r [ family ] & mask ( x86::register_size ( id ) );

		}

		void set_reg ( uint8_t id, uint64_t value ) {

			if ( id >= x86::AH && id < x86::ES ) {

				uint64_t& slot = r [ id - x86::AH ];

				slot = ( slot & ~0xFF00ull ) | ( ( value & 0xFF ) << 8 );

				return;

			}

			int32_t family = x86::register_family ( id );

			if ( family < 0 || family >= 16 ) {

				stop ( L"uses a register the emulator doesn't model" );

				return;

			}

			int32_t size = x86::register_size ( id );

			if ( size >= 8 )
				r [ family ] = value;
			else if ( size == 4 )
				r [ family ] = value & 0xFFFFFFFFull;
			else
				r [ family ] = ( r [ family ] & ~mask ( size ) ) | ( value & mask ( size ) );

		}

		uint64_t effective ( const x86::operand& item, const x86::instruction& ins ) {

			if ( item.segment == x86::FS || item.segment == x86::GS ) {

				stop ( L"reads the thread block through fs or gs" );

				return 0;

			}

			uint64_t address = static_cast < uint64_t > ( item.value );

			if ( item.base == x86::RIP || item.base == x86::EIP )
				address += ins.next ( );
			else if ( item.base )
				address += reg ( item.base );

			if ( item.index )
				address += reg ( item.index ) * ( item.scale ? item.scale : 1 );

			return wide ? address : address & 0xFFFFFFFFull;

		}

		uint64_t get ( const x86::operand& item, const x86::instruction& ins ) {

			switch ( item.type ) {

				case x86::kind::REG:
					return reg ( item.reg );

				case x86::kind::MEM:
					return read ( effective ( item, ins ), item.size );

				case x86::kind::IMM:
					return static_cast < uint64_t > ( item.value );

				case x86::kind::REL:
					return ins.target ( );

				default:
					stop ( L"has an operand the emulator doesn't model" );
					return 0;

			}

		}

		void put ( const x86::operand& item, const x86::instruction& ins, uint64_t value ) {

			if ( item.type == x86::kind::REG )
				set_reg ( item.reg, value );
			else if ( item.type == x86::kind::MEM )
				write ( effective ( item, ins ), item.size, value );
			else
				stop ( L"writes to an operand that can't be written" );

		}

		void result_flags ( uint64_t value, int32_t size ) {

			value &= mask ( size );
			zf = value == 0;
			sf = ( value & sign ( size ) ) != 0;
			pf = ( __builtin_popcountll ( value & 0xFF ) & 1 ) == 0;

		}

		bool condition ( uint8_t code ) const {

			switch ( code ) {

				case 0: return of;
				case 1: return !of;
				case 2: return cf;
				case 3: return !cf;
				case 4: return zf;
				case 5: return !zf;
				case 6: return cf || zf;
				case 7: return !cf && !zf;
				case 8: return sf;
				case 9: return !sf;
				case 10: return pf;
				case 11: return !pf;
				case 12: return sf != of;
				case 13: return sf == of;
				case 14: return zf || sf != of;
				default: return !zf && sf == of;

			}

		}

		int32_t width ( ) const {

			return wide ? 8 : 4;

		}

		void push ( uint64_t value ) {

			r [ 4 ] -= static_cast < uint64_t > ( width ( ) );
			write ( r [ 4 ], width ( ), value );

		}

		uint64_t pop ( ) {

			uint64_t value = read ( r [ 4 ], width ( ) );

			r [ 4 ] += static_cast < uint64_t > ( width ( ) );

			return value;

		}

		bool import_slot ( uint64_t address ) const {

			for ( const loader::import& item : file->imports ) {

				if ( item.slot == address )
					return true;

			}

			return false;

		}

	};

	static int32_t operand_size ( const x86::instruction& ins, int32_t index ) {

		const x86::operand& item = ins.operands [ index ];

		if ( item.type == x86::kind::REG )
			return x86::register_size ( item.reg );

		return item.size ? item.size : ( ins.operand_size ? ins.operand_size : 4 );

	}

	static bool in_range ( mnemonic name, mnemonic first, mnemonic last ) {

		return static_cast < int32_t > ( name ) >= static_cast < int32_t > ( first ) && static_cast < int32_t > ( name ) <= static_cast < int32_t > ( last );

	}

	static void scan_strings ( machine& m, emulation& out ) {

		for ( memory_page& item : m.pages ) {

			if ( !item.writers )
				continue;

			int32_t i = 0;

			while ( i < 4096 && out.strings.count ( ) < 200 ) {

				auto written = [ & ] ( int32_t at ) {

					return at < 4096 && ( item.written [ at >> 3 ] & ( 1 << ( at & 7 ) ) );

				};

				auto printable = [ ] ( uint8_t value ) {

					return ( value >= 0x20 && value < 0x7F ) || value == 9;

				};

				int32_t narrow = 0;

				while ( i + narrow < 4096 && written ( i + narrow ) && printable ( item.data [ i + narrow ] ) && narrow < 200 )
					narrow++;

				int32_t wide_count = 0;

				while ( i + wide_count * 2 + 1 < 4096 && written ( i + wide_count * 2 ) && printable ( item.data [ i + wide_count * 2 ] ) && item.data [ i + wide_count * 2 + 1 ] == 0 && wide_count < 200 )
					wide_count++;

				if ( narrow >= 4 || wide_count >= 4 ) {

					bool is_wide = wide_count >= 4 && wide_count * 2 >= narrow;
					int32_t count = is_wide ? wide_count : narrow;
					emulated_string& found = out.strings.emplace ( );
					wchar_t text [ 201 ];

					for ( int32_t k = 0; k < count; k++ )
						text [ k ] = static_cast < wchar_t > ( item.data [ i + ( is_wide ? k * 2 : k ) ] );

					text [ count ] = 0;
					found.address = item.index * 4096 + static_cast < uint64_t > ( i );
					found.writer = item.writers [ i ];
					found.text.set ( text );
					found.wide = is_wide;
					i += is_wide ? count * 2 : count;

					continue;

				}

				i++;

			}

		}

	}

	bool emulate ( const database& db, uint64_t start, const uint64_t* seed, const bool* seeded, uint64_t step_limit, emulation& out ) {

		machine m;
		uint64_t sentinel = db.long_mode ? 0x00007FFE00000000ull : 0x7FFE0000ull;
		int32_t depth = 0;

		m.db = &db;
		m.file = db.file;
		m.wide = db.long_mode;
		m.rip = start;
		m.r [ 4 ] = db.long_mode ? 0x00007FFF00100000ull : 0x0FF00000ull;
		m.r [ 5 ] = m.r [ 4 ];

		for ( int32_t i = 0; i < 16; i++ ) {

			if ( seeded && seeded [ i ] )
				m.r [ i ] = seed [ i ];

		}

		if ( !m.wide ) {

			for ( int32_t i = 0; i < 4; i++ )
				m.push ( 0 );

		} else
			m.r [ 4 ] -= 0x20;

		m.push ( sentinel );

		while ( !m.fault && out.steps < step_limit ) {

			uint8_t bytes [ 16 ];
			x86::instruction ins;

			if ( m.rip == sentinel ) {

				out.returned = true;

				break;

			}

			for ( int32_t i = 0; i < 16; i++ )
				bytes [ i ] = m.read8 ( m.rip + static_cast < uint64_t > ( i ) );

			if ( m.rip < m.file->base || m.rip >= m.file->high ( ) ) {

				m.stop ( L"jumped outside the image" );

				break;

			}

			if ( !x86::decode ( bytes, 16, m.rip, m.wide, ins ) || !ins.valid ( ) ) {

				m.stop ( L"reached bytes that don't decode" );

				break;

			}

			m.current = m.rip;
			out.steps++;

			uint64_t next = ins.next ( );
			const x86::operand& a = ins.operands [ 0 ];
			const x86::operand& b = ins.operands [ 1 ];
			int32_t size = ins.count ? operand_size ( ins, 0 ) : m.width ( );
			uint64_t full = machine::mask ( size );
			mnemonic name = ins.name;
			bool rep = ( ins.prefixes & ( x86::REP | x86::REPNE ) ) != 0;

			if ( ins.condition != 0xFF && ins.control == x86::flow::BRANCH ) {

				m.rip = m.condition ( ins.condition ) ? ins.target ( ) : next;

				continue;

			}

			if ( ins.condition != 0xFF && in_range ( name, mnemonic::seto, mnemonic::setg ) ) {

				m.put ( a, ins, m.condition ( ins.condition ) ? 1 : 0 );
				m.rip = next;

				continue;

			}

			if ( ins.condition != 0xFF && in_range ( name, mnemonic::cmovo, mnemonic::cmovg ) ) {

				uint64_t value = m.get ( b, ins );

				if ( m.condition ( ins.condition ) )
					m.put ( a, ins, value );
				else if ( size == 4 && a.type == x86::kind::REG )
					m.put ( a, ins, m.get ( a, ins ) );

				m.rip = next;

				continue;

			}

			switch ( name ) {

				case mnemonic::nop:
				case mnemonic::endbr64:
				case mnemonic::endbr32:
				case mnemonic::lfence:
				case mnemonic::mfence:
				case mnemonic::sfence:
				case mnemonic::pause:
				case mnemonic::prefetchnta:
				case mnemonic::prefetcht0:
					break;

				case mnemonic::mov:
					m.put ( a, ins, m.get ( b, ins ) & full );
					break;

				case mnemonic::movzx:
					m.put ( a, ins, m.get ( b, ins ) & machine::mask ( operand_size ( ins, 1 ) ) );
					break;

				case mnemonic::movsx:
				case mnemonic::movsxd: {

					int32_t from = operand_size ( ins, 1 );

					m.put ( a, ins, static_cast < uint64_t > ( machine::extend ( m.get ( b, ins ) & machine::mask ( from ), from ) ) & full );
					break;

				}

				case mnemonic::lea:
					m.put ( a, ins, m.effective ( b, ins ) & full );
					break;

				case mnemonic::xchg: {

					uint64_t left = m.get ( a, ins );
					uint64_t right = m.get ( b, ins );

					m.put ( a, ins, right );
					m.put ( b, ins, left );
					break;

				}

				case mnemonic::add:
				case mnemonic::adc:
				case mnemonic::xadd: {

					uint64_t left = m.get ( a, ins ) & full;
					uint64_t right = m.get ( b, ins ) & full;
					uint64_t carry = name == mnemonic::adc && m.cf ? 1 : 0;
					uint64_t value = ( left + right + carry ) & full;

					m.cf = value < left || ( carry && value == left );
					m.of = ( ( left ^ value ) & ( right ^ value ) & machine::sign ( size ) ) != 0;
					m.result_flags ( value, size );

					if ( name == mnemonic::xadd )
						m.put ( b, ins, left );

					m.put ( a, ins, value );
					break;

				}

				case mnemonic::sub:
				case mnemonic::sbb:
				case mnemonic::cmp: {

					uint64_t left = m.get ( a, ins ) & full;
					uint64_t right = m.get ( b, ins ) & full;
					uint64_t borrow = name == mnemonic::sbb && m.cf ? 1 : 0;
					uint64_t value = ( left - right - borrow ) & full;

					m.cf = left < right || ( borrow && left == right );
					m.of = ( ( left ^ right ) & ( left ^ value ) & machine::sign ( size ) ) != 0;
					m.result_flags ( value, size );

					if ( name != mnemonic::cmp )
						m.put ( a, ins, value );

					break;

				}

				case mnemonic::and_:
				case mnemonic::or_:
				case mnemonic::xor_:
				case mnemonic::test: {

					uint64_t left = m.get ( a, ins );
					uint64_t right = m.get ( b, ins );
					uint64_t value = ( name == mnemonic::or_ ? left | right : ( name == mnemonic::xor_ ? left ^ right : left & right ) ) & full;

					m.cf = false;
					m.of = false;
					m.result_flags ( value, size );

					if ( name != mnemonic::test )
						m.put ( a, ins, value );

					break;

				}

				case mnemonic::not_:
					m.put ( a, ins, ~m.get ( a, ins ) & full );
					break;

				case mnemonic::neg: {

					uint64_t value = ( 0 - m.get ( a, ins ) ) & full;

					m.cf = value != 0;
					m.of = value == machine::sign ( size );
					m.result_flags ( value, size );
					m.put ( a, ins, value );
					break;

				}

				case mnemonic::inc:
				case mnemonic::dec: {

					uint64_t left = m.get ( a, ins ) & full;
					uint64_t value = ( name == mnemonic::inc ? left + 1 : left - 1 ) & full;

					m.of = name == mnemonic::inc ? value == machine::sign ( size ) : left == machine::sign ( size );
					m.result_flags ( value, size );
					m.put ( a, ins, value );
					break;

				}

				case mnemonic::shl:
				case mnemonic::shr:
				case mnemonic::sar:
				case mnemonic::rol:
				case mnemonic::ror: {

					uint64_t value = m.get ( a, ins ) & full;
					uint32_t bits = static_cast < uint32_t > ( size * 8 );
					uint32_t count = static_cast < uint32_t > ( ins.count > 1 ? m.get ( b, ins ) : 1 ) & ( size == 8 ? 63u : 31u );

					if ( !count )
						break;

					uint64_t result = 0;

					if ( name == mnemonic::shl ) {

						m.cf = count <= bits && ( ( value >> ( bits - count ) ) & 1 );
						result = count >= bits ? 0 : ( value << count ) & full;

					} else if ( name == mnemonic::shr ) {

						m.cf = ( value >> ( count - 1 ) ) & 1;
						result = count >= bits ? 0 : value >> count;

					} else if ( name == mnemonic::sar ) {

						int64_t signed_value = machine::extend ( value, size );

						m.cf = ( static_cast < uint64_t > ( signed_value >> base::min ( count - 1, 63u ) ) ) & 1;
						result = static_cast < uint64_t > ( signed_value >> base::min ( count, 63u ) ) & full;

					} else {

						uint32_t turn = count % bits;

						result = turn ? ( name == mnemonic::rol ? ( ( value << turn ) | ( value >> ( bits - turn ) ) ) : ( ( value >> turn ) | ( value << ( bits - turn ) ) ) ) & full : value;
						m.cf = name == mnemonic::rol ? ( result & 1 ) : ( ( result >> ( bits - 1 ) ) & 1 );
						m.put ( a, ins, result );

						break;

					}

					m.of = ( ( result ^ value ) & machine::sign ( size ) ) != 0;
					m.result_flags ( result, size );
					m.put ( a, ins, result );
					break;

				}

				case mnemonic::imul:
				case mnemonic::mul: {

					if ( name == mnemonic::imul && ins.count >= 2 ) {

						int64_t left = machine::extend ( m.get ( ins.count == 3 ? b : a, ins ) & full, size );
						int64_t right = machine::extend ( m.get ( ins.count == 3 ? ins.operands [ 2 ] : b, ins ) & machine::mask ( ins.count == 3 ? operand_size ( ins, 2 ) : size ), ins.count == 3 ? operand_size ( ins, 2 ) : size );
						__int128 product = static_cast < __int128 > ( left ) * right;
						uint64_t value = static_cast < uint64_t > ( product ) & full;

						m.cf = m.of = static_cast < __int128 > ( machine::extend ( value, size ) ) != product;
						m.put ( a, ins, value );

						break;

					}

					uint64_t multiplier = m.get ( a, ins ) & full;
					uint64_t accumulator = m.r [ 0 ] & full;

					if ( size == 8 ) {

						if ( name == mnemonic::mul ) {

							unsigned __int128 product = static_cast < unsigned __int128 > ( accumulator ) * multiplier;

							m.r [ 0 ] = static_cast < uint64_t > ( product );
							m.r [ 2 ] = static_cast < uint64_t > ( product >> 64 );
							m.cf = m.of = m.r [ 2 ] != 0;

						} else {

							__int128 product = static_cast < __int128 > ( static_cast < int64_t > ( accumulator ) ) * static_cast < int64_t > ( multiplier );

							m.r [ 0 ] = static_cast < uint64_t > ( product );
							m.r [ 2 ] = static_cast < uint64_t > ( product >> 64 );
							m.cf = m.of = static_cast < __int128 > ( static_cast < int64_t > ( m.r [ 0 ] ) ) != product;

						}

					} else {

						uint64_t product = name == mnemonic::mul ? accumulator * multiplier : static_cast < uint64_t > ( machine::extend ( accumulator, size ) * machine::extend ( multiplier, size ) );
						uint64_t low = product & full;
						uint64_t high = ( product >> ( size * 8 ) ) & full;

						if ( size == 1 )
							m.set_reg ( x86::AX, product & 0xFFFF );
						else {

							m.set_reg ( x86::resize_register ( x86::RAX, static_cast < uint8_t > ( size ) ), low );
							m.set_reg ( x86::resize_register ( x86::RDX, static_cast < uint8_t > ( size ) ), high );

						}

						m.cf = m.of = high != 0;

					}

					break;

				}

				case mnemonic::div:
				case mnemonic::idiv: {

					uint64_t divisor = m.get ( a, ins ) & full;

					if ( !divisor ) {

						m.stop ( L"divides by zero" );

						break;

					}

					if ( size == 1 ) {

						uint64_t dividend = m.reg ( x86::AX );

						if ( name == mnemonic::div ) {

							m.set_reg ( x86::AL, dividend / divisor );
							m.set_reg ( x86::AH, dividend % divisor );

						} else {

							int64_t top = machine::extend ( dividend, 2 );
							int64_t bottom = machine::extend ( divisor, 1 );

							m.set_reg ( x86::AL, static_cast < uint64_t > ( top / bottom ) );
							m.set_reg ( x86::AH, static_cast < uint64_t > ( top % bottom ) );

						}

						break;

					}

					uint64_t low = m.r [ 0 ] & full;
					uint64_t high = m.r [ 2 ] & full;

					if ( size == 8 ) {

						bool simple = name == mnemonic::div ? high == 0 : high == ( static_cast < int64_t > ( low ) < 0 ? ~0ull : 0 );

						if ( !simple ) {

							m.stop ( L"divides a 128-bit value" );

							break;

						}

						if ( name == mnemonic::div ) {

							m.r [ 0 ] = low / divisor;
							m.r [ 2 ] = low % divisor;

						} else {

							m.r [ 0 ] = static_cast < uint64_t > ( static_cast < int64_t > ( low ) / static_cast < int64_t > ( divisor ) );
							m.r [ 2 ] = static_cast < uint64_t > ( static_cast < int64_t > ( low ) % static_cast < int64_t > ( divisor ) );

						}

						break;

					}

					uint64_t dividend = ( high << ( size * 8 ) ) | low;
					uint64_t quotient = 0;
					uint64_t remainder = 0;

					if ( name == mnemonic::div ) {

						quotient = dividend / divisor;
						remainder = dividend % divisor;

					} else {

						int64_t top = machine::extend ( dividend, size * 2 );
						int64_t bottom = machine::extend ( divisor, size );

						quotient = static_cast < uint64_t > ( top / bottom );
						remainder = static_cast < uint64_t > ( top % bottom );

					}

					m.set_reg ( x86::resize_register ( x86::RAX, static_cast < uint8_t > ( size ) ), quotient );
					m.set_reg ( x86::resize_register ( x86::RDX, static_cast < uint8_t > ( size ) ), remainder );
					break;

				}

				case mnemonic::cbw:
					m.set_reg ( x86::AX, static_cast < uint64_t > ( machine::extend ( m.reg ( x86::AL ), 1 ) ) & 0xFFFF );
					break;

				case mnemonic::cwde:
					m.set_reg ( x86::EAX, static_cast < uint64_t > ( machine::extend ( m.reg ( x86::AX ), 2 ) ) & 0xFFFFFFFF );
					break;

				case mnemonic::cdqe:
					m.r [ 0 ] = static_cast < uint64_t > ( machine::extend ( m.r [ 0 ] & 0xFFFFFFFF, 4 ) );
					break;

				case mnemonic::cwd:
					m.set_reg ( x86::DX, ( m.reg ( x86::AX ) & 0x8000 ) ? 0xFFFF : 0 );
					break;

				case mnemonic::cdq:
					m.set_reg ( x86::EDX, ( m.r [ 0 ] & 0x80000000ull ) ? 0xFFFFFFFF : 0 );
					break;

				case mnemonic::cqo:
					m.r [ 2 ] = ( m.r [ 0 ] >> 63 ) ? ~0ull : 0;
					break;

				case mnemonic::bswap: {

					uint64_t value = m.get ( a, ins );

					m.put ( a, ins, size == 8 ? __builtin_bswap64 ( value ) : __builtin_bswap32 ( static_cast < uint32_t > ( value ) ) );
					break;

				}

				case mnemonic::bt:
				case mnemonic::bts:
				case mnemonic::btr:
				case mnemonic::btc: {

					uint64_t value = m.get ( a, ins );
					uint32_t bit = static_cast < uint32_t > ( m.get ( b, ins ) ) & static_cast < uint32_t > ( size * 8 - 1 );

					m.cf = ( value >> bit ) & 1;

					if ( name == mnemonic::bts )
						m.put ( a, ins, value | ( 1ull << bit ) );
					else if ( name == mnemonic::btr )
						m.put ( a, ins, value & ~( 1ull << bit ) );
					else if ( name == mnemonic::btc )
						m.put ( a, ins, value ^ ( 1ull << bit ) );

					break;

				}

				case mnemonic::bsf:
				case mnemonic::bsr:
				case mnemonic::tzcnt:
				case mnemonic::lzcnt:
				case mnemonic::popcnt: {

					uint64_t value = m.get ( b, ins ) & machine::mask ( operand_size ( ins, 1 ) );
					uint64_t result = 0;
					int32_t bits = operand_size ( ins, 1 ) * 8;

					if ( name == mnemonic::popcnt )
						result = static_cast < uint64_t > ( __builtin_popcountll ( value ) );
					else if ( name == mnemonic::tzcnt || name == mnemonic::bsf )
						result = value ? static_cast < uint64_t > ( __builtin_ctzll ( value ) ) : static_cast < uint64_t > ( bits );
					else if ( name == mnemonic::lzcnt )
						result = value ? static_cast < uint64_t > ( __builtin_clzll ( value ) - ( 64 - bits ) ) : static_cast < uint64_t > ( bits );
					else
						result = value ? static_cast < uint64_t > ( 63 - __builtin_clzll ( value ) ) : 0;

					m.zf = name == mnemonic::popcnt ? value == 0 : ( name == mnemonic::bsf || name == mnemonic::bsr ? value == 0 : result == 0 );
					m.cf = ( name == mnemonic::tzcnt || name == mnemonic::lzcnt ) && value == 0;

					if ( value || ( name != mnemonic::bsf && name != mnemonic::bsr ) )
						m.put ( a, ins, result );

					break;

				}

				case mnemonic::push:
					m.push ( m.get ( a, ins ) );
					break;

				case mnemonic::pop: {

					uint64_t value = m.pop ( );

					m.put ( a, ins, value );
					break;

				}

				case mnemonic::leave:
					m.r [ 4 ] = m.r [ 5 ];
					m.r [ 5 ] = m.pop ( );
					break;

				case mnemonic::cld:
					m.df = false;
					break;

				case mnemonic::std:
					m.df = true;
					break;

				case mnemonic::clc:
					m.cf = false;
					break;

				case mnemonic::stc:
					m.cf = true;
					break;

				case mnemonic::cmc:
					m.cf = !m.cf;
					break;

				case mnemonic::movsb:
				case mnemonic::movsw:
				case mnemonic::movsd:
				case mnemonic::movsq:
				case mnemonic::stosb:
				case mnemonic::stosw:
				case mnemonic::stosd:
				case mnemonic::stosq:
				case mnemonic::lodsb:
				case mnemonic::lodsw:
				case mnemonic::lodsd:
				case mnemonic::lodsq: {

					int32_t unit = ( name == mnemonic::movsb || name == mnemonic::stosb || name == mnemonic::lodsb ) ? 1 : ( ( name == mnemonic::movsw || name == mnemonic::stosw || name == mnemonic::lodsw ) ? 2 : ( ( name == mnemonic::movsd || name == mnemonic::stosd || name == mnemonic::lodsd ) ? 4 : 8 ) );
					bool moves = name == mnemonic::movsb || name == mnemonic::movsw || name == mnemonic::movsd || name == mnemonic::movsq;
					bool stores = name == mnemonic::stosb || name == mnemonic::stosw || name == mnemonic::stosd || name == mnemonic::stosq;
					uint64_t count = rep ? ( m.r [ 1 ] & ( m.wide ? ~0ull : 0xFFFFFFFFull ) ) : 1;
					int64_t step = m.df ? -unit : unit;

					if ( count > 0x1000000 ) {

						m.stop ( L"runs a string operation over more than 16 MB" );

						break;

					}

					for ( uint64_t k = 0; k < count; k++ ) {

						if ( moves )
							m.write ( m.r [ 7 ], unit, m.read ( m.r [ 6 ], unit ) );
						else if ( stores )
							m.write ( m.r [ 7 ], unit, m.r [ 0 ] & machine::mask ( unit ) );
						else
							m.set_reg ( x86::resize_register ( x86::RAX, static_cast < uint8_t > ( unit ) ), m.read ( m.r [ 6 ], unit ) );

						if ( moves || stores )
							m.r [ 7 ] += static_cast < uint64_t > ( step );

						if ( !stores )
							m.r [ 6 ] += static_cast < uint64_t > ( step );

					}

					if ( rep )
						m.r [ 1 ] = 0;

					break;

				}

				case mnemonic::call:
				case mnemonic::jmp: {

					uint64_t target = 0;
					uint64_t slot = 0;

					if ( a.type == x86::kind::MEM ) {

						slot = m.effective ( a, ins );
						target = m.read ( slot, m.width ( ) );

					} else
						target = m.get ( a, ins );

					bool external = ( slot && m.import_slot ( slot ) ) || target < m.file->base || target >= m.file->high ( );
					bool into_code = !external && db.is_start ( target );

					if ( name == mnemonic::call && into_code && depth < 256 ) {

						m.push ( next );
						m.rip = target;
						depth++;

						continue;

					}

					if ( name == mnemonic::jmp && into_code ) {

						m.rip = target;

						continue;

					}

					if ( !external && name == mnemonic::jmp ) {

						m.stop ( L"jumps somewhere that isn't code" );

						break;

					}

					emulated_call& made = out.calls.emplace ( );

					made.site = m.current;
					made.target = slot ? slot : target;

					if ( m.wide ) {

						made.arguments [ 0 ] = m.r [ 1 ];
						made.arguments [ 1 ] = m.r [ 2 ];
						made.arguments [ 2 ] = m.r [ 8 ];
						made.arguments [ 3 ] = m.r [ 9 ];

					} else {

						for ( int32_t k = 0; k < 4; k++ )
							made.arguments [ k ] = m.read ( m.r [ 4 ] + static_cast < uint64_t > ( k * 4 ), 4 );

					}

					m.r [ 0 ] = 0;

					if ( name == mnemonic::jmp ) {

						m.rip = m.pop ( );

						if ( depth > 0 )
							depth--;

						continue;

					}

					break;

				}

				case mnemonic::ret: {

					uint64_t back = m.pop ( );

					if ( ins.count && a.type == x86::kind::IMM )
						m.r [ 4 ] += static_cast < uint64_t > ( a.value );

					if ( depth > 0 )
						depth--;

					m.rip = back;

					continue;

				}

				case mnemonic::int3:
					m.stop ( L"hit int3" );
					break;

				case mnemonic::ud2:
				case mnemonic::hlt:
					m.stop ( L"hit an instruction that ends the program" );
					break;

				default: {

					wchar_t text [ 120 ] = L"stopped at an instruction the emulator doesn't run: ";
					int32_t at = base::text_length ( text );
					const char* label = x86::mnemonic_name ( name );

					for ( int32_t k = 0; label && label [ k ] && at < 118; k++ )
						text [ at++ ] = static_cast < wchar_t > ( label [ k ] );

					text [ at ] = 0;
					m.stop ( text );

					break;

				}

			}

			if ( m.fault )
				break;

			m.rip = next;

		}

		if ( !m.fault && !out.returned && out.steps >= step_limit )
			m.stop ( L"ran out of steps" );

		for ( int32_t i = 0; i < 16; i++ )
			out.registers [ i ] = m.r [ i ];

		out.stopped_at = m.fault ? m.current : m.rip;
		out.reason.set ( m.fault ? m.reason.get ( ) : L"returned" );
		scan_strings ( m, out );

		return out.returned;

	}

}
