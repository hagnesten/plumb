#include "decompiler/lift/machine/machine.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/propagate/propagate.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "decompiler/literals/literals.h"
#include "x86/decoder/decoder.h"
#include "x86/printer/printer.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"

void decompiler::machine::note_frame ( ) {

	if ( !b->has_frame ) {

		b->has_frame = true;
		b->frame_at = frame_delta;

	}

}

void decompiler::machine::reset ( ) {

	for ( int32_t i = 0; i < 32; i++ ) {

		regs [ i ] = -1;
		reads [ i ] = 0;

		if ( block_origins )
			reg_at [ i ] = 0;

	}

	forget_kept ( );

	for ( int32_t i = 0; i < 16; i++ ) {

		stack_args [ i ] = -1;
		stack_stmt [ i ] = -1;

	}

	flag_kind = 0;
	flag_negated = false;
	entry_flag_var = -1;
	entry_flag_code = 0;
	arg_written = 0;
	pushes.clear ( );
	push_stmts.clear ( );

}

int32_t decompiler::machine::emit ( stmt kind, int32_t left, int32_t right, uint64_t address ) {

	uint32_t sources = register_sources ( right, 0 );
	uint32_t target = kind == stmt::ASSIGN && left >= 0 && b->exprs [ static_cast < size_t > ( left ) ].kind == op::VAR ? register_sources ( left, 0 ) : 0;
	statement& item = b->statements.emplace ( );

	item.kind = kind;
	item.left = left;
	item.right = right;
	item.address = address;
	item.sources = sources;
	item.target = target;

	return b->statements.count ( ) - 1;

}

