#include "ui/app.hh"
#include "ui/icons.hh"
#include "ui/format.hh"

namespace ui {

	static const wchar_t* view_titles [ static_cast < int32_t > ( view::COUNT ) ] = { L"Disassembly", L"Graph", L"C", L"Hex", L"Headers", L"Calls" };
	static const uint32_t view_glyphs [ static_cast < int32_t > ( view::COUNT ) ] = { icons::CODE, icons::BRANCH, icons::FORM, icons::CHIP, icons::INFO, icons::LAYERS };

	void app::draw_panes ( ) {

		render::canvas& c = canvas;

		for ( int32_t i = 0; i < pane_count; i++ ) {

			if ( panes [ i ].area.w < 4.0f || panes [ i ].area.h < 4.0f )
				continue;

			draw_document ( i );
			draw_tabs ( i );

		}

		if ( pane_count == 2 ) {

			math::rect seam ( panes [ 1 ].area.x - 1.0f, view_area.y, 1.0f, view_area.h );

			c.fill ( seam, 0.0f, ink ( tone::LINE_STRONG ) );

			if ( splitter ( make_id ( "split_panes" ), math::rect ( seam.x - 4.0f, view_area.y, 9.0f, view_area.h ), true ) )
				pane_split = ( mouse.x - view_area.x ) / base::max ( view_area.w, 1.0f );

		}

		select_state ( current_document ( ) );

	}

