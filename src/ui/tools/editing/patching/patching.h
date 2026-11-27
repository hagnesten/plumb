#pragma once

#include "ui/core/app/app.h"
#include "x86/assembler/assembler.h"

namespace ui {

	struct assembler_names : x86::resolver {

		const app* owner = nullptr;

		bool lookup ( const wchar_t* name, uint64_t& value ) override {

			return owner->parse_address ( name, value );

		}

	};

}