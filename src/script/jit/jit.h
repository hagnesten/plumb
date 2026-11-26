#pragma once

#include "base/list/list.h"
#include "script/values/values.h"
#include "script/tokens/tokens.h"
#include "script/tree/tree.h"

namespace script {

	class engine;
	struct jit_temp {

		int64_t payload = 0;
		int64_t type = 0;

	};

	struct jit_label {

		int32_t position = -1;
		base::list < int32_t > fixups;

	};

	struct jit_loop {

		int32_t exit = -1;
		int32_t again = -1;

	};

	struct jit_bridge {

		static void evaluate ( engine* owner, int32_t index, jit_temp* out );

		static int32_t execute ( engine* owner, int32_t index );

		static void binary ( engine* owner, int32_t index, jit_temp* left, jit_temp* right );

		static void compound ( engine* owner, int32_t index, value* slot, jit_temp* right );

		static int32_t truthy ( engine* owner, int64_t payload, int64_t type );

		static void call ( engine* owner, int32_t index, jit_temp* args, jit_temp* out );

		static void overrun ( engine* owner, int32_t index );

		static void need_number ( engine* owner, int32_t index );

	};

	struct compiler {

		engine& owner;
		base::list < uint8_t > code;
		base::list < jit_label > labels;
		base::list < jit_loop > loops;
		int32_t depth = 0;
		int32_t most = 0;
		int32_t epilogue = -1;
		int32_t bail = -1;
		int32_t frame_patch = -1;
		bool top = false;
		int32_t off_stack = 0;
		int32_t off_globals = 0;
		int32_t off_frame = 0;
		int32_t off_steps = 0;
		int32_t off_limit = 0;
		int32_t off_error = 0;
		int32_t off_stopped = 0;
		int32_t off_returned = 0;

		explicit compiler ( engine& target );

		int32_t function ( const int32_t* statements, int32_t count, bool top_level );

		void statement ( int32_t index );

		void expression ( int32_t index );

		void binary ( int32_t index );

		void logical ( int32_t index );

		void unary ( int32_t index );

		void assign_name ( int32_t index );

		void step_name ( int32_t index );

		void call ( int32_t index );

		void fallback ( int32_t index );

		void truthy ( );

		void step_check ( int32_t index );

		bool arithmetic ( symbol mark, int32_t slow );

		void load_slot ( const node& name );

		void store_slot ( const node& name );

		void slot_address ( int32_t target, const node& name );

		int32_t temp ( );

		int32_t temp_offset ( int32_t index ) const;

		void store_temp ( int32_t index );

		void load_temp ( int32_t index );

		void call_helper ( const void* target );

		void after_call ( );

		void reload_frame ( );

		int32_t label ( );

		void bind ( int32_t id );

		void jump ( int32_t condition, int32_t id );

		void byte ( uint8_t value );

		void dword ( uint32_t value );

		void qword ( uint64_t value );

		void rex ( bool wide, int32_t reg, int32_t base, bool force );

		void memory ( const uint8_t* opcode, int32_t opcode_length, int32_t reg, int32_t base, int32_t displacement, bool wide );

		void direct ( uint8_t opcode, int32_t reg, int32_t rm, bool wide );

		void move_immediate ( int32_t reg, uint64_t value );

	};

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

	constexpr int32_t value_size = static_cast < int32_t > ( sizeof ( value ) );

	constexpr int32_t value_type = static_cast < int32_t > ( __builtin_offsetof ( value, type ) );

	constexpr int32_t value_number = static_cast < int32_t > ( __builtin_offsetof ( value, number ) );

	constexpr int32_t value_ref = static_cast < int32_t > ( __builtin_offsetof ( value, ref ) );

	constexpr int32_t flow_next = 0;

	constexpr int32_t flow_return = 3;

	value from_temp ( const jit_temp& item );

	void to_temp ( value item, jit_temp& out );

	extern const uint8_t op_load [ ];

	extern const uint8_t op_store [ ];

	extern const uint8_t op_store_byte [ ];

	extern const uint8_t op_zero_extend_byte [ ];

	extern const uint8_t op_address [ ];

	extern const uint8_t op_compare_byte_immediate [ ];

	extern const uint8_t op_move_byte_immediate [ ];

	extern const uint8_t op_increment [ ];

	extern const uint8_t op_compare_memory [ ];

	void compare_immediate32 ( compiler& c, int32_t reg, int8_t immediate );

	void compare_immediate64 ( compiler& c, int32_t reg, int8_t immediate );

	void push_register ( compiler& c, int32_t reg );

	void pop_register ( compiler& c, int32_t reg );

	void set_al ( compiler& c, int32_t condition );

	int32_t comparison ( symbol mark );

}