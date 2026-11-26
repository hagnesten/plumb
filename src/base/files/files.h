#pragma once

#include "base/list/list.h"
#include "base/text/text.h"

namespace files {

	const wchar_t* folder ( );

	bool read ( const wchar_t* path, base::list < char >& result );

	bool write ( const wchar_t* path, const char* data, int32_t size );

	void write_later ( const wchar_t* path, const char* data, int32_t size );

	void settle ( );

	extern base::text pending_path;

	extern base::list < char > pending_data;

	extern bool pending;

	void flush_pending ( );

}