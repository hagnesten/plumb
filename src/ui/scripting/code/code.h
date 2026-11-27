#pragma once

#include "ui/scripting/bridge/bridge.h"
#include "analysis/graph/graph.h"

namespace ui {

	const wchar_t* flow_word ( x86::flow value );

	const wchar_t* edge_word ( analysis::edge_kind value );

	int32_t class_index ( const analysis::database& db, script_context& call, int32_t index );

	bool declared_type ( const decompiler::result& code, const decompiler::line& item, const decompiler::span& part, wchar_t* out, int32_t room );

}