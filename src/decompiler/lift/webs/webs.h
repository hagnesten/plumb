#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	struct web_state {

		builder& b;
		int32_t var = 0;
		base::list < int32_t > defs;
		base::list < int32_t > parent;
		base::list < int32_t > use_expr;
		base::list < int32_t > use_def;
		base::list < int32_t > def_of_statement;
		base::list < uint8_t > spoils_at;
		int32_t words = 0;
		int32_t spoil = -1;
		uint64_t spoil_at = 0;
		uint64_t spoil_born = 0;

		explicit web_state ( builder& owner );

		int32_t find ( int32_t x );

		void join ( int32_t a, int32_t c );

		void uses_in ( int32_t e, const uint64_t* reaching, base::list < int32_t >& seen );

		bool defines ( const statement& line ) const;

		void run ( );

	};

	int32_t own_tree ( builder& b, int32_t e, base::list < uint8_t >& visited, int32_t limit, int32_t depth );

	extern bool float_copies;

	extern bool narrow_by_callee;

	extern bool narrow_copies;

	extern bool narrow_truncations;

	extern bool narrow_extra_args;

	extern bool narrow_pointer_bits;

	extern bool select_params;

	extern bool select_inside;

	extern bool joined_params;

	extern bool ghost_param_copies;

	extern bool ghost_float_copies;

	extern bool register_param_ghosts;

	extern bool preserved_ghosts;

	bool reads_var ( const builder& b, int32_t e, int64_t var, int32_t depth );

	bool read_later ( const builder& b, int64_t var, uint64_t after );

}