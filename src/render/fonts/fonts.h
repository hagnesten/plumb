#pragma once

#include "base/list/list.h"
#include "base/math/math.h"
#include "render/colors/colors.h"

struct IDWriteTextLayout;

namespace render {

	enum class font : int32_t {

		UI,
		UI_STRONG,
		SMALL,
		SMALL_STRONG,
		TINY,
		HEADING,
		TITLE,
		DISPLAY,
		MONO,
		MONO_STRONG,
		MONO_SMALL,
		BRAND,
		CODE,
		COUNT,

	};
	enum class align : int32_t {

		LEFT,
		CENTER,
		RIGHT,

	};
	struct text_entry {

		uint64_t key = 0;
		IDWriteTextLayout* layout = nullptr;
		float width = 0.0f;
		float height = 0.0f;
		float baseline = 0.0f;
		float cap = 0.0f;
		uint32_t used = 0;

	};
	struct glyph_entry {

		uint32_t code = 0;
		uint16_t index = 0;
		float middle_x = 0.0f;
		float middle_y = 0.0f;

	};
	struct text_item {

		IDWriteTextLayout* layout;
		float x;
		float y;
		float scale;
		color tint;
		float clip [ 4 ];
		float bounds [ 4 ];

	};
	struct glyph_item {

		uint16_t index;
		float size;
		float x;
		float y;
		float scale;
		color tint;
		float clip [ 4 ];
		float bounds [ 4 ];

	};

}