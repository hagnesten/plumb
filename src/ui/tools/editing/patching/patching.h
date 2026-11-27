#pragma once

#include "ui/core/app/app.h"
#include "x86/assembler/assembler.h"

namespace ui {

	struct stack_write {

		int64_t disp;
		int32_t size;
		uint64_t value;
		uint64_t address;
		int32_t base;

	};

	struct assembler_names : x86::resolver {

		const app* owner = nullptr;

		bool lookup ( const wchar_t* name, uint64_t& value ) override {

			return owner->parse_address ( name, value );

		}

	};

}