	void app::draw_tabs ( int32_t index ) {

		render::canvas& c = canvas;
		pane& item = panes [ index ];
		math::rect area = item.strip;
		bool focused_here = index == focused_pane;

		c.fill ( area, 0.0f, ink ( tone::PANEL ) );
		c.fill ( math::rect ( area.x, area.bottom ( ) - 1.0f, area.w, 1.0f ), 0.0f, ink ( tone::LINE ) );
		c.push_clip ( area );

		float x = area.x;
		float tools = 150.0f;
		float room = base::max ( area.w - tools - 40.0f, 80.0f );
		float total = 0.0f;
		wchar_t label [ 200 ];
		int32_t close_tab = -1;

		auto tab_label = [ & ] ( const document& doc ) {

			base::writer text;

			text.add ( view_titles [ static_cast < int32_t > ( doc.kind ) ] );

			if ( ( doc.kind == view::PSEUDOCODE || doc.kind == view::GRAPH || doc.kind == view::CALLS ) && db && analysis_applied ) {

				int32_t owner = db->function_starting ( doc.kind == view::GRAPH ? doc.state.graph_function : doc.state.function );

				if ( owner >= 0 ) {

					wchar_t name [ 160 ];

					function_name ( owner, name, 160 );
					text.add ( L" \x00B7 " ).add ( name );

				}

			}

			int32_t i = 0;

			for ( ; i <= text.size ( ) && i < 199; i++ )
				label [ i ] = text.text ( ) [ i ];

			label [ 199 ] = 0;

		};

		for ( int32_t t = 0; t < item.count; t++ ) {

			tab_label ( documents [ item.tabs [ t ] ] );
			total += base::min ( c.measure ( label, render::font::UI ), 220.0f ) + 52.0f;

		}

		float squeeze = total > room ? room / total : 1.0f;

		for ( int32_t t = 0; t < item.count; t++ ) {

			const document& doc = documents [ item.tabs [ t ] ];

			tab_label ( doc );

			float text_w = base::min ( c.measure ( label, render::font::UI ), 220.0f );
			float slot_w = base::max ( ( text_w + 52.0f ) * squeeze, 64.0f );
			math::rect slot ( x, area.y, slot_w, area.h );
			uint64_t id = make_id ( "tab", static_cast < int64_t > ( doc.serial ) );
			widget& state_item = state ( id );
			bool selected = item.active == t;

			if ( interact ( id, slot, state_item ) )
				focus_document ( index, t );

			if ( middle_id == id )
				close_tab = t;

			bool inside = over ( slot ) && menu_open < 0;

			if ( selected ) {

				c.fill ( slot, 0.0f, ink ( tone::BASE ) );
				c.fill ( math::rect ( slot.x, slot.y, slot.w, 2.0f ), 0.0f, focused_here ? ink ( tone::ACCENT ) : ink ( tone::LINE_STRONG ) );

			} else if ( inside )
				c.fill ( math::rect ( slot.x, slot.y, slot.w, slot.h - 1.0f ), 0.0f, ink ( tone::HOVER ) );

			c.fill ( math::rect ( slot.right ( ) - 1.0f, slot.y, 1.0f, slot.h - ( selected ? 0.0f : 1.0f ) ), 0.0f, ink ( tone::LINE ) );
			c.icon ( view_glyphs [ static_cast < int32_t > ( doc.kind ) ], math::vec2 ( slot.x + 15.0f, slot.center ( ).y ), 11.0f, selected ? ink ( tone::ACCENT ) : ink ( tone::INK_FAINT ) );
			c.text ( label, render::font::UI, math::vec2 ( slot.x + 28.0f, slot.center ( ).y ), selected || inside ? ink ( tone::INK ) : ink ( tone::INK_SOFT ), render::align::LEFT, slot.w - 50.0f );

			if ( ( selected || inside ) && ( item.count > 1 || pane_count > 1 ) ) {

				if ( icon_button ( make_id ( "tab_close", static_cast < int64_t > ( doc.serial ) ), math::vec2 ( slot.right ( ) - 13.0f, slot.center ( ).y ), 18.0f, icons::CANCEL, 8.0f, L"Close tab (Ctrl+W)" ) )
					close_tab = t;

			}

			x = slot.right ( );

		}

		if ( icon_button ( make_id ( "tab_add", index ), math::vec2 ( x + 14.0f, area.center ( ).y ), 22.0f, icons::ADD, 10.0f, L"New disassembly tab (Ctrl+1)" ) ) {

			focused_pane = index;
			select_state ( pane_document ( index ) );
			run ( command::VIEW_LISTING );

		}

		float right = area.right ( ) - 6.0f;

		if ( right - x > 140.0f ) {

			if ( icon_button ( make_id ( "tab_split", index ), math::vec2 ( right - 12.0f, area.center ( ).y ), 22.0f, icons::LAYERS, 12.0f, pane_count > 1 ? L"Join panes (Ctrl+\\)" : L"Split side by side (Ctrl+\\)" ) ) {

				focused_pane = index;
				run ( command::SPLIT );

			}

			right -= 26.0f;

			if ( icon_button ( make_id ( "tab_forward", index ), math::vec2 ( right - 12.0f, area.center ( ).y ), 22.0f, icons::FORWARD, 12.0f, L"Forward (Alt+Right)" ) ) {

				focus_document ( index, item.active );
				run ( command::FORWARD );

			}

			right -= 24.0f;

			if ( icon_button ( make_id ( "tab_back", index ), math::vec2 ( right - 12.0f, area.center ( ).y ), 22.0f, icons::BACK, 12.0f, L"Back (Esc)" ) ) {

				focus_document ( index, item.active );
				run ( command::BACK );

			}

			right -= 32.0f;

			int32_t doc = pane_document ( index );

			if ( doc >= 0 && right - x > 230.0f ) {

				base::writer where;
				uint64_t address = documents [ doc ].kind == view::PSEUDOCODE ? documents [ doc ].state.function : documents [ doc ].state.cursor_address;

				where.hex ( address, file.wide ( ) ? 9 : 8 );
				c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( right, area.center ( ).y ), ink ( tone::INK_SOFT ), render::align::RIGHT );

			}

		}

		c.pop_clip ( );

		if ( close_tab >= 0 )
			close_document ( index, close_tab );

	}

	void app::draw_document ( int32_t index ) {

		render::canvas& c = canvas;
		pane& item = panes [ index ];
		int32_t doc = pane_document ( index );

		if ( doc < 0 ) {

			c.fill ( item.body, 0.0f, ink ( tone::BASE ) );

			return;

		}

		if ( press_event && over ( item.area ) && focused_pane != index && menu_open < 0 ) {

			focused_pane = index;

		}

		document& current = documents [ doc ];

		select_state ( doc );
		drawing_serial = current.serial;

		switch ( current.kind ) {

			case view::PSEUDOCODE:
				draw_pseudocode ( item.body );
				break;

			case view::GRAPH:
				draw_graph ( item.body );
				break;

			case view::HEX:
				draw_hex ( item.body );
				break;

			case view::HEADERS:
				draw_headers ( item.body );
				break;

			case view::CALLS:
				draw_calls ( item.body );
				break;

			default:
				draw_listing ( item.body );
				break;

		}

		if ( pane_count > 1 && index != focused_pane )
			c.fill ( item.body, 0.0f, ink ( tone::BASE ).fade ( 0.12f ) );

	}

}