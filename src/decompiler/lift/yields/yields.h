#pragma once

#include "base/types/types.h"
#include "loader/binary/binary.h"
#include "analysis/database/database.h"
#include "x86/instruction/instruction.h"
#include "decompiler/namer/namer.h"

namespace decompiler {

	int32_t internal_arguments ( const analysis::database& db, uint64_t function, bool long_mode, int32_t depth );

	int64_t argument_registers ( const analysis::database& db, uint64_t function );

	int32_t stack_arguments ( const analysis::database& db, uint64_t function );

	bool callee_parameter_type ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, int32_t index, wchar_t* out, int32_t room );

	bool stack_probe ( const analysis::database& db, const x86::instruction& ins );

	bool callee_void_result ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target, int32_t depth );

	int32_t callee_argument_width ( const analysis::database& db, uint64_t function, int32_t index, int32_t depth );

	uint8_t callee_pointer_shape ( const analysis::database& db, namer* names, uint64_t function, int32_t index, int32_t depth );

	int32_t yields_value ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, bool long_mode, int32_t depth );

	using x86::mnemonic;

	extern bool string_params;

	bool starts_with ( const wchar_t* text, const wchar_t* head );

	int32_t declared_width ( const wchar_t* text );

	int32_t known_result ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target );

	uint64_t call_target ( const x86::instruction& ins );

	int32_t callee_width ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target, bool long_mode, int32_t depth );

	bool zeroing ( const x86::instruction& ins );

	int32_t def_width ( const x86::instruction& ins, bool zeroed );

	bool void_pointer_text ( const wchar_t* text );

	int64_t entry_arguments ( const analysis::database& db, uint64_t function, bool long_mode, int32_t depth, bool floats );

}