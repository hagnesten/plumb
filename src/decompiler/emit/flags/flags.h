#pragma once

#include "base/types/types.h"

namespace decompiler {

	extern bool logic_wrap;

	extern bool flow_casts;

	extern bool ghost_numbers;

	extern bool exits_inline;

	extern bool frame_deepest;

	extern bool implicit_narrow;

	extern bool split_selects;

	extern bool char_split;

	extern bool char_address;

	extern bool char_calls;

	extern bool vector_copies;

	extern bool byte_elements;

	extern bool void_needs_char;

	extern bool void_needs_char_params;

	extern bool return_exits;

	extern bool trim_crt;

	extern bool bare_callees;

	extern bool part_macros;

	extern bool simd_converts;

	extern bool ending_fall_first;

	extern bool call_pointers;

	extern bool unnamed_results;

	extern bool intrinsic_types;

	extern bool global_arrays;

	extern bool empty_then;

	extern bool api_types;

	extern bool double_pointees;

	extern bool low_writes;

	extern bool unsigned_bounds;

	extern bool anchored_globals;

	extern bool brace_before_else;

	extern bool power_hex;

	extern bool wide_negatives;

	extern bool void_equals;

	extern bool void_stores;

	extern bool derived_types;

	extern bool integer_stores;

	extern bool float_views;

	extern bool latch_continues;

	extern bool born_numbers;

	extern bool result_ghosts;

	extern bool expression_returns;

	extern bool plain_negatives;

	extern bool unsigned_results;

	extern bool step_carriers;

	extern bool default_cases;

	extern bool expand_strings;

	extern bool raw_float_copies;

	extern bool scalar_lanes;

	extern bool float_zero;

	extern bool chain_rest;

	extern bool defer_later;

	extern bool return_casts;

	extern bool return_var_casts;

	extern bool unsigned_narrow;

	extern bool bit_tests;

	extern bool base_first;

	extern bool array_vars;

	extern bool array_memsets;

	extern bool shared_returns;

	extern bool fold_increments;

	extern bool wrapped_unsigned;

	extern bool mask_forms;

	extern bool latch_folds;

	extern bool inferred_unsigned;

	extern bool widen_assign;

	extern bool sext_products;

	extern bool collapse_casts;

	extern bool call_untruncate;

	extern bool store_untruncate;

	extern bool nested_loops;

	extern bool simd_types;

	extern bool plain_copies;

	extern bool shift_products;

	extern bool fall_then;

	extern bool zero_ranges;

	extern bool array_min_step;

	extern bool hex_divisors;

	extern bool indexed_arrays;

	extern bool bare_tests;

	extern bool fall_then_simple;

	extern bool fall_then_signs;

	extern bool fall_returns_first;

	extern bool else_returns;

	extern bool keep_step_order;

	extern bool literal_floats;

	extern bool ida_reals;

	extern bool abs_masks;

	extern bool wide_internal_args;

	extern bool early_returns;

	extern bool widened_halves;

	extern bool void_arithmetic;

	extern bool diamond_ternary;

	extern bool guarded_loops;

	extern bool pass_typed;

	extern bool callee_shapes;

	extern bool element_flavors;

	extern bool typed_locals;

	extern bool pointer_compares;

	extern bool pointer_steps;

	extern bool char_locals;

	extern bool untype_passed;

	extern bool detect_bools;

	extern bool flow_typing;

	extern bool flow_seeded_only;

	extern bool flow_calls;

	extern bool flow_loads;

	extern bool flow_index;

	extern bool last_call_names;

}