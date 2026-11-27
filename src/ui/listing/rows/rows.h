#pragma once

#include "ui/core/app/app.h"
#include "base/list/list.h"

namespace ui {

	bool symbol_char ( wchar_t value );

	void push_slice ( base::list < wchar_t >& out, const wchar_t* text, int32_t length, int32_t from, int32_t to );

	int32_t step_caret ( const wchar_t* text, int32_t length, int32_t column, bool right, bool word );

	bool word_bounds ( const wchar_t* text, int32_t length, int32_t column, int32_t& from, int32_t& to );

	int32_t family_of_text ( const wchar_t* text, int32_t length );

}