uint32_t decompiler::machine::register_sources ( int32_t e, int32_t depth ) const {

	if ( e < 0 || depth > 64 )
		return 0;

	const expr& node = b->exprs [ static_cast < size_t > ( e ) ];
	uint32_t mask = 0;

	if ( node.kind == op::VAR && node.value >= 0 && node.value < b->vars.count ( ) ) {

		int32_t family = b->vars [ static_cast < size_t > ( node.value ) ].family;

		if ( family >= 0 && family < 32 )
			mask |= 1u << family;

	}

	mask |= register_sources ( node.a, depth + 1 ) | register_sources ( node.b, depth + 1 ) | register_sources ( node.c, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		mask |= register_sources ( b->args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	return mask;

}

bool decompiler::machine::shared_vector ( int32_t value ) {

	const expr& node = b->exprs [ static_cast < size_t > ( value ) ];

	return node.size == 16 && ( node.kind == op::LOAD || ( node.kind == op::INTRINSIC && node.value >= 1000 ) );

}

bool decompiler::machine::real_product ( int32_t value ) {

	const expr& node = b->exprs [ static_cast < size_t > ( value ) ];

	if ( node.kind != op::MUL )
		return false;

	for ( int32_t k = 0; k < 2; k++ ) {

		int32_t side = k ? node.b : node.a;

		if ( side >= 0 && b->exprs [ static_cast < size_t > ( side ) ].kind == op::CONST ) {

			int64_t factor = static_cast < int64_t > ( b->exprs [ static_cast < size_t > ( side ) ].value );

			return factor > 1 || factor < 0;

		}

	}

	return true;

}

int32_t decompiler::machine::family_value ( int32_t slot ) {

	if ( regs [ slot ] >= 0 && shared_products && ( ( slot < 16 && real_product ( regs [ slot ] ) ) || ( shared_vector_reads && slot >= 16 && shared_vector ( regs [ slot ] ) ) ) && !( busy & ( 1u << slot ) ) ) {

		if ( read_at [ slot ] != now ) {

			read_at [ slot ] = now;
			reads [ slot ]++;

		}

		if ( reads [ slot ] >= 2 ) {

			materialize ( slot, 0 );

			if ( regs [ slot ] < 0 )
				return b->var_expr ( b->register_var ( slot ), 0 );

		}

	}

	if ( regs [ slot ] >= 0 )
		return regs [ slot ];

	return b->var_expr ( b->register_var ( slot ), 0 );

}

int32_t decompiler::machine::read_reg ( uint8_t reg ) {

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

void decompiler::machine::write_reg ( uint8_t reg, int32_t value ) {

	if ( reg == x86::RSP || reg == x86::ESP )
		return;

	int32_t slot = slot_of ( reg );

	if ( slot < 0 || value < 0 )
		return;

	regs [ slot ] = value;
	reg_at [ slot ] = now;
	reads [ slot ] = 0;

	if ( b->exprs [ static_cast < size_t > ( value ) ].depth > 12 )
		materialize ( slot, 0 );

	if ( slot == 1 || slot == 2 || slot == 8 || slot == 9 )
		arg_written |= 1u << ( slot == 1 ? 0 : ( slot == 2 ? 1 : ( slot == 8 ? 2 : 3 ) ) );

	if ( slot >= 16 && slot < 20 )
		arg_written |= 1u << ( slot - 16 );

}

bool decompiler::machine::stack_base ( uint8_t reg, int64_t& offset ) {

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

bool decompiler::machine::stack_slot ( const x86::operand& m, int64_t& slot ) {

	int64_t base_offset = 0;

	if ( m.index != x86::NONE || !stack_base ( m.base, base_offset ) )
		return false;

	slot = base_offset + m.value;

	return true;

}

bool decompiler::machine::holds_reg ( int32_t node, int32_t slot ) {

	if ( node < 0 || slot < 0 || slot >= 16 )
		return false;

	if ( regs [ slot ] >= 0 )
		return same_tree ( *b, node, regs [ slot ], 0 );

	const expr& item = b->exprs [ static_cast < size_t > ( node ) ];

	return item.kind == op::VAR && b->family_var [ slot ] >= 0 && item.value == b->family_var [ slot ];

}

int32_t decompiler::machine::held_sum ( const x86::operand& m ) {

	int64_t ignored = 0;

	if ( m.base == x86::NONE || m.index == x86::NONE || stack_base ( m.base, ignored ) )
		return -1;

	int32_t base_slot = slot_of ( x86::resize_register ( m.base, 8 ) );
	int32_t index_slot = slot_of ( x86::resize_register ( m.index, 8 ) );

	if ( base_slot < 0 || index_slot < 0 || base_slot >= 16 || index_slot >= 16 )
		return -1;

	auto scaled_index = [ & ] ( int32_t node ) {

		if ( m.scale <= 1 )
			return holds_reg ( node, index_slot );

		if ( node < 0 )
			return false;

		const expr& item = b->exprs [ static_cast < size_t > ( node ) ];

		if ( item.kind != op::MUL || item.b < 0 || b->exprs [ static_cast < size_t > ( item.b ) ].kind != op::CONST || b->exprs [ static_cast < size_t > ( item.b ) ].value != m.scale )
			return false;

		return holds_reg ( item.a, index_slot );

	};

	for ( int32_t s = 0; s < 16; s++ ) {

		if ( s == base_slot || s == index_slot || s == 4 || regs [ s ] < 0 )
			continue;

		const expr& sum = b->exprs [ static_cast < size_t > ( regs [ s ] ) ];

		if ( sum.kind != op::ADD || sum.size != 8 || ( sum.bits & FLOATING ) )
			continue;

		if ( ( holds_reg ( sum.a, base_slot ) && scaled_index ( sum.b ) ) || ( m.scale <= 1 && holds_reg ( sum.b, base_slot ) && scaled_index ( sum.a ) ) )
			return family_value ( s );

	}

	return -1;

}

int32_t decompiler::machine::address_of ( const x86::instruction& ins, const x86::operand& m ) {

	uint64_t absolute = 0;

	if ( held_sums ) {

		int32_t held = held_sum ( m );

		if ( held >= 0 )
			return m.value ? b->binary ( op::ADD, held, b->constant ( m.value, 8 ), 8 ) : held;

	}

	if ( m.rip_relative ( ) || ( m.base == x86::NONE && m.index == x86::NONE ) ) {

		absolute = m.rip_relative ( ) ? ins.next ( ) + static_cast < uint64_t > ( m.value ) : static_cast < uint64_t > ( m.value );

		return b->make ( op::CONST, 8, -1, -1, static_cast < int64_t > ( absolute ), POINTER );

	}

	int32_t result = -1;
	int64_t slot = 0;

	int64_t base_offset = 0;

	bool stack_indexed = false;

	if ( m.base != x86::NONE && stack_base ( m.base, base_offset ) && m.index == x86::NONE ) {

		result = b->make ( op::ADDRESS, 8, -1, -1, b->stack_var ( base_offset + m.value, 8 ), POINTER );

	} else if ( indexed_stack && m.base != x86::NONE && m.index != x86::NONE && stack_base ( m.base, base_offset ) ) {

		result = b->make ( op::ADDRESS, 8, -1, -1, b->stack_var ( base_offset + m.value, 8 ), POINTER );
		stack_indexed = true;

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
	( void ) stack_indexed;

	return result < 0 ? b->constant ( m.value, 8 ) : result;

}

bool decompiler::machine::read_only_value ( uint64_t address, uint8_t size, int64_t& value ) const {

	const loader::binary& file = *b->file;
	int32_t index = file.section_at ( address );
	uint64_t offset = 0;
	uint8_t pointer = b->long_mode ? 8 : 4;

	if ( index < 0 || ( size != 1 && size != 2 && size != 4 && size != 8 ) )
		return false;

	const loader::section& owner = file.sections [ static_cast < size_t > ( index ) ];

	if ( owner.write || owner.execute || address + size > owner.address + owner.raw || !file.offset_of ( address, offset ) )
		return false;

	for ( const loader::import& item : file.imports ) {

		if ( address + size > item.slot && address < item.slot + pointer )
			return false;

	}

	uint64_t raw = size == 1 ? file.byte ( offset ) : ( size == 2 ? file.read16 ( offset ) : ( size == 4 ? file.read32 ( offset ) : file.read64 ( offset ) ) );

	if ( size == pointer && raw && file.section_at ( raw ) >= 0 )
		return false;

	value = static_cast < int64_t > ( raw );

	return true;

}

int32_t decompiler::machine::read_memory ( const x86::instruction& ins, const x86::operand& m, uint8_t size, uint8_t bits ) {

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

	if ( ( m.segment == x86::GS || m.segment == x86::FS ) && m.base == x86::NONE && m.index == x86::NONE && !m.rip_relative ( ) ) {

		bool gs = m.segment == x86::GS;
		uint64_t offset = static_cast < uint64_t > ( m.value );

		if ( ( gs && b->long_mode && offset == 0x30 && size == 8 ) || ( !gs && !b->long_mode && offset == 0x18 && size == 4 ) )
			return b->make ( op::INTRINSIC, size, -1, -1, I_TEB, POINTER );

		int32_t id = size == 1 ? ( gs ? I_GSBYTE : I_FSBYTE ) : ( size == 2 ? ( gs ? I_GSWORD : I_FSWORD ) : ( size == 4 ? ( gs ? I_GSDWORD : I_FSDWORD ) : ( gs ? I_GS : I_FS ) ) );

		return intrinsic ( id, size, b->constant ( static_cast < int64_t > ( offset ), 4 ) );

	}

	int32_t address = address_of ( ins, m );
	const expr& where = b->exprs [ static_cast < size_t > ( address ) ];

	if ( where.kind == op::CONST && ( where.bits & POINTER ) ) {

		int64_t known = 0;

		if ( !( bits & FLOATING ) && read_only_value ( static_cast < uint64_t > ( where.value ), size, known ) )
			return b->constant ( known, size );

		if ( bits & FLOATING )
			b->float_globals.push ( static_cast < uint64_t > ( where.value ) );

		return b->make ( op::GLOBAL, size, -1, -1, where.value, bits );

	}

	note_field ( address, size, bits );

	return b->make ( op::LOAD, size, address, -1, 0, bits );

}

void decompiler::machine::note_field ( int32_t address, uint8_t size, uint8_t bits ) {

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

bool decompiler::machine::aliases ( int32_t e, bool stack, int64_t target, uint8_t size, int32_t depth, bool outgoing ) const {

	if ( e < 0 )
		return false;

	if ( depth > 64 )
		return true;

	const expr& item = b->exprs [ static_cast < size_t > ( e ) ];

	if ( item.kind == op::CALL || item.kind == op::INTRINSIC || ( item.kind == op::LOAD && !outgoing ) )
		return true;

	if ( item.kind == op::GLOBAL ) {

		if ( stack )
			return false;

		int64_t from = item.value;

		return from < target + size && target < from + item.size;

	}

	if ( item.kind == op::VAR && item.value >= 0 && item.value < b->vars.count ( ) ) {

		const variable& owner = b->vars [ static_cast < size_t > ( item.value ) ];

		if ( owner.kind == var_kind::REGISTER )
			return false;

		if ( !stack )
			return false;

		return owner.offset < target + size && target < owner.offset + owner.size;

	}

	if ( aliases ( item.a, stack, target, size, depth + 1, outgoing ) || aliases ( item.b, stack, target, size, depth + 1, outgoing ) || aliases ( item.c, stack, target, size, depth + 1, outgoing ) )
		return true;

	for ( int32_t i = 0; i < item.arg_count; i++ ) {

		if ( aliases ( b->args [ static_cast < size_t > ( item.first_arg + i ) ], stack, target, size, depth + 1, outgoing ) )
			return true;

	}

	return false;

}

void decompiler::machine::flush ( uint32_t keep, int32_t where, int64_t target, uint8_t size ) {

	int32_t ordered [ 32 ];

	for ( int32_t slot = 0; slot < 32; slot++ )
		ordered [ slot ] = slot;

	if ( flush_by_definition ) {

		for ( int32_t i = 1; i < 32; i++ ) {

			int32_t moving = ordered [ i ];
			int32_t j = i - 1;

			while ( j >= 0 && reg_at [ ordered [ j ] ] > reg_at [ moving ] ) {

				ordered [ j + 1 ] = ordered [ j ];
				j--;

			}

			ordered [ j + 1 ] = moving;

		}

	}

	for ( int32_t k = 0; k < 32; k++ ) {

		int32_t slot = ordered [ k ];

		if ( regs [ slot ] < 0 || !( keep & ( 1u << slot ) ) )
			continue;

		if ( !b->contains_load ( regs [ slot ] ) )
			continue;

		if ( where && !aliases ( regs [ slot ], where >= 2, target, size, 0, where == 3 ) )
			continue;

		materialize ( slot, 0 );

	}

}

int32_t decompiler::machine::spill ( int32_t value, uint64_t address ) {

	op kind = b->exprs [ static_cast < size_t > ( value ) ].kind;
	bool test = kind >= op::EQ && kind <= op::GE && !( b->exprs [ static_cast < size_t > ( value ) ].bits & FLOATING );
	bool flipped = test && kind == op::NE;

	if ( flipped )
		value = b->negate ( value );

	variable& made = b->vars.emplace ( );
	int32_t index = b->vars.count ( ) - 1;

	made.kind = var_kind::REGISTER;
	made.family = -1;
	made.size = b->exprs [ static_cast < size_t > ( value ) ].size;
	made.bits = test ? 0 : b->exprs [ static_cast < size_t > ( value ) ].bits & ( FLOATING | SIGNED | UNSIGNED );
	made.assigned = true;

	if ( test ) {

		uint8_t compare_bits = b->exprs [ static_cast < size_t > ( value ) ].bits;

		made.flags = true;
		made.offset = kind == op::EQ || kind == op::NE ? 4 : ( ( compare_bits & UNSIGNED ) ? 2 : 12 );

	}

	emit ( stmt::ASSIGN, b->make ( op::VAR, made.size, -1, -1, index ), value, address );

	int32_t held = b->var_expr ( index, 0 );

	return flipped ? b->make ( op::LNOT, 1, held ) : held;

}

int32_t decompiler::machine::find_same ( int32_t e, int32_t target, int32_t depth ) const {

	if ( e < 0 || target < 0 || depth > 24 )
		return -1;

	if ( b->same ( e, target ) )
		return e;

	const expr& node = b->exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::CALL || node.kind == op::INTRINSIC )
		return -1;

	int32_t found = find_same ( node.a, target, depth + 1 );

	if ( found < 0 )
		found = find_same ( node.b, target, depth + 1 );

	if ( found < 0 )
		found = find_same ( node.c, target, depth + 1 );

	return found;

}

int32_t decompiler::machine::swap_same ( int32_t e, int32_t target, int32_t var ) {

	if ( e < 0 || target < 0 )
		return e;

	for ( int32_t round = 0; round < 4; round++ ) {

		int32_t found = find_same ( e, target, 0 );

		if ( found < 0 )
			break;

		int32_t with = b->var_expr ( var, b->exprs [ static_cast < size_t > ( found ) ].size );

		if ( found == e )
			return with;

		int32_t next = b->replace ( e, found, with );

		if ( next == e )
			break;

		e = next;

	}

	return e;

}

void decompiler::machine::forget_kept ( ) {

	for ( int32_t slot = 0; slot < 32; slot++ ) {

		kept_source [ slot ] = -1;
		kept_var [ slot ] = -1;

	}

}

int32_t decompiler::machine::recover ( int32_t e, int32_t var, int32_t skip ) {

	if ( !rescue_flags || e < 0 || !b->references ( e, var, skip ) )
		return e;

	for ( int32_t slot = 0; slot < 32; slot++ ) {

		if ( kept_source [ slot ] < 0 || kept_var [ slot ] == var || !b->references ( kept_source [ slot ], var, -1 ) )
			continue;

		int32_t swapped = swap_same ( e, kept_source [ slot ], kept_var [ slot ] );

		if ( swapped != e ) {

			e = swapped;

			if ( !b->references ( e, var, skip ) )
				return e;

		}

	}

	return e;

}

void decompiler::machine::protect ( int32_t var, int32_t skip, uint64_t address ) {

	if ( flag_kind ) {

		flag_a = recover ( flag_a, var, skip );
		flag_b = recover ( flag_b, var, skip );

	}

	guard_a = recover ( guard_a, var, skip );
	guard_b = recover ( guard_b, var, skip );

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

int32_t decompiler::machine::shield ( int32_t guard, int32_t var, int32_t source, uint64_t address ) {

	if ( guard < 0 || !b->references ( guard, var, -1 ) )
		return guard;

	if ( source >= 0 && !b->references ( guard, var, source ) )
		return b->replace ( guard, source, b->var_expr ( var, sized_shield ? b->exprs [ static_cast < size_t > ( source ) ].size : 0 ) );

	return spill ( guard, address );

}

void decompiler::machine::materialize ( int32_t slot, uint64_t address ) {

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

	int32_t first_made = b->statements.count ( );

	for ( int32_t other = 0; other < 32; other++ ) {

		if ( other == slot || regs [ other ] < 0 || !b->references ( regs [ other ], var, shared_sources ? source : -1 ) )
			continue;

		if ( busy & ( 1u << other ) )
			regs [ other ] = spill ( regs [ other ], address );
		else
			materialize ( other, address );

	}

	bool stale = false;

	for ( int32_t i = first_made; i < b->statements.count ( ) && !stale; i++ ) {

		const statement& made = b->statements [ static_cast < size_t > ( i ) ];

		if ( made.kind != stmt::ASSIGN || made.left < 0 )
			continue;

		const expr& target = b->exprs [ static_cast < size_t > ( made.left ) ];

		if ( target.kind == op::VAR && target.value != var && b->references ( source, target.value, -1 ) )
			stale = true;

	}

	if ( stale && regs [ slot ] != source ) {

		source = regs [ slot ];
		value = b->exprs [ static_cast < size_t > ( source ) ];

		if ( value.kind == op::VAR && value.value == var ) {

			regs [ slot ] = -1;
			busy &= ~( 1u << slot );

			return;

		}

	}

	for ( int32_t other = 0; other < 32 && shared_sources; other++ ) {

		if ( other != slot && regs [ other ] >= 0 && b->references ( regs [ other ], var, source ) )
			regs [ other ] = spill ( regs [ other ], address );

	}

	protect ( var, source, address );

	int32_t made_line = emit ( stmt::ASSIGN, b->make ( op::VAR, b->vars [ static_cast < size_t > ( var ) ].size, -1, -1, var ), source, address );

	if ( ( !address || block_origins ) && slot >= 0 && slot < 32 && reg_at [ slot ] )
		b->statements [ static_cast < size_t > ( made_line ) ].origin = reg_at [ slot ];

	b->vars [ static_cast < size_t > ( var ) ].assigned = true;

	if ( flag_kind && value.kind != op::CONST && value.kind != op::VAR ) {

		flag_a = b->replace ( flag_a, source, b->var_expr ( var, value.size ) );
		flag_b = b->replace ( flag_b, source, b->var_expr ( var, value.size ) );

	}

	if ( rescue_flags && value.kind != op::CONST && value.kind != op::VAR ) {

		kept_source [ slot ] = value.kind == op::CAST ? value.a : source;
		kept_var [ slot ] = var;

	}

	int32_t replacement = b->make ( op::VAR, b->vars [ static_cast < size_t > ( var ) ].size, -1, -1, var );

	if ( flag_kind ) {

		int32_t sized = value.size < b->vars [ static_cast < size_t > ( var ) ].size && value.kind != op::CONST ? b->var_expr ( var, value.size ) : replacement;

		flag_a = b->replace ( flag_a, source, sized );
		flag_b = b->replace ( flag_b, source, sized );

	}

	int32_t held = sized_regs && value.size < b->vars [ static_cast < size_t > ( var ) ].size && value.kind != op::CONST && !( value.bits & FLOATING ) ? b->var_expr ( var, value.size ) : replacement;

	for ( int32_t other = 0; other < 32; other++ ) {

		if ( other != slot && regs [ other ] >= 0 )
			regs [ other ] = b->replace ( regs [ other ], source, held );

	}

	if ( value.kind != op::CONST && value.kind != op::VAR ) {

		int32_t sized = value.size < b->vars [ static_cast < size_t > ( var ) ].size ? b->var_expr ( var, value.size ) : replacement;

		if ( guard_a >= 0 )
			guard_a = b->replace ( guard_a, source, sized );

		if ( guard_b >= 0 )
			guard_b = b->replace ( guard_b, source, sized );

		if ( same_guards && value.kind == op::LOAD ) {

			guard_a = swap_same ( guard_a, source, var );
			guard_b = swap_same ( guard_b, source, var );

		}

	}

	regs [ slot ] = -1;
	busy &= ~( 1u << slot );

}

void decompiler::machine::write_memory ( const x86::instruction& ins, const x86::operand& m, uint8_t size, int32_t value ) {

	int64_t spot = 0;
	int32_t holder = -1;
	int32_t first_made = b->statements.count ( );
	uint8_t value_width = value >= 0 ? b->exprs [ static_cast < size_t > ( value ) ].size : 0;

	for ( int32_t s = 0; s < 32 && value >= 0 && holder < 0 && shared_sources; s++ ) {

		if ( regs [ s ] == value )
			holder = s;

	}

	auto refresh = [ & ] ( ) {

		if ( holder < 0 || regs [ holder ] == value )
			return;

		bool stale = false;

		for ( int32_t i = first_made; i < b->statements.count ( ) && !stale; i++ ) {

			const statement& made = b->statements [ static_cast < size_t > ( i ) ];

			if ( made.kind == stmt::ASSIGN && made.left >= 0 && b->exprs [ static_cast < size_t > ( made.left ) ].kind == op::VAR && b->references ( value, static_cast < int32_t > ( b->exprs [ static_cast < size_t > ( made.left ) ].value ), -1 ) )
				stale = true;

		}

		if ( !stale )
			return;

		if ( regs [ holder ] >= 0 ) {

			value = regs [ holder ];

			return;

		}

		value = b->var_expr ( b->register_var ( holder ), value_width );
		holder = -1;

	};

	if ( m.rip_relative ( ) )
		flush ( live, 1, static_cast < int64_t > ( ins.next ( ) + static_cast < uint64_t > ( m.value ) ), size );
	else if ( stack_slot ( m, spot ) )
		flush ( live, outgoing_args && b->long_mode && spot - delta >= 0x20 && spot - delta < 0x20 + 8 * 12 ? 3 : 2, spot, size );
	else
		flush ( live );

	refresh ( );

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

		if ( b->long_mode && slot >= 8 && slot <= 32 && ( slot & 7 ) == 0 ) {

			static const int32_t homes [ 4 ] = { 1, 2, 8, 9 };
			int32_t position = static_cast < int32_t > ( slot / 8 ) - 1;
			const expr& incoming = b->exprs [ static_cast < size_t > ( value ) ];
			bool spill = prologue && incoming.kind == op::VAR && incoming.value >= 0 && incoming.value < b->vars.count ( ) && b->vars [ static_cast < size_t > ( incoming.value ) ].kind != var_kind::STACK && ( b->vars [ static_cast < size_t > ( incoming.value ) ].family == homes [ position ] || b->vars [ static_cast < size_t > ( incoming.value ) ].family == 16 + position );

			if ( !spill )
				b->home_local |= 1u << position;

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

			const expr& stored = b->exprs [ static_cast < size_t > ( value ) ];

			if ( wide_zero_args && item.size == 16 && position + 1 < 16 && stored.kind == op::CONST && stored.value == 0 ) {

				stack_args [ position + 1 ] = b->constant ( 0, 8 );
				stack_stmt [ position + 1 ] = made;

			}

		}

		return;

	}

	int32_t address = address_of ( ins, m );

	refresh ( );

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

int32_t decompiler::machine::read ( const x86::instruction& ins, int32_t index, uint8_t bits ) {

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

void decompiler::machine::write ( const x86::instruction& ins, int32_t index, int32_t value ) {

	const x86::operand& item = ins.operands [ index ];

	if ( item.type == x86::kind::REG )
		write_reg ( item.reg, value );
	else if ( item.type == x86::kind::MEM )
		write_memory ( ins, item, item.size, value );

}

int32_t decompiler::machine::condition ( int32_t code ) {

	builder& k = *b;
	uint8_t size = flag_size;

	if ( ( flag_kind == 0 || flag_a < 0 ) && entry_flag_var >= 0 ) {

		if ( code == entry_flag_code )
			return k.var_expr ( entry_flag_var, 1 );

		if ( ( code ^ 1 ) == entry_flag_code )
			return k.make ( op::LNOT, 1, k.var_expr ( entry_flag_var, 1 ) );

	}

	if ( flag_kind == 0 || flag_a < 0 )
		return k.make ( op::INTRINSIC, 1, -1, -1, I_FLAGS );

	int32_t a = flag_a;
	int32_t z = k.constant ( 0, size );

	if ( size < 8 && k.exprs [ static_cast < size_t > ( a ) ].size > size && k.exprs [ static_cast < size_t > ( a ) ].size <= 8 && !( k.exprs [ static_cast < size_t > ( a ) ].bits & FLOATING ) )
		a = k.make ( op::CAST, size, a, -1, 0, 0 );

	switch ( flag_kind ) {

		case 1: {

			int32_t c = flag_b;

			if ( c >= 0 && size < 8 && k.exprs [ static_cast < size_t > ( c ) ].size > size && k.exprs [ static_cast < size_t > ( c ) ].size <= 8 && !( k.exprs [ static_cast < size_t > ( c ) ].bits & FLOATING ) )
				c = k.make ( op::CAST, size, c, -1, 0, 0 );

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

			if ( !( flag_b == a || flag_b == flag_a || ( k.exprs [ static_cast < size_t > ( a ) ].kind == k.exprs [ static_cast < size_t > ( flag_b ) ].kind && k.exprs [ static_cast < size_t > ( a ) ].kind == op::VAR && k.exprs [ static_cast < size_t > ( a ) ].value == k.exprs [ static_cast < size_t > ( flag_b ) ].value ) || ( self_tests && flag_b >= 0 && k.same ( flag_a, flag_b ) ) ) )
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

			if ( ( code == 2 || code == 3 ) && k.exprs [ static_cast < size_t > ( a ) ].kind == op::NEG && k.exprs [ static_cast < size_t > ( a ) ].a >= 0 ) {

				int32_t inner = k.exprs [ static_cast < size_t > ( a ) ].a;

				return k.binary ( code == 2 ? op::NE : op::EQ, inner, k.constant ( 0, k.exprs [ static_cast < size_t > ( inner ) ].size ), k.exprs [ static_cast < size_t > ( inner ) ].size );

			}

			if ( ( code == 2 || code == 3 ) && flag_negated && negated_carry )
				return k.binary ( code == 2 ? op::NE : op::EQ, a, z, size );

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

			const expr& tested = k.exprs [ static_cast < size_t > ( flag_a ) ];

			if ( bittest_calls && ( code == 2 || code == 3 ) && tested.kind == op::VAR && tested.value >= 0 && ( tested.size == 4 || tested.size == 8 ) && !( tested.bits & FLOATING ) ) {

				int32_t where = k.make ( op::ADDRESS, 8, -1, -1, tested.value, POINTER );
				int32_t call = intrinsic ( I_BITTEST, 1, where, flag_b );

				return code == 2 ? call : k.negate ( call );

			}

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

int32_t decompiler::machine::select_fold ( op kind, int32_t left, int32_t right, uint8_t size ) {

	builder& k = *b;

	if ( left < 0 || right < 0 )
		return -1;

	int32_t pick = -1;
	int32_t other = -1;

	if ( k.exprs [ static_cast < size_t > ( left ) ].kind == op::TERNARY && k.exprs [ static_cast < size_t > ( right ) ].kind == op::CONST ) {

		pick = left;
		other = right;

	} else if ( k.exprs [ static_cast < size_t > ( right ) ].kind == op::TERNARY && k.exprs [ static_cast < size_t > ( left ) ].kind == op::CONST && kind != op::SUB ) {

		pick = right;
		other = left;

	}

	if ( pick < 0 )
		return -1;

	const expr chosen = k.exprs [ static_cast < size_t > ( pick ) ];

	if ( chosen.a < 0 || chosen.b < 0 || chosen.c < 0 || k.exprs [ static_cast < size_t > ( chosen.b ) ].kind != op::CONST || k.exprs [ static_cast < size_t > ( chosen.c ) ].kind != op::CONST )
		return -1;

	int64_t yes = k.exprs [ static_cast < size_t > ( chosen.b ) ].value;
	int64_t no = k.exprs [ static_cast < size_t > ( chosen.c ) ].value;
	int64_t with = k.exprs [ static_cast < size_t > ( other ) ].value;

	auto apply = [ & ] ( int64_t x ) -> int64_t {

		switch ( kind ) {

			case op::AND: return x & with;
			case op::OR: return x | with;
			case op::XOR: return x ^ with;
			case op::ADD: return x + with;
			case op::SUB: return x - with;
			default: return x;

		}

	};

	if ( kind != op::AND && kind != op::OR && kind != op::XOR && kind != op::ADD && kind != op::SUB )
		return -1;

	uint64_t mask = size >= 8 ? ~0ull : ( ( 1ull << ( size * 8 ) ) - 1 );
	int64_t then_value = static_cast < int64_t > ( static_cast < uint64_t > ( apply ( yes ) ) & mask );
	int64_t else_value = static_cast < int64_t > ( static_cast < uint64_t > ( apply ( no ) ) & mask );
	int32_t test = chosen.a;

	if ( then_value == 0 && else_value != 0 ) {

		test = k.negate ( test );

		int64_t held = then_value;

		then_value = else_value;
		else_value = held;

	}

	int32_t made = k.make ( op::TERNARY, size, test, k.constant ( then_value, size ) );

	k.exprs [ static_cast < size_t > ( made ) ].c = k.constant ( else_value, size );
	k.deepen ( made );

	return made;

}

void decompiler::machine::set_flags ( int32_t kind, int32_t a, int32_t c, uint8_t size ) {

	flag_result = -1;
	flag_negated = false;
	flag_kind = kind;
	flag_a = a;
	flag_b = c;
	flag_size = size;

}

bool decompiler::machine::jumps_through_rax ( uint64_t address ) const {

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

int32_t decompiler::machine::call_expr ( const x86::instruction& ins, uint32_t live_after ) {

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

	uint8_t result_size = k.long_mode ? 8 : 4;
	uint8_t result_bits = 0;

	if ( direct ) {

		const wchar_t* api = nullptr;

		if ( k.names->name ( direct, true, name, 200 ) )
			api = api_prototype ( name );

		if ( !api && callee >= 0 && ( k.db->functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) && k.db->functions [ static_cast < size_t > ( callee ) ].import >= 0 )
			api = api_prototype ( k.file->imports [ static_cast < size_t > ( k.db->functions [ static_cast < size_t > ( callee ) ].import ) ].name.get ( ) );

		if ( api ) {

			c_prototype shape;
			base::writer problem;

			if ( parse_prototype ( api, k.long_mode, shape, problem ) && !shape.result_type.pointer && !shape.result_type.empty && shape.result_type.size > 0 && shape.result_type.size < result_size && !( shape.result_type.bits & FLOATING ) ) {

				result_size = shape.result_type.size;
				result_bits = shape.result_type.bits & ( SIGNED | UNSIGNED );

			}

		}

	}

	int32_t made = k.make ( op::CALL, result_size, target, -1, static_cast < int64_t > ( direct ), result_bits );
	int32_t first = k.args.count ( );
	base::list < int32_t > list;

	if ( k.long_mode ) {

		static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
		int32_t highest = -1;

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( arg_written & ( 1u << i ) )
				highest = i;

		}

		int32_t stacked = -1;

		for ( int32_t i = 4; i < 16 && stack_args [ i ] >= 0; i++ ) {

			highest = base::max ( highest, i );
			stacked = i;

		}

		int64_t mask = -1;

		if ( count >= 0 )
			highest = count - 1;
		else if ( direct && callee >= 0 ) {

			int32_t inner = internal_arguments ( *k.db, direct, true, 0 );
			int32_t reach = inner;
			int32_t extra = -1;

			if ( inner >= 0 && float_calls ) {

				mask = argument_registers ( *k.db, direct );
				extra = stack_arguments ( *k.db, direct );

				for ( int32_t i = 0; i < 4 && mask > 0; i++ ) {

					if ( mask & ( ( 1ll << gpr [ i ] ) | ( 1ll << ( 16 + i ) ) ) )
						reach = base::max ( reach, i + 1 );

				}

			}

			if ( inner >= 0 && extra > 0 )
				highest = base::max ( reach - 1, 3 + extra );
			else if ( inner >= 0 && extra == 0 && reach > inner )
				highest = reach - 1;
			else if ( inner >= 0 )
				highest = inner < 4 && no_phantom_stack ? inner - 1 : base::max ( inner - 1, stacked );

		}

		if ( through_vtable )
			highest = base::max ( highest, 0 );

		for ( int32_t i = 0; i <= highest && i < 16; i++ ) {

			if ( i < 4 ) {

				bool wanted = mask > 0 && ( mask & ( 1ll << ( 16 + i ) ) ) && ( !( mask & ( 1ll << gpr [ i ] ) ) || ( regs [ 16 + i ] >= 0 && regs [ gpr [ i ] ] < 0 ) );
				bool floating = wanted || ( regs [ 16 + i ] >= 0 && ( k.exprs [ static_cast < size_t > ( regs [ 16 + i ] ) ].bits & FLOATING ) && regs [ gpr [ i ] ] < 0 );

				int32_t passed = floating ? ( regs [ 16 + i ] >= 0 ? regs [ 16 + i ] : family_value ( 16 + i ) ) : family_value ( gpr [ i ] );

				if ( wanted && passed >= 0 && k.exprs [ static_cast < size_t > ( passed ) ].kind == op::CONST && k.exprs [ static_cast < size_t > ( passed ) ].value == 0 && ( k.exprs [ static_cast < size_t > ( passed ) ].size != 4 || !( k.exprs [ static_cast < size_t > ( passed ) ].bits & FLOATING ) ) )
					passed = k.make ( op::CAST, 4, k.constant ( 0, 4 ), -1, 0, FLOATING );

				list.push ( passed );

			} else if ( stack_args [ i ] >= 0 ) {

				list.push ( stack_args [ i ] );

				if ( stack_stmt [ i ] >= 0 )
					k.statements [ static_cast < size_t > ( stack_stmt [ i ] ) ].kind = stmt::NOP;

			} else
				list.push ( k.var_expr ( k.stack_var ( delta + i * 8 + ( tail_stack_args && ins.name != mnemonic::call ? 8 : 0 ), 8 ), 0 ) );

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

bool decompiler::machine::cookie_call ( int32_t call ) {

	const expr& item = b->exprs [ static_cast < size_t > ( call ) ];

	if ( item.arg_count != 1 )
		return false;

	const expr& arg = b->exprs [ static_cast < size_t > ( b->args [ static_cast < size_t > ( item.first_arg ) ] ) ];

	if ( arg.kind != op::XOR || arg.a < 0 )
		return false;

	const expr& left = b->exprs [ static_cast < size_t > ( arg.a ) ];

	return left.kind == op::VAR && b->vars [ static_cast < size_t > ( left.value ) ].cookie;

}

void decompiler::machine::clobber ( ) {

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

void decompiler::machine::unhandled ( const x86::instruction& ins, uint32_t def ) {

	wchar_t text [ 200 ];
	namer_bridge names_bridge;

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

int32_t decompiler::machine::intrinsic ( int32_t id, uint8_t size, int32_t a, int32_t c, int32_t d ) {

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

void decompiler::machine::step ( const x86::instruction& ins, uint32_t live_after, uint32_t def ) {

	builder& k = *b;
	uint8_t size = ins.count ? ins.operands [ 0 ].size : 0;

	live = live_after;
	now = ins.address;

	if ( ins.control != x86::flow::NONE && ins.name != mnemonic::call )
		return;

	auto arith = [ & ] ( op kind, uint8_t bits ) {

		int32_t left = read ( ins, 0 );
		int32_t right = read ( ins, 1 );

		if ( negate_pairs && ( kind == op::OR || kind == op::AND ) && size == 1 ) {

			auto boolean = [ & ] ( int32_t e ) {

				op test = k.exprs [ static_cast < size_t > ( e ) ].kind;

				return ( test >= op::EQ && test <= op::GE ) || test == op::LNOT;

			};

			if ( boolean ( left ) && boolean ( right ) ) {

				int32_t held = left;

				left = right;
				right = held;

			}

		}

		int32_t value = select_fold ( kind, left, right, size );

		if ( value < 0 )
			value = k.binary ( kind, left, right, size, bits );

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
				note_frame ( );

				return;

			}

			if ( ins.operands [ 0 ].type == x86::kind::REG && ( ins.operands [ 0 ].reg == x86::RSP || ins.operands [ 0 ].reg == x86::ESP ) ) {

				if ( frame && ins.operands [ 1 ].type == x86::kind::REG && ( ins.operands [ 1 ].reg == x86::RBP || ins.operands [ 1 ].reg == x86::EBP ) )
					delta = frame_delta;

				return;

			}

			if ( ins.operands [ 0 ].type == x86::kind::REG && x86::register_family ( ins.operands [ 0 ].reg ) == 0 && ins.operands [ 1 ].type == x86::kind::IMM && ins.operands [ 1 ].value >= 0x1000 && ins.operands [ 1 ].value < 0x1000000 )
				probe_size = ins.operands [ 1 ].value;

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
				note_frame ( );

				return;

			}

			if ( narrow_lea && size == 4 && k.long_mode && !ins.operands [ 1 ].rip_relative ( ) && ins.operands [ 1 ].base != x86::RSP && ins.operands [ 1 ].base != x86::RBP && ( ins.operands [ 1 ].base != x86::NONE || ins.operands [ 1 ].index != x86::NONE ) ) {

				const x86::operand& m = ins.operands [ 1 ];
				int32_t sum = -1;

				if ( m.base != x86::NONE )
					sum = read_reg ( x86::resize_register ( m.base, 4 ) );

				if ( m.index != x86::NONE ) {

					int32_t index = read_reg ( x86::resize_register ( m.index, 4 ) );

					if ( m.scale > 1 )
						index = k.binary ( op::MUL, index, k.constant ( m.scale, 4 ), 4 );

					sum = sum < 0 ? index : k.binary ( op::ADD, sum, index, 4 );

				}

				if ( m.value )
					sum = k.binary ( op::ADD, sum, k.constant ( static_cast < int32_t > ( m.value ), 4 ), 4 );

				write ( ins, 0, sum );

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
				else if ( ins.operands [ 1 ].type == x86::kind::REG && x86::register_family ( ins.operands [ 1 ].reg ) == 0 && probe_size > 0 && ins.name == mnemonic::sub )
					delta -= probe_size;

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
				int32_t value = k.binary ( op::SUB, left, right, size );

				write ( ins, 0, value );
				set_flags ( 1, left, right, size );
				flag_result = ins.operands [ 0 ].type == x86::kind::REG ? value : -1;

				return;

			}

			arith ( op::ADD, 0 );

			return;

		case mnemonic::adc:
		case mnemonic::sbb:

			if ( ins.name == mnemonic::sbb && same_registers ( ins ) ) {

				int32_t test = condition ( 2 );
				int32_t chosen = k.make ( op::TERNARY, size, test, k.constant ( -1, size ) );

				k.exprs [ static_cast < size_t > ( chosen ) ].c = k.constant ( 0, size );
				k.deepen ( chosen );
				write ( ins, 0, chosen );
				set_flags ( 3, chosen, -1, size );

				return;

			}

			if ( carry_ops && flag_kind ) {

				int32_t test = condition ( 2 );

				if ( test >= 0 ) {

					op kind = ins.name == mnemonic::adc ? op::ADD : op::SUB;
					int32_t left = read ( ins, 0 );
					int32_t right = read ( ins, 1 );
					int32_t carry = k.make ( op::CAST, size, test, -1, 0, UNSIGNED );
					const expr& amount = k.exprs [ static_cast < size_t > ( right ) ];
					int32_t value = amount.kind == op::CONST && amount.value == 0 ? ( kind == op::ADD ? k.binary ( op::ADD, carry, left, size ) : k.binary ( op::SUB, left, carry, size ) ) : k.binary ( kind, k.binary ( kind, left, right, size ), carry, size );

					write ( ins, 0, value );
					set_flags ( 3, value, -1, size );

					return;

				}

			}

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
			flag_negated = true;

			return;

		}

		case mnemonic::not_: {

			int32_t source = read ( ins, 0 );
			int32_t folded = select_fold ( op::XOR, source, k.constant ( -1, size ), size );

			write ( ins, 0, folded >= 0 ? folded : k.make ( op::NOT, size, source ) );
			return;

		}

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

			if ( bittest_calls && ins.operands [ 0 ].type == x86::kind::REG && ( size == 4 || size == 8 ) ) {

				int32_t slot = slot_of ( ins.operands [ 0 ].reg );

				if ( slot >= 0 && slot < 16 && regs [ slot ] >= 0 && k.exprs [ static_cast < size_t > ( regs [ slot ] ) ].kind != op::VAR )
					materialize ( slot, ins.address );

			}

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

				int32_t slot = ins.operands [ 1 ].type == x86::kind::REG ? slot_of ( ins.operands [ 1 ].reg ) : -1;

				if ( kept_exchanges && slot >= 0 && slot < 16 ) {

					int32_t value = intrinsic ( I_XCHG, size, address_of ( ins, ins.operands [ 0 ] ), right );

					flush ( 0xFFFFFFFFu );

					if ( live & ( 1u << slot ) ) {

						write ( ins, 1, value );
						materialize ( slot, ins.address );

					} else {

						emit ( stmt::EXPR, -1, value, ins.address );
						regs [ slot ] = -1;

					}

					return;

				}

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

			int32_t expected = k.cast ( family_value ( 0 ), size, 0 );
			int32_t value = intrinsic ( I_CMPXCHG, size, ins.operands [ 0 ].type == x86::kind::MEM ? address_of ( ins, ins.operands [ 0 ] ) : read ( ins, 0 ), read ( ins, 1 ), expected );
			const expr& seed = k.exprs [ static_cast < size_t > ( expected ) ];
			int32_t compared = seed.kind == op::CONST ? k.constant ( seed.value, size ) : -1;

			if ( compared < 0 )
				compared = spill ( expected, ins.address );

			flush ( 0xFFFFFFFFu );
			regs [ 0 ] = value;
			materialize ( 0, ins.address );
			set_flags ( 1, family_value ( 0 ), compared, size );

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

			flush ( k.long_mode ? ~( ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 8 ) | ( 1u << 9 ) | ( 1u << 10 ) | ( 1u << 11 ) | ( 0x3Fu << 16 ) ) : 0xFFFFFFFFu );

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

			if ( shared_vectors && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG ) {

				int32_t from = slot_of ( ins.operands [ 1 ].reg );

				if ( from >= 16 && regs [ from ] >= 0 && shared_vector ( regs [ from ] ) && !( busy & ( 1u << from ) ) )
					materialize ( from, 0 );

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

			write ( ins, 0, intrinsic ( sse_extras ? ( ins.name == mnemonic::xorps ? I_XOR_PS : ( ins.name == mnemonic::xorpd ? I_XOR_PD : I_XOR_SI128 ) ) : I_XOR, 16, read ( ins, 0 ), read ( ins, 1 ) ) );
			return;

		case mnemonic::andps:
		case mnemonic::andpd:
			write ( ins, 0, intrinsic ( sse_extras ? ( ins.name == mnemonic::andps ? I_AND_PS : I_AND_PD ) : I_AND, 16, read ( ins, 0 ), read ( ins, 1 ) ) );
			return;

		case mnemonic::orps:
		case mnemonic::orpd:
			write ( ins, 0, intrinsic ( sse_extras ? ( ins.name == mnemonic::orps ? I_OR_PS : I_OR_PD ) : I_OR, 16, read ( ins, 0 ), read ( ins, 1 ) ) );
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

		int32_t slot = ins.operands [ 0 ].type == x86::kind::REG ? slot_of ( ins.operands [ 0 ].reg ) : -1;
		int32_t kept_source = -1;

		if ( shared_cmov_constants && ins.operands [ 1 ].type == x86::kind::REG ) {

			int32_t source = slot_of ( ins.operands [ 1 ].reg );

			if ( source >= 0 && source < 16 && source != slot && regs [ source ] >= 0 && ( live & ( 1u << source ) ) && k.exprs [ static_cast < size_t > ( regs [ source ] ) ].kind == op::CONST ) {

				kept_source = k.constant ( k.exprs [ static_cast < size_t > ( regs [ source ] ) ].value, size );
				materialize ( source, ins.address );

			}

		}

		if ( ordered_cmov_loads && slot >= 0 && slot < 16 && regs [ slot ] >= 0 && k.exprs [ static_cast < size_t > ( regs [ slot ] ) ].kind == op::CONST ) {

			for ( int32_t held = 0; held < 16; held++ ) {

				int32_t value = regs [ held ];

				if ( held == slot || value < 0 || !( live & ( 1u << held ) ) || !( 0xF0E8u & ( 1u << held ) ) || reg_at [ held ] >= reg_at [ slot ] )
					continue;

				const expr& held_value = k.exprs [ static_cast < size_t > ( value ) ];

				if ( held_value.kind == op::LOAD || ( held_value.kind == op::CAST && held_value.a >= 0 && k.exprs [ static_cast < size_t > ( held_value.a ) ].kind == op::LOAD ) )
					materialize ( held, ins.address );

			}

		}

		for ( int32_t held = 0; held < 16; held++ ) {

			int32_t value = regs [ held ];

			if ( held == slot || value < 0 || ( value != flag_a && value != flag_b ) )
				continue;

			op kind = k.exprs [ static_cast < size_t > ( value ) ].kind;

			if ( kind != op::VAR && kind != op::CONST && !( select_loads && kind == op::LOAD && !( k.exprs [ static_cast < size_t > ( value ) ].bits & FLOATING ) && !( live & ( 1u << held ) ) && flag_kind == 1 && flag_b >= 0 && k.exprs [ static_cast < size_t > ( flag_b ) ].kind == op::CONST ) )
				materialize ( held, ins.address );

		}

		if ( slot >= 0 ) {

			int32_t current = regs [ slot ];

			if ( current >= 0 && k.exprs [ static_cast < size_t > ( current ) ].kind != op::VAR )
				materialize ( slot, ins.address );

		}

		int32_t chosen = k.make ( op::TERNARY, size, condition ( ins.condition ), kept_source >= 0 ? kept_source : read ( ins, 1 ) );

		k.exprs [ static_cast < size_t > ( chosen ) ].c = read ( ins, 0 );
		k.deepen ( chosen );
		write ( ins, 0, chosen );

		if ( slot >= 0 && slot < 16 )
			materialize ( slot, ins.address );

		return;

	}

	if ( packed ( ins ) )
		return;

	unhandled ( ins, def );

}

bool decompiler::machine::packed ( const x86::instruction& ins ) {

	builder& k = *b;

	if ( sse_extras && ins.count >= 3 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 2 ].type == x86::kind::IMM && ( ins.name == mnemonic::cmpps || ins.name == mnemonic::cmpss || ins.name == mnemonic::cmppd || ins.name == mnemonic::cmpsd ) ) {

		int32_t family = ins.name == mnemonic::cmpps ? 0 : ( ins.name == mnemonic::cmpss ? 1 : ( ins.name == mnemonic::cmppd ? 2 : 3 ) );
		int32_t predicate = static_cast < int32_t > ( ins.operands [ 2 ].value & 7 );
		int32_t value = intrinsic ( compare_base + family * 8 + predicate, 16, read ( ins, 0 ), read ( ins, 1 ) );

		k.exprs [ static_cast < size_t > ( value ) ].bits |= FLOATING;
		write ( ins, 0, value );

		return true;

	}

	if ( sse_extras && ins.count == 2 && ( ins.name == mnemonic::movlps || ins.name == mnemonic::movhps || ins.name == mnemonic::movlpd || ins.name == mnemonic::movhpd ) ) {

		bool high = ins.name == mnemonic::movhps || ins.name == mnemonic::movhpd;
		bool doubles = ins.name == mnemonic::movlpd || ins.name == mnemonic::movhpd;

		if ( ins.operands [ 0 ].type == x86::kind::MEM && ins.operands [ 1 ].type == x86::kind::REG ) {

			int32_t where = address_of ( ins, ins.operands [ 0 ] );
			int32_t value = read ( ins, 1 );

			flush ( live );
			emit ( stmt::EXPR, -1, intrinsic ( doubles ? ( high ? I_STOREH_PD : I_STOREL_PD ) : ( high ? I_STOREH_PS : I_STOREL_PS ), 8, where, value ), ins.address );

			return true;

		}

		if ( ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::MEM ) {

			int32_t where = address_of ( ins, ins.operands [ 1 ] );
			int32_t value = intrinsic ( doubles ? ( high ? I_LOADH_PD : I_LOADL_PD ) : ( high ? I_LOADH_PI : I_LOADL_PI ), 16, read ( ins, 0 ), where );

			k.exprs [ static_cast < size_t > ( value ) ].bits |= FLOATING;
			write ( ins, 0, value );

			return true;

		}

	}

	int32_t index = -1;
	int32_t total = packed_forms_count;

	for ( int32_t i = 0; i < total; i++ ) {

		if ( packed_forms [ i ].name == ins.name ) {

			index = i;

			break;

		}

	}

	if ( index < 0 || ins.count < 2 || ins.operands [ 0 ].type != x86::kind::REG )
		return false;

	bool all_ones = ( ins.name == mnemonic::pcmpeqb || ins.name == mnemonic::pcmpeqw || ins.name == mnemonic::pcmpeqd || ins.name == mnemonic::pcmpeqq ) && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 1 ].reg == ins.operands [ 0 ].reg;

	if ( all_ones ) {

		write ( ins, 0, k.constant ( -1, 16 ) );

		return true;

	}

	const packed_form& item = packed_forms [ index ];
	uint8_t bits = item.floating ? FLOATING : 0;
	int32_t id = packed_base + index * 2;
	int32_t value = -1;

	auto read = [ & ] ( const x86::instruction& from, int32_t at ) {

		if ( from.operands [ at ].type == x86::kind::IMM )
			return k.constant ( from.operands [ at ].value & 0xFF, 4 );

		if ( scalar_packs && from.operands [ at ].type == x86::kind::REG ) {

			int32_t slot = slot_of ( from.operands [ at ].reg );

			if ( slot >= 16 && slot < 32 && regs [ slot ] >= 0 ) {

				const expr& held = k.exprs [ static_cast < size_t > ( regs [ slot ] ) ];

				if ( packed_forms [ index ].floating && held.size < 16 && ( held.bits & FLOATING ) && ( held.kind == op::ADD || held.kind == op::SUB || held.kind == op::MUL || held.kind == op::DIV || held.kind == op::NEG ) )
					materialize ( slot, from.address );

			}

		}

		return this->read ( from, at );

	};

	switch ( item.form ) {

		case P_BINARY:
			value = intrinsic ( id, 16, read ( ins, 0 ), read ( ins, 1 ) );
			break;

		case P_UNARY:
			value = intrinsic ( id, 16, read ( ins, 1 ) );
			break;

		case P_BINARY_IMM:

			if ( ins.count < 3 )
				return false;

			value = intrinsic ( id, 16, read ( ins, 0 ), read ( ins, 1 ), read ( ins, 2 ) );
			break;

		case P_UNARY_IMM:

			if ( ins.count < 3 )
				return false;

			value = intrinsic ( id, 16, read ( ins, 1 ), read ( ins, 2 ) );
			break;

		case P_SHIFT:

			if ( ins.operands [ 1 ].type == x86::kind::IMM )
				value = intrinsic ( id + 1, 16, read ( ins, 0 ), read ( ins, 1 ) );
			else
				value = intrinsic ( id, 16, read ( ins, 0 ), read ( ins, 1 ) );

			break;

		case P_TO_GPR:
			value = intrinsic ( id, 4, read ( ins, 1 ) );
			write ( ins, 0, value );
			return true;

		case P_TO_GPR_IMM:

			if ( ins.count < 3 )
				return false;

			value = intrinsic ( id, 4, read ( ins, 1 ), read ( ins, 2 ) );
			write ( ins, 0, value );
			return true;

		default:
			return false;

	}

	k.exprs [ static_cast < size_t > ( value ) ].bits |= bits;
	write ( ins, 0, value );

	return true;

}