#include "decompiler/internal.hh"

namespace decompiler {

	using x86::mnemonic;

	static const wchar_t* const intrinsics [ ] = {
		L"__debugbreak", L"memset", L"memcpy", L"_tzcnt", L"_lzcnt", L"__popcnt", L"_byteswap", L"_rotl", L"_rotr", L"__umulh", L"__mulh",
		L"sqrtf", L"sqrt", L"fminf", L"fmaxf", L"fmin", L"fmax", L"__rdtsc", L"__cpuid", L"_InterlockedExchangeAdd", L"_InterlockedCompareExchange",
		L"_InterlockedExchange", L"__overflow", L"__parity", L"__unordered", L"__readfsqword", L"__readgsqword", L"__fastfail", L"_bittest", L"__halt",
		L"_mm_and", L"_mm_or", L"_mm_xor", L"_mm_andnot", L"__shiftleft128", L"__shiftright128", L"__readeflags", L"_xgetbv", L"__syscall"
	};

	enum intrinsic_id : int32_t {

		I_DEBUGBREAK, I_MEMSET, I_MEMCPY, I_TZCNT, I_LZCNT, I_POPCNT, I_BYTESWAP, I_ROTL, I_ROTR, I_UMULH, I_MULH,
		I_SQRTF, I_SQRT, I_FMINF, I_FMAXF, I_FMIN, I_FMAX, I_RDTSC, I_CPUID, I_XADD, I_CMPXCHG,
		I_XCHG, I_OVERFLOW, I_PARITY, I_UNORDERED, I_FS, I_GS, I_FASTFAIL, I_BITTEST, I_HALT,
		I_AND, I_OR, I_XOR, I_ANDNOT, I_SHLD, I_SHRD, I_FLAGS, I_XGETBV, I_SYSCALL,

	};

	const wchar_t* intrinsic_name ( int32_t id ) {

		return id >= 0 && id < static_cast < int32_t > ( sizeof ( intrinsics ) / sizeof ( intrinsics [ 0 ] ) ) ? intrinsics [ id ] : L"__intrinsic";

	}

	int32_t builder::make ( op kind, uint8_t size, int32_t a, int32_t b, int64_t value, uint8_t bits ) {

		expr& item = exprs.emplace ( );

		item.kind = kind;
		item.size = size;
		item.a = a;
		item.b = b;
		item.value = value;
		item.bits = bits;

		int32_t deepest = 0;

		if ( a >= 0 )
			deepest = exprs [ static_cast < size_t > ( a ) ].depth;

		if ( b >= 0 )
			deepest = base::max ( deepest, exprs [ static_cast < size_t > ( b ) ].depth );

		exprs.back ( ).depth = deepest + 1;

		return exprs.count ( ) - 1;

	}

	void builder::deepen ( int32_t e ) {

		expr& item = exprs [ static_cast < size_t > ( e ) ];
		const int32_t children [ 3 ] = { item.a, item.b, item.c };
		int32_t deepest = 0;

		for ( int32_t child : children ) {

			if ( child >= 0 )
				deepest = base::max ( deepest, exprs [ static_cast < size_t > ( child ) ].depth );

		}

		for ( int32_t i = 0; i < item.arg_count; i++ )
			deepest = base::max ( deepest, exprs [ static_cast < size_t > ( args [ static_cast < size_t > ( item.first_arg + i ) ] ) ].depth );

		item.depth = deepest + 1;

	}

	static int64_t truncate ( int64_t value, uint8_t size ) {

		if ( size >= 8 )
			return value;

		int32_t shift = 64 - size * 8;

		return static_cast < int64_t > ( static_cast < uint64_t > ( value ) << shift ) >> shift;

	}

	int32_t builder::constant ( int64_t value, uint8_t size ) {

		return make ( op::CONST, size, -1, -1, truncate ( value, size ) );

	}

	int32_t builder::cast ( int32_t a, uint8_t size, uint8_t bits ) {

		if ( a < 0 )
			return a;

		const expr& source = exprs [ static_cast < size_t > ( a ) ];

		if ( source.kind == op::CONST && !( bits & FLOATING ) ) {

			uint8_t keep = static_cast < uint8_t > ( ( source.bits | bits ) & POINTER );
			int32_t made = constant ( ( bits & SIGNED ) || keep ? truncate ( source.value, source.size ) : ( source.size >= 8 ? source.value : static_cast < int64_t > ( static_cast < uint64_t > ( source.value ) & ( ( 1ull << ( source.size * 8 ) ) - 1 ) ) ), size );

			exprs [ static_cast < size_t > ( made ) ].bits |= keep;

			return made;

		}

		if ( source.size == size && ( source.bits & FLOATING ) == ( bits & FLOATING ) )
			return a;

		if ( source.kind == op::CAST && source.size > size && !( bits & FLOATING ) && !( source.bits & FLOATING ) )
			return cast ( source.a, size, bits );

		return make ( op::CAST, size, a, -1, 0, bits );

	}

	int32_t builder::binary ( op kind, int32_t a, int32_t b, uint8_t size, uint8_t bits ) {

		if ( a < 0 || b < 0 )
			return a < 0 ? b : a;

		const expr left = exprs [ static_cast < size_t > ( a ) ];
		const expr right = exprs [ static_cast < size_t > ( b ) ];
		bool floating = ( bits & FLOATING ) != 0;

		if ( !floating && left.kind == op::CONST && right.kind == op::CONST ) {

			int64_t x = left.value;
			int64_t y = right.value;

			switch ( kind ) {

				case op::ADD:
					return constant ( x + y, size );

				case op::SUB:
					return constant ( x - y, size );

				case op::MUL:
					return constant ( x * y, size );

				case op::AND:
					return constant ( x & y, size );

				case op::OR:
					return constant ( x | y, size );

				case op::XOR:
					return constant ( x ^ y, size );

				case op::SHL:
					return constant ( static_cast < int64_t > ( static_cast < uint64_t > ( x ) << ( y & 63 ) ), size );

				default:
					break;

			}

		}

		if ( !floating && right.kind == op::CONST ) {

			if ( ( kind == op::AND || kind == op::MUL ) && right.value == 0 )
				return constant ( 0, size );

			if ( kind == op::OR && truncate ( right.value, size ) == -1 )
				return constant ( -1, size );

			if ( kind == op::AND && truncate ( right.value, size ) == -1 )
				return a;

			if ( ( kind == op::ADD || kind == op::SUB || kind == op::OR || kind == op::XOR || kind == op::SHL || kind == op::SHR ) && right.value == 0 )
				return left.size == size ? a : cast ( a, size, 0 );

			if ( kind == op::MUL && right.value == 1 )
				return a;

			if ( ( kind == op::ADD || kind == op::SUB ) && left.kind == op::ADD && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST ) {

				int64_t merged = exprs [ static_cast < size_t > ( left.b ) ].value + ( kind == op::ADD ? right.value : -right.value );

				return binary ( op::ADD, left.a, constant ( merged, size ), size, bits );

			}

			if ( kind == op::SUB )
				return binary ( op::ADD, a, constant ( -right.value, size ), size, bits );

		}

		if ( !floating && left.kind == op::CONST && ( kind == op::ADD || kind == op::MUL || kind == op::AND || kind == op::OR || kind == op::XOR ) )
			return binary ( kind, b, a, size, bits );

		uint8_t result_size = size;

		if ( kind >= op::EQ && kind <= op::LOR )
			result_size = 1;

		int32_t made = make ( kind, result_size, a, b, 0, bits );

		return made;

	}

	int32_t builder::negate ( int32_t condition ) {

		const expr item = exprs [ static_cast < size_t > ( condition ) ];

		switch ( item.kind ) {

			case op::EQ:
				return make ( op::NE, 1, item.a, item.b, 0, item.bits );

			case op::NE:
				return make ( op::EQ, 1, item.a, item.b, 0, item.bits );

			case op::LT:
				return ( item.bits & FLOATING ) ? make ( op::LNOT, 1, condition ) : make ( op::GE, 1, item.a, item.b, 0, item.bits );

			case op::GE:
				return ( item.bits & FLOATING ) ? make ( op::LNOT, 1, condition ) : make ( op::LT, 1, item.a, item.b, 0, item.bits );

			case op::LE:
				return ( item.bits & FLOATING ) ? make ( op::LNOT, 1, condition ) : make ( op::GT, 1, item.a, item.b, 0, item.bits );

			case op::GT:
				return ( item.bits & FLOATING ) ? make ( op::LNOT, 1, condition ) : make ( op::LE, 1, item.a, item.b, 0, item.bits );

			case op::LNOT:
				return item.a;

			case op::LAND:
				return make ( op::LOR, 1, negate ( item.a ), negate ( item.b ) );

			case op::LOR:
				return make ( op::LAND, 1, negate ( item.a ), negate ( item.b ) );

			case op::CONST:
				return constant ( item.value ? 0 : 1, 1 );

			default:
				return make ( op::LNOT, 1, condition );

		}

	}

	int32_t builder::var_expr ( int32_t index, uint8_t size ) {

		variable& item = vars [ static_cast < size_t > ( index ) ];
		int32_t made = make ( op::VAR, item.size, -1, -1, index, item.bits );

		item.uses++;

		if ( size && size < item.size )
			return cast ( made, size, 0 );

		return made;

	}

	int32_t builder::register_var ( int32_t family ) {

		if ( family_var [ family ] >= 0 )
			return family_var [ family ];

		variable& item = vars.emplace ( );

		item.kind = var_kind::REGISTER;
		item.family = family;
		item.size = static_cast < uint8_t > ( family >= 16 ? 8 : ( long_mode ? 8 : 4 ) );

		if ( family >= 16 )
			item.bits = FLOATING;

		family_var [ family ] = vars.count ( ) - 1;

		return family_var [ family ];

	}

	int32_t builder::stack_var ( int64_t offset, uint8_t size ) {

		for ( int32_t i = 0; i < vars.count ( ); i++ ) {

			variable& item = vars [ static_cast < size_t > ( i ) ];

			if ( item.kind == var_kind::STACK && item.offset == offset ) {

				if ( size > item.size )
					item.size = size;

				return i;

			}

		}

		if ( long_mode && offset >= 8 && offset <= 32 && ( offset & 7 ) == 0 ) {

			static const int32_t homes [ 4 ] = { 1, 2, 8, 9 };
			int32_t position = static_cast < int32_t > ( offset / 8 ) - 1;
			int32_t family = homes [ position ];

			if ( entry_live & ( ( 1u << family ) | ( 1u << ( 16 + position ) ) ) )
				return register_var ( ( entry_live & ( 1u << family ) ) ? family : 16 + position );

		}

		variable& item = vars.emplace ( );

		item.kind = var_kind::STACK;
		item.offset = offset;
		item.size = size;

		if ( ( long_mode && offset >= 0x28 ) || ( !long_mode && offset >= 4 ) ) {

			item.kind = var_kind::PARAMETER;
			item.position = long_mode ? static_cast < int32_t > ( ( offset - 0x28 ) / 8 ) + 4 : static_cast < int32_t > ( ( offset - 4 ) / 4 );
			params = base::max ( params, item.position + 1 );

		}

		return vars.count ( ) - 1;

	}

	bool builder::contains_load ( int32_t e ) const {

		if ( e < 0 )
			return false;

		const expr& item = exprs [ static_cast < size_t > ( e ) ];

		if ( item.kind == op::LOAD || item.kind == op::CALL || item.kind == op::GLOBAL || item.kind == op::INTRINSIC )
			return true;

		if ( item.kind == op::VAR && vars [ static_cast < size_t > ( item.value ) ].kind != var_kind::REGISTER )
			return true;

		if ( contains_load ( item.a ) || contains_load ( item.b ) || contains_load ( item.c ) )
			return true;

		for ( int32_t i = 0; i < item.arg_count; i++ ) {

			if ( contains_load ( args [ static_cast < size_t > ( item.first_arg + i ) ] ) )
				return true;

		}

		return false;

	}

	bool builder::references ( int32_t e, int32_t var, int32_t skip ) const {

		if ( e < 0 || e == skip )
			return false;

		const expr& item = exprs [ static_cast < size_t > ( e ) ];

		if ( ( item.kind == op::VAR || ( item.kind == op::ADDRESS && item.a < 0 ) ) && item.value == var )
			return true;

		if ( references ( item.a, var, skip ) || references ( item.b, var, skip ) || references ( item.c, var, skip ) )
			return true;

		for ( int32_t i = 0; i < item.arg_count; i++ ) {

			if ( references ( args [ static_cast < size_t > ( item.first_arg + i ) ], var, skip ) )
				return true;

		}

		return false;

	}

