#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/list/list.h"

namespace decompiler {

	void flow_pointers ( builder& b, base::list < uint8_t >& element );

	void flavor_elements ( builder& b, base::list < uint8_t >& element );

	void typed_pointers ( builder& b, base::list < uint8_t >& element );

}