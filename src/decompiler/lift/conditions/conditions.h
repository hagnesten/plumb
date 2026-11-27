#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	void count_vars ( const builder& b, int32_t e, base::list < int32_t >& uses, int32_t depth );

	int32_t substitute ( builder& b, int32_t e, int64_t var, int32_t value, int32_t depth );

	extern bool ghost_folds;

	extern bool ghost_exprs;

	extern bool calls_keep_exprs;

	extern bool ghost_call_copies;

	extern bool ghost_forwards;

	extern bool stack_param_copies;

	extern bool ghost_sinks;

	extern bool copy_fixed;

	extern bool copy_into;

	extern bool calls_forward;

	extern bool swap_copies;

	extern bool copy_local;

	extern bool expr_fixed;

	extern bool keep_destination;

	extern bool shared_results;

	extern bool intact_exprs;

	extern bool sink_past_moves;

	extern bool calls_intact;

	extern bool broken_keep;

	extern bool origin_numbers;

	bool has_call ( const builder& b, int32_t e, int32_t depth );

	bool register_only ( const builder& b, int32_t e, int32_t depth );

	int32_t occurrences ( const builder& b, int32_t e, int64_t var, int32_t depth );

	bool writes_between ( const builder& b, int32_t block, uint32_t mask, uint64_t from, uint64_t to );

	bool holds_through ( const builder& b, uint32_t mask, int32_t from_block, uint64_t from, int32_t to_block, uint64_t to );

	extern bool fold_pointers;

	extern bool fold_vector_zero;

	extern bool ghost_vector_zero;

	extern bool fold_addresses;

	extern bool copy_constants;

	extern bool zero_registers;

	extern bool zero_stack_slots;

	extern bool reload_selects;

	extern bool dead_stack_stores;

	extern int32_t switch_cases;

	extern bool ghost_addresses;

	extern bool keep_returned_constants;

	bool mentions ( const builder& b, int32_t e, int64_t var, int32_t depth );

	int32_t zero_fold ( builder& b, int32_t e, int32_t depth );

	extern bool sign_ops;

	extern bool sign_copies;

	extern bool widened_returns;

	extern bool inline_switches;

	extern bool sign_pointers;

	void note_signs ( const builder& b, int32_t e, base::list < uint8_t >& marks, int32_t depth );

	int32_t strip_casts ( const builder& b, int32_t e );

	extern bool assign_in_tests;

	extern bool comma_uses;

	bool bare_var ( const builder& b, int32_t e, int64_t v );

	int32_t explicit_test ( builder& b, int32_t e, int64_t v );

	int32_t comma_test ( builder& b, int32_t e, int64_t v, int32_t target, int32_t source );

	bool tests_directly ( const builder& b, int32_t e, int64_t v );

	extern bool guard_defs;

}