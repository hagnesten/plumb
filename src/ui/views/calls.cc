#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	void app::build_calls ( uint64_t function ) {

		call_nodes.clear ( );
		calls_for = function;
		calls_revision = names_revision;
		calls_db = db;

		auto add = [ & ] ( uint64_t address, int32_t column, int32_t parent ) {

			int32_t used = 0;

			for ( const call_node& item : call_nodes ) {

				if ( item.column != column )
					continue;

				if ( item.address == address )
					return;

				used++;

			}

			if ( used >= ( column == -1 || column == 1 ? 16 : 20 ) )
				return;

			call_node& made = call_nodes.emplace ( );

			made.address = address;
			made.column = column;
			made.parent = parent;

		};

		auto callers = [ & ] ( uint64_t target, base::list < uint64_t >& out, int32_t limit ) {

			int32_t first = 0;
			int32_t count = 0;

			db->refs_to ( target, first, count );

			for ( int32_t i = 0; i < count && out.count ( ) < limit; i++ ) {

				const analysis::xref& ref = db->refs [ static_cast < size_t > ( first + i ) ];

				if ( ref.type != analysis::ref::CALL && ref.type != analysis::ref::JUMP )
					continue;

				int32_t owner = db->function_at ( file.base + ref.source );

				if ( owner < 0 )
					continue;

				uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;
				bool seen = start == target;

				for ( uint64_t item : out )
					seen = seen || item == start;

				if ( !seen )
					out.push ( start );

			}

		};

		auto callees = [ & ] ( uint64_t start, base::list < uint64_t >& out, int32_t limit ) {

			int32_t owner = db->function_starting ( start );

			if ( owner < 0 )
				return;

			uint64_t end = db->functions [ static_cast < size_t > ( owner ) ].end;
			uint64_t at = start;

			while ( at < end && out.count ( ) < limit ) {

				x86::instruction ins;

				if ( !db->is_start ( at ) || !db->decode ( at, ins ) ) {

					at++;

					continue;

				}

				at += ins.length;

				if ( ins.control != x86::flow::CALL && ins.control != x86::flow::JUMP )
					continue;

				uint64_t target = 0;
				bool slot = false;

				if ( ins.direct ( ) )
					target = ins.target ( );
				else if ( ins.memory_target ( target ) ) {

					for ( const loader::import& item : file.imports )
						slot = slot || item.slot == target;

					if ( !slot )
						continue;

				} else
					continue;

				if ( !slot && ( target == start || db->function_starting ( target ) < 0 ) )
					continue;

				if ( !slot && ins.control == x86::flow::JUMP && target >= start && target < end )
					continue;

				bool seen = false;

				for ( uint64_t item : out )
					seen = seen || item == target;

				if ( !seen )
					out.push ( target );

			}

		};

		add ( function, 0, -1 );

		base::list < uint64_t > found;

		callers ( function, found, 4096 );
		call_totals [ 0 ] = found.count ( );

		for ( uint64_t item : found )
			add ( item, -1, 0 );

		found.clear ( );
		callees ( function, found, 4096 );
		call_totals [ 1 ] = found.count ( );

		for ( uint64_t item : found )
			add ( item, 1, 0 );

		int32_t total = call_nodes.count ( );

		for ( int32_t i = 1; i < total; i++ ) {

			call_node node = call_nodes [ static_cast < size_t > ( i ) ];
			base::list < uint64_t > more;

			if ( node.column < 0 )
				callers ( node.address, more, 4 );
			else
				callees ( node.address, more, 4 );

			for ( uint64_t item : more ) {

				if ( item != function )
					add ( item, node.column * 2, i );

			}

		}

	}

	void app::draw_calls ( math::rect area ) {

		render::canvas& c = canvas;

		c.fill ( area, 0.0f, ink ( tone::BASE ) );

		if ( !db || !analysis_applied ) {

			c.text ( L"The call graph appears when analysis finishes.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

			return;

		}

		if ( db->function_starting ( now->function ) < 0 ) {

			int32_t owner = db->function_at ( now->function ? now->function : now->cursor_address );

			if ( owner < 0 ) {

				c.text ( L"The cursor isn't inside a function.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

				return;

			}

			now->function = db->functions [ static_cast < size_t > ( owner ) ].start;

		}

		if ( calls_for != now->function || calls_revision != names_revision || calls_db != db )
			build_calls ( now->function );

		uint64_t id = make_id ( "calls", drawing_serial );
		widget& surface = state ( id );
		float node_h = 30.0f;
		float spacing = 10.0f;
		float top = area.y + 58.0f;
		int32_t per_column [ 5 ] = { };

		for ( const call_node& item : call_nodes )
			per_column [ item.column + 2 ]++;

		int32_t low = per_column [ 0 ] ? 0 : 1;
		int32_t high = per_column [ 4 ] ? 4 : 3;
		float shown_columns = static_cast < float > ( high - low + 1 );
		float col_w = base::clamp ( ( area.w - 80.0f ) / shown_columns - 34.0f, 110.0f, 280.0f );
		float gap = base::clamp ( ( area.w - col_w * shown_columns - 40.0f ) / ( shown_columns - 1.0f ), 24.0f, 110.0f );
		float left = area.center ( ).x - ( col_w * shown_columns + gap * ( shown_columns - 1.0f ) ) * 0.5f - static_cast < float > ( low ) * ( col_w + gap );

		int32_t tallest = 0;

		for ( int32_t i = 0; i < 5; i++ )
			tallest = base::max ( tallest, per_column [ i ] );

		float content = static_cast < float > ( tallest ) * ( node_h + spacing ) + 40.0f;
		float room = area.h - 96.0f;

		scroll_region ( id, area );
		now->code_scroll.target -= take_wheel ( id ) * 60.0f;
		glide ( now->code_scroll, base::max ( content - room, 0.0f ), 1.0f );
		interact ( id, area, surface );

		float shift = now->code_scroll.shown;
		int32_t placed [ 5 ] = { };

		for ( call_node& item : call_nodes ) {

			int32_t slot = item.column + 2;
			float column_h = static_cast < float > ( per_column [ slot ] ) * ( node_h + spacing ) - spacing;
			float y0 = content > room ? top : top + ( room - column_h ) * 0.5f;

			item.box = math::rect ( left + static_cast < float > ( slot ) * ( col_w + gap ), y0 + static_cast < float > ( placed [ slot ]++ ) * ( node_h + spacing ) - shift, col_w, node_h );

		}

		int32_t hovered = -1;

		if ( over ( area ) && mouse.y > area.y + 40.0f && mouse.y < area.bottom ( ) - 34.0f ) {

			for ( int32_t i = 0; i < call_nodes.count ( ); i++ ) {

				math::rect box = call_nodes [ static_cast < size_t > ( i ) ].box;

				if ( mouse.x >= box.x && mouse.x < box.right ( ) && mouse.y >= box.y && mouse.y < box.bottom ( ) )
					hovered = i;

			}

		}

		if ( hovered >= 0 )
			wanted_cursor = pointer::HAND;

		if ( active_id == id && press_event && hovered >= 0 ) {

			uint64_t address = call_nodes [ static_cast < size_t > ( hovered ) ].address;

			now->cursor_address = address;

			if ( double_id == id && hovered > 0 && db->function_starting ( address ) >= 0 ) {

				now->back_stack.push ( now->function );
				now->forward_stack.clear ( );
				now->function = address;
				now->code_scroll = scroller ( );
				calls_for = ~0ull;
				pending = base::max ( pending, 3 );

				return;

			}

		}

		c.push_clip ( math::rect ( area.x, area.y + 40.0f, area.w, area.h - 74.0f ) );

		for ( int32_t i = 0; i < call_nodes.count ( ); i++ ) {

			const call_node& item = call_nodes [ static_cast < size_t > ( i ) ];

			if ( item.parent < 0 )
				continue;

			const call_node& other = call_nodes [ static_cast < size_t > ( item.parent ) ];
			const call_node& from = item.column < 0 ? item : other;
			const call_node& to = item.column < 0 ? other : item;
			math::vec2 a ( from.box.right ( ), from.box.center ( ).y );
			math::vec2 b ( to.box.x, to.box.center ( ).y );
			float middle = math::floor ( ( a.x + b.x ) * 0.5f );
			bool lit = i == hovered || item.parent == hovered || item.address == now->cursor_address;
			render::color tint = ink ( lit ? tone::ACCENT : tone::LINE_STRONG );
			float width = lit ? 1.6f : 1.0f;

			c.fill ( math::rect ( a.x, a.y - width * 0.5f, middle - a.x, width ), 0.0f, tint );
			c.fill ( math::rect ( middle - width * 0.5f, base::min ( a.y, b.y ) - width * 0.5f, width, math::abs ( b.y - a.y ) + width ), 0.0f, tint );
			c.fill ( math::rect ( middle, b.y - width * 0.5f, b.x - middle - 6.0f, width ), 0.0f, tint );
			c.polygon ( math::vec2 ( b.x - 7.0f, b.y - 4.0f ), math::vec2 ( b.x - 7.0f, b.y + 4.0f ), b, b, 0.0f, tint );

		}

		wchar_t name [ 200 ];

		for ( int32_t i = 0; i < call_nodes.count ( ); i++ ) {

			const call_node& item = call_nodes [ static_cast < size_t > ( i ) ];
			bool center = i == 0;
			bool chosen = item.address == now->cursor_address;
			int32_t owner = db->function_starting ( item.address );
			bool external = owner < 0 || ( db->functions [ static_cast < size_t > ( owner ) ].flags & analysis::THUNK );
			tone color = external ? tone::IMPORT : tone::FUNCTION;

			if ( !symbol_name ( item.address, true, name, 200 ) ) {

				base::writer fallback;

				fallback.add ( L"sub_" ).hex ( item.address );

				for ( int32_t k = 0; k <= fallback.size ( ) && k < 200; k++ )
					name [ k ] = fallback.text ( ) [ k ];

			}

			
			c.outline ( item.box, 5.0f, ink ( center ? tone::RAISED : tone::PANEL ), chosen || i == hovered ? 1.6f : 1.0f, chosen ? ink ( tone::SELECT_LINE ) : ( i == hovered ? ink ( tone::ACCENT_SOFT ) : ink ( tone::LINE_STRONG ) ) );

			if ( center )
				c.fill ( math::rect ( item.box.x + 1.0f, item.box.y + 6.0f, 3.0f, item.box.h - 12.0f ), 1.5f, ink ( tone::ACCENT ) );

			c.text ( name, center ? render::font::MONO_STRONG : render::font::MONO_SMALL, math::vec2 ( item.box.x + 12.0f, item.box.center ( ).y ), ink ( color ), render::align::LEFT, item.box.w - 22.0f );

		}

		c.pop_clip ( );

		const wchar_t* headings [ 5 ] = { L"Callers of callers", L"Callers", L"Function", L"Callees", L"What they call" };

		for ( int32_t i = low; i <= high; i++ ) {

			float x = left + static_cast < float > ( i ) * ( col_w + gap );
			base::writer heading;

			heading.add ( headings [ i ] );

			if ( i != 2 )
				heading.add ( L"  " ).number ( per_column [ i ] );

			if ( ( i == 1 || i == 3 ) && call_totals [ i / 2 ] > per_column [ i ] )
				heading.add ( L" of " ).number ( call_totals [ i / 2 ] );

			c.text ( heading.text ( ), render::font::TINY, math::vec2 ( x + 2.0f, area.y + 24.0f ), ink ( tone::INK_FAINT ) );

			if ( !per_column [ i ] && ( i == 1 || i == 3 ) )
				c.text ( i == 1 ? L"Nothing calls this directly" : L"No direct calls", render::font::SMALL, math::vec2 ( x + 2.0f, top + 12.0f ), ink ( tone::INK_FAINT ) );

		}

		c.text ( L"Click to select   \x00B7   double-click to center   \x00B7   Enter opens the disassembly   \x00B7   F5 decompiles", render::font::SMALL, math::vec2 ( area.x + 20.0f, area.bottom ( ) - 18.0f ), ink ( tone::INK_FAINT ), render::align::LEFT, area.w - 40.0f );

	}

}