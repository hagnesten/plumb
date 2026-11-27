#pragma once

#include "ui/scripting/bridge/bridge.h"
#include "base/list/list.h"

namespace ui {

	void push_pairs ( script_context& call, const base::list < user_name >& list );

}