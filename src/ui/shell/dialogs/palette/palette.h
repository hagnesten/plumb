#pragma once

#include "ui/core/app/app.h"
#include "base/list/list.h"

namespace ui {

	int32_t fuzzy_score ( const wchar_t* text, const wchar_t* query );

	void keep_best ( base::list < palette_entry >& items, const palette_entry& entry, int32_t room );

	int32_t name_score ( const wchar_t* text, const wchar_t* query, int32_t bonus );

}