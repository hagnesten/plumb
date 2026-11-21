#include "script/script.hh"
#include <windows.h>

namespace script {

	enum jit_register : int32_t {

		RAX,
		RCX,
		RDX,
		RBX,
		RSP,
		RBP,
		RSI,
		RDI,
		R8,
		R9,
		R10,
		R11,
		R12,
		R13,
		R14,
		R15,

	};

	enum jit_condition : int32_t {

		ALWAYS = -1,
		BELOW = 2,
		EQUAL = 4,
		NOT_EQUAL = 5,
		ABOVE = 7,
		LESS = 12,
		GREATER_EQUAL = 13,
		LESS_EQUAL = 14,
		GREATER = 15,

	};

	static constexpr int32_t value_size = static_cast < int32_t > ( sizeof ( value ) );
	static constexpr int32_t value_type = static_cast < int32_t > ( __builtin_offsetof ( value, type ) );
	static constexpr int32_t value_number = static_cast < int32_t > ( __builtin_offsetof ( value, number ) );
	static constexpr int32_t value_ref = static_cast < int32_t > ( __builtin_offsetof ( value, ref ) );
	static constexpr int32_t flow_next = 0;
	static constexpr int32_t flow_return = 3;

	static value from_temp ( const jit_temp& item ) {

		value made;

		made.type = static_cast < kind > ( item.type );
		made.number = item.payload;

		if ( made.type == kind::TEXT || made.type == kind::LIST )
			made.ref = static_cast < int32_t > ( item.payload );

		return made;

	}

	static void to_temp ( value item, jit_temp& out ) {

		out.type = static_cast < int64_t > ( item.type );
		out.payload = item.type == kind::TEXT || item.type == kind::LIST ? item.ref : ( item.type == kind::NUMBER ? item.number : 0 );

	}

	void jit_bridge::evaluate ( engine* owner, int32_t index, jit_temp* out ) {

		to_temp ( owner->evaluate ( index ), *out );

	}

	int32_t jit_bridge::execute ( engine* owner, int32_t index ) {

		return static_cast < int32_t > ( owner->execute ( index ) );

	}

	void jit_bridge::binary ( engine* owner, int32_t index, jit_temp* left, jit_temp* right ) {

		const node& at = owner->nodes [ static_cast < size_t > ( index ) ];

		to_temp ( owner->binary_op ( at, at.mark, from_temp ( *left ), from_temp ( *right ) ), *left );

	}

	void jit_bridge::compound ( engine* owner, int32_t index, value* slot, jit_temp* right ) {

		const node& at = owner->nodes [ static_cast < size_t > ( index ) ];
		value result = owner->binary_op ( at, compound_operator ( at.mark ), *slot, from_temp ( *right ) );

		if ( owner->error.set )
			return;

		*slot = result;
		to_temp ( result, *right );

	}

	int32_t jit_bridge::truthy ( engine* owner, int64_t payload, int64_t type ) {

		jit_temp item;

		item.payload = payload;
		item.type = type;

		return owner->truthy ( from_temp ( item ) ) ? 1 : 0;

	}

	void jit_bridge::call ( engine* owner, int32_t index, jit_temp* args, jit_temp* out ) {

		const node& at = owner->nodes [ static_cast < size_t > ( index ) ];
		value inline_args [ 8 ];
		base::list < value > spilled;
		value* converted = inline_args;

		if ( at.count > 8 ) {

			spilled.resize ( static_cast < size_t > ( at.count ) );
			converted = spilled.data ( );

		}

		for ( int32_t i = 0; i < at.count; i++ )
			converted [ i ] = from_temp ( args [ i ] );

		value result = at.b == 0 ? owner->call_user ( at, at.c, converted, at.count ) : owner->call_function ( at, converted, at.count );

		to_temp ( result, *out );

	}

	void jit_bridge::overrun ( engine* owner, int32_t index ) {

		owner->overran ( owner->nodes [ static_cast < size_t > ( index ) ] );

	}

	void jit_bridge::need_number ( engine* owner, int32_t index ) {

		owner->runtime_error ( owner->nodes [ static_cast < size_t > ( index ) ], L"This operator needs a number." );

	}

