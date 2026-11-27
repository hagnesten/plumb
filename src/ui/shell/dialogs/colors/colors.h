#pragma once

#include "ui/core/app/app.h"

namespace ui {

	extern const tone choices [ ];

	extern const char* choice_keys [ ];

	extern const wchar_t* choice_labels [ ];

	constexpr int32_t choice_count = 37;

	extern const tone listing_items [ ];

	extern const tone code_items [ ];

	extern const tone interface_items [ ];

	extern const sample_token listing_sample [ ];

	extern const sample_token code_sample [ ];

	int32_t tone_choice_count ( );

	tone tone_choice ( int32_t index );

	const char* tone_choice_key ( int32_t index );

	const wchar_t* tone_choice_label ( int32_t index );

	const wchar_t* label_of ( tone value );

	void put_hex ( field& target, uint32_t value );

}