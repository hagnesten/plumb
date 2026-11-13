#include "ui/app.hh"

namespace ui {

	static const wchar_t* dock_titles [ 2 ] = { L"Browser", L"Output" };
	static const wchar_t* side_titles [ 3 ] = { L"Dock on the left", L"Dock on the right", L"Dock at the bottom" };

	void app::reset_layout ( ) {

		docks [ 0 ] = dock_side::LEFT;
		docks [ 1 ] = dock_side::BOTTOM;
		dock_size [ 0 ] = 288.0f;
		dock_size [ 1 ] = 300.0f;
		dock_size [ 2 ] = 190.0f;
		browser_open = true;
		output_open = true;
		pane_split = 0.5f;
		pending = base::max ( pending, 3 );
		log ( log_kind::INFO, L"Window layout reset." );

	}

	void app::dock_handles ( ) {

		math::rect* areas [ 2 ] = { &browser_area, &output_area };

		for ( int32_t p = 0; p < 2; p++ ) {

			const math::rect& area = *areas [ p ];

			if ( area.w < 60.0f || area.h < 40.0f )
				continue;

			math::rect header ( area.x, area.y, area.w, p == 1 ? 31.0f : 34.0f );
			uint64_t id = make_id ( "dock_handle", p );
			widget& item = state ( id );

			interact ( id, header, item );

			if ( active_id == id && press_event ) {

				dock_drag = p;
				dock_from = mouse;
				dock_moving = false;
				dock_target = -1;

			}

		}

		if ( dock_drag < 0 )
			return;

		if ( !mouse_down ) {

			if ( dock_moving && dock_target >= 0 && docks [ dock_drag ] != static_cast < dock_side > ( dock_target ) ) {

				docks [ dock_drag ] = static_cast < dock_side > ( dock_target );
				log ( log_kind::INFO, base::writer ( ).add ( dock_titles [ dock_drag ] ).add ( L" moved. Window > Reset window layout puts everything back." ).text ( ) );

			}

			dock_drag = -1;
			dock_moving = false;
			dock_target = -1;

			return;

		}

		float dx = mouse.x - dock_from.x;
		float dy = mouse.y - dock_from.y;

		if ( !dock_moving && dx * dx + dy * dy > 64.0f )
			dock_moving = true;

	}

	void app::draw_docks ( ) {

		render::canvas& c = canvas;
		math::rect* areas [ 2 ] = { &browser_area, &output_area };

		if ( dock_areas [ 0 ].w > 1.0f )
			c.fill ( math::rect ( dock_areas [ 0 ].right ( ) - 1.0f, dock_areas [ 0 ].y, 1.0f, dock_areas [ 0 ].h ), 0.0f, ink ( tone::LINE ) );

		if ( dock_areas [ 1 ].w > 1.0f )
			c.fill ( math::rect ( dock_areas [ 1 ].x, dock_areas [ 1 ].y, 1.0f, dock_areas [ 1 ].h ), 0.0f, ink ( tone::LINE ) );

		if ( dock_areas [ 2 ].h > 1.0f )
			c.fill ( math::rect ( dock_areas [ 2 ].x, dock_areas [ 2 ].y, dock_areas [ 2 ].w, 1.0f ), 0.0f, ink ( tone::LINE ) );

		for ( int32_t p = 0; p < 2; p++ ) {

			const math::rect& area = *areas [ p ];
			const math::rect& side = dock_areas [ static_cast < int32_t > ( docks [ p ] ) ];

			if ( area.w < 2.0f || area.h < 2.0f )
				continue;

			if ( docks [ p ] == dock_side::BOTTOM ) {

				if ( area.x > side.x + 1.0f )
					c.fill ( math::rect ( area.x, area.y, 1.0f, area.h ), 0.0f, ink ( tone::LINE ) );

			} else if ( area.y > side.y + 1.0f )
				c.fill ( math::rect ( area.x, area.y, area.w, 1.0f ), 0.0f, ink ( tone::LINE ) );

		}

		if ( dock_drag < 0 || !dock_moving ) {

			if ( dock_shows [ 0 ] > 0.98f && dock_areas [ 0 ].w > 1.0f && splitter ( make_id ( "split_dock_left" ), math::rect ( dock_areas [ 0 ].right ( ) - 4.0f, dock_areas [ 0 ].y, 8.0f, dock_areas [ 0 ].h ), true ) )
				dock_size [ 0 ] = base::clamp ( mouse.x - dock_areas [ 0 ].x, 200.0f, base::max ( w * 0.45f, 200.0f ) );

			if ( dock_shows [ 1 ] > 0.98f && dock_areas [ 1 ].w > 1.0f && splitter ( make_id ( "split_dock_right" ), math::rect ( dock_areas [ 1 ].x - 4.0f, dock_areas [ 1 ].y, 8.0f, dock_areas [ 1 ].h ), true ) )
				dock_size [ 1 ] = base::clamp ( dock_areas [ 1 ].right ( ) - mouse.x, 200.0f, base::max ( w * 0.45f, 200.0f ) );

			if ( dock_shows [ 2 ] > 0.98f && dock_areas [ 2 ].h > 1.0f && splitter ( make_id ( "split_dock_bottom" ), math::rect ( dock_areas [ 2 ].x, dock_areas [ 2 ].y - 4.0f, dock_areas [ 2 ].w, 8.0f ), false ) )
				dock_size [ 2 ] = base::clamp ( dock_areas [ 2 ].bottom ( ) - mouse.y, 90.0f, base::max ( ( status_area.y - title_area.bottom ( ) ) * 0.6f, 90.0f ) );

			return;

		}

		float left_edge = center_area.x + center_area.w * 0.25f;
		float right_edge = center_area.right ( ) - center_area.w * 0.25f;
		int32_t target = -1;

		if ( mouse.x < left_edge )
			target = 0;
		else if ( mouse.x > right_edge )
			target = 1;
		else if ( mouse.y > center_area.y + center_area.h * 0.5f )
			target = 2;

		dock_target = target;
		wanted_cursor = pointer::HAND;

		if ( target >= 0 ) {

			math::rect preview;

			if ( target == 0 )
				preview = math::rect ( rail_area.right ( ), center_area.y, dock_size [ 0 ], center_area.h );
			else if ( target == 1 )
				preview = math::rect ( w - dock_size [ 1 ], center_area.y, dock_size [ 1 ], center_area.h );
			else
				preview = math::rect ( center_area.x, center_area.bottom ( ) - dock_size [ 2 ], center_area.w, dock_size [ 2 ] );

			preview = math::rect ( preview.x + 4.0f, preview.y + 4.0f, preview.w - 8.0f, preview.h - 8.0f );
			c.fill ( preview, metric::radius, ink ( tone::BASE ).fade ( 0.88f ) );
			c.outline ( preview, metric::radius, ink ( tone::ACCENT ).fade ( 0.16f ), 1.5f, ink ( tone::ACCENT ).fade ( 0.8f ) );
			c.text ( side_titles [ target ], render::font::UI_STRONG, preview.center ( ), ink ( tone::ACCENT ), render::align::CENTER );

		}

		math::rect ghost ( mouse.x + 12.0f, mouse.y + 10.0f, c.measure ( dock_titles [ dock_drag ], render::font::UI_STRONG ) + 20.0f, 26.0f );

		c.outline ( ghost, 4.0f, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );
		c.text ( dock_titles [ dock_drag ], render::font::UI_STRONG, math::vec2 ( ghost.x + 10.0f, ghost.center ( ).y ), ink ( tone::INK ) );

	}

}