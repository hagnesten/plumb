#pragma once

#include "ui/core/app/app.h"

namespace ui {

	struct command_info {

		const wchar_t* label;
		const wchar_t* keys;

	};

	extern const command_info commands [ ];

	const wchar_t* command_label ( command action );

	const wchar_t* command_keys ( command action );

}