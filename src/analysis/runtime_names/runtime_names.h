#pragma once

#include "base/types/types.h"

namespace analysis {

	const wchar_t* runtime_name ( uint64_t key );

	struct runtime_entry {

		uint64_t key;
		const wchar_t* name;

	};

	extern const runtime_entry runtime_table [ ];

}