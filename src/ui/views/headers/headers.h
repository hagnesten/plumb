#pragma once

#include "ui/core/app/app.h"
#include "base/writer/writer.h"

namespace ui {

	struct header_row {

		int32_t kind = 1;
		const wchar_t* name = L"";
		uint64_t offset = 0;
		uint32_t size = 0;
		wchar_t value [ 48 ] = { };
		wchar_t meaning [ 160 ] = { };

	};

	void describe_time ( uint32_t stamp, base::writer& out );

	void describe_flags ( uint32_t value, const uint32_t* bits, const wchar_t* const* labels, int32_t count, base::writer& out );

}