	compiler::compiler ( engine& target ) : owner ( target ) {

		const char* base_address = reinterpret_cast < const char* > ( &target );

		off_stack = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.stack ) - base_address );
		off_globals = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.globals ) - base_address );
		off_frame = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.frame_base ) - base_address );
		off_steps = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.steps ) - base_address );
		off_limit = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.step_limit ) - base_address );
		off_error = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.error.set ) - base_address );
		off_stopped = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.stopped ) - base_address );
		off_returned = static_cast < int32_t > ( reinterpret_cast < const char* > ( &target.returned ) - base_address );

	}

	void compiler::byte ( uint8_t value_byte ) {

		code.push ( value_byte );

	}

	void compiler::dword ( uint32_t value_word ) {

		for ( int32_t i = 0; i < 4; i++ )
			code.push ( static_cast < uint8_t > ( value_word >> ( i * 8 ) ) );

	}

	void compiler::qword ( uint64_t value_word ) {

		for ( int32_t i = 0; i < 8; i++ )
			code.push ( static_cast < uint8_t > ( value_word >> ( i * 8 ) ) );

	}

	void compiler::rex ( bool wide, int32_t reg, int32_t base_reg, bool force ) {

		uint8_t prefix = static_cast < uint8_t > ( 0x40 | ( wide ? 8 : 0 ) | ( reg & 8 ? 4 : 0 ) | ( base_reg & 8 ? 1 : 0 ) );

		if ( prefix != 0x40 || force )
			byte ( prefix );

	}

	void compiler::memory ( const uint8_t* opcode, int32_t opcode_length, int32_t reg, int32_t base_reg, int32_t displacement, bool wide ) {

		rex ( wide, reg, base_reg, false );

		for ( int32_t i = 0; i < opcode_length; i++ )
			byte ( opcode [ i ] );

		byte ( static_cast < uint8_t > ( 0x80 | ( ( reg & 7 ) << 3 ) | ( base_reg & 7 ) ) );

		if ( ( base_reg & 7 ) == 4 )
			byte ( 0x24 );

		dword ( static_cast < uint32_t > ( displacement ) );

	}

	void compiler::direct ( uint8_t opcode, int32_t reg, int32_t rm, bool wide ) {

		rex ( wide, reg, rm, false );
		byte ( opcode );
		byte ( static_cast < uint8_t > ( 0xC0 | ( ( reg & 7 ) << 3 ) | ( rm & 7 ) ) );

	}

	void compiler::move_immediate ( int32_t reg, uint64_t value_word ) {

		if ( value_word <= 0xFFFFFFFFull ) {

			rex ( false, 0, reg, false );
			byte ( static_cast < uint8_t > ( 0xB8 + ( reg & 7 ) ) );
			dword ( static_cast < uint32_t > ( value_word ) );

			return;

		}

		rex ( true, 0, reg, false );
		byte ( static_cast < uint8_t > ( 0xB8 + ( reg & 7 ) ) );
		qword ( value_word );

	}

	static const uint8_t op_load [ ] = { 0x8B };
	static const uint8_t op_store [ ] = { 0x89 };
	static const uint8_t op_store_byte [ ] = { 0x88 };
	static const uint8_t op_zero_extend_byte [ ] = { 0x0F, 0xB6 };
	static const uint8_t op_address [ ] = { 0x8D };
	static const uint8_t op_compare_byte_immediate [ ] = { 0x80 };
	static const uint8_t op_move_byte_immediate [ ] = { 0xC6 };
	static const uint8_t op_increment [ ] = { 0xFF };
	static const uint8_t op_compare_memory [ ] = { 0x39 };

	static void compare_immediate32 ( compiler& c, int32_t reg, int8_t immediate ) {

		c.rex ( false, 0, reg, false );
		c.byte ( 0x83 );
		c.byte ( static_cast < uint8_t > ( 0xF8 | ( reg & 7 ) ) );
		c.byte ( static_cast < uint8_t > ( immediate ) );

	}

	static void compare_immediate64 ( compiler& c, int32_t reg, int8_t immediate ) {

		c.rex ( true, 0, reg, false );
		c.byte ( 0x83 );
		c.byte ( static_cast < uint8_t > ( 0xF8 | ( reg & 7 ) ) );
		c.byte ( static_cast < uint8_t > ( immediate ) );

	}

	static void push_register ( compiler& c, int32_t reg ) {

		if ( reg & 8 )
			c.byte ( 0x41 );

		c.byte ( static_cast < uint8_t > ( 0x50 + ( reg & 7 ) ) );

	}

	static void pop_register ( compiler& c, int32_t reg ) {

		if ( reg & 8 )
			c.byte ( 0x41 );

		c.byte ( static_cast < uint8_t > ( 0x58 + ( reg & 7 ) ) );

	}

	static void set_al ( compiler& c, int32_t condition ) {

		c.byte ( 0x0F );
		c.byte ( static_cast < uint8_t > ( 0x90 + condition ) );
		c.byte ( 0xC0 );
		c.byte ( 0x0F );
		c.byte ( 0xB6 );
		c.byte ( 0xC0 );

	}

	int32_t compiler::label ( ) {

		labels.emplace ( );

		return labels.count ( ) - 1;

	}

	void compiler::bind ( int32_t id ) {

		jit_label& item = labels [ static_cast < size_t > ( id ) ];

		item.position = code.count ( );

		for ( int32_t fix : item.fixups ) {

			uint32_t relative = static_cast < uint32_t > ( item.position - ( fix + 4 ) );

			for ( int32_t i = 0; i < 4; i++ )
				code [ static_cast < size_t > ( fix + i ) ] = static_cast < uint8_t > ( relative >> ( i * 8 ) );

		}

		item.fixups.clear ( );

	}

	void compiler::jump ( int32_t condition, int32_t id ) {

		if ( condition == ALWAYS )
			byte ( 0xE9 );
		else {

			byte ( 0x0F );
			byte ( static_cast < uint8_t > ( 0x80 + condition ) );

		}

		jit_label& item = labels [ static_cast < size_t > ( id ) ];

		if ( item.position >= 0 ) {

			dword ( static_cast < uint32_t > ( item.position - ( code.count ( ) + 4 ) ) );

			return;

		}

		item.fixups.push ( code.count ( ) );
		dword ( 0 );

	}

	int32_t compiler::temp ( ) {

		int32_t index = depth++;

		most = base::max ( most, depth );

		return index;

	}

	int32_t compiler::temp_offset ( int32_t index ) const {

		return -( 64 + 16 * ( index + 1 ) );

	}

	void compiler::store_temp ( int32_t index ) {

		memory ( op_store, 1, RAX, RBP, temp_offset ( index ), true );
		memory ( op_store, 1, RCX, RBP, temp_offset ( index ) + 8, true );

	}

	void compiler::load_temp ( int32_t index ) {

		memory ( op_load, 1, RAX, RBP, temp_offset ( index ), true );
		memory ( op_load, 1, RCX, RBP, temp_offset ( index ) + 8, true );

	}

	void compiler::load_slot ( const node& name ) {

		int32_t base_reg = name.b == 1 ? R14 : R13;
		int32_t displacement = name.c * value_size;

		memory ( op_zero_extend_byte, 2, RCX, base_reg, displacement + value_type, false );
		memory ( op_load, 1, RAX, base_reg, displacement + value_number, true );

	}

	void compiler::store_slot ( const node& name ) {

		int32_t base_reg = name.b == 1 ? R14 : R13;
		int32_t displacement = name.c * value_size;

		memory ( op_store_byte, 1, RCX, base_reg, displacement + value_type, false );
		memory ( op_store, 1, RAX, base_reg, displacement + value_number, true );
		memory ( op_store, 1, RAX, base_reg, displacement + value_ref, false );

	}

	void compiler::slot_address ( int32_t target, const node& name ) {

		memory ( op_address, 1, target, name.b == 1 ? R14 : R13, name.c * value_size, true );

	}

	void compiler::reload_frame ( ) {

		memory ( op_load, 1, R13, R12, off_stack, true );
		memory ( op_load, 1, R11, R12, off_frame, false );
		byte ( 0x4D );
		byte ( 0x6B );
		byte ( 0xDB );
		byte ( static_cast < uint8_t > ( value_size ) );
		direct ( 0x01, R11, R13, true );

	}

	void compiler::call_helper ( const void* target ) {

		move_immediate ( RAX, reinterpret_cast < uint64_t > ( target ) );
		byte ( 0xFF );
		byte ( 0xD0 );

	}

	void compiler::after_call ( ) {

		memory ( op_compare_byte_immediate, 1, 7, R12, off_error, false );
		byte ( 0 );
		jump ( NOT_EQUAL, bail );
		memory ( op_compare_byte_immediate, 1, 7, R12, off_stopped, false );
		byte ( 0 );
		jump ( NOT_EQUAL, bail );
		reload_frame ( );

	}

	void compiler::truthy ( ) {

		int32_t slow = label ( );
		int32_t done = label ( );

		compare_immediate32 ( *this, RCX, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, slow );
		direct ( 0x85, RAX, RAX, true );
		byte ( 0x0F );
		byte ( 0x95 );
		byte ( 0xC0 );
		jump ( ALWAYS, done );
		bind ( slow );
		direct ( 0x89, RCX, R8, true );
		direct ( 0x89, RAX, RDX, true );
		direct ( 0x89, R12, RCX, true );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::truthy ) );
		bind ( done );

	}

	void compiler::step_check ( int32_t index ) {

		int32_t fine = label ( );

		memory ( op_increment, 1, 0, R12, off_steps, true );
		memory ( op_load, 1, RAX, R12, off_limit, true );
		memory ( op_compare_memory, 1, RAX, R12, off_steps, true );
		jump ( LESS_EQUAL, fine );
		direct ( 0x89, R12, RCX, true );
		move_immediate ( RDX, static_cast < uint32_t > ( index ) );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::overrun ) );
		jump ( ALWAYS, bail );
		bind ( fine );

	}

	bool compiler::arithmetic ( symbol mark, int32_t slow ) {

		switch ( mark ) {

			case symbol::PLUS:
				direct ( 0x01, RDX, RAX, true );
				return true;

			case symbol::MINUS:
				direct ( 0x29, RDX, RAX, true );
				return true;

			case symbol::STAR:
				byte ( 0x48 );
				byte ( 0x0F );
				byte ( 0xAF );
				byte ( 0xC2 );
				return true;

			case symbol::AMPERSAND:
				direct ( 0x21, RDX, RAX, true );
				return true;

			case symbol::PIPE:
				direct ( 0x09, RDX, RAX, true );
				return true;

			case symbol::CARET:
				direct ( 0x31, RDX, RAX, true );
				return true;

			case symbol::SHIFT_LEFT:
			case symbol::SHIFT_RIGHT:
				compare_immediate64 ( *this, RDX, 63 );
				jump ( ABOVE, slow );
				direct ( 0x89, RDX, RCX, true );
				byte ( 0x48 );
				byte ( 0xD3 );
				byte ( mark == symbol::SHIFT_LEFT ? 0xE0 : 0xF8 );
				return true;

			case symbol::SLASH:
			case symbol::PERCENT:
				direct ( 0x85, RDX, RDX, true );
				jump ( EQUAL, slow );
				compare_immediate64 ( *this, RDX, -1 );
				jump ( EQUAL, slow );
				direct ( 0x89, RDX, R9, true );
				byte ( 0x48 );
				byte ( 0x99 );
				byte ( 0x49 );
				byte ( 0xF7 );
				byte ( 0xF9 );

				if ( mark == symbol::PERCENT )
					direct ( 0x89, RDX, RAX, true );

				return true;

			default:
				return false;

		}

	}

	static int32_t comparison ( symbol mark ) {

		switch ( mark ) {

			case symbol::LESS:
				return LESS;

			case symbol::LESS_EQUAL:
				return LESS_EQUAL;

			case symbol::GREATER:
				return GREATER;

			case symbol::GREATER_EQUAL:
				return GREATER_EQUAL;

			case symbol::EQUAL_EQUAL:
				return EQUAL;

			case symbol::BANG_EQUAL:
				return NOT_EQUAL;

			default:
				return -2;

		}

	}

	void compiler::fallback ( int32_t index ) {

		int32_t out = temp ( );

		direct ( 0x89, R12, RCX, true );
		move_immediate ( RDX, static_cast < uint32_t > ( index ) );
		memory ( op_address, 1, R8, RBP, temp_offset ( out ), true );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::evaluate ) );
		after_call ( );
		load_temp ( out );
		depth--;

	}

	void compiler::binary ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];
		int32_t left = temp ( );
		int32_t right = temp ( );
		int32_t slow = label ( );
		int32_t done = label ( );

		expression ( at.a );
		store_temp ( left );
		expression ( at.b );
		direct ( 0x89, RAX, RDX, true );
		direct ( 0x89, RCX, R8, true );
		load_temp ( left );
		compare_immediate32 ( *this, RCX, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, slow );
		compare_immediate32 ( *this, R8, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, slow );

		int32_t condition = comparison ( at.mark );

		if ( condition != -2 ) {

			direct ( 0x39, RDX, RAX, true );
			set_al ( *this, condition );

		} else if ( !arithmetic ( at.mark, slow ) )
			jump ( ALWAYS, slow );

		move_immediate ( RCX, static_cast < uint32_t > ( kind::NUMBER ) );
		jump ( ALWAYS, done );
		bind ( slow );
		memory ( op_store, 1, RDX, RBP, temp_offset ( right ), true );
		memory ( op_store, 1, R8, RBP, temp_offset ( right ) + 8, true );
		direct ( 0x89, R12, RCX, true );
		move_immediate ( RDX, static_cast < uint32_t > ( index ) );
		memory ( op_address, 1, R8, RBP, temp_offset ( left ), true );
		memory ( op_address, 1, R9, RBP, temp_offset ( right ), true );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::binary ) );
		after_call ( );
		load_temp ( left );
		bind ( done );
		depth -= 2;

	}

	void compiler::logical ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];
		int32_t decided = label ( );
		int32_t done = label ( );
		bool either = at.type == node_kind::OR;

		expression ( at.a );
		truthy ( );
		byte ( 0x84 );
		byte ( 0xC0 );
		jump ( either ? NOT_EQUAL : EQUAL, decided );
		expression ( at.b );
		truthy ( );
		byte ( 0x0F );
		byte ( 0xB6 );
		byte ( 0xC0 );
		jump ( ALWAYS, done );
		bind ( decided );
		move_immediate ( RAX, either ? 1 : 0 );
		bind ( done );
		move_immediate ( RCX, static_cast < uint32_t > ( kind::NUMBER ) );

	}

	void compiler::unary ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];

		expression ( at.a );

		if ( at.mark == symbol::BANG ) {

			truthy ( );
			byte ( 0x34 );
			byte ( 0x01 );
			byte ( 0x0F );
			byte ( 0xB6 );
			byte ( 0xC0 );
			move_immediate ( RCX, static_cast < uint32_t > ( kind::NUMBER ) );

			return;

		}

		int32_t bad = label ( );
		int32_t done = label ( );

		compare_immediate32 ( *this, RCX, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, bad );

		if ( at.mark == symbol::MINUS ) {

			byte ( 0x48 );
			byte ( 0xF7 );
			byte ( 0xD8 );

		} else if ( at.mark == symbol::TILDE ) {

			byte ( 0x48 );
			byte ( 0xF7 );
			byte ( 0xD0 );

		}

		jump ( ALWAYS, done );
		bind ( bad );
		direct ( 0x89, R12, RCX, true );
		move_immediate ( RDX, static_cast < uint32_t > ( index ) );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::need_number ) );
		jump ( ALWAYS, bail );
		bind ( done );

	}

	void compiler::assign_name ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];
		const node target = owner.nodes [ static_cast < size_t > ( at.a ) ];

		expression ( at.b );

		if ( at.mark == symbol::ASSIGN ) {

			store_slot ( target );

			return;

		}

		int32_t slow = label ( );
		int32_t done = label ( );
		int32_t right = temp ( );

		direct ( 0x89, RAX, RDX, true );
		direct ( 0x89, RCX, R8, true );
		load_slot ( target );
		compare_immediate32 ( *this, RCX, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, slow );
		compare_immediate32 ( *this, R8, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, slow );

		if ( !arithmetic ( compound_operator ( at.mark ), slow ) )
			jump ( ALWAYS, slow );

		move_immediate ( RCX, static_cast < uint32_t > ( kind::NUMBER ) );
		store_slot ( target );
		jump ( ALWAYS, done );
		bind ( slow );
		memory ( op_store, 1, RDX, RBP, temp_offset ( right ), true );
		memory ( op_store, 1, R8, RBP, temp_offset ( right ) + 8, true );
		direct ( 0x89, R12, RCX, true );
		move_immediate ( RDX, static_cast < uint32_t > ( index ) );
		slot_address ( R8, target );
		memory ( op_address, 1, R9, RBP, temp_offset ( right ), true );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::compound ) );
		after_call ( );
		load_temp ( right );
		bind ( done );
		depth--;

	}

	void compiler::step_name ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];
		const node target = owner.nodes [ static_cast < size_t > ( at.a ) ];
		int32_t bad = label ( );
		int32_t done = label ( );

		load_slot ( target );
		compare_immediate32 ( *this, RCX, static_cast < int8_t > ( kind::NUMBER ) );
		jump ( NOT_EQUAL, bad );
		direct ( 0x89, RAX, RDX, true );
		byte ( 0x48 );
		byte ( 0x83 );
		byte ( at.mark == symbol::PLUS_PLUS ? 0xC0 : 0xE8 );
		byte ( 0x01 );
		store_slot ( target );

		if ( at.type == node_kind::POSTFIX )
			direct ( 0x89, RDX, RAX, true );

		jump ( ALWAYS, done );
		bind ( bad );
		fallback ( index );
		bind ( done );

	}

	void compiler::call ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];
		int32_t first = depth;

		for ( int32_t i = 0; i < at.count; i++ )
			temp ( );

		int32_t out = temp ( );

		for ( int32_t k = 0; k < at.count; k++ ) {

			expression ( owner.items [ static_cast < size_t > ( at.first + k ) ] );
			store_temp ( first + at.count - 1 - k );

		}

		direct ( 0x89, R12, RCX, true );
		move_immediate ( RDX, static_cast < uint32_t > ( index ) );

		if ( at.count )
			memory ( op_address, 1, R8, RBP, temp_offset ( first + at.count - 1 ), true );
		else
			move_immediate ( R8, 0 );

		memory ( op_address, 1, R9, RBP, temp_offset ( out ), true );
		call_helper ( reinterpret_cast < const void* > ( &jit_bridge::call ) );
		after_call ( );
		load_temp ( out );
		depth -= at.count + 1;

	}

	void compiler::expression ( int32_t index ) {

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];

		switch ( at.type ) {

			case node_kind::NUMBER:
				move_immediate ( RAX, static_cast < uint64_t > ( at.number ) );
				move_immediate ( RCX, static_cast < uint32_t > ( kind::NUMBER ) );
				return;

			case node_kind::TEXT:
				move_immediate ( RAX, static_cast < uint32_t > ( at.text ) );
				move_immediate ( RCX, static_cast < uint32_t > ( kind::TEXT ) );
				return;

			case node_kind::NOTHING:
				move_immediate ( RAX, 0 );
				move_immediate ( RCX, static_cast < uint32_t > ( kind::NONE ) );
				return;

			case node_kind::NAME:
				load_slot ( at );
				return;

			case node_kind::BINARY:
				binary ( index );
				return;

			case node_kind::AND:
			case node_kind::OR:
				logical ( index );
				return;

			case node_kind::UNARY:
				unary ( index );
				return;

			case node_kind::TERNARY: {

				int32_t other = label ( );
				int32_t done = label ( );

				expression ( at.a );
				truthy ( );
				byte ( 0x84 );
				byte ( 0xC0 );
				jump ( EQUAL, other );
				expression ( at.b );
				jump ( ALWAYS, done );
				bind ( other );
				expression ( at.c );
				bind ( done );

				return;

			}

			case node_kind::ASSIGN:

				if ( owner.nodes [ static_cast < size_t > ( at.a ) ].type == node_kind::NAME )
					assign_name ( index );
				else
					fallback ( index );

				return;

			case node_kind::PREFIX:
			case node_kind::POSTFIX:

				if ( owner.nodes [ static_cast < size_t > ( at.a ) ].type == node_kind::NAME )
					step_name ( index );
				else
					fallback ( index );

				return;

			case node_kind::CALL:
				call ( index );
				return;

			default:
				fallback ( index );
				return;

		}

	}

	void compiler::statement ( int32_t index ) {

		if ( index < 0 )
			return;

		const node at = owner.nodes [ static_cast < size_t > ( index ) ];

		switch ( at.type ) {

			case node_kind::BLOCK:

				for ( int32_t i = 0; i < at.count; i++ )
					statement ( owner.items [ static_cast < size_t > ( at.first + i ) ] );

				return;

			case node_kind::EMPTY:
				return;

			case node_kind::EXPR:
				expression ( at.a );
				return;

			case node_kind::VAR:

				for ( int32_t i = 0; i < at.count; i++ ) {

					const node item = owner.nodes [ static_cast < size_t > ( owner.items [ static_cast < size_t > ( at.first + i ) ] ) ];

					if ( item.a >= 0 )
						expression ( item.a );
					else {

						move_immediate ( RAX, 0 );
						move_immediate ( RCX, static_cast < uint32_t > ( kind::NONE ) );

					}

					store_slot ( item );

				}

				return;

			case node_kind::IF: {

				int32_t other = label ( );
				int32_t done = label ( );

				expression ( at.a );
				truthy ( );
				byte ( 0x84 );
				byte ( 0xC0 );
				jump ( EQUAL, other );
				statement ( at.b );
				jump ( ALWAYS, done );
				bind ( other );
				statement ( at.c );
				bind ( done );

				return;

			}

			case node_kind::WHILE: {

				jit_loop& loop = loops.emplace ( );
				int32_t exit = label ( );
				int32_t again = label ( );

				loop.exit = exit;
				loop.again = again;
				bind ( again );
				step_check ( index );
				expression ( at.a );
				truthy ( );
				byte ( 0x84 );
				byte ( 0xC0 );
				jump ( EQUAL, exit );
				statement ( at.b );
				jump ( ALWAYS, again );
				bind ( exit );
				loops.pop ( );

				return;

			}

			case node_kind::DO: {

				jit_loop& loop = loops.emplace ( );
				int32_t exit = label ( );
				int32_t again = label ( );
				int32_t top_label = label ( );

				loop.exit = exit;
				loop.again = again;
				bind ( top_label );
				step_check ( index );
				statement ( at.b );
				bind ( again );
				expression ( at.a );
				truthy ( );
				byte ( 0x84 );
				byte ( 0xC0 );
				jump ( NOT_EQUAL, top_label );
				bind ( exit );
				loops.pop ( );

				return;

			}

			case node_kind::FOR: {

				statement ( at.a );

				jit_loop& loop = loops.emplace ( );
				int32_t exit = label ( );
				int32_t again = label ( );
				int32_t top_label = label ( );

				loop.exit = exit;
				loop.again = again;
				bind ( top_label );
				step_check ( index );

				if ( at.b >= 0 ) {

					expression ( at.b );
					truthy ( );
					byte ( 0x84 );
					byte ( 0xC0 );
					jump ( EQUAL, exit );

				}

				statement ( at.d );
				bind ( again );

				if ( at.c >= 0 )
					expression ( at.c );

				jump ( ALWAYS, top_label );
				bind ( exit );
				loops.pop ( );

				return;

			}

			case node_kind::BREAK:
			case node_kind::CONTINUE:

				if ( loops.empty ( ) ) {

					direct ( 0x89, R12, RCX, true );
					move_immediate ( RDX, static_cast < uint32_t > ( index ) );
					call_helper ( reinterpret_cast < const void* > ( &jit_bridge::execute ) );
					jump ( ALWAYS, bail );

					return;

				}

				jump ( ALWAYS, at.type == node_kind::BREAK ? loops.back ( ).exit : loops.back ( ).again );

				return;

			case node_kind::RETURN:

				if ( at.a >= 0 )
					expression ( at.a );
				else {

					move_immediate ( RAX, 0 );
					move_immediate ( RCX, static_cast < uint32_t > ( kind::NONE ) );

				}

				memory ( op_store_byte, 1, RCX, R12, off_returned + value_type, false );
				memory ( op_store, 1, RAX, R12, off_returned + value_number, true );
				memory ( op_store, 1, RAX, R12, off_returned + value_ref, false );

				if ( top ) {

					memory ( op_move_byte_immediate, 1, 0, R12, off_stopped, false );
					byte ( 1 );

				}

				move_immediate ( RAX, flow_return );
				jump ( ALWAYS, epilogue );

				return;

			default:
				return;

		}

	}

	int32_t compiler::function ( const int32_t* statements, int32_t count, bool top_level ) {

		int32_t start = code.count ( );

		top = top_level;
		depth = 0;
		most = 0;
		loops.clear ( );
		epilogue = label ( );
		bail = label ( );

		push_register ( *this, RBP );
		direct ( 0x89, RSP, RBP, true );
		push_register ( *this, RBX );
		push_register ( *this, RSI );
		push_register ( *this, RDI );
		push_register ( *this, R12 );
		push_register ( *this, R13 );
		push_register ( *this, R14 );
		push_register ( *this, R15 );
		byte ( 0x48 );
		byte ( 0x81 );
		byte ( 0xEC );
		frame_patch = code.count ( );
		dword ( 0 );
		direct ( 0x89, RCX, R12, true );
		memory ( op_load, 1, R14, R12, off_globals, true );
		reload_frame ( );

		for ( int32_t i = 0; i < count; i++ )
			statement ( statements [ i ] );

		move_immediate ( RAX, flow_next );
		jump ( ALWAYS, epilogue );
		bind ( bail );
		move_immediate ( RAX, flow_return );
		bind ( epilogue );
		memory ( op_address, 1, RSP, RBP, -56, true );
		pop_register ( *this, R15 );
		pop_register ( *this, R14 );
		pop_register ( *this, R13 );
		pop_register ( *this, R12 );
		pop_register ( *this, RDI );
		pop_register ( *this, RSI );
		pop_register ( *this, RBX );
		pop_register ( *this, RBP );
		byte ( 0xC3 );

		uint32_t frame = static_cast < uint32_t > ( 8 + 16 * most + 32 );

		for ( int32_t i = 0; i < 4; i++ )
			code [ static_cast < size_t > ( frame_patch + i ) ] = static_cast < uint8_t > ( frame >> ( i * 8 ) );

		return start;

	}

	void engine::release_jit ( ) {

		if ( jit_memory )
			VirtualFree ( jit_memory, 0, MEM_RELEASE );

		jit_memory = nullptr;
		jit_size = 0;
		jit_top = 0;
		jit_used = false;
		jit_entries.clear ( );

	}

	engine::~engine ( ) {

		release_jit ( );

	}

	bool engine::compile ( ) {

#if defined ( _M_X64 ) || defined ( __x86_64__ )

		release_jit ( );

		compiler* builder = new compiler ( *this );
		base::list < int32_t > starts;

		for ( const function_def& item : functions )
			starts.push ( builder->function ( &item.body, 1, false ) );

		int32_t top_start = builder->function ( top_level.data ( ), top_level.count ( ), true );
		size_t bytes = static_cast < size_t > ( builder->code.count ( ) );
		void* block = VirtualAlloc ( nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

		if ( !block ) {

			delete builder;

			return false;

		}

		uint8_t* target = static_cast < uint8_t* > ( block );

		for ( size_t i = 0; i < bytes; i++ )
			target [ i ] = builder->code [ i ];

		DWORD previous = 0;

		if ( !VirtualProtect ( block, bytes, PAGE_EXECUTE_READ, &previous ) ) {

			VirtualFree ( block, 0, MEM_RELEASE );
			delete builder;

			return false;

		}

		FlushInstructionCache ( GetCurrentProcess ( ), block, bytes );
		jit_memory = block;
		jit_size = bytes;
		jit_top = reinterpret_cast < uint64_t > ( target + top_start );

		for ( int32_t start : starts )
			jit_entries.push ( reinterpret_cast < uint64_t > ( target + start ) );

		delete builder;

		return true;

#else

		return false;

#endif

	}

}
