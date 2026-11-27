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

	extern bool sse_extras;

	extern bool find_variadic;

	extern bool merge_slots;

	extern bool no_phantom_stack;

	extern bool indexed_stack;

	extern bool narrow_lea;

	extern bool shared_products;

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

}