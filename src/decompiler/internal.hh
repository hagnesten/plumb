#pragma once

#include "decompiler/decompiler.hh"
#include "analysis/graph.hh"

namespace decompiler {

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
		int32_t params = 0;
		bool has_prototype = false;
		c_prototype prototype;
		base::list < base::text > var_types;
		base::list < base::text > var_structs;
		uint8_t param_bits [ 16 ] = { };
		bool returns_value = false;
		bool bare_return = false;
		bool returns_float = false;
		uint8_t return_size = 0;
		int32_t locals = 0;
		int64_t pushed = 0;

		int32_t make ( op kind, uint8_t size, int32_t a = -1, int32_t b = -1, int64_t value = 0, uint8_t bits = 0 );

		void deepen ( int32_t e );

		int32_t constant ( int64_t value, uint8_t size );

		int32_t binary ( op kind, int32_t a, int32_t b, uint8_t size, uint8_t bits = 0 );

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

		void merge_conditions ( );

		void sink_into_tests ( );

		void split_webs ( );

		void coalesce_copies ( );

		void narrow_webs ( );

		void substitute_known ( );

		base::list < base::text > call_names;

		bool lift ( );

	};

	bool emit ( builder& b );

	const wchar_t* intrinsic_name ( int32_t id );

	int32_t import_arguments ( const wchar_t* name );

}