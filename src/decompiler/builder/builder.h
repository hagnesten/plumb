#pragma once

#include "base/list/list.h"
#include "base/text/text.h"
#include "base/writer/writer.h"
#include "loader/binary/binary.h"
#include "analysis/database/database.h"
#include "analysis/graph/graph.h"
#include "x86/instruction/instruction.h"
#include "decompiler/output/output.h"
#include "decompiler/prototypes/prototypes.h"
#include "decompiler/namer/namer.h"

namespace decompiler {

	struct counter_scan;

	enum class op : uint8_t {

		CONST,
		REAL,
		VAR,
		LOAD,
		ADDRESS,
		GLOBAL,
		TEXT,
		CALL,
		INTRINSIC,
		CAST,
		NOT,
		NEG,
		LNOT,
		ADD,
		SUB,
		MUL,
		DIV,
		MOD,
		AND,
		OR,
		XOR,
		SHL,
		SHR,
		EQ,
		NE,
		LT,
		LE,
		GT,
		GE,
		LAND,
		LOR,
		TERNARY,
		SET,

	};

	enum expr_bits : uint8_t {

		SIGNED = 1,
		UNSIGNED = 2,
		FLOATING = 4,
		POINTER = 8,

	};

	struct expr {

		op kind = op::CONST;
		uint8_t size = 8;
		uint8_t bits = 0;
		int32_t a = -1;
		int32_t b = -1;
		int32_t c = -1;
		int64_t value = 0;
		int32_t first_arg = 0;
		int32_t arg_count = 0;
		int32_t depth = 1;

	};

	enum class var_kind : uint8_t {

		REGISTER,
		STACK,
		PARAMETER,

	};

	struct variable {

		var_kind kind = var_kind::REGISTER;
		int32_t family = -1;
		int64_t offset = 0;
		uint8_t size = 8;
		uint8_t bits = 0;
		int32_t position = -1;
		int32_t number = 0;
		int32_t uses = 0;
		int32_t layout = -1;
		bool cookie = false;
		bool saved = false;
		bool assigned = false;
		bool flags = false;
		bool ghost = false;
		uint64_t born = 0;

	};

	enum class stmt : uint8_t {

		ASSIGN,
		STORE,
		EXPR,
		RETURN,
		ASM,
		NOP,

	};

	struct statement {

		stmt kind = stmt::NOP;
		int32_t left = -1;
		int32_t right = -1;
		uint64_t address = 0;
		uint64_t origin = 0;
		uint32_t sources = 0;
		uint32_t target = 0;

	};

	enum class term : uint8_t {

		NONE,
		FALL,
		BRANCH,
		RETURN,
		SWITCH,
		STOP,
		TAIL,

	};

	struct block_code {

		int32_t first = 0;
		int32_t count = 0;
		term end = term::NONE;
		int32_t condition = -1;
		int32_t taken = -1;
		int32_t other = -1;
		int32_t selector = -1;
		int32_t returned = -1;
		uint64_t last = 0;
		int64_t delta = 0;
		bool delta_known = false;
		bool reachable = false;
		int32_t flag_kind = 0;
		int32_t flag_a = -1;
		int32_t flag_b = -1;
		uint8_t flag_size = 4;
		int32_t preds = 0;
		int32_t writes_first = 0;
		int32_t writes_count = 0;

	};

	struct packed_form {

		x86::mnemonic name;
		const wchar_t* text;
		const wchar_t* immediate;
		uint8_t form = 0;
		bool floating = false;

	};

	struct global_access {

		int32_t index = -1;
		int64_t scale = 1;
		uint64_t start = 0;
		uint8_t size = 0;
		uint64_t base = 0;
		uint8_t element = 0;
		bool grouped = false;
		bool renamed = false;

	};

	struct field_use {

		int32_t var = -1;
		int64_t offset = 0;
		uint8_t size = 0;
		uint8_t bits = 0;
		int32_t count = 0;

	};

	struct builder {

