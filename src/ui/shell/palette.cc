#include "ui/app.hh"
#include "ui/icons.hh"
#include <windows.h>

namespace ui {

	static int32_t fuzzy_score ( const wchar_t* text, const wchar_t* query ) {

		if ( !query [ 0 ] )
			return 1;

		int32_t score = 0;
		int32_t q = 0;
		bool boundary = true;

		for ( int32_t i = 0; text [ i ] && query [ q ]; i++ ) {

			wchar_t a = text [ i ] >= L'A' && text [ i ] <= L'Z' ? static_cast < wchar_t > ( text [ i ] + 32 ) : text [ i ];
			wchar_t b = query [ q ] >= L'A' && query [ q ] <= L'Z' ? static_cast < wchar_t > ( query [ q ] + 32 ) : query [ q ];

			if ( a == b ) {

				score += boundary ? 8 : 2;
				q++;

			}

			boundary = text [ i ] == L' ';

		}

		return query [ q ] ? 0 : score;

	}

	void app::draw_palette ( ) {

		settle ( palette_show, palette_open ? 1.0f : 0.0f );

		if ( palette_show <= 0.004f && !palette_open )
			return;

		render::canvas& c = canvas;
		float shown = math::saturate ( palette_show );
		float width_box = base::min ( 600.0f, w - 40.0f );
		float x = w * 0.5f - width_box * 0.5f;
		float y = title_area.bottom ( ) + 40.0f;
		uint64_t blocker = make_id ( "palette_blocker" );
		widget& block = state ( blocker );

		if ( palette_open && interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) ) {

			palette_open = false;
			focus_id = 0;
			focused_field = nullptr;

		}

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.2f * shown ) );

		if ( palette_open && palette_field.revision != palette_seen ) {

			palette_seen = palette_field.revision;
			palette_items.clear ( );
			palette_pick = 0;

			if ( !palette_address ) {

				for ( int32_t i = 0; i < static_cast < int32_t > ( command::COUNT ); i++ ) {

					int32_t score = fuzzy_score ( command_label ( static_cast < command > ( i ) ), palette_field.text );

					if ( !score )
						continue;

					palette_entry entry = { static_cast < command > ( i ), score + ( enabled ( static_cast < command > ( i ) ) ? 100 : 0 ) };
					int32_t at = palette_items.count ( );

					palette_items.push ( entry );

					while ( at > 0 && palette_items [ static_cast < size_t > ( at - 1 ) ].score < entry.score ) {

						palette_items [ static_cast < size_t > ( at ) ] = palette_items [ static_cast < size_t > ( at - 1 ) ];
						at--;

					}

					palette_items [ static_cast < size_t > ( at ) ] = entry;

				}

			}

		}

		int32_t visible = base::min ( palette_items.count ( ), 14 );
		float row_h = metric::palette_row;
		float head_h = metric::field + 12.0f;
		float list_h = palette_address ? 40.0f : static_cast < float > ( visible ) * row_h + ( visible ? 8.0f : 0.0f );
		math::rect box ( x, y, width_box, head_h + list_h );

		palette_pick = palette_items.count ( ) ? base::clamp ( palette_pick, 0, palette_items.count ( ) - 1 ) : 0;
		c.push ( math::vec2 ( box.center ( ).x, box.y ), 1.0f, shown );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "palette_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );

		bool submitted = text_field ( make_id ( "palette" ), palette_field, math::rect ( box.x + 6.0f, box.y + 6.0f, box.w - 12.0f, metric::field ), palette_address ? L"Address, RVA (+1A30), symbol or section" : L"Type a command", palette_address ? icons::FORWARD : icons::SEARCH, palette_address ? render::font::MONO : render::font::UI );

		c.fill ( math::rect ( box.x, box.y + head_h - 1.0f, box.w, 1.0f ), 0.0f, ink ( tone::LINE ) );

		if ( palette_address ) {

			uint64_t address = 0;
			bool valid = file.loaded ( ) && parse_address ( palette_field.text, address );
			base::writer hint;

			if ( !file.loaded ( ) )
				hint.add ( L"Open a file first." );
			else if ( !palette_field.length )
				hint.add ( L"Enter jumps.  Plain hex is a virtual address, +hex is relative to the image base." );
			else if ( valid ) {

				int32_t owner = file.section_at ( address );

				hint.add ( L"Go to " ).add ( owner >= 0 ? file.sections [ static_cast < size_t > ( owner ) ].name : L"(outside the image)" ).add ( L":" ).hex ( address, file.wide ( ) ? 16 : 8 );

			} else
				hint.add ( L"That isn't an address or a known name." );

			c.text ( hint.text ( ), render::font::SMALL, math::vec2 ( box.x + 12.0f, box.y + head_h + 20.0f ), valid ? ink ( tone::INK_SOFT ) : ink ( tone::INK_FAINT ) );

			if ( submitted && valid ) {

				palette_open = false;
				focus_id = 0;
				focused_field = nullptr;
				go ( address, true );

			}

		} else {

			float target = static_cast < float > ( palette_pick );

			if ( palette_pick_y < -0.5f )
				palette_pick_y = target;

			settle ( palette_pick_y, target );

			float list_top = box.y + head_h + 4.0f;

			if ( visible )
				c.fill ( math::rect ( box.x + 4.0f, list_top + palette_pick_y * row_h, box.w - 8.0f, row_h ), 2.0f, ink ( tone::SELECT ) );

			for ( int32_t i = 0; i < visible; i++ ) {

				const palette_entry& entry = palette_items [ static_cast < size_t > ( i ) ];
				math::rect row ( box.x + 4.0f, list_top + static_cast < float > ( i ) * row_h, box.w - 8.0f, row_h );
				uint64_t id = make_id ( "palette_row", i );
				widget& item = state ( id );
				bool usable = enabled ( entry.action );

				if ( interact ( id, row, item ) ) {

					palette_pick = i;
					submitted = true;

				}

				if ( item.hover > 0.5f && mouse_inside )
					palette_pick = i;

				c.text ( command_label ( entry.action ), render::font::UI, math::vec2 ( row.x + 10.0f, row.center ( ).y ), usable ? ink ( tone::INK ) : ink ( tone::INK_FAINT ) );

				if ( !usable )
					c.text ( L"unavailable", render::font::SMALL, math::vec2 ( row.right ( ) - 14.0f - ( command_keys ( entry.action ) [ 0 ] ? c.measure ( command_keys ( entry.action ), render::font::SMALL ) + 24.0f : 0.0f ), row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

				key_cap ( math::vec2 ( row.right ( ) - 10.0f, row.center ( ).y ), command_keys ( entry.action ), ink ( tone::INK_SOFT ), true );

			}

			if ( submitted && palette_items.count ( ) ) {

				command action = palette_items [ static_cast < size_t > ( palette_pick ) ].action;

				palette_open = false;
				focus_id = 0;
				focused_field = nullptr;
				run ( action );

			}

		}

		c.pop ( );

	}

}