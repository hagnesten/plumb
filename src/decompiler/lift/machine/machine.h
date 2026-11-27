#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/namer/namer.h"
#include "x86/printer/printer.h"
#include "base/list/list.h"

namespace decompiler {

	using x86::mnemonic;

	struct namer_bridge : x86::symbolizer {

		namer* target;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

			return target->name ( address, code, out, room );

		}

	};

	struct machine {

		builder* b = nullptr;
		int32_t regs [ 32 ];
		uint64_t reg_at [ 32 ] = { };
		uint8_t reads [ 32 ] = { };
		uint64_t read_at [ 32 ] = { };
		uint64_t now = 0;
		int32_t entry_flag_var = -1;
		uint8_t entry_flag_code = 0;
		int32_t flag_kind = 0;
		int32_t flag_result = -1;
		int32_t flag_a = -1;
		int32_t flag_b = -1;
		uint8_t flag_size = 4;
		bool flag_negated = false;
		int64_t delta = 0;
		bool frame = false;
		int64_t frame_delta = 0;

		void note_frame ( );
		int64_t probe_size = 0;
		uint32_t arg_written = 0;
		int32_t stack_args [ 16 ];
		int32_t stack_stmt [ 16 ];
		base::list < int32_t > pushes;
		base::list < int32_t > push_stmts;
		uint32_t live = 0;
		bool prologue = true;

		void reset ( );

		int32_t emit ( stmt kind, int32_t left, int32_t right, uint64_t address );

		uint32_t register_sources ( int32_t e, int32_t depth ) const;

		bool shared_vector ( int32_t value );

		bool real_product ( int32_t value );

		int32_t family_value ( int32_t slot );

		int32_t read_reg ( uint8_t reg );

		void write_reg ( uint8_t reg, int32_t value );

		bool stack_base ( uint8_t reg, int64_t& offset );

		bool stack_slot ( const x86::operand& m, int64_t& slot );

		bool holds_reg ( int32_t node, int32_t slot );

		int32_t held_sum ( const x86::operand& m );

		int32_t address_of ( const x86::instruction& ins, const x86::operand& m );

		int32_t read_memory ( const x86::instruction& ins, const x86::operand& m, uint8_t size, uint8_t bits );

		void note_field ( int32_t address, uint8_t size, uint8_t bits );

		bool aliases ( int32_t e, bool stack, int64_t target, uint8_t size, int32_t depth, bool outgoing = false ) const;

		void flush ( uint32_t keep, int32_t where = 0, int64_t target = 0, uint8_t size = 0 );

		uint32_t busy = 0;
		int32_t guard_a = -1;
		int32_t guard_b = -1;

		int32_t spill ( int32_t value, uint64_t address );

		int32_t find_same ( int32_t e, int32_t target, int32_t depth ) const;

		int32_t swap_same ( int32_t e, int32_t target, int32_t var );

		int32_t kept_source [ 32 ];
		int32_t kept_var [ 32 ];

		void forget_kept ( );

		int32_t recover ( int32_t e, int32_t var, int32_t skip );

		void protect ( int32_t var, int32_t skip, uint64_t address );

		int32_t shield ( int32_t guard, int32_t var, int32_t source, uint64_t address );

		void materialize ( int32_t slot, uint64_t address );

		void write_memory ( const x86::instruction& ins, const x86::operand& m, uint8_t size, int32_t value );

		int32_t read ( const x86::instruction& ins, int32_t index, uint8_t bits = 0 );

		void write ( const x86::instruction& ins, int32_t index, int32_t value );

		int32_t condition ( int32_t code );

		int32_t select_fold ( op kind, int32_t left, int32_t right, uint8_t size );

		void set_flags ( int32_t kind, int32_t a, int32_t c, uint8_t size );

		bool jumps_through_rax ( uint64_t address ) const;

		int32_t call_expr ( const x86::instruction& ins, uint32_t live_after );

		bool cookie_call ( int32_t call );

		void clobber ( );

		void unhandled ( const x86::instruction& ins, uint32_t def );

		int32_t intrinsic ( int32_t id, uint8_t size, int32_t a, int32_t c = -1, int32_t d = -1 );

		void step ( const x86::instruction& ins, uint32_t live_after, uint32_t def );

		bool packed ( const x86::instruction& ins );

	};

}