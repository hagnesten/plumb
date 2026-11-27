#pragma once

#include "ui/core/app/app.h"

namespace ui {

	struct menu_spec {

		const wchar_t* title;
		command items [ 20 ];
		int32_t count;

	};

	extern const menu_spec menus [ ];

	extern const menu_spec code_menu;

	constexpr int32_t menu_count = 9;

	extern float menu_x [ menu_count ];

	extern float menu_w [ menu_count ];

}