		const analysis::database* db = nullptr;
		const loader::binary* file = nullptr;
		namer* names = nullptr;
		result* out = nullptr;
		analysis::function_graph graph;
		bool long_mode = true;
		uint64_t function = 0;
		base::list < expr > exprs;
		base::list < int32_t > args;
		base::list < variable > vars;
		base::list < statement > statements;
		base::list < block_code > code;
		base::list < base::text > asm_text;
		base::list < field_use > fields;
		base::list < uint64_t > float_globals;
		base::list < int64_t > saved_offsets;
		base::list < int32_t > saved_families;
		int32_t family_var [ 32 ];
		uint32_t entry_live = 0;
		uint32_t home_local = 0;
		uint8_t spill_widths [ 4 ] = { };
		int32_t params = 0;
		bool has_prototype = false;
		c_prototype prototype;
		base::list < base::text > var_types;
		base::list < base::text > var_structs;
		base::list < uint64_t > number_at;
		base::list < uint64_t > ghost_at;
		base::list < uint64_t > write_at;
		base::list < uint32_t > write_regs;
		uint64_t result_at = 0;
		int32_t return_points = 0;
		uint8_t param_bits [ 16 ] = { };
		bool returns_value = false;
		bool bare_return = false;
		bool returns_float = false;
		uint8_t return_size = 0;
		uint8_t result_width = 0;
		int32_t locals = 0;
		int64_t pushed = 0;
		int64_t deepest = 0;
		bool has_frame = false;
		int32_t variadic_from = -1;
		int32_t va_var = -1;
		int64_t frame_at = 0;

		int32_t make ( op kind, uint8_t size, int32_t a = -1, int32_t b = -1, int64_t value = 0, uint8_t bits = 0 );

		void deepen ( int32_t e );

		int32_t constant ( int64_t value, uint8_t size );

		int32_t binary ( op kind, int32_t a, int32_t b, uint8_t size, uint8_t bits = 0 );

		int32_t fold_arithmetic ( op kind, int32_t a, int32_t b, uint8_t size );

		bool same ( int32_t x, int32_t y ) const;

		int32_t cast ( int32_t a, uint8_t size, uint8_t bits );

		int32_t negate ( int32_t condition );

		int32_t var_expr ( int32_t index, uint8_t size );

		int32_t register_var ( int32_t family );

		int32_t stack_var ( int64_t offset, uint8_t size );

		bool contains_load ( int32_t e ) const;

		bool references ( int32_t e, int32_t var, int32_t skip ) const;

		int32_t replace ( int32_t e, int32_t from, int32_t to );

		bool float_return = false;

		void propagate ( );

		void prune ( );

		void fold_returns ( );

		void reindex_edges ( );

		void merge_conditions ( );

		void sink_into_tests ( );

		void infer_signs ( );

		void guard_expressions ( );

		void order_compares ( );

		void fold_divisions ( );

		void split_divisions ( );

		void fold_switches ( );

		void chain_switches ( );

		bool api_result_type ( );

		base::text result_text;

		void split_webs ( );

		void coalesce_copies ( );

		void coalesce_selects ( );

		void narrow_webs ( );

		void narrow_byte_params ( uint8_t width );

		void narrow_floats ( );

		void substitute_known ( );

		void fold_constants ( );

		void zero_edges ( );

		void global_reloads ( );

		void reuse_condition_loads ( );

		void reuse_store_loads ( );

		void retest_loads ( );

		void inline_pre_increments ( );

		void shared_exit ( );

		void scale_counters ( );

		base::list < int32_t > counter_bases;

		bool counter_load ( int32_t e, int64_t v, int32_t& global, int32_t& offset ) const;

		void counter_walk ( int32_t e, counter_scan& scan, int32_t depth ) const;

		int32_t counter_rewrite ( int32_t e, int64_t v, int32_t element, int32_t depth );
		void propagate_register_constants ( );

		void inline_temps ( );

		void merge_stack_slots ( );

		void inner_addresses ( );

		void detect_variadic ( );

		base::list < base::text > call_names;
		base::list < base::text > call_types;
		base::list < int32_t > call_counts;

		bool lift ( );

	};

	using x86::mnemonic;

	int64_t truncate ( int64_t value, uint8_t size );

	bool same_tree ( const builder& k, int32_t x, int32_t y, int32_t depth );

}