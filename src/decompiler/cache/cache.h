#pragma once

#include "base/list/list.h"
#include "decompiler/output/output.h"

namespace decompiler {

	class cache {

	public:

		cache ( ) = default;

		cache ( const cache& ) = delete;

		cache& operator= ( const cache& ) = delete;

		~cache ( );

		result* find ( uint64_t function, uint32_t revision );

		result* make ( uint64_t function );

		void clear ( );

	private:

		base::list < result* > entries;

	};

}