#pragma once

#include "ui/scripting/bridge/bridge.h"
#include "base/list/list.h"

namespace ui {

	bool load_text_file ( const wchar_t* path, base::list < wchar_t >& out );

}