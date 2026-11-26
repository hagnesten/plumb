#pragma once

#include "script/engine/engine.h"

namespace script {

	extern const wchar_t* const reserved [ ];

	bool reserved_word ( const wchar_t* word );

	bool assignment_mark ( symbol mark );

	bool level_has ( int32_t level, symbol mark );

}