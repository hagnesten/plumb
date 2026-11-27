#pragma once

#include "ui/core/app/app.h"
#include "analysis/graph/graph.h"
#include "base/list/list.h"

namespace ui {

	struct graph_line {

		uint64_t address = 0;
		int32_t text = 0;
		int32_t length = 0;
		int32_t split = 0;
		int32_t first_run = 0;
		int32_t run_count = 0;
		x86::flow control = x86::flow::NONE;

	};

	struct graph_node {

		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;
		int32_t first_line = 0;
		int32_t line_count = 0;
		int32_t layer = 0;
		int32_t order = 0;
		int32_t in_index = 0;

	};

	struct graph_wire {

		int32_t first_point = 0;
		int32_t point_count = 0;
		analysis::edge_kind kind = analysis::edge_kind::FALL;
		int32_t from = 0;
		int32_t to = 0;

	};

	struct graph_layout {

		uint64_t function = 0;
		uint32_t revision = 0;
		const analysis::database* owner = nullptr;
		uint64_t stamp = 0;
		analysis::function_graph graph;
		base::list < graph_node > nodes;
		base::list < graph_line > lines;
		base::list < wchar_t > text;
		base::list < token_run > runs;
		base::list < math::vec2 > points;
		base::list < graph_wire > wires;
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
		float adv = 7.0f;
		float line_h = 17.0f;

	};

	extern base::list < graph_layout* > layouts;

	void place_layers ( graph_layout& layout );

	graph_layout* find_layout ( app& owner, uint64_t function, render::canvas& c );

}