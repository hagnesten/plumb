#pragma once

#include "ui/scripting/bridge/bridge.h"

namespace ui {

	int32_t field_index ( const user_struct& owner, script_context& call, int32_t index );

	void push_field ( script_context& call, script::value list, const user_field& item );

}