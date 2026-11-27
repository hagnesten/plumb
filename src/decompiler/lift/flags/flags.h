#pragma once

#include "base/types/types.h"

namespace decompiler {

	extern bool late_divisions;

	extern bool quiet_nops;

	extern bool held_sums;

	extern bool fold_scaled_shifts;

	extern bool negate_pairs;

	extern bool float_flips;

	extern bool joined_sets;

	extern bool joined_moves;

	extern bool select_loads;

	extern bool carried_flags;

	extern bool outgoing_args;

	extern bool float_calls;

	extern bool zero_paths;

	extern bool slot_escapes;

	extern bool global_calls;

	extern bool byte_params;

	extern bool byte_returns;

	extern bool sse_extras;

	extern bool find_variadic;

	extern bool merge_slots;

	extern bool no_phantom_stack;

	extern bool indexed_stack;

	extern bool narrow_lea;

	extern bool shared_products;

	extern bool param_copies;

	extern bool masked_signs;

	extern bool masked_casts;

	extern bool cookie_keeps_result;

	extern bool next_reloads;

	extern bool narrow_copy_groups;

	extern bool scaled_counters;

	extern bool import_callees;

	extern bool carry_ops;

	extern bool shared_vectors;

	extern bool shared_vector_reads;

	extern bool flush_by_definition;

	extern bool sized_regs;

	extern bool same_guards;

	extern bool shared_sources;

	extern bool carried_floats;

	extern bool filled_returns;

	extern bool block_writers;

	extern bool negated_carry;

	extern bool block_origins;

	extern bool result_numbers;

	extern bool pure_intrinsics;

	extern bool rescue_flags;

	extern bool scalar_packs;

	extern bool sized_shield;

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
	extern bool sign_ops;
	extern bool sign_copies;
	extern bool widened_returns;
	extern bool inline_switches;
	extern bool sign_pointers;
	extern bool assign_in_tests;
	extern bool comma_uses;
	extern bool guard_defs;

}