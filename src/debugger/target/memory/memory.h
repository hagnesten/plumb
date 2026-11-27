#pragma once

#include "base/types/types.h"

namespace debugger {

	bool read_memory ( uint64_t address, void* out, size_t size );

	bool write_memory ( uint64_t address, const void* data, size_t size );

	bool write_byte ( uint64_t address, uint8_t value );

	bool peek ( uint64_t address, void* out, size_t size );

	bool poke ( uint64_t address, const void* data, size_t size );

}