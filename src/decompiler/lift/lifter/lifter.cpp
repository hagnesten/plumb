#include "decompiler/lift/lifter/lifter.h"
#include "decompiler/lift/constants/constants.h"
#include "decompiler/lift/machine/machine.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "decompiler/literals/literals.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"

int32_t decompiler::call_arguments ( builder& b, const x86::instruction& ins ) {

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

	return internal_arguments ( *b.db, direct, b.long_mode, 0 );

}

bool decompiler::cookie_check ( const base::list < x86::instruction >& list, int32_t index ) {

	if ( index < 1 || list [ static_cast < size_t > ( index ) ].name != mnemonic::call )
		return false;

	const x86::instruction& before = list [ static_cast < size_t > ( index - 1 ) ];

	return before.name == mnemonic::xor_ && before.count == 2 && before.operands [ 0 ].type == x86::kind::REG && before.operands [ 1 ].type == x86::kind::REG && ( before.operands [ 0 ].reg == x86::RCX || before.operands [ 0 ].reg == x86::ECX ) && ( before.operands [ 1 ].reg == x86::RSP || before.operands [ 1 ].reg == x86::RBP || before.operands [ 1 ].reg == x86::ESP || before.operands [ 1 ].reg == x86::EBP );

}

void decompiler::annotate ( builder& b, const base::list < x86::instruction >& list, base::list < uint32_t >& uses, base::list < uint32_t >& defs ) {

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

bool decompiler::builder::lift ( ) {

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

		for ( int32_t k = 1; k + 1 < list.count ( ); k++ ) {

			const x86::instruction& probe = list [ static_cast < size_t > ( k ) ];
			const x86::instruction& after = list [ static_cast < size_t > ( k + 1 ) ];

			if ( probe.name != mnemonic::call || after.name != mnemonic::sub || after.count != 2 || after.operands [ 0 ].type != x86::kind::REG || x86::register_family ( after.operands [ 0 ].reg ) != 4 || after.operands [ 1 ].type != x86::kind::REG || x86::register_family ( after.operands [ 1 ].reg ) != 0 )
				continue;

			bool sized = false;

			for ( int32_t back = k - 1; back >= 0 && back >= k - 8; back-- ) {

				const x86::instruction& before = list [ static_cast < size_t > ( back ) ];

				if ( before.name == mnemonic::mov && before.count == 2 && before.operands [ 0 ].type == x86::kind::REG && x86::register_family ( before.operands [ 0 ].reg ) == 0 && before.operands [ 1 ].type == x86::kind::IMM ) {

					sized = true;

					break;

				}

			}

			if ( sized )
				list.remove_at ( static_cast < size_t > ( k ) );

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
		int32_t strict = internal_arguments ( *db, function, long_mode, 0 );

		if ( strict >= 0 ) {

			for ( int32_t i = strict; i < 4; i++ ) {

				if ( !( entry_live & ( 1u << ( 16 + i ) ) ) )
					entry_live &= ~( 1u << gpr [ i ] );

			}

		}

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

	base::list < int32_t > joined;

	joined.fill ( static_cast < size_t > ( count ), -1 );

	for ( int32_t pass = 0; pass < 4; pass++ )
	for ( int32_t i = 1; i < count; i++ ) {

		const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( i ) ];

		if ( joined [ static_cast < size_t > ( i ) ] >= 0 || list.empty ( ) )
			continue;

		if ( code [ static_cast < size_t > ( i ) ].preds < 2 ) {

			bool through = false;

			for ( const analysis::edge& link : graph.edges ) {

				if ( link.to == i && joined [ static_cast < size_t > ( link.from ) ] >= 0 )
					through = true;

			}

			if ( !through )
				continue;

		}

		const x86::instruction& last = list [ list.size ( ) - 1 ];

		if ( last.control != x86::flow::BRANCH || last.condition == 0xFF )
			continue;

		bool neutral = true;

		for ( int32_t k = 0; k + 1 < list.count ( ) && neutral; k++ ) {

			mnemonic name = list [ static_cast < size_t > ( k ) ].name;

			neutral = name == mnemonic::mov || name == mnemonic::lea || name == mnemonic::movzx || name == mnemonic::movsx || name == mnemonic::movsxd || name == mnemonic::nop;

		}

		if ( !neutral )
			continue;

		bool all_set = true;

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to != i )
				continue;

			const base::list < x86::instruction >& before = listings [ static_cast < size_t > ( link.from ) ];

			if ( before.empty ( ) || before [ before.size ( ) - 1 ].control == x86::flow::CALL )
				all_set = false;

		}

		if ( !all_set )
			continue;

		variable& made = vars.emplace ( );

		made.kind = var_kind::REGISTER;
		made.size = 1;
		made.flags = true;
		made.offset = last.condition;
		joined [ static_cast < size_t > ( i ) ] = vars.count ( ) - 1;

	}

	base::list < int32_t > set_joined;
	base::list < uint8_t > set_code;

	set_joined.fill ( static_cast < size_t > ( count ), -1 );
	set_code.fill ( static_cast < size_t > ( count ), 0 );

	for ( int32_t i = 1; i < count && joined_sets; i++ ) {

		const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( i ) ];

		if ( joined [ static_cast < size_t > ( i ) ] >= 0 || list.empty ( ) || code [ static_cast < size_t > ( i ) ].preds < 2 )
			continue;

		int32_t found = -1;

		for ( int32_t k = 0; k < list.count ( ); k++ ) {

			const x86::instruction& ins = list [ static_cast < size_t > ( k ) ];

			if ( static_cast < int32_t > ( ins.name ) >= static_cast < int32_t > ( mnemonic::seto ) && static_cast < int32_t > ( ins.name ) <= static_cast < int32_t > ( mnemonic::setg ) && ins.condition != 0xFF ) {

				found = k;

				break;

			}

			if ( joined_moves && static_cast < int32_t > ( ins.name ) >= static_cast < int32_t > ( mnemonic::cmovo ) && static_cast < int32_t > ( ins.name ) <= static_cast < int32_t > ( mnemonic::cmovg ) && ins.condition != 0xFF ) {

				found = k;

				break;

			}

			mnemonic name = ins.name;

			if ( !( name == mnemonic::mov || name == mnemonic::lea || name == mnemonic::movzx || name == mnemonic::movsx || name == mnemonic::movsxd || name == mnemonic::nop ) )
				break;

		}

		if ( found < 0 )
			continue;

		bool all_set = true;

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.to != i )
				continue;

			const base::list < x86::instruction >& before = listings [ static_cast < size_t > ( link.from ) ];

			if ( before.empty ( ) || before [ before.size ( ) - 1 ].control == x86::flow::CALL || before [ before.size ( ) - 1 ].control == x86::flow::BRANCH )
				all_set = false;

		}

		if ( !all_set )
			continue;

		uint8_t wanted = list [ static_cast < size_t > ( found ) ].condition;
		uint8_t stored = wanted == 5 ? 4 : wanted;
		variable& made = vars.emplace ( );

		made.kind = var_kind::REGISTER;
		made.size = 1;
		made.flags = true;
		made.offset = stored;
		set_joined [ static_cast < size_t > ( i ) ] = vars.count ( ) - 1;
		set_code [ static_cast < size_t > ( i ) ] = stored;

	}

	write_at.clear ( );
	write_regs.clear ( );

	for ( int32_t index = 0; index < count; index++ ) {

		const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( index ) ];
		block_code& entry = code [ static_cast < size_t > ( index ) ];

		entry.writes_first = write_at.count ( );

		for ( int32_t i = 0; i < list.count ( ); i++ ) {

			uint32_t d = ins_defs [ static_cast < size_t > ( index ) ] [ static_cast < size_t > ( i ) ];

			if ( list [ static_cast < size_t > ( i ) ].name == mnemonic::call )
				d |= long_mode ? 0xF07u : 0x7u;

			if ( !d )
				continue;

			write_at.push ( list [ static_cast < size_t > ( i ) ].address );
			write_regs.push ( d );

		}

		entry.writes_count = write_at.count ( ) - entry.writes_first;

	}

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

		if ( set_joined [ static_cast < size_t > ( index ) ] >= 0 ) {

			state.entry_flag_var = set_joined [ static_cast < size_t > ( index ) ];
			state.entry_flag_code = set_code [ static_cast < size_t > ( index ) ];

		}

		out_block.first = statements.count ( );
		out_block.reachable = true;

		bool pushing = index == 0;
		int32_t table_at = -1;
		uint8_t table_reg = 0;
		int32_t table_selector = -1;

		if ( !list.empty ( ) && list.back ( ).control == x86::flow::JUMP && graph.table >= 0 && item.out_count > 1 ) {

			for ( int32_t i = list.count ( ) - 1; i >= 0 && table_at < 0; i-- ) {

				const x86::instruction& ins = list [ static_cast < size_t > ( i ) ];

				for ( int32_t o = 0; o < ins.count; o++ ) {

					if ( ins.operands [ o ].type == x86::kind::MEM && ins.operands [ o ].index != x86::NONE && ( ins.operands [ o ].size == 1 || ins.operands [ o ].scale >= 4 ) ) {

						table_at = i;
						table_reg = x86::resize_register ( ins.operands [ o ].index, 4 );

						break;

					}

				}

			}

		}

		for ( int32_t i = 0; i < list.count ( ); i++ ) {

			const x86::instruction& ins = list [ static_cast < size_t > ( i ) ];

			if ( i == table_at )
				table_selector = state.read_reg ( table_reg );

			if ( pushing && ins.name == mnemonic::push && ins.count && ins.operands [ 0 ].type == x86::kind::REG )
				pushed += long_mode ? 8 : 4;
			else if ( !( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::MEM && ins.operands [ 1 ].type == x86::kind::REG ) )
				pushing = false;

			state.step ( ins, after [ static_cast < size_t > ( i ) ], def_list [ static_cast < size_t > ( i ) ] );

			if ( state.delta < deepest )
				deepest = state.delta;

			if ( ins.name == mnemonic::call || ( ins.count && ins.operands [ 0 ].type == x86::kind::MEM && ins.name != mnemonic::push ) )
				state.prologue = false;

			if ( ins.name != mnemonic::push && ins.name != mnemonic::sub && ins.name != mnemonic::mov && ins.name != mnemonic::lea )
				state.prologue = false;

		}

		const x86::instruction* last = list.empty ( ) ? nullptr : &list.back ( );

		out_block.last = item.last;

		for ( int32_t e = 0; e < item.out_count; e++ ) {

			int32_t to = graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to;

			if ( to != index && set_joined [ static_cast < size_t > ( to ) ] >= 0 )
				state.emit ( stmt::ASSIGN, var_expr ( set_joined [ static_cast < size_t > ( to ) ], 1 ), state.condition ( set_code [ static_cast < size_t > ( to ) ] ), item.last );

		}

		if ( joined [ static_cast < size_t > ( index ) ] < 0 ) {

			base::list < int32_t > pending;
			base::list < uint8_t > done;

			done.fill ( static_cast < size_t > ( count ), 0 );

			for ( int32_t e = 0; e < item.out_count; e++ )
				pending.push ( graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to );

			while ( pending.count ( ) ) {

				int32_t to = pending [ 0 ];

				pending.remove_at ( 0 );

				if ( to == index || done [ static_cast < size_t > ( to ) ] || joined [ static_cast < size_t > ( to ) ] < 0 )
					continue;

				done [ static_cast < size_t > ( to ) ] = 1;

				uint8_t code_of = listings [ static_cast < size_t > ( to ) ].back ( ).condition;

				state.emit ( stmt::ASSIGN, var_expr ( joined [ static_cast < size_t > ( to ) ], 1 ), state.condition ( code_of ), item.last );

				const analysis::block& next = graph.blocks [ static_cast < size_t > ( to ) ];

				for ( int32_t e = 0; e < next.out_count; e++ )
					pending.push ( graph.edges [ static_cast < size_t > ( next.first_out + e ) ].to );

			}

		}

		if ( last && last->control == x86::flow::BRANCH ) {

			if ( state.flag_kind == 1 && state.flag_a >= 0 && state.flag_b >= 0 && last->condition >= 4 && last->condition <= 5 ) {

				for ( int32_t slot = 0; slot < 16; slot++ ) {

					int32_t held = state.regs [ slot ];

					if ( held < 0 || held != state.flag_result )
						continue;

					uint8_t size = state.flag_size;

					state.materialize ( slot, last->address );
					state.set_flags ( 3, var_expr ( register_var ( slot ), size ), -1, size );

					break;

				}

			}

			if ( state.flag_kind == 3 && state.flag_a >= 0 && last->condition >= 4 && last->condition <= 5 ) {

				for ( int32_t slot = 0; slot < 16; slot++ ) {

					if ( state.regs [ slot ] == state.flag_a && exprs [ static_cast < size_t > ( state.flag_a ) ].kind != op::VAR && exprs [ static_cast < size_t > ( state.flag_a ) ].kind != op::CONST ) {

						state.materialize ( slot, last->address );

						break;

					}

				}

			}

			out_block.end = term::BRANCH;
			if ( joined [ static_cast < size_t > ( index ) ] >= 0 )
				out_block.condition = var_expr ( joined [ static_cast < size_t > ( index ) ], 1 );
			else
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
			int32_t xmm = state.regs [ 16 ];

			if ( rax < 0 && family_var [ 0 ] >= 0 )
				rax = var_expr ( family_var [ 0 ], 0 );

			if ( xmm < 0 && family_var [ 16 ] >= 0 )
				xmm = var_expr ( family_var [ 16 ], 0 );

			bool rax_written = false;

			for ( int32_t k = 0; k < count && !rax_written; k++ )
				rax_written = ( defs [ static_cast < size_t > ( k ) ] & 1u ) != 0;

			int32_t last_writer = 0;
			const base::list < x86::instruction >& own = listings [ static_cast < size_t > ( index ) ];

			for ( int32_t k = own.count ( ) - 2; k >= 0 && block_writers && !last_writer; k-- ) {

				const x86::instruction& ins = own [ static_cast < size_t > ( k ) ];
				uint32_t u = 0;
				uint32_t d = 0;

				if ( ins.name == mnemonic::call )
					break;

				effects ( ins, long_mode, u, d );

				if ( d & ( 1u << 16 ) ) {

					switch ( ins.name ) {

						case mnemonic::movsd:
						case mnemonic::movss:
						case mnemonic::addsd:
						case mnemonic::addss:
						case mnemonic::subsd:
						case mnemonic::subss:
						case mnemonic::mulsd:
						case mnemonic::mulss:
						case mnemonic::divsd:
						case mnemonic::divss:
						case mnemonic::sqrtsd:
						case mnemonic::sqrtss:
						case mnemonic::minsd:
						case mnemonic::minss:
						case mnemonic::maxsd:
						case mnemonic::maxss:
						case mnemonic::cvtsi2sd:
						case mnemonic::cvtsi2ss:
						case mnemonic::cvtss2sd:
						case mnemonic::cvtsd2ss:
							last_writer = 2;
							break;

						default:
							last_writer = 3;
							break;

					}

				} else if ( d & 1u )
					last_writer = 1;

			}

			if ( last_writer == 2 && float_return && state.regs [ 16 ] >= 0 ) {

				out_block.returned = state.regs [ 16 ];
				returns_float = true;

			} else if ( carried_floats && float_return && xmm >= 0 && !rax_written ) {

				out_block.returned = xmm;
				returns_float = true;

			} else if ( rax >= 0 && !( exprs [ static_cast < size_t > ( rax ) ].kind == op::VAR && vars [ static_cast < size_t > ( exprs [ static_cast < size_t > ( rax ) ].value ) ].kind == var_kind::PARAMETER && false ) )
				out_block.returned = rax;
			else if ( state.regs [ 16 ] >= 0 ) {

				out_block.returned = state.regs [ 16 ];
				returns_float = true;

			}

		} else if ( last && last->control == x86::flow::JUMP && graph.table >= 0 && item.out_count > 1 ) {

			out_block.end = term::SWITCH;

			int32_t selector = table_selector;
			out_block.selector = selector >= 0 ? selector : make ( op::INTRINSIC, 4, -1, -1, I_FLAGS );

		} else if ( last && last->control == x86::flow::JUMP && item.out_count == 0 ) {

			out_block.end = term::TAIL;
			state.flush ( long_mode ? ~( ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 8 ) | ( 1u << 9 ) | ( 1u << 10 ) | ( 1u << 11 ) | ( 0x3Fu << 16 ) ) : 0xFFFFFFFFu );

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

		bool flags_read = false;

		for ( int32_t e = 0; e < item.out_count && carried_flags; e++ ) {

			int32_t to = graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to;

			if ( to < 0 || to >= count )
				continue;

			const base::list < x86::instruction >& next_list = listings [ static_cast < size_t > ( to ) ];

			if ( !next_list.empty ( ) && next_list [ 0 ].control == x86::flow::BRANCH && next_list [ 0 ].condition != 0xFF && code [ static_cast < size_t > ( to ) ].preds == 1 )
				flags_read = true;

		}

		for ( int32_t slot = 0; slot < 16 && flags_read; slot++ ) {

			int32_t held = state.regs [ slot ];

			if ( held < 0 || ( held != flag_a && held != flag_b ) || slot == 4 )
				continue;

			op kind = exprs [ static_cast < size_t > ( held ) ].kind;

			if ( kind != op::VAR && kind != op::CONST )
				keep |= 1u << slot;

		}

		for ( int32_t slot = 0; slot < 32; slot++ ) {

			if ( state.regs [ slot ] >= 0 && state.regs [ slot ] == flag_a && ( keep & ( 1u << slot ) ) )
				flag_a_slot = slot;

			if ( state.regs [ slot ] >= 0 && state.regs [ slot ] == flag_b && ( keep & ( 1u << slot ) ) )
				flag_b_slot = slot;

		}

		int32_t flush_order [ 32 ];

		for ( int32_t slot = 0; slot < 32; slot++ )
			flush_order [ slot ] = slot;

		if ( flush_by_definition ) {

			for ( int32_t i = 1; i < 32; i++ ) {

				int32_t moving = flush_order [ i ];
				int32_t j = i - 1;

				while ( j >= 0 && state.reg_at [ flush_order [ j ] ] > state.reg_at [ moving ] ) {

					flush_order [ j + 1 ] = flush_order [ j ];
					j--;

				}

				flush_order [ j + 1 ] = moving;

			}

		}

		for ( int32_t k = 0; k < 32; k++ ) {

			int32_t slot = flush_order [ k ];

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

	merge_stack_slots ( );
	detect_variadic ( );
	propagate ( );

	uint32_t result_mask = returns_float ? ( 1u << 16 ) : 1u;
	base::list < uint8_t > reaching;

	reaching.fill ( static_cast < size_t > ( count ), 0 );

	for ( bool moved = true; moved; ) {

		moved = false;

		for ( const analysis::edge& link : graph.edges ) {

			if ( link.from < 0 || link.from >= count || link.to < 0 || link.to >= count || reaching [ static_cast < size_t > ( link.to ) ] )
				continue;

			if ( reaching [ static_cast < size_t > ( link.from ) ] || ( defs [ static_cast < size_t > ( link.from ) ] & result_mask ) ) {

				reaching [ static_cast < size_t > ( link.to ) ] = 1;
				moved = true;

			}

		}

	}

	for ( int32_t i = 0; i < count; i++ ) {

		block_code& item = code [ static_cast < size_t > ( i ) ];

		if ( item.end == term::RETURN && item.returned < 0 && item.reachable )
			bare_return = true;

		if ( item.end != term::RETURN || item.returned < 0 )
			continue;

		bool defined = ( defs [ static_cast < size_t > ( i ) ] & result_mask ) != 0 || reaching [ static_cast < size_t > ( i ) ];

		if ( !defined ) {

			if ( item.reachable )
				bare_return = true;

			continue;

		}

		returns_value = true;
		return_size = base::max ( return_size, exprs [ static_cast < size_t > ( item.returned ) ].size );

	}

	if ( returns_value && filled_returns ) {

		int32_t family = returns_float ? 16 : 0;

		for ( block_code& item : code ) {

			if ( item.end == term::RETURN && item.reachable && item.returned < 0 && family_var [ family ] >= 0 )
				item.returned = var_expr ( family_var [ family ], 0 );

		}

	}

	if ( returns_value && !returns_float && result_numbers ) {

		base::list < uint8_t > visited;
		base::list < int32_t > pending;

		visited.fill ( static_cast < size_t > ( count ), 0 );

		for ( int32_t i = 0; i < count; i++ ) {

			const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( i ) ];

			if ( code [ static_cast < size_t > ( i ) ].reachable && !list.empty ( ) && list [ list.size ( ) - 1 ].name == mnemonic::ret ) {

				visited [ static_cast < size_t > ( i ) ] = 1;
				pending.push ( i );
				return_points++;

			}

		}

		for ( int32_t q = 0; q < pending.count ( ); q++ ) {

			int32_t at = pending [ static_cast < size_t > ( q ) ];
			const base::list < x86::instruction >& list = listings [ static_cast < size_t > ( at ) ];
			bool found = false;

			for ( int32_t k = list.count ( ) - 1; k >= 0 && !found; k-- ) {

				const x86::instruction& ins = list [ static_cast < size_t > ( k ) ];
				uint32_t u = 0;
				uint32_t d = 0;

				if ( ins.name == mnemonic::ret )
					continue;

				effects ( ins, long_mode, u, d );

				if ( ins.name == mnemonic::call || ( d & 1u ) ) {

					found = true;

					if ( !result_at || ins.address < result_at )
						result_at = ins.address;

				}

			}

			if ( found )
				continue;

			for ( const analysis::edge& link : graph.edges ) {

				if ( link.to != at || link.from < 0 || link.from >= count || visited [ static_cast < size_t > ( link.from ) ] )
					continue;

				visited [ static_cast < size_t > ( link.from ) ] = 1;
				pending.push ( link.from );

			}

		}

	}

	int32_t width = has_prototype || returns_float ? -1 : yields_value ( *db, *file, names, function, long_mode, 0 );

	if ( width == 0 ) {

		returns_value = false;
		bare_return = true;

		for ( block_code& item : code ) {

			if ( item.end == term::RETURN )
				item.returned = -1;

		}

	} else if ( width >= 1 && width <= 3 ) {

		result_width = static_cast < uint8_t > ( width );

		if ( width <= 2 )
			return_size = 1;

	}

	prune ( );
	fold_returns ( );
	split_webs ( );
	fold_constants ( );
	fold_constant_branches ( *this );

	if ( zero_paths )
		zero_edges ( );

	if ( global_calls )
		global_reloads ( );

	inline_temps ( );
	prune ( );

	if ( api_result_type ( ) )
		narrow_webs ( );
	merge_conditions ( );
	fold_switches ( );
	fold_divisions ( );
	infer_signs ( );
	guard_expressions ( );
	fold_constant_trees ( *this );
	fold_constant_branches ( *this );

	if ( byte_params ) {

		narrow_byte_params ( 1 );
		narrow_byte_params ( 2 );

	}

	return true;

}