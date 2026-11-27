#pragma once

#include "base/types/types.h"
#include "loader/binary/binary.h"

namespace decompiler {

	bool char_literal ( int64_t value, wchar_t* out, int32_t room );
	bool string_literal ( const loader::binary& file, uint64_t address, wchar_t* out, int32_t room );

}