#pragma once

#include "ui/core/app/app.h"

namespace ui {

	bool read_clipboard ( void* window, wchar_t* out, int32_t room );

	bool word_char ( wchar_t value );

	void field_erase ( field& value, int32_t from, int32_t to );

	void field_insert ( field& value, const wchar_t* text );

}