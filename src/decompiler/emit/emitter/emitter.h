#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "base/list/list.h"

namespace decompiler {

	struct emitter {

		builder& b;
		sink out;
		bool dry = false;
		int32_t count = 0;
		base::list < base::list < int32_t > > succ;
		base::list < base::list < int32_t > > pred;
		base::list < int32_t > idom;
		base::list < int32_t > ipdom;
		base::list < int32_t > ipdom_cut;
		base::list < uint8_t > cut;
		bool cutting = false;
		base::list < int32_t > rpo_index;
		base::list < uint8_t > header;
		base::list < base::list < uint8_t > > loop_body;
		base::list < uint8_t > emitted;
		base::list < uint8_t > wanted_label;
		base::list < uint8_t > labeled;
		base::list < int32_t > var_number;
		base::list < uint8_t > referenced;
		int32_t next_number = 0;
		int32_t result_var = -1;
		int32_t si128_var = -1;
		base::list < uint8_t > zero_known;
		bool inline_mode = false;
		bool ended = false;
		bool bare_unsigned = false;
		bool vector_store = false;
		bool truth = false;
		bool in_logic = false;
		bool untruncate = false;
		bool word_store = false;
		bool divisor_suffix = false;
		uint8_t holder_size = 0;
		op holder_kind = op::CONST;
		int32_t forced_exit = -1;
		int32_t forced_outer = -1;
		int32_t nest_depth = 0;
		base::list < int32_t > array_of;
		base::list < int32_t > array_step;
		base::list < int32_t > array_count;
		base::list < int32_t > fold_at;
		base::list < int8_t > fold_how;
		base::list < int32_t > fold_cond;
		base::list < int32_t > fold_cond_var;
		base::list < int8_t > fold_cond_how;
		int32_t active_var = -1;
		int8_t active_how = 0;
		int64_t active_unit = 1;
		base::list < int64_t > fold_unit;
		base::list < int64_t > fold_cond_unit;
		int32_t active_slot = -1;
		bool active_cond = false;
		base::list < int32_t > fold_inc;
		base::list < uint8_t > fold_done;
		base::list < int32_t > fold_cond_inc;
		base::list < uint8_t > fold_cond_done;
		bool explicit_zero = false;
		base::list < uint8_t > hidden;
		base::list < uint8_t > element;
		base::list < uint8_t > boolean;
		base::list < uint8_t > vector;
		bool chain_falls = false;
		base::list < int32_t > carried;
		bool inline_returns = true;
		bool call_unused = false;
		bool fall_returns = true;
		bool step_needs_init = false;
		bool by_address = true;
		bool indexing = true;
		wchar_t param_type [ 64 ] = { };
		bool defer_joins = true;
		bool rotate_guards = false;
		base::list < int8_t > loop_letter;
		int32_t next_letter = 0;
		int32_t last_assign = -1;
		int32_t last_assign_line = -1;
		base::list < int32_t > recent_assign;
		base::list < int32_t > recent_line;

		bool find_init ( int64_t var, int32_t& found, int32_t& line );
		bool operand = false;
		bool no_casts = false;
		base::list < uint64_t > number_addresses;
		base::list < int64_t > number_values;
		int32_t last_occurrence = 0;

		int32_t gotos = 0;
		int32_t depth = 0;
		bool no_ternary = false;
		int32_t next_temp = 1;
		base::list < int32_t > hoisted;
		base::list < int32_t > hoisted_temp;

		explicit emitter ( builder& owner );

		void hex ( uint64_t value, wchar_t* out_text );

		bool renumber ( );

		void var_name ( int32_t index, wchar_t* text );

		void default_name ( int32_t index, wchar_t* text );

		bool has_struct ( int32_t var );

		int32_t lane_var ( int32_t e ) const;

		bool part_macro ( int32_t e );

		bool zero_widened ( int32_t e ) const;

		int32_t wide_reads ( int32_t e, int64_t var, int32_t depth ) const;

