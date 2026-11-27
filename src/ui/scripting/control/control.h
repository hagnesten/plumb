#pragma once

#include "ui/scripting/bridge/bridge.h"

namespace ui {

	struct script_setting {

		const wchar_t* key;
		bool app::* member;
		bool code;

	};

	extern const script_setting script_settings [ ];

	extern const wchar_t* view_words [ ];

	const wchar_t* state_word ( debugger::status value );

	int32_t font_role ( const wchar_t* text );

	int32_t color_choice ( const wchar_t* text );

}