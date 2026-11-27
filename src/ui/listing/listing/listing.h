#pragma once

#include "ui/core/app/app.h"
#include "base/list/list.h"

namespace ui {

	struct mark {

		uint64_t address;
		uint64_t size;
		int32_t kind;
		int32_t ref;

	};

	void sort_marks ( base::list < mark >& items );

	bool padding_value ( uint8_t value );

	bool content_kind ( line_kind kind );

}