		bool upper_dead ( int32_t at, int64_t var ) const;

		bool low_write ( int32_t left, int32_t right, int32_t at );

		const field_use* field_of ( int32_t var, int64_t offset );

		void type_name ( uint8_t size, uint8_t bits, bool pointer, bool memory = false );

		void name_after_type ( );

		bool abs_mask ( int32_t e, bool doubles ) const;

		void vector_load ( int32_t address, uint8_t flavor );

		void var_type ( int32_t index );

		static int32_t precedence ( op kind );

		uint64_t shape_target = 0;
		bool shape_ok = false;
		c_prototype* shape = nullptr;
		uint64_t derived_target = 0;
		bool derived_ok = false;
		c_prototype* derived_shape = nullptr;

		~emitter ( );

		const wchar_t* derived_param ( uint64_t target, int32_t index, bool& pointer );

		bool var_type_text ( int32_t index, wchar_t* text, int32_t room );

		bool pointer_arg_cast ( uint64_t target, int32_t index, int32_t argument, wchar_t* cast, int32_t room );

		bool derived_same ( uint64_t target, int32_t index, int32_t v );

		bool callee_unsigned ( uint64_t target, int32_t index );

		bool no_return ( const expr& call );

		bool callee_wide ( uint64_t target, int32_t index );

		void number ( int64_t value, uint8_t size, bool hex_form, bool is_unsigned );

		void put_number ( const wchar_t* text, int64_t value );

		int32_t next_occurrence ( int64_t value );

		void real ( int64_t bits, uint8_t size );

		bool simple_constant ( int64_t value );

		void pointer_constant ( uint64_t address );

		bool put_folded ( int32_t var, const wchar_t* name );

		int64_t var_step ( int32_t v ) const;

		bool char_typed_sum ( int32_t e ) const;

		int32_t count_var ( int32_t e, int64_t v, int32_t depth ) const;

		bool count_call ( int32_t e, int32_t depth ) const;

		void plan_increments ( );

		bool unfold_missed ( );

		bool array_element ( int32_t v, uint8_t size, uint8_t bits, bool address );

		int32_t bit_operand ( int32_t e ) const;

		bool logical_pair ( const expr& item ) const;

		void select_arm ( int32_t left, int32_t arm );

		bool slot_part ( int32_t e ) const;

		void index_number ( int64_t amount );

		int32_t pointer_step ( int32_t e ) const;

		bool void_element_load ( int32_t e ) const;

		bool typed_value ( int32_t e ) const;

		void pointer_cast ( int32_t v, bool twice = false );

		int32_t char_var ( int32_t e ) const;

		bool char_terms ( int32_t e, int32_t& var, int32_t& index, int64_t& offset ) const;

		bool char_load ( const expr& item, int32_t parent );

		bool global_index ( int32_t address, int32_t size, int32_t& index, uint64_t& start ) const;

		base::list < global_access > anchors;
		base::list < int32_t > scale_defs;
		int32_t printing_line = -1;
		bool anchors_ready = false;

		bool split_global ( int32_t address, global_access& out_access ) const;

		bool pointer_slot ( uint64_t address ) const;

		int32_t bare_index ( int32_t e ) const;

		void collect_anchors ( );

		bool anchored_access ( const expr& item, int32_t parent );

		void global_element ( uint64_t start, int32_t index );

		bool indexed_element ( const expr& item );

		void lvalue_or_load ( const expr& item, int32_t parent );

		void extended_load ( const expr& item, int32_t parent, uint8_t sign );

		void value ( int32_t index, int32_t parent, bool hex_hint = false );

		void value_body ( int32_t index, int32_t parent, bool hex_hint );

		bool float_target ( int32_t left );

		void collect_ternaries ( int32_t index, base::list < int32_t >& found, bool top );

		bool hoist ( int32_t index, uint64_t address );

		void assign_target ( int32_t left );

		void branch_open ( int32_t test, uint64_t address );

		void branch_else ( uint64_t address );

