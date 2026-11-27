#pragma once

#include "ui/core/app/app.h"
#include "base/list/list.h"

namespace ui {

	int32_t find_entry ( const base::list < app::library_entry >& entries, uint64_t key );

}