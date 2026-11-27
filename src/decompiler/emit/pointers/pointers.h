#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	struct pointer_access {

		int32_t var;
		int64_t offset;
		uint8_t form;
		bool indexed;

	};

	void flow_pointers ( builder& b, base::list < uint8_t >& element );

	void flavor_elements ( builder& b, base::list < uint8_t >& element );

	void typed_pointers ( builder& b, base::list < uint8_t >& element );

}