		void branch_close ( );

		bool split_ternary ( int32_t chosen, int32_t& test, int32_t& yes, int32_t& no );

		void branch_assign ( int32_t left, int32_t chosen, uint64_t address, int32_t temp = 0 );

		void branch_return ( int32_t chosen, uint64_t address );

		void semicolon ( );

		int32_t statement_index ( const statement& item ) const;

		void inline_statement ( int32_t index );

		void statement_line ( const statement& item );

		void emit_line ( uint64_t address, const wchar_t* text, syntax kind );

		void label ( int32_t block );

		void go_to ( int32_t block, uint64_t address );

		int32_t label_number ( int32_t block );

		base::list < int32_t > label_extra;

		bool inline_return ( int32_t block, uint64_t address );

		void body ( int32_t block );

		int32_t default_return ( int32_t block );

		bool empty_block ( int32_t block );

		void open_brace ( uint64_t address, const wchar_t* keyword, int32_t condition, bool negated );

		void close_brace ( );

		void brace ( const wchar_t* text );

		void paren_open ( );

		void paren_close ( );

		void else_line ( );

		void while_tail ( uint64_t address, int32_t condition );

		bool dead_exit ( ) const;

		void return_line ( const block_code& item );

		void jump_line ( const wchar_t* keyword, uint64_t address );

		struct frame {

			int32_t loop = -1;
			int32_t exit = -1;
			int32_t outer = -1;
			int32_t latch = -1;

		};

		bool in_loop ( int32_t loop, int32_t block );

		bool special ( int32_t target, int32_t stop, const frame& context, uint64_t address );

		void region ( int32_t block, int32_t stop, frame context );

		int32_t follow_of ( int32_t block, int32_t stop, const frame& context );

		int32_t loop_follow ( int32_t block, int32_t loop );

		bool silent_return ( int32_t block );

		static op swapped ( op kind );

		bool equivalent ( int32_t guard, int32_t test, int32_t var, int32_t seed, int32_t depth );

		int32_t sinkable_step ( int32_t latch, int32_t test );

		int32_t single_assign ( int32_t block, int32_t follow );

		int32_t counted_step ( int32_t latch, int32_t test );

		int32_t strip ( int32_t e ) const;

		bool guarded_entry ( int32_t start, int64_t init, op kind, int32_t bound );

		bool entry_true ( int32_t test, int32_t step, int32_t start = -1 );

		int32_t rotate ( int32_t block, int32_t entry, int32_t leave, int32_t guard_test, const frame& context );

		bool deferred ( int32_t target, int32_t from );

		bool branch_fed ( int32_t block ) const;

		bool negative_test ( int32_t condition ) const;

		int32_t branch ( int32_t block, int32_t stop, const frame& context, bool chained );

		int32_t region_size ( int32_t start, int32_t stop );

		int32_t loop_step ( int32_t start, const base::list < uint8_t >& members, int32_t condition );

		void name_loop_var ( int32_t var );

		bool usable_step ( int32_t step ) const;

		int32_t step_carrier ( int32_t step, int32_t latch ) const;

		bool open_for ( uint64_t address, int32_t condition, bool negated, int32_t step, bool need_init, int32_t carrier = -1 );

		int32_t loop ( int32_t start, const frame& outer );

		void region_header ( int32_t start, frame context );

		int32_t choice ( int32_t block, int32_t stop, const frame& context );

		void run ( );

		void analyze ( );

		bool dominates ( int32_t a, int32_t v );

		void postorder ( int32_t total, int32_t root, bool reverse, base::list < int32_t >& order );

		bool small_block ( int32_t v );

		bool exit_block ( int32_t v );

		void neighbors ( int32_t v, bool reverse, base::list < int32_t >& out_list );

		void incoming ( int32_t v, bool reverse, base::list < int32_t >& out_list );

		void dominate ( const base::list < int32_t >& rpo, bool reverse, base::list < int32_t >& result_list );

	};

}