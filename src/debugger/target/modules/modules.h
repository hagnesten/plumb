#pragma once

#include "base/list/list.h"

namespace debugger {

	struct module_info {

		uint64_t base = 0;
		uint64_t size = 0;
		wchar_t path [ 300 ] = { };

	};

	void path_of ( void* file, wchar_t* out, int32_t room );

	const wchar_t* leaf_of ( const wchar_t* path );

	uint64_t image_size ( uint64_t base );

	void add_module ( uint64_t base, void* file );

	void module_ready ( uint64_t base );

	bool matches_module ( const wchar_t* path );

	int32_t modules ( base::list < module_info >& out );

}