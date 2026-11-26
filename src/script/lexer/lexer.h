#pragma once

#include "script/engine/engine.h"

namespace script {

	bool letter ( wchar_t c );

	bool digit ( wchar_t c );

	int32_t hex_digit ( wchar_t c );

	extern const symbol_spelling spellings [ ];

}