	int32_t builder::replace ( int32_t e, int32_t from, int32_t to ) {

		if ( e < 0 )
			return e;

		if ( e == from )
			return to;

		expr item = exprs [ static_cast < size_t > ( e ) ];
		int32_t a = replace ( item.a, from, to );
		int32_t b = replace ( item.b, from, to );
		int32_t c = replace ( item.c, from, to );
		bool changed = a != item.a || b != item.b || c != item.c;
		base::list < int32_t > list;

		for ( int32_t i = 0; i < item.arg_count; i++ ) {

			int32_t original = args [ static_cast < size_t > ( item.first_arg + i ) ];
			int32_t value = replace ( original, from, to );

			list.push ( value );

			if ( value != original )
				changed = true;

		}

		if ( !changed )
			return e;

		item.a = a;
		item.b = b;
		item.c = c;

		if ( item.arg_count ) {

			item.first_arg = args.count ( );

			for ( int32_t value : list )
				args.push ( value );

		}

		exprs.push ( item );

		return exprs.count ( ) - 1;

	}

	static bool nonvolatile ( int32_t family, bool long_mode ) {

		if ( family == 3 || family == 5 || family == 6 || family == 7 )
			return true;

		return long_mode && ( ( family >= 12 && family < 16 ) || ( family >= 22 && family < 32 ) );

	}

	void builder::propagate ( ) {

		for ( int32_t v = 0; v < vars.count ( ); v++ ) {

			const variable& item = vars [ static_cast < size_t > ( v ) ];

			if ( item.kind != var_kind::REGISTER || item.family < 0 || !nonvolatile ( item.family, long_mode ) )
				continue;

			int32_t count = 0;
			int32_t found = -1;

			for ( int32_t s = 0; s < statements.count ( ); s++ ) {

				const statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::ASSIGN && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && exprs [ static_cast < size_t > ( line.left ) ].value == v ) {

					count++;
					found = s;

				}

			}

			if ( count != 1 || ( entry_live & ( 1u << item.family ) ) )
				continue;

			const expr value = exprs [ static_cast < size_t > ( statements [ static_cast < size_t > ( found ) ].right ) ];

			if ( value.kind != op::CONST )
				continue;

			statements [ static_cast < size_t > ( found ) ].kind = stmt::NOP;

			for ( int32_t i = 0; i < exprs.count ( ); i++ ) {

				expr& node = exprs [ static_cast < size_t > ( i ) ];

				if ( node.kind == op::VAR && node.value == v ) {

					node.kind = op::CONST;
					node.value = value.value;
					node.bits = value.bits;

				}

			}

		}

		int32_t total = exprs.count ( );

		for ( int32_t i = 0; i < total; i++ ) {

			expr node = exprs [ static_cast < size_t > ( i ) ];

			if ( node.kind != op::ADD || node.a < 0 || node.b < 0 )
				continue;

			const expr left = exprs [ static_cast < size_t > ( node.a ) ];
			const expr right = exprs [ static_cast < size_t > ( node.b ) ];

			if ( left.kind == op::CONST && right.kind == op::CONST ) {

				expr& target = exprs [ static_cast < size_t > ( i ) ];

				target.kind = op::CONST;
				target.value = left.value + right.value;
				target.bits = static_cast < uint8_t > ( ( left.bits | right.bits ) & POINTER );
				target.a = -1;
				target.b = -1;

			} else if ( right.kind == op::CONST && left.kind == op::ADD && left.b >= 0 && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST ) {

				int64_t merged = exprs [ static_cast < size_t > ( left.b ) ].value + right.value;
				int32_t made = constant ( merged, node.size );

				exprs [ static_cast < size_t > ( i ) ].a = left.a;
				exprs [ static_cast < size_t > ( i ) ].b = made;

			} else if ( left.kind == op::ADD && left.a >= 0 && left.b >= 0 && exprs [ static_cast < size_t > ( left.a ) ].kind == op::CONST && exprs [ static_cast < size_t > ( left.b ) ].kind == op::CONST ) {

				int32_t made = constant ( exprs [ static_cast < size_t > ( left.a ) ].value + exprs [ static_cast < size_t > ( left.b ) ].value, node.size );

				exprs [ static_cast < size_t > ( made ) ].bits |= POINTER;
				exprs [ static_cast < size_t > ( i ) ].a = node.b;
				exprs [ static_cast < size_t > ( i ) ].b = made;

			}

		}

		uint64_t low = file->low ( );
		uint64_t high = file->high ( );

