#pragma once

#include "ui/core/app/app.h"
#include "base/list/list.h"

namespace ui {

	void project_path ( const loader::binary& file, wchar_t* out, int32_t room );

	void put_line ( base::list < char >& out, const char* key, const wchar_t* value );

	int64_t read_integer ( const wchar_t* text );

}