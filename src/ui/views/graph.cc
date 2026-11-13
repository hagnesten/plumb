#include "ui/app.hh"
#include "ui/icons.hh"
#include "ui/tokens.hh"
#include "analysis/graph.hh"

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

	static base::list < graph_layout* > layouts;

	static void place_layers ( graph_layout& layout ) {

		const analysis::function_graph& graph = layout.graph;
		int32_t count = graph.blocks.count ( );
		base::list < uint8_t > color;
		base::list < uint8_t > back;
		base::list < int32_t > post;
		base::list < int32_t > stack;
		base::list < int32_t > cursor;

		color.fill ( static_cast < size_t > ( count ), 0 );
		back.fill ( graph.edges.size ( ), 0 );
		cursor.fill ( static_cast < size_t > ( count ), 0 );

		for ( int32_t root = 0; root < count; root++ ) {

			if ( color [ static_cast < size_t > ( root ) ] )
				continue;

			stack.push ( root );
			color [ static_cast < size_t > ( root ) ] = 1;

			while ( !stack.empty ( ) ) {

				int32_t v = stack.back ( );
				const analysis::block& item = graph.blocks [ static_cast < size_t > ( v ) ];
				int32_t& at = cursor [ static_cast < size_t > ( v ) ];

				if ( at < item.out_count ) {

					int32_t e = item.first_out + at;
					int32_t to = graph.edges [ static_cast < size_t > ( e ) ].to;

					at++;

					if ( color [ static_cast < size_t > ( to ) ] == 1 )
						back [ static_cast < size_t > ( e ) ] = 1;
					else if ( !color [ static_cast < size_t > ( to ) ] ) {

						color [ static_cast < size_t > ( to ) ] = 1;
						stack.push ( to );

					}

					continue;

				}

				color [ static_cast < size_t > ( v ) ] = 2;
				post.push ( v );
				stack.pop ( );

			}

		}

		for ( int32_t i = post.count ( ) - 1; i >= 0; i-- ) {

			int32_t v = post [ static_cast < size_t > ( i ) ];
			const analysis::block& item = graph.blocks [ static_cast < size_t > ( v ) ];

			for ( int32_t k = 0; k < item.out_count; k++ ) {

				int32_t e = item.first_out + k;

				if ( back [ static_cast < size_t > ( e ) ] )
					continue;

				int32_t to = graph.edges [ static_cast < size_t > ( e ) ].to;

				layout.nodes [ static_cast < size_t > ( to ) ].layer = base::max ( layout.nodes [ static_cast < size_t > ( to ) ].layer, layout.nodes [ static_cast < size_t > ( v ) ].layer + 1 );

			}

		}

		int32_t layers = 0;

		for ( const graph_node& node : layout.nodes )
			layers = base::max ( layers, node.layer + 1 );

		base::list < base::list < int32_t > > rows;

		rows.resize ( static_cast < size_t > ( layers ) );

		for ( int32_t i = post.count ( ) - 1; i >= 0; i-- ) {

			int32_t v = post [ static_cast < size_t > ( i ) ];

			rows [ static_cast < size_t > ( layout.nodes [ static_cast < size_t > ( v ) ].layer ) ].push ( v );

		}

		auto renumber = [ & ] ( ) {

			for ( base::list < int32_t >& row : rows ) {

				for ( int32_t i = 0; i < row.count ( ); i++ )
					layout.nodes [ static_cast < size_t > ( row [ static_cast < size_t > ( i ) ] ) ].order = i;

			}

		};

		renumber ( );

		base::list < float > keys;

		keys.fill ( static_cast < size_t > ( count ), 0.0f );

		for ( int32_t sweep = 0; sweep < 6; sweep++ ) {

			bool down = ( sweep & 1 ) == 0;

			for ( int32_t step = 0; step < layers; step++ ) {

				int32_t l = down ? step : layers - 1 - step;
				base::list < int32_t >& row = rows [ static_cast < size_t > ( l ) ];

				for ( int32_t v : row ) {

					float sum = 0.0f;
					int32_t n = 0;

					for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

						const analysis::edge& link = graph.edges [ static_cast < size_t > ( e ) ];

						if ( back [ static_cast < size_t > ( e ) ] )
							continue;

						if ( down && link.to == v && layout.nodes [ static_cast < size_t > ( link.from ) ].layer < l ) {

							sum += static_cast < float > ( layout.nodes [ static_cast < size_t > ( link.from ) ].order );
							n++;

						} else if ( !down && link.from == v && layout.nodes [ static_cast < size_t > ( link.to ) ].layer > l ) {

							sum += static_cast < float > ( layout.nodes [ static_cast < size_t > ( link.to ) ].order );
							n++;

						}

					}

					keys [ static_cast < size_t > ( v ) ] = n ? sum / static_cast < float > ( n ) : static_cast < float > ( layout.nodes [ static_cast < size_t > ( v ) ].order );

				}

				base::sort ( row.data ( ), row.count ( ), [ & ] ( int32_t a, int32_t b ) { return keys [ static_cast < size_t > ( a ) ] < keys [ static_cast < size_t > ( b ) ]; } );

				for ( int32_t i = 0; i < row.count ( ); i++ )
					layout.nodes [ static_cast < size_t > ( row [ static_cast < size_t > ( i ) ] ) ].order = i;

			}

		}

		const float gap_x = 30.0f;
		const float gap_y = 54.0f;
		float y = 0.0f;

		for ( int32_t l = 0; l < layers; l++ ) {

			base::list < int32_t >& row = rows [ static_cast < size_t > ( l ) ];
			float x = 0.0f;
			float tallest = 0.0f;

			for ( int32_t v : row ) {

				graph_node& node = layout.nodes [ static_cast < size_t > ( v ) ];

				node.x = x;
				node.y = y;
				x += node.w + gap_x;
				tallest = base::max ( tallest, node.h );

			}

			float shift = -( x - gap_x ) * 0.5f;

			for ( int32_t v : row )
				layout.nodes [ static_cast < size_t > ( v ) ].x += shift;

			y += tallest + gap_y;

		}

		for ( int32_t pass = 0; pass < 8; pass++ ) {

			bool down = ( pass & 1 ) == 0;

			for ( int32_t step = 0; step < layers; step++ ) {

				int32_t l = down ? step : layers - 1 - step;
				base::list < int32_t >& row = rows [ static_cast < size_t > ( l ) ];
				base::list < float > wanted;

				wanted.fill ( row.size ( ), 0.0f );

				float drift = 0.0f;
				int32_t pulled = 0;

				for ( int32_t i = 0; i < row.count ( ); i++ ) {

					int32_t v = row [ static_cast < size_t > ( i ) ];
					graph_node& node = layout.nodes [ static_cast < size_t > ( v ) ];
					float sum = 0.0f;
					int32_t n = 0;

					for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

						const analysis::edge& link = graph.edges [ static_cast < size_t > ( e ) ];

						if ( back [ static_cast < size_t > ( e ) ] )
							continue;

						int32_t other = down ? ( link.to == v ? link.from : -1 ) : ( link.from == v ? link.to : -1 );

						if ( other < 0 )
							continue;

						const graph_node& peer = layout.nodes [ static_cast < size_t > ( other ) ];

						sum += peer.x + peer.w * 0.5f;
						n++;

					}

					wanted [ static_cast < size_t > ( i ) ] = n ? sum / static_cast < float > ( n ) - node.w * 0.5f : node.x;

				}

				float limit = -1e30f;

				for ( int32_t i = 0; i < row.count ( ); i++ ) {

					graph_node& node = layout.nodes [ static_cast < size_t > ( row [ static_cast < size_t > ( i ) ] ) ];

					node.x = base::max ( wanted [ static_cast < size_t > ( i ) ], limit );
					limit = node.x + node.w + gap_x;

				}

				limit = 1e30f;

				for ( int32_t i = row.count ( ) - 1; i >= 0; i-- ) {

					graph_node& node = layout.nodes [ static_cast < size_t > ( row [ static_cast < size_t > ( i ) ] ) ];

					if ( node.x + node.w > limit )
						node.x = limit - node.w;

					drift += wanted [ static_cast < size_t > ( i ) ] - node.x;
					pulled++;
					limit = node.x - gap_x;

				}

				if ( pulled ) {

					float shift = drift / static_cast < float > ( pulled ) * 0.5f;

					for ( int32_t v : row )
						layout.nodes [ static_cast < size_t > ( v ) ].x += shift;

				}

			}

		}

		layout.left = 1e30f;
		layout.top = 1e30f;
		layout.right = -1e30f;
		layout.bottom = -1e30f;

		for ( const graph_node& node : layout.nodes ) {

			layout.left = base::min ( layout.left, node.x );
			layout.top = base::min ( layout.top, node.y );
			layout.right = base::max ( layout.right, node.x + node.w );
			layout.bottom = base::max ( layout.bottom, node.y + node.h );

		}

		base::list < int32_t > in_seen;
		base::list < int32_t > in_total;

		in_seen.fill ( static_cast < size_t > ( count ), 0 );
		in_total.fill ( static_cast < size_t > ( count ), 0 );

		for ( const analysis::edge& link : graph.edges )
			in_total [ static_cast < size_t > ( link.to ) ]++;

		int32_t lane = 0;

		for ( int32_t e = 0; e < graph.edges.count ( ); e++ ) {

			const analysis::edge& link = graph.edges [ static_cast < size_t > ( e ) ];
			const graph_node& from = layout.nodes [ static_cast < size_t > ( link.from ) ];
			const graph_node& to = layout.nodes [ static_cast < size_t > ( link.to ) ];
			const analysis::block& source = graph.blocks [ static_cast < size_t > ( link.from ) ];
			int32_t slot = e - source.first_out;
			float sx = from.x + from.w * static_cast < float > ( slot + 1 ) / static_cast < float > ( source.out_count + 1 );
			float sy = from.y + from.h;
			int32_t entry = in_seen [ static_cast < size_t > ( link.to ) ]++;
			float tx = to.x + to.w * static_cast < float > ( entry + 1 ) / static_cast < float > ( in_total [ static_cast < size_t > ( link.to ) ] + 1 );
			float ty = to.y;
			graph_wire wire;

			wire.first_point = layout.points.count ( );
			wire.kind = link.kind;
			wire.from = link.from;
			wire.to = link.to;

			if ( !back [ static_cast < size_t > ( e ) ] && to.layer > from.layer ) {

				float mid = ty - 16.0f - static_cast < float > ( entry % 4 ) * 6.0f;

				if ( to.layer > from.layer + 1 )
					mid = sy + 18.0f + static_cast < float > ( slot % 3 ) * 6.0f;

				layout.points.push ( math::vec2 ( sx, sy ) );
				layout.points.push ( math::vec2 ( sx, mid ) );
				layout.points.push ( math::vec2 ( tx, mid ) );
				layout.points.push ( math::vec2 ( tx, ty ) );

			} else {

				float edge_x = base::min ( from.x, to.x ) - 24.0f - static_cast < float > ( lane % 6 ) * 8.0f;

				for ( const graph_node& node : layout.nodes ) {

					if ( node.y + node.h >= to.y && node.y <= from.y + from.h )
						edge_x = base::min ( edge_x, node.x - 24.0f - static_cast < float > ( lane % 6 ) * 8.0f );

				}

				lane++;
				layout.left = base::min ( layout.left, edge_x - 10.0f );
				layout.points.push ( math::vec2 ( sx, sy ) );
				layout.points.push ( math::vec2 ( sx, sy + 14.0f ) );
				layout.points.push ( math::vec2 ( edge_x, sy + 14.0f ) );
				layout.points.push ( math::vec2 ( edge_x, ty - 14.0f ) );
				layout.points.push ( math::vec2 ( tx, ty - 14.0f ) );
				layout.points.push ( math::vec2 ( tx, ty ) );

			}

			wire.point_count = layout.points.count ( ) - wire.first_point;
			layout.wires.push ( wire );

		}

	}

	static graph_layout* find_layout ( app& owner, uint64_t function, render::canvas& c ) {

		uint64_t stamp = owner.db->decoded ^ ( static_cast < uint64_t > ( owner.db->functions.count ( ) ) << 32 );

		for ( int32_t i = 0; i < layouts.count ( ); i++ ) {

			graph_layout* item = layouts [ static_cast < size_t > ( i ) ];

			if ( item->function == function && item->owner == owner.db && item->stamp == stamp && item->revision == owner.names_revision )
				return item;

		}

		if ( layouts.count ( ) >= 8 ) {

			delete layouts [ 0 ];
			layouts.remove_at ( 0 );

		}

		graph_layout* made = new graph_layout ( );

		made->function = function;
		made->owner = owner.db;
		made->stamp = stamp;
		made->revision = owner.names_revision;
		made->adv = c.measure ( L"0000000000", render::font::MONO_SMALL ) * 0.1f;
		layouts.push ( made );

		if ( !analysis::build_graph ( *owner.db, function, made->graph ) )
			return made;

		listing_names namer;
		wchar_t name [ 200 ];

		namer.owner = &owner;

		for ( int32_t b = 0; b < made->graph.blocks.count ( ); b++ ) {

			const analysis::block& item = made->graph.blocks [ static_cast < size_t > ( b ) ];
			graph_node& node = made->nodes.emplace ( );
			int32_t widest = 12;

			node.first_line = made->lines.count ( );

			graph_line& head = made->lines.emplace ( );

			head.address = item.start;
			head.text = made->text.count ( );

			if ( !owner.symbol_name ( item.start, true, name, 200 ) )
				name [ 0 ] = 0;

			for ( int32_t i = 0; name [ i ]; i++ )
				made->text.push ( name [ i ] );

			head.length = made->text.count ( ) - head.text;
			head.split = head.length;
			widest = base::max ( widest, head.length + 6 );

			uint64_t at = item.start;

			while ( at < item.end && made->lines.count ( ) - node.first_line < 400 ) {

				x86::instruction ins;

				if ( !owner.db->decode ( at, ins ) )
					break;

				token_line line;

				line.owner = &owner;
				line.db = owner.db;
				x86::format ( ins, line, &namer, 0 );

				graph_line& row = made->lines.emplace ( );

				row.address = at;
				row.text = made->text.count ( );
				row.length = line.length;
				row.split = line.operand_start < 0 ? line.length : line.operand_start;
				row.first_run = made->runs.count ( );
				row.run_count = line.count;
				row.control = ins.control;

				for ( int32_t i = 0; i < line.length; i++ )
					made->text.push ( line.text [ i ] );

				for ( int32_t r = 0; r < line.count; r++ ) {

					token_run run = line.runs [ r ];

					if ( run.color == tone::MNEMONIC )
						run.color = mnemonic_tone ( ins );

					made->runs.push ( run );

				}

				int32_t operands = row.length - row.split;

				widest = base::max ( widest, base::max ( row.split, 7 ) + 1 + operands );
				at = ins.next ( );

			}

			node.line_count = made->lines.count ( ) - node.first_line;
			node.w = static_cast < float > ( base::min ( widest, 90 ) ) * made->adv + 22.0f;
			node.h = static_cast < float > ( node.line_count ) * made->line_h + 12.0f;

		}

		place_layers ( *made );

		return made;

	}

	void app::draw_graph ( math::rect area ) {

		render::canvas& c = canvas;

		c.fill ( area, 0.0f, ink ( tone::BASE ) );

		if ( !db || !analysis_applied ) {

			c.text ( L"Analyzing\x2026 the graph opens as soon as functions are known.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

			return;

		}

		if ( !now->graph_function ) {

			int32_t owner = db->function_at ( now->cursor_address );

			if ( owner < 0 ) {

				c.text ( L"The cursor isn't inside a function.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

				return;

			}

			now->graph_function = db->functions [ static_cast < size_t > ( owner ) ].start;

		}

		graph_layout* layout = find_layout ( *this, now->graph_function, c );

		if ( !layout || layout->nodes.empty ( ) ) {

			c.text ( L"No blocks to draw for this function.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

			return;

		}

		uint64_t id = make_id ( "graph", drawing_serial );
		widget& surface = state ( id );
		math::vec2 center = area.center ( );
		int32_t selected = layout->graph.block_at ( now->cursor_address );

		if ( selected < 0 )
			selected = 0;

		if ( !now->graph_ready || now->graph_block < 0 ) {

			const graph_node& node = layout->nodes [ static_cast < size_t > ( selected ) ];
			float fit = base::min ( area.w / ( layout->right - layout->left + 80.0f ), area.h / ( layout->bottom - layout->top + 80.0f ) );

			now->graph_tzoom = now->graph_ready ? now->graph_tzoom : ( fit >= 0.8f ? base::min ( fit, 1.0f ) : 1.0f );
			now->graph_tx = node.x + node.w * 0.5f;
			now->graph_ty = node.y + base::min ( node.h * 0.5f, area.h * 0.3f / now->graph_tzoom );

			if ( !now->graph_ready ) {

				now->graph_x = now->graph_tx;
				now->graph_y = now->graph_ty;
				now->graph_zoom = now->graph_tzoom;

			}

			now->graph_ready = true;
			now->graph_block = selected;

		}

		scroll_region ( id, area );
		interact ( id, area, surface );

		float wheel = take_wheel ( id );

		if ( wheel != 0.0f ) {

			if ( key_control ) {

				float before = now->graph_tzoom;
				float after = base::clamp ( before * math::exp ( wheel * 0.14f ), 0.12f, 2.5f );
				math::vec2 world ( now->graph_tx + ( mouse.x - center.x ) / before, now->graph_ty + ( mouse.y - center.y ) / before );

				now->graph_tzoom = after;
				now->graph_tx = world.x - ( mouse.x - center.x ) / after;
				now->graph_ty = world.y - ( mouse.y - center.y ) / after;

			} else if ( key_shift )
				now->graph_tx -= wheel * 70.0f / now->graph_tzoom;
			else
				now->graph_ty -= wheel * 70.0f / now->graph_tzoom;

		}

		auto to_world = [ & ] ( math::vec2 p ) {

			return math::vec2 ( now->graph_x + ( p.x - center.x ) / now->graph_zoom, now->graph_y + ( p.y - center.y ) / now->graph_zoom );

		};

		int32_t hit_node = -1;
		int32_t hit_line = -1;

		if ( over ( area ) ) {

			math::vec2 world = to_world ( mouse );

			for ( int32_t i = 0; i < layout->nodes.count ( ); i++ ) {

				const graph_node& node = layout->nodes [ static_cast < size_t > ( i ) ];

				if ( world.x >= node.x && world.x < node.x + node.w && world.y >= node.y && world.y < node.y + node.h ) {

					hit_node = i;
					hit_line = base::clamp ( static_cast < int32_t > ( ( world.y - node.y - 6.0f ) / layout->line_h ), 0, node.line_count - 1 );

				}

			}

		}

		if ( active_id == id && press_event && over ( area ) ) {

			if ( hit_node >= 0 ) {

				const graph_line& line = layout->lines [ static_cast < size_t > ( layout->nodes [ static_cast < size_t > ( hit_node ) ].first_line + hit_line ) ];

				now->cursor_address = line.address;
				now->graph_block = hit_node;

			} else {

				now->dragging = true;
				now->drag_from = mouse;
				now->drag_origin = math::vec2 ( now->graph_tx, now->graph_ty );

			}

		}

		if ( now->dragging ) {

			if ( mouse_down ) {

				now->graph_tx = now->drag_origin.x - ( mouse.x - now->drag_from.x ) / now->graph_zoom;
				now->graph_ty = now->drag_origin.y - ( mouse.y - now->drag_from.y ) / now->graph_zoom;
				now->graph_x = now->graph_tx;
				now->graph_y = now->graph_ty;
				wanted_cursor = pointer::HAND;

			} else
				now->dragging = false;

		}

		settle ( now->graph_x, now->graph_tx );
		settle ( now->graph_y, now->graph_ty );
		settle ( now->graph_zoom, now->graph_tzoom );

		float zoom = now->graph_zoom;
		uint64_t follow = 0;
		bool hand = false;

		c.push_clip ( area );
		c.push ( math::vec2 ( 0.0f, 0.0f ), zoom, 1.0f, math::vec2 ( center.x - now->graph_x * zoom, center.y - now->graph_y * zoom ) );
		c.scale_glyphs = true;

		float stroke = base::max ( 1.4f / zoom, 1.0f );

		for ( const graph_wire& wire : layout->wires ) {

			tone color = tone::MNEMONIC;

			if ( wire.kind == analysis::edge_kind::TAKEN )
				color = tone::SUCCESS;
			else if ( wire.kind == analysis::edge_kind::NOT_TAKEN )
				color = tone::DANGER;
			else if ( wire.kind == analysis::edge_kind::CASE )
				color = tone::CONTROL;

			bool lit = wire.from == now->graph_block || wire.to == now->graph_block;
			render::color tint = ink ( color ).fade ( lit ? 1.0f : 0.62f );
			float width = lit ? stroke * 1.6f : stroke;

			for ( int32_t p = 0; p + 1 < wire.point_count; p++ ) {

				math::vec2 a = layout->points [ static_cast < size_t > ( wire.first_point + p ) ];
				math::vec2 b = layout->points [ static_cast < size_t > ( wire.first_point + p + 1 ) ];
				math::rect segment ( base::min ( a.x, b.x ) - width * 0.5f, base::min ( a.y, b.y ) - width * 0.5f, math::abs ( b.x - a.x ) + width, math::abs ( b.y - a.y ) + width );

				c.fill ( segment, 0.0f, tint );

			}

			math::vec2 tip = layout->points [ static_cast < size_t > ( wire.first_point + wire.point_count - 1 ) ];

			c.polygon ( math::vec2 ( tip.x - 5.0f, tip.y - 8.0f ), math::vec2 ( tip.x + 5.0f, tip.y - 8.0f ), tip, tip, 0.0f, tint );

		}

		for ( int32_t i = 0; i < layout->nodes.count ( ); i++ ) {

			const graph_node& node = layout->nodes [ static_cast < size_t > ( i ) ];
			math::rect box ( node.x, node.y, node.w, node.h );
			bool chosen = i == now->graph_block;

			c.outline ( box, 2.0f, ink ( tone::PANEL ), chosen ? 1.5f : 1.0f, chosen ? ink ( tone::SELECT_LINE ) : ink ( tone::LINE_STRONG ) );
			c.fill ( math::rect ( node.x + 1.0f, node.y + 1.0f, node.w - 2.0f, layout->line_h + 4.0f ), 1.0f, ink ( tone::RAISED ) );
			c.fill ( math::rect ( node.x + 1.0f, node.y + layout->line_h + 5.0f, node.w - 2.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );

		}

		for ( int32_t i = 0; i < layout->nodes.count ( ); i++ ) {

			const graph_node& node = layout->nodes [ static_cast < size_t > ( i ) ];
			math::vec2 screen_top ( center.x + ( node.x - now->graph_x ) * zoom, center.y + ( node.y - now->graph_y ) * zoom );

			if ( screen_top.y > area.bottom ( ) || screen_top.y + node.h * zoom < area.y || screen_top.x > area.right ( ) || screen_top.x + node.w * zoom < area.x )
				continue;

			for ( int32_t l = 0; l < node.line_count; l++ ) {

				const graph_line& line = layout->lines [ static_cast < size_t > ( node.first_line + l ) ];
				float ly = node.y + 6.0f + static_cast < float > ( l ) * layout->line_h;
				float cy = ly + layout->line_h * 0.5f;
				float x0 = node.x + 11.0f;

				if ( l > 0 && line.address == now->cursor_address )
					c.fill ( math::rect ( node.x + 1.0f, ly, node.w - 2.0f, layout->line_h ), 0.0f, ink ( tone::SELECT ) );

				if ( l == 0 ) {

					wchar_t piece [ 200 ];
					int32_t length = base::min ( line.length, 199 );

					for ( int32_t k = 0; k < length; k++ )
						piece [ k ] = layout->text [ static_cast < size_t > ( line.text + k ) ];

					piece [ length ] = 0;

					base::writer where;

					where.hex ( line.address );

					float name_w = c.measure ( piece, render::font::MONO_SMALL );
					float where_w = c.measure ( where.text ( ), render::font::MONO_SMALL );
					float right = node.x + node.w - 10.0f;

					c.text ( piece, render::font::MONO_SMALL, math::vec2 ( x0, cy ), ink ( i == 0 ? tone::FUNCTION : tone::LABEL ), render::align::LEFT, right - x0 );

					if ( x0 + name_w + 12.0f + where_w <= right )
						c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( right, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT );

					continue;

				}

				for ( int32_t r = 0; r < line.run_count; r++ ) {

					const token_run& run = layout->runs [ static_cast < size_t > ( line.first_run + r ) ];
					bool head = run.start < line.split;
					int32_t column = head ? run.start : base::max ( line.split, 7 ) + 1 + run.start - line.split;
					float rx = x0 + static_cast < float > ( column ) * layout->adv;
					wchar_t piece [ 200 ];
					int32_t length = base::min ( run.length, 199 );

					if ( rx > node.x + node.w - 8.0f )
						break;

					for ( int32_t k = 0; k < length; k++ )
						piece [ k ] = layout->text [ static_cast < size_t > ( line.text + run.start + k ) ];

					piece [ length ] = 0;
					c.text ( piece, render::font::MONO_SMALL, math::vec2 ( rx, cy ), ink ( run.color ), render::align::LEFT, node.x + node.w - 8.0f - rx );

					if ( run.target && i == hit_node && l == hit_line ) {

						math::vec2 world = to_world ( mouse );

						if ( world.x >= rx && world.x < rx + static_cast < float > ( length ) * layout->adv ) {

							hand = true;
							c.fill ( math::rect ( rx, cy + 6.0f, static_cast < float > ( length ) * layout->adv, 1.0f ), 0.0f, ink ( run.color ) );

							if ( double_id == id )
								follow = run.target;

						}

					}

				}

			}

		}

		c.scale_glyphs = false;
		c.pop ( );

		float map_w = base::min ( 180.0f, area.w * 0.22f );
		float span_w = layout->right - layout->left + 40.0f;
		float span_h = layout->bottom - layout->top + 40.0f;
		float map_scale = base::min ( base::min ( map_w / span_w, 150.0f / span_h ), 0.25f );
		math::rect map ( area.right ( ) - span_w * map_scale - 12.0f, area.bottom ( ) - span_h * map_scale - 12.0f, span_w * map_scale, span_h * map_scale );
		bool overflowing = span_w * zoom > area.w || span_h * zoom > area.h;

		if ( overflowing && map.w > 12.0f && map.h > 20.0f && area.w > 420.0f ) {

			uint64_t map_id = make_id ( "graph_map", drawing_serial );
			widget& map_state = state ( map_id );

			interact ( map_id, map, map_state );
			c.outline ( map, 2.0f, ink ( tone::PANEL ), 1.0f, ink ( tone::LINE_STRONG ) );

			for ( int32_t i = 0; i < layout->nodes.count ( ); i++ ) {

				const graph_node& node = layout->nodes [ static_cast < size_t > ( i ) ];

				c.fill ( math::rect ( map.x + ( node.x - layout->left + 20.0f ) * map_scale, map.y + ( node.y - layout->top + 20.0f ) * map_scale, base::max ( node.w * map_scale, 1.5f ), base::max ( node.h * map_scale, 1.5f ) ), 0.0f, i == now->graph_block ? ink ( tone::ACCENT ) : ink ( tone::INK_FAINT ).fade ( 0.6f ) );

			}

			float view_w = area.w / zoom;
			float view_h = area.h / zoom;
			math::rect frame ( map.x + ( now->graph_x - view_w * 0.5f - layout->left + 20.0f ) * map_scale, map.y + ( now->graph_y - view_h * 0.5f - layout->top + 20.0f ) * map_scale, view_w * map_scale, view_h * map_scale );

			c.push_clip ( map );
			c.outline ( frame, 0.0f, ink ( tone::ACCENT ).fade ( 0.06f ), 1.0f, ink ( tone::ACCENT ).fade ( 0.8f ) );
			c.pop_clip ( );

			if ( active_id == map_id && mouse_down ) {

				now->graph_tx = layout->left - 20.0f + ( mouse.x - map.x ) / map_scale;
				now->graph_ty = layout->top - 20.0f + ( mouse.y - map.y ) / map_scale;
				now->graph_x = now->graph_tx;
				now->graph_y = now->graph_ty;

			}

		}

		base::writer status;

		status.number ( layout->nodes.count ( ) ).add ( layout->nodes.count ( ) == 1 ? L" block" : L" blocks" ).add ( L"  \x00B7  " ).number ( static_cast < int64_t > ( zoom * 100.0f + 0.5f ) ).add ( L"%  \x00B7  Space for text, Ctrl+wheel to zoom" );
		c.text ( status.text ( ), render::font::SMALL, math::vec2 ( area.x + 14.0f, area.bottom ( ) - 14.0f ), ink ( tone::INK_FAINT ) );
		c.pop_clip ( );

		if ( hand )
			wanted_cursor = pointer::HAND;

		if ( follow ) {

			int32_t inside = layout->graph.block_at ( follow );

			if ( inside >= 0 && layout->graph.blocks [ static_cast < size_t > ( inside ) ].start == follow ) {

				const graph_node& node = layout->nodes [ static_cast < size_t > ( inside ) ];

				now->back_stack.push ( now->cursor_address );
				now->cursor_address = follow;
				now->graph_block = inside;
				now->graph_tx = node.x + node.w * 0.5f;
				now->graph_ty = node.y + base::min ( node.h * 0.5f, area.h * 0.3f / zoom );

			} else
				go ( follow, true );

		}

	}

}