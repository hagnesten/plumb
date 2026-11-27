#pragma once

#include "ui/core/app/app.h"

namespace ui {

	int32_t wrap_piece ( const wchar_t* text, int32_t left, int32_t columns, int32_t& skip );

	int32_t wrap_rows ( const wchar_t* text, int32_t length, int32_t columns );

}