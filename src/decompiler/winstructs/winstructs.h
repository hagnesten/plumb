#pragma once

#include "base/types/types.h"

namespace decompiler {

	struct builtin_field {

		const wchar_t* owner;
		int32_t offset;
		int32_t size;
		int32_t count;
		const wchar_t* name;
		const wchar_t* type;
		const wchar_t* pointee;

	};

	const builtin_field* builtin_field_at ( const wchar_t* owner, int64_t offset, int32_t& index );

}