		for ( expr& node : exprs ) {

			if ( node.kind != op::CONST || node.size < ( long_mode ? 8 : 4 ) )
				continue;

			uint64_t value = static_cast < uint64_t > ( node.value ) & ( long_mode ? ~0ull : 0xFFFFFFFFull );

			if ( value >= low && value < high && ( value == file->base || file->section_at ( value ) > 0 ) )
				node.bits |= POINTER;

		}

	}

	static void count_reads ( const builder& b, int32_t e, base::list < int32_t >& reads ) {

		if ( e < 0 )
			return;

		const expr& item = b.exprs [ static_cast < size_t > ( e ) ];

		if ( ( item.kind == op::VAR || ( item.kind == op::ADDRESS && item.a < 0 ) ) && item.value >= 0 && item.value < reads.count ( ) )
			reads [ static_cast < size_t > ( item.value ) ]++;

		count_reads ( b, item.a, reads );
		count_reads ( b, item.b, reads );
		count_reads ( b, item.c, reads );

		for ( int32_t i = 0; i < item.arg_count; i++ )
			count_reads ( b, b.args [ static_cast < size_t > ( item.first_arg + i ) ], reads );

	}

	static bool has_effects ( const builder& b, int32_t e ) {

		if ( e < 0 )
			return false;

		const expr& item = b.exprs [ static_cast < size_t > ( e ) ];

		if ( item.kind == op::CALL || item.kind == op::INTRINSIC )
			return true;

		if ( has_effects ( b, item.a ) || has_effects ( b, item.b ) || has_effects ( b, item.c ) )
			return true;

		for ( int32_t i = 0; i < item.arg_count; i++ ) {

			if ( has_effects ( b, b.args [ static_cast < size_t > ( item.first_arg + i ) ] ) )
				return true;

		}

		return false;

	}

	static void release_reads ( const builder& b, int32_t e, base::list < int32_t >& pending ) {

		if ( e < 0 )
			return;

		const expr& item = b.exprs [ static_cast < size_t > ( e ) ];

		if ( ( item.kind == op::VAR || item.kind == op::ADDRESS ) && item.value >= 0 && item.value < pending.count ( ) )
			pending [ static_cast < size_t > ( item.value ) ] = -1;

		release_reads ( b, item.a, pending );
		release_reads ( b, item.b, pending );
		release_reads ( b, item.c, pending );

		for ( int32_t i = 0; i < item.arg_count; i++ )
			release_reads ( b, b.args [ static_cast < size_t > ( item.first_arg + i ) ], pending );

	}

	void builder::prune ( ) {

		base::list < int32_t > reads;
		base::list < int32_t > pending;

		pending.fill ( vars.size ( ), -1 );

		for ( const block_code& block : code ) {

			for ( int32_t s = block.first; s < block.first + block.count && s < statements.count ( ); s++ ) {

				statement& line = statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				if ( line.kind == stmt::ASM ) {

					for ( int32_t& slot : pending )
						slot = -1;

					continue;

				}

				release_reads ( *this, line.right, pending );

				bool direct = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

				if ( !direct ) {

					release_reads ( *this, line.left, pending );

					continue;

				}

				int32_t target = static_cast < int32_t > ( exprs [ static_cast < size_t > ( line.left ) ].value );

				if ( target < 0 || target >= vars.count ( ) )
					continue;

				const variable& item = vars [ static_cast < size_t > ( target ) ];

				if ( item.kind == var_kind::STACK || ( item.kind == var_kind::PARAMETER && item.family < 0 ) || item.cookie || item.saved )
					continue;

				int32_t earlier = pending [ static_cast < size_t > ( target ) ];

				if ( earlier >= 0 ) {

					statement& dead = statements [ static_cast < size_t > ( earlier ) ];

					if ( has_effects ( *this, dead.right ) ) {

						dead.kind = stmt::EXPR;
						dead.left = -1;

					} else
						dead.kind = stmt::NOP;

				}

				pending [ static_cast < size_t > ( target ) ] = s;

			}

			for ( int32_t& slot : pending )
				slot = -1;

		}

		for ( int32_t round = 0; round < 8; round++ ) {

			reads.clear ( );
			reads.fill ( vars.size ( ), 0 );

			for ( const statement& line : statements ) {

				if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
					continue;

				if ( line.kind != stmt::ASSIGN || line.left < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
					count_reads ( *this, line.left, reads );

				count_reads ( *this, line.right, reads );

			}

			for ( const block_code& item : code ) {

				count_reads ( *this, item.condition, reads );
				count_reads ( *this, item.selector, reads );

				if ( returns_value || item.end == term::TAIL )
					count_reads ( *this, item.returned, reads );

			}

			bool changed = false;

			for ( statement& line : statements ) {

				if ( line.kind != stmt::ASSIGN || line.left < 0 )
					continue;

				const expr& target = exprs [ static_cast < size_t > ( line.left ) ];

				if ( target.kind != op::VAR || target.value < 0 || target.value >= vars.count ( ) )
					continue;

				const variable& item = vars [ static_cast < size_t > ( target.value ) ];

				if ( item.kind == var_kind::STACK || ( item.kind == var_kind::PARAMETER && item.family < 0 ) || item.cookie || item.saved || reads [ static_cast < size_t > ( target.value ) ] )
					continue;

				if ( has_effects ( *this, line.right ) ) {

					line.kind = stmt::EXPR;
					line.left = -1;

				} else
					line.kind = stmt::NOP;

				changed = true;

			}

			if ( !changed )
				break;

		}

	}

	static int32_t slot_of ( uint8_t reg ) {

		if ( !reg || reg == x86::RIP || reg == x86::EIP )
			return -1;

		int32_t family = x86::register_family ( reg );

		if ( family < 16 )
			return family;

		if ( family >= 32 && family < 48 )
			return 16 + family - 32;

		return -1;

	}

	static bool same_registers ( const x86::instruction& ins ) {

		return ins.count >= 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 0 ].reg == ins.operands [ 1 ].reg;

	}

	static void effects ( const x86::instruction& ins, bool long_mode, uint32_t& use, uint32_t& def ) {

		use = 0;
		def = 0;

		for ( int32_t i = 0; i < ins.count; i++ ) {

			const x86::operand& item = ins.operands [ i ];

			if ( item.type == x86::kind::REG ) {

				int32_t slot = slot_of ( item.reg );

				if ( slot < 0 )
					continue;

				if ( item.access & x86::WRITE )
					def |= 1u << slot;

				if ( ( item.access & x86::READ ) || ( i == 0 && item.size < 4 && slot < 16 ) )
					use |= 1u << slot;

			} else if ( item.type == x86::kind::MEM ) {

				int32_t base_slot = slot_of ( item.base );
				int32_t index_slot = slot_of ( item.index );

				if ( base_slot >= 0 )
					use |= 1u << base_slot;

				if ( index_slot >= 0 )
					use |= 1u << index_slot;

			}

		}

		switch ( ins.name ) {

			case mnemonic::xor_:
			case mnemonic::sub:
			case mnemonic::pxor:
			case mnemonic::xorps:
			case mnemonic::xorpd:

				if ( same_registers ( ins ) )
					use &= ~( 1u << slot_of ( ins.operands [ 0 ].reg ) );

				break;

			case mnemonic::call:

				def |= long_mode ? ( ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 8 ) | ( 1u << 9 ) | ( 1u << 10 ) | ( 1u << 11 ) | ( 0x3Fu << 16 ) ) : 7u;

				break;

			case mnemonic::ret:
				use |= 1u | ( 1u << 16 );
				break;

			case mnemonic::div:
			case mnemonic::idiv:
				use |= 5u;
				def |= 5u;
				break;

			case mnemonic::mul:
			case mnemonic::imul:

				if ( ins.count == 1 ) {

					use |= 1u;
					def |= 5u;

				}

				break;

			case mnemonic::cdq:
			case mnemonic::cqo:
			case mnemonic::cwd:
				use |= 1u;
				def |= 4u;
				break;

			case mnemonic::cdqe:
			case mnemonic::cwde:
			case mnemonic::cbw:
				use |= 1u;
				def |= 1u;
				break;

			case mnemonic::stosb:
			case mnemonic::stosw:
			case mnemonic::stosd:
			case mnemonic::stosq:
				use |= ( 1u << 7 ) | ( 1u << 1 ) | 1u;
				def |= ( 1u << 7 ) | ( 1u << 1 );
				break;

			case mnemonic::movsb:
			case mnemonic::movsw:
			case mnemonic::movsd_:
			case mnemonic::movsq:
				use |= ( 1u << 6 ) | ( 1u << 7 ) | ( 1u << 1 );
				def |= ( 1u << 6 ) | ( 1u << 7 ) | ( 1u << 1 );
				break;

			case mnemonic::cpuid:
				use |= 3u;
				def |= 0xFu;
				break;

			case mnemonic::rdtsc:
				def |= 5u;
				break;

			case mnemonic::xchg:
			case mnemonic::xadd:

				if ( ins.count == 2 && ins.operands [ 1 ].type == x86::kind::REG && slot_of ( ins.operands [ 1 ].reg ) >= 0 )
					def |= 1u << slot_of ( ins.operands [ 1 ].reg );

				break;

			case mnemonic::cmpxchg:
				use |= 1u;
				def |= 1u;
				break;

			default:
				break;

		}

		use &= ~( 1u << 4 );
		def &= ~( 1u << 4 );

	}

	struct machine {

		builder* b = nullptr;
		int32_t regs [ 32 ];
		int32_t flag_kind = 0;
		int32_t flag_a = -1;
		int32_t flag_b = -1;
		uint8_t flag_size = 4;
		int64_t delta = 0;
		bool frame = false;
		int64_t frame_delta = 0;
		uint32_t arg_written = 0;
		int32_t stack_args [ 16 ];
		int32_t stack_stmt [ 16 ];
		base::list < int32_t > pushes;
		base::list < int32_t > push_stmts;
		uint32_t live = 0;
		bool prologue = true;

		void reset ( ) {

			for ( int32_t i = 0; i < 32; i++ )
				regs [ i ] = -1;

			for ( int32_t i = 0; i < 16; i++ ) {

				stack_args [ i ] = -1;
				stack_stmt [ i ] = -1;

			}

			flag_kind = 0;
			arg_written = 0;
			pushes.clear ( );
			push_stmts.clear ( );

		}

		int32_t emit ( stmt kind, int32_t left, int32_t right, uint64_t address ) {

			statement& item = b->statements.emplace ( );

			item.kind = kind;
			item.left = left;
			item.right = right;
			item.address = address;

			return b->statements.count ( ) - 1;

		}

		int32_t family_value ( int32_t slot ) {

			if ( regs [ slot ] >= 0 )
				return regs [ slot ];

			return b->var_expr ( b->register_var ( slot ), 0 );

		}

		int32_t read_reg ( uint8_t reg ) {

			if ( reg == x86::RSP || reg == x86::ESP )
				return b->make ( op::ADDRESS, 8, -1, -1, b->stack_var ( delta, 8 ), POINTER );

			int32_t slot = slot_of ( reg );

			if ( slot < 0 )
				return b->constant ( 0, 8 );

			uint8_t size = x86::register_size ( reg );
			int32_t value = family_value ( slot );

			if ( reg >= x86::AH && reg < x86::ES )
				return b->cast ( b->binary ( op::SHR, value, b->constant ( 8, 1 ), 8 ), 1, UNSIGNED );

			if ( slot >= 16 )
				return value;

			if ( b->exprs [ static_cast < size_t > ( value ) ].size > size )
				return b->cast ( value, size, 0 );

			return value;

		}

		void write_reg ( uint8_t reg, int32_t value ) {

			if ( reg == x86::RSP || reg == x86::ESP )
				return;

			int32_t slot = slot_of ( reg );

			if ( slot < 0 || value < 0 )
				return;

			regs [ slot ] = value;

			if ( b->exprs [ static_cast < size_t > ( value ) ].depth > 12 )
				materialize ( slot, 0 );

			if ( slot == 1 || slot == 2 || slot == 8 || slot == 9 )
				arg_written |= 1u << ( slot == 1 ? 0 : ( slot == 2 ? 1 : ( slot == 8 ? 2 : 3 ) ) );

			if ( slot >= 16 && slot < 20 )
				arg_written |= 1u << ( slot - 16 );

		}

		bool stack_base ( uint8_t reg, int64_t& offset ) {

			if ( reg == x86::RSP || reg == x86::ESP ) {

				offset = delta;

				return true;

			}

			if ( frame && ( reg == x86::RBP || reg == x86::EBP ) ) {

				offset = frame_delta;

				return true;

			}

			int32_t slot = slot_of ( reg );

			if ( slot < 0 || slot >= 16 || regs [ slot ] < 0 )
				return false;

			const expr& value = b->exprs [ static_cast < size_t > ( regs [ slot ] ) ];

			if ( value.kind == op::ADDRESS && value.a < 0 && value.value >= 0 && b->vars [ static_cast < size_t > ( value.value ) ].kind != var_kind::REGISTER ) {

				offset = b->vars [ static_cast < size_t > ( value.value ) ].offset;

				return true;

			}

			if ( value.kind == op::ADD && value.a >= 0 && value.b >= 0 && b->exprs [ static_cast < size_t > ( value.b ) ].kind == op::CONST ) {

				const expr& inner = b->exprs [ static_cast < size_t > ( value.a ) ];

				if ( inner.kind == op::ADDRESS && inner.a < 0 && inner.value >= 0 && b->vars [ static_cast < size_t > ( inner.value ) ].kind != var_kind::REGISTER ) {

					offset = b->vars [ static_cast < size_t > ( inner.value ) ].offset + b->exprs [ static_cast < size_t > ( value.b ) ].value;

					return true;

				}

			}

			return false;

		}

		bool stack_slot ( const x86::operand& m, int64_t& slot ) {

			int64_t base_offset = 0;

			if ( m.index != x86::NONE || !stack_base ( m.base, base_offset ) )
				return false;

			slot = base_offset + m.value;

			return true;

		}

		int32_t address_of ( const x86::instruction& ins, const x86::operand& m ) {

			uint64_t absolute = 0;

			if ( m.rip_relative ( ) || ( m.base == x86::NONE && m.index == x86::NONE ) ) {

				absolute = m.rip_relative ( ) ? ins.next ( ) + static_cast < uint64_t > ( m.value ) : static_cast < uint64_t > ( m.value );

				return b->make ( op::CONST, 8, -1, -1, static_cast < int64_t > ( absolute ), POINTER );

			}

			int32_t result = -1;
			int64_t slot = 0;

			int64_t base_offset = 0;

			if ( m.base != x86::NONE && stack_base ( m.base, base_offset ) && m.index == x86::NONE ) {

				result = b->make ( op::ADDRESS, 8, -1, -1, b->stack_var ( base_offset + m.value, 8 ), POINTER );

			} else if ( m.base != x86::NONE ) {

				result = read_reg ( x86::resize_register ( m.base, 8 ) );

				if ( m.value )
					result = b->binary ( op::ADD, result, b->constant ( m.value, 8 ), 8 );

			}

			if ( m.index != x86::NONE ) {

				int32_t index = read_reg ( x86::resize_register ( m.index, 8 ) );

				if ( m.scale > 1 )
					index = b->binary ( op::MUL, index, b->constant ( m.scale, 8 ), 8 );

				result = result < 0 ? index : b->binary ( op::ADD, result, index, 8 );

				if ( m.base == x86::NONE && m.value )
					result = b->binary ( op::ADD, result, b->constant ( m.value, 8 ), 8 );

			}

			( void ) slot;

			return result < 0 ? b->constant ( m.value, 8 ) : result;

		}

		int32_t read_memory ( const x86::instruction& ins, const x86::operand& m, uint8_t size, uint8_t bits ) {

			int64_t slot = 0;

			if ( stack_slot ( m, slot ) ) {

				for ( int32_t i = 0; i < b->saved_offsets.count ( ); i++ ) {

					if ( b->saved_offsets [ static_cast < size_t > ( i ) ] == slot )
						return b->var_expr ( b->register_var ( b->saved_families [ static_cast < size_t > ( i ) ] ), size );

				}

				int32_t index = b->stack_var ( slot, size );
				variable& item = b->vars [ static_cast < size_t > ( index ) ];

				item.bits |= bits;

				return b->var_expr ( index, size );

			}

			int32_t address = address_of ( ins, m );
			const expr& where = b->exprs [ static_cast < size_t > ( address ) ];

			if ( where.kind == op::CONST && ( where.bits & POINTER ) ) {

				if ( bits & FLOATING )
					b->float_globals.push ( static_cast < uint64_t > ( where.value ) );

				return b->make ( op::GLOBAL, size, -1, -1, where.value, bits );

			}

			note_field ( address, size, bits );

			return b->make ( op::LOAD, size, address, -1, 0, bits );

		}

		void note_field ( int32_t address, uint8_t size, uint8_t bits ) {

			const expr& where = b->exprs [ static_cast < size_t > ( address ) ];
			int32_t var = -1;
			int64_t offset = 0;

			if ( where.kind == op::VAR )
				var = static_cast < int32_t > ( where.value );
			else if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && b->exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b->exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

				var = static_cast < int32_t > ( b->exprs [ static_cast < size_t > ( where.a ) ].value );
				offset = b->exprs [ static_cast < size_t > ( where.b ) ].value;

			}

			if ( var < 0 || offset < 0 || offset > 0x10000 )
				return;

			b->vars [ static_cast < size_t > ( var ) ].bits |= POINTER;

			for ( field_use& item : b->fields ) {

				if ( item.var == var && item.offset == offset ) {

					item.count++;
					item.bits |= bits;

					if ( size > item.size )
						item.size = size;

					return;

				}

			}

			field_use& item = b->fields.emplace ( );

			item.var = var;
			item.offset = offset;
			item.size = size;
			item.bits = bits;
			item.count = 1;

		}

		void flush ( uint32_t keep ) {

			for ( int32_t slot = 0; slot < 32; slot++ ) {

				if ( regs [ slot ] < 0 || !( keep & ( 1u << slot ) ) )
					continue;

				if ( !b->contains_load ( regs [ slot ] ) )
					continue;

				materialize ( slot, 0 );

			}

		}

		uint32_t busy = 0;
		int32_t guard_a = -1;
		int32_t guard_b = -1;

		int32_t spill ( int32_t value, uint64_t address ) {

			variable& made = b->vars.emplace ( );
			int32_t index = b->vars.count ( ) - 1;

			made.kind = var_kind::REGISTER;
			made.family = -1;
			made.size = b->exprs [ static_cast < size_t > ( value ) ].size;
			made.bits = b->exprs [ static_cast < size_t > ( value ) ].bits & ( FLOATING | SIGNED | UNSIGNED );
			made.assigned = true;
			emit ( stmt::ASSIGN, b->make ( op::VAR, made.size, -1, -1, index ), value, address );

			return b->var_expr ( index, 0 );

		}

		void protect ( int32_t var, int32_t skip, uint64_t address ) {

			if ( flag_kind && b->references ( flag_a, var, skip ) )
				flag_a = spill ( flag_a, address );

			if ( flag_kind && b->references ( flag_b, var, skip ) )
				flag_b = spill ( flag_b, address );

			for ( int32_t i = 0; i < pushes.count ( ); i++ ) {

				if ( b->references ( pushes [ static_cast < size_t > ( i ) ], var, skip ) )
					pushes [ static_cast < size_t > ( i ) ] = spill ( pushes [ static_cast < size_t > ( i ) ], address );

			}

			guard_a = shield ( guard_a, var, skip, address );
			guard_b = shield ( guard_b, var, skip, address );

		}

		int32_t shield ( int32_t guard, int32_t var, int32_t source, uint64_t address ) {

			if ( guard < 0 || !b->references ( guard, var, -1 ) )
				return guard;

			if ( source >= 0 && !b->references ( guard, var, source ) )
				return b->replace ( guard, source, b->var_expr ( var, 0 ) );

			return spill ( guard, address );

		}

		void materialize ( int32_t slot, uint64_t address ) {

			if ( regs [ slot ] < 0 || ( busy & ( 1u << slot ) ) )
				return;

			int32_t var = b->register_var ( slot );
			int32_t source = regs [ slot ];
			expr value = b->exprs [ static_cast < size_t > ( source ) ];

			if ( value.bits & FLOATING )
				b->vars [ static_cast < size_t > ( var ) ].bits |= FLOATING;

			if ( value.kind == op::VAR && value.value == var ) {

				regs [ slot ] = -1;

				return;

			}

			busy |= 1u << slot;

			for ( int32_t other = 0; other < 32; other++ ) {

				if ( other == slot || regs [ other ] < 0 || !b->references ( regs [ other ], var, -1 ) )
					continue;

				if ( busy & ( 1u << other ) )
					regs [ other ] = spill ( regs [ other ], address );
				else
					materialize ( other, address );

			}

			protect ( var, source, address );
			emit ( stmt::ASSIGN, b->make ( op::VAR, b->vars [ static_cast < size_t > ( var ) ].size, -1, -1, var ), source, address );
			b->vars [ static_cast < size_t > ( var ) ].assigned = true;

			if ( flag_kind && value.kind != op::CONST && value.kind != op::VAR ) {

				flag_a = b->replace ( flag_a, source, b->var_expr ( var, value.size ) );
				flag_b = b->replace ( flag_b, source, b->var_expr ( var, value.size ) );

			}

			int32_t replacement = b->make ( op::VAR, b->vars [ static_cast < size_t > ( var ) ].size, -1, -1, var );

			if ( flag_kind ) {

				flag_a = b->replace ( flag_a, source, replacement );
				flag_b = b->replace ( flag_b, source, replacement );

			}

			for ( int32_t other = 0; other < 32; other++ ) {

				if ( other != slot && regs [ other ] >= 0 )
					regs [ other ] = b->replace ( regs [ other ], source, replacement );

			}

			regs [ slot ] = -1;
			busy &= ~( 1u << slot );

		}

		void write_memory ( const x86::instruction& ins, const x86::operand& m, uint8_t size, int32_t value ) {

			flush ( live );

			for ( int32_t s = 0; s < 32 && value >= 0; s++ ) {

				if ( regs [ s ] != value || !( live & ( 1u << s ) ) )
					continue;

				const expr& held = b->exprs [ static_cast < size_t > ( value ) ];

				if ( held.kind != op::VAR && held.kind != op::CONST && held.depth >= 3 ) {

					uint8_t width = held.size;

					materialize ( s, ins.address );
					value = b->var_expr ( b->register_var ( s ), width );

				}

				break;

			}

			int64_t slot = 0;

			if ( stack_slot ( m, slot ) ) {

				const expr incoming = b->exprs [ static_cast < size_t > ( value ) ];

				if ( incoming.kind == op::VAR ) {

					const variable& origin = b->vars [ static_cast < size_t > ( incoming.value ) ];

					if ( origin.kind == var_kind::REGISTER && origin.family >= 0 && !origin.assigned && nonvolatile ( origin.family, b->long_mode ) ) {

						b->saved_offsets.push ( slot );
						b->saved_families.push ( origin.family );

						return;

					}

				}

				for ( int32_t i = 0; i < b->saved_offsets.count ( ); i++ ) {

					if ( b->saved_offsets [ static_cast < size_t > ( i ) ] == slot ) {

						b->saved_offsets.remove_at ( static_cast < size_t > ( i ) );
						b->saved_families.remove_at ( static_cast < size_t > ( i ) );

						break;

					}

				}

				int32_t index = b->stack_var ( slot, size );

				protect ( index, -1, ins.address );
				variable& item = b->vars [ static_cast < size_t > ( index ) ];
				const expr& source = b->exprs [ static_cast < size_t > ( value ) ];

				if ( source.bits & FLOATING )
					item.bits |= FLOATING;

				if ( prologue && source.kind == op::VAR && b->vars [ static_cast < size_t > ( source.value ) ].kind == var_kind::REGISTER && b->vars [ static_cast < size_t > ( source.value ) ].family == ( index >= 0 ? b->vars [ static_cast < size_t > ( index ) ].family : -2 ) )
					return;

				if ( item.kind == var_kind::REGISTER && source.kind == op::VAR && source.value == index )
					return;

				if ( prologue && source.kind == op::VAR && b->vars [ static_cast < size_t > ( source.value ) ].kind == var_kind::REGISTER ) {

					int32_t family = b->vars [ static_cast < size_t > ( source.value ) ].family;

					if ( family == 3 || family == 5 || family == 6 || family == 7 || ( family >= 12 && family < 16 ) || family >= 22 ) {

						item.saved = true;

						return;

					}

				}

				if ( source.kind == op::XOR && b->contains_load ( source.a ) ) {

					const expr& left = b->exprs [ static_cast < size_t > ( source.a ) ];
					const expr& right = b->exprs [ static_cast < size_t > ( source.b ) ];

					if ( left.kind == op::GLOBAL && right.kind == op::ADDRESS ) {

						item.cookie = true;

						return;

					}

				}

				int64_t relative = slot - delta;
				int32_t base_offset = b->long_mode ? 0x20 : 0;

				int32_t made = emit ( stmt::ASSIGN, b->make ( op::VAR, item.size, -1, -1, index ), value, ins.address );

				if ( b->long_mode && relative >= base_offset && relative < base_offset + 8 * 12 && ( relative & 7 ) == 0 ) {

					int32_t position = 4 + static_cast < int32_t > ( ( relative - base_offset ) / 8 );

					stack_args [ position ] = value;
					stack_stmt [ position ] = made;

				}

				return;

			}

			int32_t address = address_of ( ins, m );
			const expr& where = b->exprs [ static_cast < size_t > ( address ) ];

			if ( where.kind == op::CONST && ( where.bits & POINTER ) ) {

				const expr& source = b->exprs [ static_cast < size_t > ( value ) ];

				if ( source.bits & FLOATING )
					b->float_globals.push ( static_cast < uint64_t > ( where.value ) );

				emit ( stmt::ASSIGN, b->make ( op::GLOBAL, size, -1, -1, where.value, source.bits & FLOATING ), value, ins.address );

				return;

			}

			if ( flag_kind && b->contains_load ( flag_a ) )
				flag_a = spill ( flag_a, ins.address );

			if ( flag_kind && b->contains_load ( flag_b ) )
				flag_b = spill ( flag_b, ins.address );

			note_field ( address, size, b->exprs [ static_cast < size_t > ( value ) ].bits & FLOATING );
			emit ( stmt::STORE, b->make ( op::LOAD, size, address, -1, 0, b->exprs [ static_cast < size_t > ( value ) ].bits & FLOATING ), value, ins.address );

		}

		int32_t read ( const x86::instruction& ins, int32_t index, uint8_t bits = 0 ) {

			const x86::operand& item = ins.operands [ index ];

			switch ( item.type ) {

				case x86::kind::REG:
					return read_reg ( item.reg );

				case x86::kind::MEM:
					return read_memory ( ins, item, item.size, bits );

				case x86::kind::IMM: {

					int32_t made = b->constant ( item.value, item.size ? item.size : 4 );
					uint64_t value = static_cast < uint64_t > ( item.value ) & ( b->long_mode ? ~0ull : 0xFFFFFFFFull );

					if ( item.size >= 4 && b->file->section_at ( value ) > 0 )
						b->exprs [ static_cast < size_t > ( made ) ].bits |= POINTER;

					return made;

				}

				case x86::kind::REL:
					return b->make ( op::CONST, 8, -1, -1, item.value, POINTER );

				default:
					return b->constant ( 0, 4 );

			}

		}

		void write ( const x86::instruction& ins, int32_t index, int32_t value ) {

			const x86::operand& item = ins.operands [ index ];

			if ( item.type == x86::kind::REG )
				write_reg ( item.reg, value );
			else if ( item.type == x86::kind::MEM )
				write_memory ( ins, item, item.size, value );

		}

		int32_t condition ( int32_t code ) {

			builder& k = *b;
			uint8_t size = flag_size;

			if ( flag_kind == 0 || flag_a < 0 )
				return k.make ( op::INTRINSIC, 1, -1, -1, I_FLAGS );

			int32_t a = flag_a;
			int32_t z = k.constant ( 0, size );

			switch ( flag_kind ) {

				case 1: {

					int32_t c = flag_b;

					switch ( code ) {

						case 2:
							return k.binary ( op::LT, a, c, size, UNSIGNED );

						case 3:
							return k.binary ( op::GE, a, c, size, UNSIGNED );

						case 4:
							return k.binary ( op::EQ, a, c, size );

						case 5:
							return k.binary ( op::NE, a, c, size );

						case 6:
							return k.binary ( op::LE, a, c, size, UNSIGNED );

						case 7:
							return k.binary ( op::GT, a, c, size, UNSIGNED );

						case 8:
							return k.binary ( op::LT, k.binary ( op::SUB, a, c, size ), z, size, SIGNED );

						case 9:
							return k.binary ( op::GE, k.binary ( op::SUB, a, c, size ), z, size, SIGNED );

						case 12:
							return k.binary ( op::LT, a, c, size, SIGNED );

						case 13:
							return k.binary ( op::GE, a, c, size, SIGNED );

						case 14:
							return k.binary ( op::LE, a, c, size, SIGNED );

						case 15:
							return k.binary ( op::GT, a, c, size, SIGNED );

						default:
							break;

					}

					break;

				}

				case 2: {

					int32_t value = a;

					if ( !( flag_b == a || ( k.exprs [ static_cast < size_t > ( a ) ].kind == k.exprs [ static_cast < size_t > ( flag_b ) ].kind && k.exprs [ static_cast < size_t > ( a ) ].kind == op::VAR && k.exprs [ static_cast < size_t > ( a ) ].value == k.exprs [ static_cast < size_t > ( flag_b ) ].value ) ) )
						value = k.binary ( op::AND, a, flag_b, size );

					switch ( code ) {

						case 4:
						case 6:
							return k.binary ( op::EQ, value, z, size );

						case 5:
						case 7:
							return k.binary ( op::NE, value, z, size );

						case 8:
						case 12:
							return k.binary ( op::LT, value, z, size, SIGNED );

						case 9:
						case 13:
							return k.binary ( op::GE, value, z, size, SIGNED );

						case 14:
							return k.binary ( op::LE, value, z, size, SIGNED );

						case 15:
							return k.binary ( op::GT, value, z, size, SIGNED );

						case 2:
							return k.constant ( 0, 1 );

						case 3:
							return k.constant ( 1, 1 );

						default:
							break;

					}

					break;

				}

				case 3:

					switch ( code ) {

						case 4:
						case 6:
							return k.binary ( op::EQ, a, z, size );

						case 5:
						case 7:
							return k.binary ( op::NE, a, z, size );

						case 8:
						case 12:
							return k.binary ( op::LT, a, z, size, SIGNED );

						case 9:
						case 13:
							return k.binary ( op::GE, a, z, size, SIGNED );

						case 14:
							return k.binary ( op::LE, a, z, size, SIGNED );

						case 15:
							return k.binary ( op::GT, a, z, size, SIGNED );

						default:
							break;

					}

					break;

				case 4: {

					int32_t c = flag_b;

					switch ( code ) {

						case 2:
							return k.binary ( op::LT, a, c, size, FLOATING );

						case 3:
							return k.binary ( op::GE, a, c, size, FLOATING );

						case 4:
							return k.binary ( op::EQ, a, c, size, FLOATING );

						case 5:
							return k.binary ( op::NE, a, c, size, FLOATING );

						case 6:
							return k.binary ( op::LE, a, c, size, FLOATING );

						case 7:
							return k.binary ( op::GT, a, c, size, FLOATING );

						case 10:
						case 11: {

							int32_t made = k.make ( op::INTRINSIC, 1, -1, -1, I_UNORDERED );

							k.exprs [ static_cast < size_t > ( made ) ].first_arg = k.args.count ( );
							k.args.push ( a );
							k.args.push ( c );
							k.exprs [ static_cast < size_t > ( made ) ].arg_count = 2;
							k.deepen ( made );

							return code == 10 ? made : k.negate ( made );

						}

						default:
							break;

					}

					break;

				}

				case 5: {

					int32_t bit = k.binary ( op::AND, k.binary ( op::SHR, a, flag_b, size ), k.constant ( 1, size ), size );

					if ( code == 2 )
						return k.binary ( op::NE, bit, z, size );

					if ( code == 3 )
						return k.binary ( op::EQ, bit, z, size );

					break;

				}

				default:
					break;

			}

			int32_t made = k.make ( op::INTRINSIC, 1, -1, -1, code <= 1 ? I_OVERFLOW : ( code == 10 || code == 11 ? I_PARITY : I_FLAGS ) );

			return code == 1 || code == 11 ? k.negate ( made ) : made;

		}

		void set_flags ( int32_t kind, int32_t a, int32_t c, uint8_t size ) {

			flag_kind = kind;
			flag_a = a;
			flag_b = c;
			flag_size = size;

		}

		bool jumps_through_rax ( uint64_t address ) const {

			const builder& k = *b;

			for ( int32_t hop = 0; hop < 4; hop++ ) {

				x86::instruction step;

				if ( !k.db->decode ( address, step ) || step.name != mnemonic::jmp || step.count != 1 )
					return false;

				if ( step.operands [ 0 ].type == x86::kind::REG )
					return step.operands [ 0 ].reg == x86::RAX;

				if ( step.direct ( ) ) {

					address = step.target ( );

					continue;

				}

				uint64_t offset = 0;

				if ( step.operands [ 0 ].type != x86::kind::MEM || !step.operands [ 0 ].rip_relative ( ) || !k.file->offset_of ( step.next ( ) + static_cast < uint64_t > ( step.operands [ 0 ].value ), offset ) || offset + 8 > k.file->size )
					return false;

				address = k.file->read64 ( offset );

			}

			return false;

		}

		int32_t call_expr ( const x86::instruction& ins, uint32_t live_after ) {

			builder& k = *b;
			int32_t target = -1;
			uint64_t direct = 0;
			int32_t count = -1;

			if ( ins.direct ( ) )
				direct = ins.target ( );
			else if ( ins.operands [ 0 ].type == x86::kind::MEM && ( ins.operands [ 0 ].rip_relative ( ) || ( ins.operands [ 0 ].base == x86::NONE && ins.operands [ 0 ].index == x86::NONE ) ) )
				direct = ins.operands [ 0 ].rip_relative ( ) ? ins.next ( ) + static_cast < uint64_t > ( ins.operands [ 0 ].value ) : static_cast < uint64_t > ( ins.operands [ 0 ].value );
			else
				target = read ( ins, 0 );

			if ( direct && k.long_mode && jumps_through_rax ( direct ) ) {

				target = family_value ( 0 );
				direct = 0;

			}

			bool through_vtable = false;

			if ( target >= 0 && k.long_mode ) {

				const expr& slot = k.exprs [ static_cast < size_t > ( target ) ];

				if ( slot.kind == op::LOAD && slot.a >= 0 ) {

					const expr& where = k.exprs [ static_cast < size_t > ( slot.a ) ];

					through_vtable = where.kind == op::LOAD || ( where.kind == op::ADD && where.a >= 0 && k.exprs [ static_cast < size_t > ( where.a ) ].kind == op::LOAD );

				}

			}

			wchar_t name [ 200 ];
			int32_t callee = direct ? k.db->function_starting ( direct ) : -1;

			if ( direct && k.names->name ( direct, true, name, 200 ) )
				count = import_arguments ( name );

			if ( count < 0 && callee >= 0 && ( k.db->functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) ) {

				int32_t import = k.db->functions [ static_cast < size_t > ( callee ) ].import;

				if ( import >= 0 )
					count = import_arguments ( k.file->imports [ static_cast < size_t > ( import ) ].name.get ( ) );

			}

			int32_t made = k.make ( op::CALL, 8, target, -1, static_cast < int64_t > ( direct ) );
			int32_t first = k.args.count ( );
			base::list < int32_t > list;

			if ( k.long_mode ) {

				static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
				int32_t highest = -1;

				for ( int32_t i = 0; i < 4; i++ ) {

					if ( arg_written & ( 1u << i ) )
						highest = i;

				}

				for ( int32_t i = 4; i < 16 && stack_args [ i ] >= 0; i++ )
					highest = base::max ( highest, i );

				if ( count >= 0 )
					highest = count - 1;

				if ( through_vtable )
					highest = base::max ( highest, 0 );

				for ( int32_t i = 0; i <= highest && i < 16; i++ ) {

					if ( i < 4 ) {

						bool floating = regs [ 16 + i ] >= 0 && ( k.exprs [ static_cast < size_t > ( regs [ 16 + i ] ) ].bits & FLOATING ) && regs [ gpr [ i ] ] < 0;

						list.push ( floating ? regs [ 16 + i ] : family_value ( gpr [ i ] ) );

					} else if ( stack_args [ i ] >= 0 ) {

						list.push ( stack_args [ i ] );

						if ( stack_stmt [ i ] >= 0 )
							k.statements [ static_cast < size_t > ( stack_stmt [ i ] ) ].kind = stmt::NOP;

					} else
						list.push ( k.var_expr ( k.stack_var ( delta + i * 8, 8 ), 0 ) );

				}

			} else {

				int32_t take = count >= 0 ? base::min ( count, pushes.count ( ) ) : pushes.count ( );

				for ( int32_t i = 0; i < take; i++ ) {

					int32_t at = pushes.count ( ) - 1 - i;

					list.push ( pushes [ static_cast < size_t > ( at ) ] );

					if ( push_stmts [ static_cast < size_t > ( at ) ] >= 0 )
						k.statements [ static_cast < size_t > ( push_stmts [ static_cast < size_t > ( at ) ] ) ].kind = stmt::NOP;

				}

				pushes.clear ( );
				push_stmts.clear ( );

			}

			for ( int32_t value : list )
				k.args.push ( value );

			k.exprs [ static_cast < size_t > ( made ) ].first_arg = first;
			k.exprs [ static_cast < size_t > ( made ) ].arg_count = list.count ( );

			for ( int32_t value : list )
				k.exprs [ static_cast < size_t > ( made ) ].depth = base::max ( k.exprs [ static_cast < size_t > ( made ) ].depth, k.exprs [ static_cast < size_t > ( value ) ].depth + 1 );
			( void ) live_after;

			return made;

		}

		bool cookie_call ( int32_t call ) {

			const expr& item = b->exprs [ static_cast < size_t > ( call ) ];

			if ( item.arg_count != 1 )
				return false;

			const expr& arg = b->exprs [ static_cast < size_t > ( b->args [ static_cast < size_t > ( item.first_arg ) ] ) ];

			if ( arg.kind != op::XOR || arg.a < 0 )
				return false;

			const expr& left = b->exprs [ static_cast < size_t > ( arg.a ) ];

			return left.kind == op::VAR && b->vars [ static_cast < size_t > ( left.value ) ].cookie;

		}

		void clobber ( ) {

			if ( !b->long_mode ) {

				regs [ 1 ] = -1;
				regs [ 2 ] = -1;

				return;

			}

			static const int32_t volatile_regs [ ] = { 1, 2, 8, 9, 10, 11, 16, 17, 18, 19, 20, 21 };

			for ( int32_t slot : volatile_regs )
				regs [ slot ] = -1;

			for ( int32_t i = 0; i < 16; i++ ) {

				stack_args [ i ] = -1;
				stack_stmt [ i ] = -1;

			}

			arg_written = 0;

		}

		void unhandled ( const x86::instruction& ins, uint32_t def ) {

			wchar_t text [ 200 ];
			struct bridge : x86::symbolizer {

				namer* target;

				bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

					return target->name ( address, code, out, room );

				}

			} names_bridge;

			names_bridge.target = b->names;
			flush ( 0xFFFFFFFFu );

			for ( int32_t slot = 0; slot < 32; slot++ )
				materialize ( slot, ins.address );

			x86::format_text ( ins, text, 200, &names_bridge, 0 );
			b->asm_text.push ( base::text ( text ) );
			emit ( stmt::ASM, b->asm_text.count ( ) - 1, -1, ins.address );

			for ( int32_t slot = 0; slot < 32; slot++ ) {

				if ( def & ( 1u << slot ) ) {

					regs [ slot ] = -1;
					b->register_var ( slot );

				}

			}

		}

		int32_t intrinsic ( int32_t id, uint8_t size, int32_t a, int32_t c = -1, int32_t d = -1 ) {

			int32_t made = b->make ( op::INTRINSIC, size, -1, -1, id );
			int32_t first = b->args.count ( );
			int32_t count = 0;

			if ( a >= 0 ) {

				b->args.push ( a );
				count++;

			}

			if ( c >= 0 ) {

				b->args.push ( c );
				count++;

			}

			if ( d >= 0 ) {

				b->args.push ( d );
				count++;

			}

			b->exprs [ static_cast < size_t > ( made ) ].first_arg = first;
			b->exprs [ static_cast < size_t > ( made ) ].arg_count = count;
			b->deepen ( made );

			return made;

		}

		void step ( const x86::instruction& ins, uint32_t live_after, uint32_t def ) {

			builder& k = *b;
			uint8_t size = ins.count ? ins.operands [ 0 ].size : 0;

			live = live_after;

			if ( ins.control != x86::flow::NONE && ins.name != mnemonic::call )
				return;

			auto arith = [ & ] ( op kind, uint8_t bits ) {

				int32_t left = read ( ins, 0 );
				int32_t right = read ( ins, 1 );
				int32_t value = k.binary ( kind, left, right, size, bits );

				write ( ins, 0, value );
				set_flags ( 3, value, -1, size );

			};

			auto vector_arith = [ & ] ( op kind, uint8_t width ) {

				int32_t left = read ( ins, ins.count == 3 ? 1 : 0, FLOATING );
				int32_t right = read ( ins, ins.count == 3 ? 2 : 1, FLOATING );
				int32_t value = k.binary ( kind, k.cast ( left, width, FLOATING ), k.cast ( right, width, FLOATING ), width, FLOATING );

				write ( ins, 0, value );

			};

			switch ( ins.name ) {

				case mnemonic::nop:
				case mnemonic::endbr64:
				case mnemonic::endbr32:
				case mnemonic::pause:
				case mnemonic::prefetchnta:
				case mnemonic::prefetcht0:
				case mnemonic::prefetcht1:
				case mnemonic::prefetcht2:
				case mnemonic::prefetchw:
				case mnemonic::lfence:
				case mnemonic::mfence:
				case mnemonic::sfence:
				case mnemonic::fwait:
				case mnemonic::vzeroupper:
					return;

				case mnemonic::int3:
					emit ( stmt::EXPR, -1, intrinsic ( I_DEBUGBREAK, 4, -1 ), ins.address );
					return;

				case mnemonic::mov:

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RBP || ins.operands [ 0 ].reg == x86::EBP ) && ins.operands [ 1 ].type == x86::kind::REG && ( ins.operands [ 1 ].reg == x86::RSP || ins.operands [ 1 ].reg == x86::ESP ) ) {

						frame = true;
						frame_delta = delta;

						return;

					}

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RSP || ins.operands [ 0 ].reg == x86::ESP ) ) {

						if ( frame && ins.operands [ 1 ].type == x86::kind::REG && ( ins.operands [ 1 ].reg == x86::RBP || ins.operands [ 1 ].reg == x86::EBP ) )
							delta = frame_delta;

						return;

					}

					write ( ins, 0, read ( ins, 1 ) );

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RBP ) )
						frame = false;

					return;

				case mnemonic::movzx:
					write ( ins, 0, k.cast ( read ( ins, 1 ), size, UNSIGNED ) );
					return;

				case mnemonic::movsx:
				case mnemonic::movsxd:
					write ( ins, 0, k.cast ( read ( ins, 1 ), size, SIGNED ) );
					return;

				case mnemonic::lea:

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RSP || ins.operands [ 0 ].reg == x86::ESP ) ) {

						if ( ins.operands [ 1 ].base == x86::RSP || ins.operands [ 1 ].base == x86::ESP )
							delta += ins.operands [ 1 ].value;
						else if ( frame && ( ins.operands [ 1 ].base == x86::RBP || ins.operands [ 1 ].base == x86::EBP ) )
							delta = frame_delta + ins.operands [ 1 ].value;

						return;

					}

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RBP || ins.operands [ 0 ].reg == x86::EBP ) && ( ins.operands [ 1 ].base == x86::RSP || ins.operands [ 1 ].base == x86::ESP ) && ins.operands [ 1 ].index == x86::NONE ) {

						frame = true;
						frame_delta = delta + ins.operands [ 1 ].value;

						return;

					}

					write ( ins, 0, k.cast ( address_of ( ins, ins.operands [ 1 ] ), size, POINTER ) );
					return;

				case mnemonic::push: {

					delta -= k.long_mode ? 8 : 4;

					int32_t value = read ( ins, 0 );
					const expr& source = k.exprs [ static_cast < size_t > ( value ) ];

					if ( prologue && source.kind == op::VAR && k.vars [ static_cast < size_t > ( source.value ) ].kind == var_kind::REGISTER )
						return;

					if ( source.kind == op::VAR ) {

						const variable& origin = k.vars [ static_cast < size_t > ( source.value ) ];

						if ( origin.kind == var_kind::REGISTER && origin.family >= 0 && !origin.assigned && nonvolatile ( origin.family, k.long_mode ) ) {

							pushes.push ( value );
							push_stmts.push ( -1 );

							return;

						}

					}

					pushes.push ( value );

					int32_t index = k.stack_var ( delta, k.long_mode ? 8 : 4 );

					push_stmts.push ( emit ( stmt::ASSIGN, k.make ( op::VAR, k.vars [ static_cast < size_t > ( index ) ].size, -1, -1, index ), value, ins.address ) );

					return;

				}

				case mnemonic::pop:

					if ( !pushes.empty ( ) && !prologue ) {

						write ( ins, 0, pushes.back ( ) );
						pushes.pop ( );

						if ( push_stmts.back ( ) >= 0 )
							k.statements [ static_cast < size_t > ( push_stmts.back ( ) ) ].kind = stmt::NOP;

						push_stmts.pop ( );

					} else if ( ins.operands [ 0 ].type == x86::kind::REG && slot_of ( ins.operands [ 0 ].reg ) >= 0 )
						regs [ slot_of ( ins.operands [ 0 ].reg ) ] = -1;

					delta += k.long_mode ? 8 : 4;

					return;

				case mnemonic::add:
				case mnemonic::sub:

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RSP || ins.operands [ 0 ].reg == x86::ESP ) ) {

						if ( ins.operands [ 1 ].type == x86::kind::IMM )
							delta += ins.name == mnemonic::add ? ins.operands [ 1 ].value : -ins.operands [ 1 ].value;

						if ( !k.long_mode && ins.name == mnemonic::add ) {

							pushes.clear ( );
							push_stmts.clear ( );

						}

						return;

					}

					if ( ins.name == mnemonic::sub && same_registers ( ins ) ) {

						write ( ins, 0, k.constant ( 0, size ) );
						set_flags ( 3, k.constant ( 0, size ), -1, size );

						return;

					}

					if ( ins.name == mnemonic::sub ) {

						int32_t left = read ( ins, 0 );
						int32_t right = read ( ins, 1 );

						write ( ins, 0, k.binary ( op::SUB, left, right, size ) );
						set_flags ( 1, left, right, size );

						return;

					}

					arith ( op::ADD, 0 );

					return;

				case mnemonic::adc:
				case mnemonic::sbb:
					arith ( ins.name == mnemonic::adc ? op::ADD : op::SUB, 0 );
					return;

				case mnemonic::and_:

					if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RSP || ins.operands [ 0 ].reg == x86::ESP ) )
						return;

					arith ( op::AND, 0 );
					return;

				case mnemonic::or_:
					arith ( op::OR, 0 );
					return;

				case mnemonic::xor_:

					if ( same_registers ( ins ) ) {

						write ( ins, 0, k.constant ( 0, size ) );
						set_flags ( 3, k.constant ( 0, size ), -1, size );

						return;

					}

					arith ( op::XOR, 0 );
					return;

				case mnemonic::shl:
					arith ( op::SHL, 0 );
					return;

				case mnemonic::shr:
					arith ( op::SHR, UNSIGNED );
					return;

				case mnemonic::sar:
					arith ( op::SHR, SIGNED );
					return;

				case mnemonic::rol:
				case mnemonic::ror:
					write ( ins, 0, intrinsic ( ins.name == mnemonic::rol ? I_ROTL : I_ROTR, size, read ( ins, 0 ), read ( ins, 1 ) ) );
					return;

				case mnemonic::inc:
				case mnemonic::dec: {

					int32_t value = k.binary ( op::ADD, read ( ins, 0 ), k.constant ( ins.name == mnemonic::inc ? 1 : -1, size ), size );

					write ( ins, 0, value );
					set_flags ( 3, value, -1, size );

					return;

				}

				case mnemonic::neg: {

					int32_t value = k.make ( op::NEG, size, read ( ins, 0 ) );

					write ( ins, 0, value );
					set_flags ( 3, value, -1, size );

					return;

				}

				case mnemonic::not_:
					write ( ins, 0, k.make ( op::NOT, size, read ( ins, 0 ) ) );
					return;

				case mnemonic::imul:

					if ( ins.count == 3 ) {

						write ( ins, 0, k.binary ( op::MUL, read ( ins, 1 ), read ( ins, 2 ), size, SIGNED ) );

						return;

					}

					if ( ins.count == 2 ) {

						arith ( op::MUL, SIGNED );

						return;

					}

					[[fallthrough]];

				case mnemonic::mul: {

					uint8_t width = ins.operands [ 0 ].size;
					int32_t left = family_value ( 0 );
					int32_t right = read ( ins, 0 );

					left = k.cast ( left, width, 0 );
					regs [ 2 ] = width == 8 ? intrinsic ( ins.name == mnemonic::mul ? I_UMULH : I_MULH, 8, left, right ) : k.binary ( op::SHR, k.binary ( op::MUL, k.cast ( left, 8, UNSIGNED ), k.cast ( right, 8, UNSIGNED ), 8 ), k.constant ( width * 8, 1 ), 8, UNSIGNED );
					regs [ 0 ] = k.binary ( op::MUL, left, right, width, ins.name == mnemonic::imul ? SIGNED : UNSIGNED );

					return;

				}

				case mnemonic::div:
				case mnemonic::idiv: {

					uint8_t width = ins.operands [ 0 ].size;
					uint8_t bits = ins.name == mnemonic::idiv ? SIGNED : UNSIGNED;
					int32_t left = k.cast ( family_value ( 0 ), width, 0 );
					int32_t right = read ( ins, 0 );

					regs [ 2 ] = k.binary ( op::MOD, left, right, width, bits );
					regs [ 0 ] = k.binary ( op::DIV, left, right, width, bits );

					return;

				}

				case mnemonic::cdqe:
					regs [ 0 ] = k.cast ( k.cast ( family_value ( 0 ), 4, 0 ), 8, SIGNED );
					return;

				case mnemonic::cwde:
					regs [ 0 ] = k.cast ( k.cast ( family_value ( 0 ), 2, 0 ), 4, SIGNED );
					return;

				case mnemonic::cbw:
					regs [ 0 ] = k.cast ( k.cast ( family_value ( 0 ), 1, 0 ), 2, SIGNED );
					return;

				case mnemonic::cdq:
				case mnemonic::cqo:
				case mnemonic::cwd: {

					uint8_t width = ins.name == mnemonic::cqo ? 8 : ( ins.name == mnemonic::cdq ? 4 : 2 );

					regs [ 2 ] = k.binary ( op::SHR, k.cast ( family_value ( 0 ), width, 0 ), k.constant ( width * 8 - 1, 1 ), width, SIGNED );

					return;

				}

				case mnemonic::cmp:
					set_flags ( 1, read ( ins, 0 ), read ( ins, 1 ), size );
					return;

				case mnemonic::test:

					if ( same_registers ( ins ) ) {

						int32_t value = read ( ins, 0 );

						set_flags ( 2, value, value, size );

						return;

					}

					set_flags ( 2, read ( ins, 0 ), read ( ins, 1 ), size );
					return;

				case mnemonic::bt:
					set_flags ( 5, read ( ins, 0 ), read ( ins, 1 ), size );
					return;

				case mnemonic::bts:
				case mnemonic::btr:
				case mnemonic::btc: {

					int32_t mask = k.binary ( op::SHL, k.constant ( 1, size ), read ( ins, 1 ), size );
					int32_t value = read ( ins, 0 );

					set_flags ( 5, value, read ( ins, 1 ), size );

					if ( ins.name == mnemonic::bts )
						value = k.binary ( op::OR, value, mask, size );
					else if ( ins.name == mnemonic::btr )
						value = k.binary ( op::AND, value, k.make ( op::NOT, size, mask ), size );
					else
						value = k.binary ( op::XOR, value, mask, size );

					write ( ins, 0, value );

					return;

				}

				case mnemonic::xchg: {

					if ( same_registers ( ins ) )
						return;

					int32_t left = read ( ins, 0 );
					int32_t right = read ( ins, 1 );

					if ( ins.operands [ 0 ].type == x86::kind::MEM ) {

						write ( ins, 1, intrinsic ( I_XCHG, size, k.make ( op::ADDRESS, 8, address_of ( ins, ins.operands [ 0 ] ), -1, -1 ), right ) );

						return;

					}

					write ( ins, 0, right );
					write ( ins, 1, left );

					return;

				}

				case mnemonic::xadd: {

					int32_t value = intrinsic ( I_XADD, size, ins.operands [ 0 ].type == x86::kind::MEM ? address_of ( ins, ins.operands [ 0 ] ) : read ( ins, 0 ), read ( ins, 1 ) );

					flush ( 0xFFFFFFFFu );
					write ( ins, 1, value );

					if ( slot_of ( ins.operands [ 1 ].reg ) >= 0 )
						materialize ( slot_of ( ins.operands [ 1 ].reg ), ins.address );

					return;

				}

				case mnemonic::cmpxchg: {

					int32_t value = intrinsic ( I_CMPXCHG, size, ins.operands [ 0 ].type == x86::kind::MEM ? address_of ( ins, ins.operands [ 0 ] ) : read ( ins, 0 ), read ( ins, 1 ), k.cast ( family_value ( 0 ), size, 0 ) );

					flush ( 0xFFFFFFFFu );
					regs [ 0 ] = value;
					materialize ( 0, ins.address );
					set_flags ( 1, family_value ( 0 ), family_value ( 0 ), size );

					return;

				}

				case mnemonic::bsf:
				case mnemonic::tzcnt:
					write ( ins, 0, intrinsic ( I_TZCNT, size, read ( ins, 1 ) ) );
					return;

				case mnemonic::bsr:
				case mnemonic::lzcnt:
					write ( ins, 0, intrinsic ( I_LZCNT, size, read ( ins, 1 ) ) );
					return;

				case mnemonic::popcnt:
					write ( ins, 0, intrinsic ( I_POPCNT, size, read ( ins, 1 ) ) );
					return;

				case mnemonic::bswap:
					write ( ins, 0, intrinsic ( I_BYTESWAP, size, read ( ins, 0 ) ) );
					return;

				case mnemonic::rdtsc:
					regs [ 0 ] = intrinsic ( I_RDTSC, 8, -1 );
					materialize ( 0, ins.address );
					regs [ 2 ] = k.binary ( op::SHR, family_value ( 0 ), k.constant ( 32, 1 ), 8, UNSIGNED );
					return;

				case mnemonic::stosb:
				case mnemonic::stosw:
				case mnemonic::stosd:
				case mnemonic::stosq:
				case mnemonic::movsb:
				case mnemonic::movsw:
				case mnemonic::movsd_:
				case mnemonic::movsq: {

					if ( !( ins.prefixes & x86::REP ) ) {

						unhandled ( ins, def );

						return;

					}

					bool store = ins.name == mnemonic::stosb || ins.name == mnemonic::stosw || ins.name == mnemonic::stosd || ins.name == mnemonic::stosq;
					int32_t unit = ( ins.name == mnemonic::stosb || ins.name == mnemonic::movsb ) ? 1 : ( ( ins.name == mnemonic::stosw || ins.name == mnemonic::movsw ) ? 2 : ( ( ins.name == mnemonic::stosd || ins.name == mnemonic::movsd_ ) ? 4 : 8 ) );
					int32_t length = family_value ( 1 );

					if ( unit > 1 )
						length = k.binary ( op::MUL, length, k.constant ( unit, 8 ), 8 );

					flush ( 0xFFFFFFFFu );
					emit ( stmt::EXPR, -1, intrinsic ( store ? I_MEMSET : I_MEMCPY, 8, family_value ( 7 ), store ? k.cast ( family_value ( 0 ), 1, 0 ) : family_value ( 6 ), length ), ins.address );
					regs [ 1 ] = k.constant ( 0, 8 );

					return;

				}

				case mnemonic::call: {

					if ( regs [ 1 ] >= 0 ) {

						const expr& argument = k.exprs [ static_cast < size_t > ( regs [ 1 ] ) ];

						if ( argument.kind == op::XOR && argument.a >= 0 ) {

							const expr& left = k.exprs [ static_cast < size_t > ( argument.a ) ];

							if ( left.kind == op::VAR && k.vars [ static_cast < size_t > ( left.value ) ].cookie ) {

								regs [ 1 ] = -1;

								return;

							}

						}

					}

					flush ( 0xFFFFFFFFu );

					int32_t call = call_expr ( ins, live_after );

					if ( cookie_call ( call ) ) {

						clobber ( );

						return;

					}

					clobber ( );

					uint64_t callee = static_cast < uint64_t > ( k.exprs [ static_cast < size_t > ( call ) ].value );
					const wchar_t* declared = callee && k.names ? k.names->prototype ( callee ) : nullptr;
					bool nothing = false;

					if ( declared && declared [ 0 ] == L'v' && declared [ 1 ] == L'o' && declared [ 2 ] == L'i' && declared [ 3 ] == L'd' && declared [ 4 ] == L' ' ) {

						const wchar_t* after = declared + 5;

						while ( *after == L' ' )
							after++;

						nothing = *after != L'*';

					}

					if ( nothing ) {

						emit ( stmt::EXPR, -1, call, ins.address );
						regs [ 0 ] = -1;

					} else if ( k.long_mode ? ( live_after & 1u ) : ( live_after & 1u ) ) {

						regs [ 0 ] = call;
						materialize ( 0, ins.address );

					} else if ( live_after & ( 1u << 16 ) ) {

						k.exprs [ static_cast < size_t > ( call ) ].bits |= FLOATING;
						regs [ 16 ] = call;
						materialize ( 16, ins.address );

					} else {

						emit ( stmt::EXPR, -1, call, ins.address );
						regs [ 0 ] = -1;

					}

					return;

				}

				case mnemonic::movss:
				case mnemonic::movsd: {

					uint8_t width = ins.name == mnemonic::movss ? 4 : 8;

					if ( ins.count == 3 ) {

						unhandled ( ins, def );

						return;

					}

					int32_t value = k.cast ( read ( ins, 1, FLOATING ), width, FLOATING );

					write ( ins, 0, value );

					return;

				}

				case mnemonic::movaps:
				case mnemonic::movups:
				case mnemonic::movapd:
				case mnemonic::movupd:
				case mnemonic::movdqa:
				case mnemonic::movdqu:
				case mnemonic::lddqu:

					if ( ins.prefixes & x86::VEX_L ) {

						unhandled ( ins, def );

						return;

					}

					write ( ins, 0, read ( ins, 1, ins.operands [ 0 ].type == x86::kind::REG && x86::is_vector ( ins.operands [ 0 ].reg ) ? 0 : 0 ) );
					return;

				case mnemonic::movd:
				case mnemonic::movq: {

					int32_t value = read ( ins, 1 );
					bool to_vector = ins.operands [ 0 ].type == x86::kind::REG && x86::is_vector ( ins.operands [ 0 ].reg );
					uint8_t width = ins.name == mnemonic::movd ? 4 : 8;
					const expr& source = k.exprs [ static_cast < size_t > ( value ) ];

					if ( to_vector && source.kind == op::CONST ) {

						value = k.make ( op::REAL, width, -1, -1, source.value, FLOATING );

					} else if ( to_vector )
						value = k.make ( op::CAST, width, value, -1, 0, FLOATING | POINTER );
					else
						value = k.cast ( value, width, 0 );

					write ( ins, 0, value );

					return;

				}

				case mnemonic::addss:
					vector_arith ( op::ADD, 4 );
					return;

				case mnemonic::addsd:
					vector_arith ( op::ADD, 8 );
					return;

				case mnemonic::subss:
					vector_arith ( op::SUB, 4 );
					return;

				case mnemonic::subsd:
					vector_arith ( op::SUB, 8 );
					return;

				case mnemonic::mulss:
					vector_arith ( op::MUL, 4 );
					return;

				case mnemonic::mulsd:
					vector_arith ( op::MUL, 8 );
					return;

				case mnemonic::divss:
					vector_arith ( op::DIV, 4 );
					return;

				case mnemonic::divsd:
					vector_arith ( op::DIV, 8 );
					return;

				case mnemonic::sqrtss:
				case mnemonic::sqrtsd:
					write ( ins, 0, intrinsic ( ins.name == mnemonic::sqrtss ? I_SQRTF : I_SQRT, ins.name == mnemonic::sqrtss ? 4 : 8, read ( ins, ins.count - 1, FLOATING ) ) );
					k.exprs.back ( ).bits |= FLOATING;
					return;

				case mnemonic::minss:
				case mnemonic::maxss:
				case mnemonic::minsd:
				case mnemonic::maxsd: {

					bool single = ins.name == mnemonic::minss || ins.name == mnemonic::maxss;
					bool lower = ins.name == mnemonic::minss || ins.name == mnemonic::minsd;
					int32_t value = intrinsic ( single ? ( lower ? I_FMINF : I_FMAXF ) : ( lower ? I_FMIN : I_FMAX ), single ? 4 : 8, read ( ins, ins.count - 2, FLOATING ), read ( ins, ins.count - 1, FLOATING ) );

					k.exprs [ static_cast < size_t > ( value ) ].bits |= FLOATING;
					write ( ins, 0, value );

					return;

				}

				case mnemonic::cvtsi2ss:
				case mnemonic::cvtsi2sd:
					write ( ins, 0, k.make ( op::CAST, ins.name == mnemonic::cvtsi2ss ? 4 : 8, k.cast ( read ( ins, ins.count - 1 ), ins.operands [ ins.count - 1 ].size, SIGNED ), -1, 0, FLOATING ) );
					return;

				case mnemonic::cvttss2si:
				case mnemonic::cvttsd2si:
				case mnemonic::cvtss2si:
				case mnemonic::cvtsd2si:
					write ( ins, 0, k.make ( op::CAST, size, read ( ins, 1, FLOATING ), -1, 0, SIGNED ) );
					return;

				case mnemonic::cvtss2sd:
				case mnemonic::cvtsd2ss:
					write ( ins, 0, k.make ( op::CAST, ins.name == mnemonic::cvtss2sd ? 8 : 4, read ( ins, ins.count - 1, FLOATING ), -1, 0, FLOATING ) );
					return;

				case mnemonic::xorps:
				case mnemonic::xorpd:
				case mnemonic::pxor:

					if ( same_registers ( ins ) ) {

						write ( ins, 0, k.constant ( 0, 16 ) );

						return;

					}

					write ( ins, 0, intrinsic ( I_XOR, 16, read ( ins, 0 ), read ( ins, 1 ) ) );
					return;

				case mnemonic::andps:
				case mnemonic::andpd:
					write ( ins, 0, intrinsic ( I_AND, 16, read ( ins, 0 ), read ( ins, 1 ) ) );
					return;

				case mnemonic::orps:
				case mnemonic::orpd:
					write ( ins, 0, intrinsic ( I_OR, 16, read ( ins, 0 ), read ( ins, 1 ) ) );
					return;

				case mnemonic::ucomiss:
				case mnemonic::comiss:
				case mnemonic::ucomisd:
				case mnemonic::comisd: {

					uint8_t width = ins.name == mnemonic::ucomiss || ins.name == mnemonic::comiss ? 4 : 8;

					set_flags ( 4, k.cast ( read ( ins, 0, FLOATING ), width, FLOATING ), k.cast ( read ( ins, 1, FLOATING ), width, FLOATING ), width );

					return;

				}

				case mnemonic::cpuid:
					unhandled ( ins, def );
					return;

				case mnemonic::hlt:
					emit ( stmt::EXPR, -1, intrinsic ( I_HALT, 4, -1 ), ins.address );
					return;

				default:
					break;

			}

			int32_t value = static_cast < int32_t > ( ins.name );

			if ( value >= static_cast < int32_t > ( mnemonic::seto ) && value <= static_cast < int32_t > ( mnemonic::setg ) ) {

				write ( ins, 0, k.cast ( condition ( ins.condition ), 1, UNSIGNED ) );

				return;

			}

			if ( value >= static_cast < int32_t > ( mnemonic::cmovo ) && value <= static_cast < int32_t > ( mnemonic::cmovg ) ) {

				if ( ins.operands [ 0 ].type == x86::kind::REG && slot_of ( ins.operands [ 0 ].reg ) >= 0 ) {

				int32_t slot = slot_of ( ins.operands [ 0 ].reg );
				int32_t current = regs [ slot ];

				if ( current >= 0 && k.exprs [ static_cast < size_t > ( current ) ].kind != op::VAR && k.exprs [ static_cast < size_t > ( current ) ].kind != op::CONST && k.exprs [ static_cast < size_t > ( current ) ].depth >= 3 )
					materialize ( slot, ins.address );

			}

			int32_t chosen = k.make ( op::TERNARY, size, condition ( ins.condition ), read ( ins, 1 ) );

				k.exprs [ static_cast < size_t > ( chosen ) ].c = read ( ins, 0 );
				k.deepen ( chosen );
				write ( ins, 0, chosen );

				return;

			}

			unhandled ( ins, def );

		}

	};

	static int32_t call_arguments ( builder& b, const x86::instruction& ins ) {

		uint64_t direct = 0;

		if ( ins.direct ( ) )
			direct = ins.target ( );
		else if ( ins.count && ins.operands [ 0 ].type == x86::kind::MEM && ins.operands [ 0 ].rip_relative ( ) )
			direct = ins.next ( ) + static_cast < uint64_t > ( ins.operands [ 0 ].value );

		if ( !direct )
			return -1;

		const wchar_t* declared = b.names->prototype ( direct );

		if ( declared ) {

			c_prototype shape;
			base::writer problem;

			if ( parse_prototype ( declared, b.long_mode, shape, problem ) )
				return shape.count;

		}

		wchar_t name [ 200 ];

		if ( b.names->name ( direct, true, name, 200 ) ) {

			int32_t known = import_arguments ( name );

			if ( known >= 0 )
				return known;

		}

		int32_t callee = b.db->function_starting ( direct );

		if ( callee >= 0 && ( b.db->functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) && b.db->functions [ static_cast < size_t > ( callee ) ].import >= 0 )
			return import_arguments ( b.file->imports [ static_cast < size_t > ( b.db->functions [ static_cast < size_t > ( callee ) ].import ) ].name.get ( ) );

		return -1;

	}

	static bool cookie_check ( const base::list < x86::instruction >& list, int32_t index ) {

		if ( index < 1 || list [ static_cast < size_t > ( index ) ].name != mnemonic::call )
			return false;

		const x86::instruction& before = list [ static_cast < size_t > ( index - 1 ) ];

		return before.name == mnemonic::xor_ && before.count == 2 && before.operands [ 0 ].type == x86::kind::REG && before.operands [ 1 ].type == x86::kind::REG && ( before.operands [ 0 ].reg == x86::RCX || before.operands [ 0 ].reg == x86::ECX ) && ( before.operands [ 1 ].reg == x86::RSP || before.operands [ 1 ].reg == x86::RBP || before.operands [ 1 ].reg == x86::ESP || before.operands [ 1 ].reg == x86::EBP );

	}

	static void annotate ( builder& b, const base::list < x86::instruction >& list, base::list < uint32_t >& uses, base::list < uint32_t >& defs ) {

		static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
		uint32_t written = 0;

		uses.resize ( list.size ( ) );
		defs.resize ( list.size ( ) );

		for ( int32_t i = 0; i < list.count ( ); i++ ) {

			const x86::instruction& ins = list [ static_cast < size_t > ( i ) ];
			uint32_t u = 0;
			uint32_t d = 0;

			effects ( ins, b.long_mode, u, d );

			if ( ins.name == mnemonic::ret && !b.float_return )
				u &= ~( 1u << 16 );

			bool tail = false;

			if ( ins.control == x86::flow::JUMP && b.long_mode && ins.count ) {

				if ( ins.direct ( ) ) {

					uint64_t target = ins.target ( );
					int32_t here = b.db->function_at ( ins.address );

					tail = b.db->function_starting ( target ) >= 0 && b.db->function_at ( target ) != here;

				} else if ( ins.operands [ 0 ].type == x86::kind::MEM && ins.operands [ 0 ].rip_relative ( ) )
					tail = true;

			}

			if ( tail ) {

				int32_t count = call_arguments ( b, ins );
				int32_t highest = -1;

				for ( int32_t p = 0; p < 4; p++ ) {

					if ( written & ( 1u << p ) )
						highest = p;

				}

				if ( count >= 0 )
					highest = base::min ( count, 4 ) - 1;

				for ( int32_t p = 0; p <= highest; p++ )
					u |= ( written & ( 1u << ( 4 + p ) ) ) ? ( 1u << ( 16 + p ) ) : ( 1u << gpr [ p ] );

			} else if ( ins.name == mnemonic::call ) {

				if ( cookie_check ( list, i ) ) {

					u = 1u << 1;
					d = 0;

				} else if ( b.long_mode ) {

					int32_t count = call_arguments ( b, ins );
					int32_t highest = -1;

					for ( int32_t p = 0; p < 4; p++ ) {

						if ( written & ( 1u << p ) )
							highest = p;

					}

					if ( count >= 0 )
						highest = base::min ( count, 4 ) - 1;

					for ( int32_t p = 0; p <= highest; p++ )
						u |= ( written & ( 1u << ( 4 + p ) ) ) ? ( 1u << ( 16 + p ) ) : ( 1u << gpr [ p ] );

				}

				written = 0;

			} else if ( b.long_mode ) {

				for ( int32_t p = 0; p < 4; p++ ) {

					if ( d & ( 1u << gpr [ p ] ) )
						written |= 1u << p;

					if ( d & ( 1u << ( 16 + p ) ) )
						written |= ( 1u << p ) | ( 1u << ( 4 + p ) );

				}

			}

			uses [ static_cast < size_t > ( i ) ] = u;
			defs [ static_cast < size_t > ( i ) ] = d;

		}

	}

	bool builder::lift ( ) {

		for ( int32_t i = 0; i < 32; i++ )
			family_var [ i ] = -1;

		if ( !analysis::build_graph ( *db, function, graph ) )
			return false;

		int32_t count = graph.blocks.count ( );
		base::list < uint32_t > uses;
		base::list < uint32_t > defs;
		base::list < uint32_t > live_in;
		base::list < uint32_t > live_out;

		uses.fill ( static_cast < size_t > ( count ), 0 );
		defs.fill ( static_cast < size_t > ( count ), 0 );
		live_in.fill ( static_cast < size_t > ( count ), 0 );
		live_out.fill ( static_cast < size_t > ( count ), 0 );
		code.resize ( static_cast < size_t > ( count ) );

		base::list < base::list < x86::instruction > > listings;
		base::list < base::list < uint32_t > > ins_uses;
		base::list < base::list < uint32_t > > ins_defs;

		listings.resize ( static_cast < size_t > ( count ) );
		ins_uses.resize ( static_cast < size_t > ( count ) );
		ins_defs.resize ( static_cast < size_t > ( count ) );

		for ( int32_t i = 0; i < count; i++ ) {

			const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
			base::list < x86::instruction >& list = listings [ static_cast < size_t > ( i ) ];
			uint64_t at = item.start;

			while ( at < item.end ) {

				x86::instruction ins;

				if ( !db->decode ( at, ins ) )
					break;

				list.push ( ins );
				at = ins.next ( );

			}

		}

		for ( int32_t i = 0; i < count && !float_return; i++ ) {

			const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( i ) ];

			if ( list.empty ( ) || list [ list.size ( ) - 1 ].name != mnemonic::ret )
				continue;

			for ( int32_t k = list.count ( ) - 2; k >= 0; k-- ) {

				const x86::instruction& ins = list [ static_cast < size_t > ( k ) ];
				uint32_t u = 0;
				uint32_t d = 0;

				if ( ins.name == mnemonic::call )
					break;

				effects ( ins, long_mode, u, d );

				if ( d & ( 1u << 16 ) ) {

					float_return = true;

					break;

				}

				if ( d & 1u )
					break;

			}

		}

		for ( int32_t i = 0; i < count; i++ ) {

			base::list < x86::instruction >& list = listings [ static_cast < size_t > ( i ) ];
			uint32_t use = 0;
			uint32_t def = 0;

			annotate ( *this, list, ins_uses [ static_cast < size_t > ( i ) ], ins_defs [ static_cast < size_t > ( i ) ] );

			for ( int32_t k = 0; k < list.count ( ); k++ ) {

				use |= ins_uses [ static_cast < size_t > ( i ) ] [ static_cast < size_t > ( k ) ] & ~def;
				def |= ins_defs [ static_cast < size_t > ( i ) ] [ static_cast < size_t > ( k ) ];

			}

			uses [ static_cast < size_t > ( i ) ] = use;
			defs [ static_cast < size_t > ( i ) ] = def;

		}

		for ( int32_t round = 0; round < 64; round++ ) {

			bool changed = false;

			for ( int32_t i = count - 1; i >= 0; i-- ) {

				const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
				uint32_t out_mask = 0;

				for ( int32_t e = 0; e < item.out_count; e++ )
					out_mask |= live_in [ static_cast < size_t > ( graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to ) ];

				uint32_t in_mask = uses [ static_cast < size_t > ( i ) ] | ( out_mask & ~defs [ static_cast < size_t > ( i ) ] );

				if ( out_mask != live_out [ static_cast < size_t > ( i ) ] || in_mask != live_in [ static_cast < size_t > ( i ) ] ) {

					live_out [ static_cast < size_t > ( i ) ] = out_mask;
					live_in [ static_cast < size_t > ( i ) ] = in_mask;
					changed = true;

				}

			}

			if ( !changed )
				break;

		}

		entry_live = live_in [ 0 ];

		if ( long_mode ) {

			static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };

			for ( int32_t i = 0; i < 4; i++ ) {

				bool integer = ( entry_live & ( 1u << gpr [ i ] ) ) != 0;
				bool floating = ( entry_live & ( 1u << ( 16 + i ) ) ) != 0;

				if ( !integer && !floating )
					continue;

				params = base::max ( params, i + 1 );

				int32_t var = register_var ( integer ? gpr [ i ] : 16 + i );

				vars [ static_cast < size_t > ( var ) ].kind = var_kind::PARAMETER;
				vars [ static_cast < size_t > ( var ) ].position = i;

				if ( floating && !integer )
					vars [ static_cast < size_t > ( var ) ].bits |= FLOATING;

			}

		}

		base::list < int32_t > order;
		base::list < uint8_t > seen;

		seen.fill ( static_cast < size_t > ( count ), 0 );
		order.push ( 0 );
		seen [ 0 ] = 1;
		code [ 0 ].delta_known = true;

		for ( int32_t k = 0; k < order.count ( ); k++ ) {

			int32_t index = order [ static_cast < size_t > ( k ) ];
			const analysis::block& item = graph.blocks [ static_cast < size_t > ( index ) ];

			for ( int32_t e = 0; e < item.out_count; e++ ) {

				int32_t to = graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to;

				if ( !seen [ static_cast < size_t > ( to ) ] ) {

					seen [ static_cast < size_t > ( to ) ] = 1;
					order.push ( to );

				}

			}

		}

		for ( const analysis::edge& link : graph.edges )
			code [ static_cast < size_t > ( link.to ) ].preds++;

		machine state;

		state.b = this;

		for ( int32_t index : order ) {

			const analysis::block& item = graph.blocks [ static_cast < size_t > ( index ) ];
			block_code& out_block = code [ static_cast < size_t > ( index ) ];
			const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( index ) ];
			base::list < uint32_t > after;
			base::list < uint32_t > def_list;
			uint32_t running = live_out [ static_cast < size_t > ( index ) ];

			after.resize ( list.size ( ) );
			def_list.resize ( list.size ( ) );

			for ( int32_t i = list.count ( ) - 1; i >= 0; i-- ) {

				uint32_t u = ins_uses [ static_cast < size_t > ( index ) ] [ static_cast < size_t > ( i ) ];
				uint32_t d = ins_defs [ static_cast < size_t > ( index ) ] [ static_cast < size_t > ( i ) ];

				after [ static_cast < size_t > ( i ) ] = running;
				def_list [ static_cast < size_t > ( i ) ] = d;
				running = u | ( running & ~d );

			}

			state.reset ( );
			state.delta = out_block.delta;
			state.prologue = index == 0;

			if ( out_block.preds == 1 && out_block.flag_kind ) {

				state.flag_kind = out_block.flag_kind;
				state.flag_a = out_block.flag_a;
				state.flag_b = out_block.flag_b;
				state.flag_size = out_block.flag_size;

			}
			out_block.first = statements.count ( );
			out_block.reachable = true;

			bool pushing = index == 0;

			for ( int32_t i = 0; i < list.count ( ); i++ ) {

				const x86::instruction& ins = list [ static_cast < size_t > ( i ) ];

				if ( pushing && ins.name == mnemonic::push && ins.count && ins.operands [ 0 ].type == x86::kind::REG )
					pushed += long_mode ? 8 : 4;
				else if ( !( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::MEM && ins.operands [ 1 ].type == x86::kind::REG ) )
					pushing = false;

				state.step ( ins, after [ static_cast < size_t > ( i ) ], def_list [ static_cast < size_t > ( i ) ] );

				if ( ins.name == mnemonic::call || ( ins.count && ins.operands [ 0 ].type == x86::kind::MEM && ins.name != mnemonic::push ) )
					state.prologue = false;

				if ( ins.name != mnemonic::push && ins.name != mnemonic::sub && ins.name != mnemonic::mov && ins.name != mnemonic::lea )
					state.prologue = false;

			}

			const x86::instruction* last = list.empty ( ) ? nullptr : &list.back ( );

			out_block.last = item.last;

			if ( last && last->control == x86::flow::BRANCH ) {

				out_block.end = term::BRANCH;
				out_block.condition = last->condition != 0xFF ? state.condition ( last->condition ) : make ( op::INTRINSIC, 1, -1, -1, I_FLAGS );

				for ( int32_t e = 0; e < item.out_count; e++ ) {

					const analysis::edge& link = graph.edges [ static_cast < size_t > ( item.first_out + e ) ];

					if ( link.kind == analysis::edge_kind::TAKEN )
						out_block.taken = link.to;
					else
						out_block.other = link.to;

				}

				if ( out_block.taken < 0 || out_block.other < 0 ) {

					out_block.end = term::FALL;
					out_block.taken = out_block.taken < 0 ? out_block.other : out_block.taken;

				}

			} else if ( last && last->control == x86::flow::RETURN ) {

				out_block.end = term::RETURN;

				int32_t rax = state.regs [ 0 ];

				if ( rax < 0 && family_var [ 0 ] >= 0 )
					rax = var_expr ( family_var [ 0 ], 0 );

				if ( rax >= 0 && !( exprs [ static_cast < size_t > ( rax ) ].kind == op::VAR && vars [ static_cast < size_t > ( exprs [ static_cast < size_t > ( rax ) ].value ) ].kind == var_kind::PARAMETER && false ) )
					out_block.returned = rax;
				else if ( state.regs [ 16 ] >= 0 ) {

					out_block.returned = state.regs [ 16 ];
					returns_float = true;

				}

			} else if ( last && last->control == x86::flow::JUMP && graph.table >= 0 && item.out_count > 1 ) {

				out_block.end = term::SWITCH;

				const analysis::jump_table& table = db->tables [ static_cast < size_t > ( graph.table ) ];
				int32_t selector = -1;

				for ( int32_t i = list.count ( ) - 1; i >= 0 && selector < 0; i-- ) {

					const x86::instruction& ins = list [ static_cast < size_t > ( i ) ];

					for ( int32_t o = 0; o < ins.count; o++ ) {

						if ( ins.operands [ o ].type == x86::kind::MEM && ins.operands [ o ].index != x86::NONE && ( ins.operands [ o ].size == 1 || ins.operands [ o ].scale >= 4 ) ) {

							selector = state.read_reg ( x86::resize_register ( ins.operands [ o ].index, 4 ) );

							break;

						}

					}

				}

				( void ) table;
				out_block.selector = selector >= 0 ? selector : make ( op::INTRINSIC, 4, -1, -1, I_FLAGS );

			} else if ( last && last->control == x86::flow::JUMP && item.out_count == 0 ) {

				out_block.end = term::TAIL;
				state.flush ( 0xFFFFFFFFu );

				int32_t call = state.call_expr ( *last, 0xFFFFFFFFu );

				out_block.returned = call;

			} else if ( last && last->control == x86::flow::STOP ) {

				out_block.end = term::STOP;

			} else if ( item.out_count > 0 ) {

				out_block.end = term::FALL;
				out_block.taken = graph.edges [ static_cast < size_t > ( item.first_out ) ].to;

			} else
				out_block.end = term::STOP;

			uint32_t keep = live_out [ static_cast < size_t > ( index ) ];
			int32_t flag_a = state.flag_a;
			int32_t flag_b = state.flag_b;
			int32_t flag_a_slot = -1;
			int32_t flag_b_slot = -1;

			state.guard_a = out_block.condition;
			state.guard_b = out_block.selector;

			for ( int32_t slot = 0; slot < 32; slot++ ) {

				if ( state.regs [ slot ] >= 0 && state.regs [ slot ] == flag_a && ( keep & ( 1u << slot ) ) )
					flag_a_slot = slot;

				if ( state.regs [ slot ] >= 0 && state.regs [ slot ] == flag_b && ( keep & ( 1u << slot ) ) )
					flag_b_slot = slot;

			}

			for ( int32_t slot = 0; slot < 32; slot++ ) {

				if ( slot == 4 )
					continue;

				if ( state.regs [ slot ] >= 0 && ( keep & ( 1u << slot ) ) && out_block.end != term::RETURN && out_block.end != term::TAIL )
					state.materialize ( slot, item.last );

			}

			out_block.condition = state.guard_a;
			out_block.selector = state.guard_b;
			state.guard_a = -1;
			state.guard_b = -1;
			out_block.count = statements.count ( ) - out_block.first;

			if ( flag_a_slot >= 0 )
				flag_a = var_expr ( register_var ( flag_a_slot ), exprs [ static_cast < size_t > ( flag_a ) ].size );

			if ( flag_b_slot >= 0 )
				flag_b = var_expr ( register_var ( flag_b_slot ), exprs [ static_cast < size_t > ( flag_b ) ].size );

			for ( int32_t e = 0; e < item.out_count; e++ ) {

				block_code& next = code [ static_cast < size_t > ( graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to ) ];

				if ( !next.delta_known ) {

					next.delta = state.delta;
					next.delta_known = true;

				}

				next.flag_kind = state.flag_kind;
				next.flag_a = flag_a;
				next.flag_b = flag_b;
				next.flag_size = state.flag_size;

			}

		}

		propagate ( );

		for ( int32_t i = 0; i < count; i++ ) {

			block_code& item = code [ static_cast < size_t > ( i ) ];

			if ( item.end != term::RETURN || item.returned < 0 )
				continue;

			bool defined = ( defs [ static_cast < size_t > ( i ) ] & ( returns_float ? ( 1u << 16 ) : 1u ) ) != 0;

			if ( !defined ) {

				int32_t incoming = 0;
				int32_t defining = 0;

				for ( const analysis::edge& link : graph.edges ) {

					if ( link.to != i )
						continue;

					incoming++;

					if ( defs [ static_cast < size_t > ( link.from ) ] & 1u )
						defining++;

				}

				defined = incoming > 0 && incoming == defining;

			}

			if ( !defined )
				continue;

			returns_value = true;
			return_size = base::max ( return_size, exprs [ static_cast < size_t > ( item.returned ) ].size );

		}

		prune ( );
		fold_returns ( );
		split_webs ( );

		return true;

	}

}