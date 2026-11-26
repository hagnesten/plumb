#pragma once

#include "script/engine/engine.h"
#include "base/list/list.h"

namespace script {

	void put ( base::list < wchar_t >& out, const wchar_t* text, int32_t length = -1 );

	void put_digits ( base::list < wchar_t >& out, uint64_t value, int32_t radix, bool upper );

	wchar_t fold_upper ( wchar_t c );

	bool parse_number ( const wchar_t* text, int32_t length, int64_t radix, int64_t& out );

}