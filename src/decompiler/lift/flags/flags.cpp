#include "decompiler/lift/flags/flags.h"

bool decompiler::late_divisions = true;

bool decompiler::shared_cmov_constants = true;

bool decompiler::store_loads = true;

bool decompiler::retested_loads = true;

bool decompiler::sext_narrowing = true;

bool decompiler::bittest_calls = true;

bool decompiler::ordered_cmov_loads = true;

bool decompiler::quiet_nops = true;

bool decompiler::held_sums = true;

bool decompiler::fold_scaled_shifts = true;

bool decompiler::negate_pairs = true;

bool decompiler::float_flips = true;

bool decompiler::joined_sets = true;

bool decompiler::joined_moves = true;

bool decompiler::select_loads = true;

bool decompiler::carried_flags = true;

bool decompiler::outgoing_args = true;

bool decompiler::float_calls = true;

bool decompiler::zero_paths = true;
bool decompiler::slot_escapes = true;
bool decompiler::global_calls = true;
bool decompiler::byte_params = true;
bool decompiler::byte_returns = true;

bool decompiler::sse_extras = true;

bool decompiler::find_variadic = true;

bool decompiler::merge_slots = true;

bool decompiler::no_phantom_stack = true;

bool decompiler::indexed_stack = true;

bool decompiler::narrow_lea = true;

bool decompiler::shared_products = true;

bool decompiler::param_copies = true;

bool decompiler::masked_signs = true;
bool decompiler::masked_casts = true;
bool decompiler::cookie_keeps_result = true;
bool decompiler::next_reloads = true;
bool decompiler::narrow_copy_groups = true;
bool decompiler::wide_zero_args = true;
bool decompiler::condition_loads = true;

bool decompiler::scaled_counters = true;

bool decompiler::import_callees = true;

bool decompiler::carry_ops = true;

bool decompiler::shared_vectors = true;

bool decompiler::shared_vector_reads = false;

bool decompiler::flush_by_definition = true;

bool decompiler::sized_regs = true;

bool decompiler::same_guards = true;

bool decompiler::shared_sources = true;

bool decompiler::carried_floats = true;

bool decompiler::filled_returns = true;

bool decompiler::block_writers = true;

bool decompiler::negated_carry = true;

bool decompiler::block_origins = true;

bool decompiler::result_numbers = true;

bool decompiler::pure_intrinsics = true;

bool decompiler::rescue_flags = true;

bool decompiler::scalar_packs = true;

bool decompiler::sized_shield = true;

bool decompiler::ghost_folds = false;
bool decompiler::ghost_exprs = false;
bool decompiler::calls_keep_exprs = true;
bool decompiler::ghost_call_copies = false;
bool decompiler::ghost_forwards = false;
bool decompiler::stack_param_copies = true;
bool decompiler::ghost_sinks = false;
bool decompiler::copy_fixed = true;
bool decompiler::copy_into = true;
bool decompiler::calls_forward = true;
bool decompiler::swap_copies = true;
bool decompiler::copy_local = true;
bool decompiler::expr_fixed = true;
bool decompiler::keep_destination = true;
bool decompiler::shared_results = true;
bool decompiler::intact_exprs = true;
bool decompiler::sink_past_moves = true;
bool decompiler::calls_intact = true;
bool decompiler::broken_keep = false;
bool decompiler::origin_numbers = true;
bool decompiler::fold_pointers = true;
bool decompiler::fold_vector_zero = true;
bool decompiler::ghost_vector_zero = false;
bool decompiler::fold_addresses = true;
bool decompiler::copy_constants = true;
bool decompiler::zero_registers = true;
bool decompiler::zero_stack_slots = true;
bool decompiler::reload_selects = true;
bool decompiler::dead_stack_stores = true;
int32_t decompiler::switch_cases = 3;
bool decompiler::ghost_addresses = false;
bool decompiler::keep_returned_constants = false;
bool decompiler::sign_ops = true;
bool decompiler::sign_copies = true;
bool decompiler::widened_returns = true;
bool decompiler::inline_switches = true;
bool decompiler::sign_pointers = true;
bool decompiler::assign_in_tests = true;
bool decompiler::comma_uses = true;
bool decompiler::guard_defs = true;