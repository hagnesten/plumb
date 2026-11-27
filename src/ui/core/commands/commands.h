#pragma once

#include "ui/core/app/app.h"
#include "x86/printer/printer.h"

namespace ui {

	struct names_bridge : x86::symbolizer {

		const app* owner;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

			return owner->symbol_name ( address, code, out, room );

		}

	};

	struct command_info {

		const wchar_t* label;
		const wchar_t* keys;

	};

	extern const command_info commands [ ];

	const wchar_t* command_label ( command action );

	const wchar_t* command_keys ( command action );

}