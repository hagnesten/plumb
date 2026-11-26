#pragma once

#include "analysis/database/database.h"

namespace analysis {

	void demangle ( const char* mangled, wchar_t* out, int32_t room );

	bool read_name ( const loader::binary& file, uint64_t address, char* out, int32_t room );

}