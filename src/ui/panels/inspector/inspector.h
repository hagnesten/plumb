#pragma once

#include "ui/core/app/app.h"
#include "base/writer/writer.h"

namespace ui {

	struct value_row {

		const wchar_t* name;
		int32_t bytes;

	};

	void float_text ( base::writer& out, double value );

}