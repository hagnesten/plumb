#pragma once

#include "analysis/database/database.h"

namespace analysis {

	enum class edge_kind : uint8_t {

		FALL,
		TAKEN,
		NOT_TAKEN,
		JUMP,
		CASE,

	};

	struct edge {

		int32_t from = 0;
		int32_t to = 0;
		edge_kind kind = edge_kind::FALL;
		int32_t value = 0;

	};

	struct block {

		uint64_t start = 0;
		uint64_t end = 0;
		uint64_t last = 0;
		int32_t instructions = 0;
		int32_t first_out = 0;
		int32_t out_count = 0;
		bool returns = false;
		bool leaves = false;

	};

	struct function_graph {

		uint64_t function = 0;
		base::list < block > blocks;
		base::list < edge > edges;
		int32_t table = -1;

		int32_t block_at ( uint64_t address ) const;

	};

	bool build_graph ( const database& db, uint64_t function, function_graph& out );

	struct pending_edge {

		uint64_t from;
		uint64_t to;
		edge_kind kind;
		int32_t value;

	};

}