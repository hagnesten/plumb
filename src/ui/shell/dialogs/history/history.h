#pragma once

#include "ui/core/app/app.h"
#include "base/list/list.h"

namespace ui {

	void split_lines ( const char* text, base::list < base::list < char > >& out );

	bool same_line ( const base::list < char >& a, const base::list < char >& b );

	bool contains_line ( const base::list < base::list < char > >& lines, const base::list < char >& line );

	void field_of ( const char* line, int32_t index, wchar_t* out, int32_t room, bool rest );

}