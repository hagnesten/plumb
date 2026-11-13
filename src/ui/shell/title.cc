#include "ui/app.hh"
#include "ui/icons.hh"
#include <windows.h>

namespace ui {

	struct menu_spec {

		const wchar_t* title;
		command items [ 20 ];
		int32_t count;

	};

	static const menu_spec menus [ ] = {
		{ L"File", { command::OPEN, command::SAVE_PROJECT, command::CLOSE, command::COUNT, command::TOGGLE_AUTOSAVE, command::COUNT, command::EXIT }, 7 },
		{ L"Edit", { command::UNDO, command::REDO, command::COUNT, command::COPY, command::COPY_FUNCTION, command::COUNT, command::RENAME, command::COMMENT, command::NOTE, command::SET_TYPE, command::BOOKMARK, command::COUNT, command::NUMBER_RADIX, command::NUMBER_CHAR, command::NUMBER_BINARY, command::NUMBER_FLOAT, command::NUMBER_RESET }, 17 },
		{ L"Jump", { command::GO_ADDRESS, command::GO_ENTRY, command::FOLLOW, command::COUNT, command::BACK, command::FORWARD, command::COUNT, command::NEXT_SECTION, command::PREVIOUS_SECTION, command::COUNT, command::FIND, command::PALETTE }, 12 },
		{ L"View", { command::TOGGLE_GRAPH, command::VIEW_PSEUDOCODE, command::SWAP_VIEW, command::COUNT, command::VIEW_LISTING, command::VIEW_HEX, command::VIEW_HEADERS, command::VIEW_CALLS, command::COUNT, command::SPLIT, command::NEXT_TAB, command::CLOSE_TAB, command::COUNT, command::THEME, command::TOGGLE_BYTES, command::TOGGLE_SPACING }, 16 },
		{ L"Window", { command::SHOW_FUNCTIONS, command::SHOW_IMPORTS, command::SHOW_EXPORTS, command::SHOW_STRINGS, command::SHOW_SECTIONS, command::SHOW_CLASSES, command::SHOW_STRUCTS, command::SHOW_REFERENCES, command::SHOW_SEARCH, command::SHOW_NOTES, command::COUNT, command::TOGGLE_BROWSER, command::TOGGLE_INSPECTOR, command::TOGGLE_OUTPUT, command::RESET_LAYOUT }, 15 },
		{ L"Tools", { command::SEARCH_ALL, command::SHOW_REFERENCES, command::COUNT, command::MAKE_SIGNATURE, command::FIND_SIGNATURE, command::COUNT, command::XOR_DECODE, command::STRUCT_FROM_VAR, command::EXPORT_CLASSES, command::EXPORT_REPORT, command::COUNT, command::LEARN_LIBRARY, command::COMPARE_FILE, command::COUNT, command::TOGGLE_TERNARY }, 15 },
		{ L"Patch", { command::ASSEMBLE, command::PATCH_BYTES, command::REVERT_PATCH, command::COUNT, command::EXPORT_PATCHED }, 5 },
		{ L"Debug", { command::DEBUG_RUN, command::DEBUG_COVERAGE, command::DEBUG_PAUSE, command::DEBUG_STOP, command::COUNT, command::DEBUG_STEP_INTO, command::DEBUG_STEP_OVER, command::COUNT, command::DEBUG_BREAKPOINT, command::DEBUG_CONDITION, command::DEBUG_WATCH }, 11 },
		{ L"Help", { command::SHORTCUTS, command::COUNT, command::ABOUT }, 3 }
	};

	static constexpr int32_t menu_count = 9;
	static float menu_x [ menu_count ];
	static float menu_w [ menu_count ];

	void app::draw_menu_bar ( math::rect area ) {

		render::canvas& c = canvas;
		float x = area.x;

		for ( int32_t i = 0; i < menu_count; i++ ) {

			float text_w = c.measure ( menus [ i ].title, render::font::UI );
			math::rect slot ( x, area.y + 5.0f, text_w + 18.0f, area.h - 10.0f );
			uint64_t id = make_id ( "menu_title", i );
			widget& item = state ( id );
			bool clicked = interact ( id, slot, item );

			menu_x [ i ] = slot.x;
			menu_w [ i ] = slot.w;

			if ( clicked ) {

				menu_open = menu_open == i ? -1 : i;
				menu_focus = -1;

			} else if ( menu_open >= 0 && menu_open != i && hot == id )
				menu_open = i;

			float lit = base::max ( item.hover, menu_open == i ? 1.0f : 0.0f );

			c.fill ( slot, metric::radius, ink ( tone::HOVER ).fade ( lit * 1.8f ) );
			c.text ( menus [ i ].title, render::font::UI, math::vec2 ( slot.x + 9.0f, slot.center ( ).y ), lit > 0.5f ? ink ( tone::INK ) : ink ( tone::INK_SOFT ) );
			x = slot.right ( );

		}

	}

	void app::draw_title ( ) {

		render::canvas& c = canvas;
		math::rect area = title_area;

		c.fill ( area, 0.0f, ink ( tone::TITLE ) );
		c.fill ( math::rect ( 0.0f, area.bottom ( ) - 1.0f, w, 1.0f ), 0.0f, ink ( tone::LINE ) );
		draw_brand ( math::vec2 ( 18.0f, area.center ( ).y ), 16.0f );
		c.text ( L"Plumb", render::font::BRAND, math::vec2 ( 31.0f, area.center ( ).y ), ink ( tone::INK ) );
		c.fill ( math::rect ( 78.0f, area.y + 9.0f, 1.0f, area.h - 18.0f ), 0.0f, ink ( tone::LINE_STRONG ) );
		draw_menu_bar ( math::rect ( 82.0f, area.y, 520.0f, area.h ) );

		float buttons = 46.0f * 3.0f;
		float right_edge = w - buttons;

		if ( file.loaded ( ) ) {

			base::writer detail;

			detail.add ( file.format_name ( ) ).add ( L"  \x00B7  " ).add ( file.machine_name ( ) );

			float name_w = c.measure ( file.name.get ( ), render::font::UI_STRONG );
			float detail_w = c.measure ( detail.text ( ), render::font::SMALL );
			float total = name_w + 14.0f + detail_w;
			float x = base::max ( w * 0.5f - total * 0.5f, 640.0f );

			if ( x + total < right_edge - 12.0f ) {

				c.text ( file.name.get ( ), render::font::UI_STRONG, math::vec2 ( x, area.center ( ).y ), ink ( tone::INK ) );

				if ( dirty )
					detail.add ( L"  \x00B7  unsaved" );

				c.text ( detail.text ( ), render::font::SMALL, math::vec2 ( x + name_w + 14.0f, area.center ( ).y ), ink ( dirty ? tone::WARNING : tone::INK_FAINT ) );

			}

		}

		const uint32_t glyphs [ 3 ] = { icons::MINIMIZE, maximized ? icons::RESTORE : icons::MAXIMIZE, icons::CLOSE };
		const wchar_t* labels [ 3 ] = { L"Minimize", maximized ? L"Restore" : L"Maximize", L"Close" };

		for ( int32_t i = 0; i < 3; i++ ) {

			math::rect slot ( right_edge + 46.0f * static_cast < float > ( i ), 0.0f, 46.0f, area.h - 1.0f );
			uint64_t id = make_id ( "window_button", i );
			widget& item = state ( id );
			bool clicked = interact ( id, slot, item, i == 1 ? hit::MAXIMIZE : hit::CLIENT );
			render::color wash = i == 2 ? render::rgb ( 0xC42B1C ) : ink ( tone::HOVER );
			float lit = i == 2 ? item.hover : item.hover * 1.4f;

			c.fill ( slot, 0.0f, wash.fade ( lit ) );
			c.icon ( glyphs [ i ], slot.center ( ), 10.0f, i == 2 ? render::mix ( ink ( tone::INK_SOFT ), render::rgb ( 0xFFFFFF ), item.hover ) : ink ( tone::INK_SOFT ) );
			tip ( id, labels [ i ], slot );

			if ( clicked )
				pending_request = i == 0 ? request::MINIMIZE : ( i == 1 ? request::MAXIMIZE : request::CLOSE );

		}

	}

	void app::draw_menu ( ) {

		settle ( menu_show, menu_open >= 0 ? 1.0f : 0.0f );

		if ( menu_open < 0 ) {

			menu_show = 0.0f;

			return;

		}

		render::canvas& c = canvas;
		const menu_spec& spec = menus [ menu_open ];
		float row_h = metric::menu_row;
		float width_menu = 270.0f;
		float height_menu = 6.0f;

		for ( int32_t i = 0; i < spec.count; i++ )
			height_menu += spec.items [ i ] == command::COUNT ? 7.0f : row_h;

		math::rect area ( menu_x [ menu_open ], title_area.bottom ( ) - 1.0f, width_menu, height_menu );
		uint64_t blocker = make_id ( "menu_blocker" );
		widget& block = state ( blocker );

		if ( interact ( blocker, math::rect ( 0.0f, title_area.bottom ( ), w, h - title_area.bottom ( ) ), block ) )
			menu_open = -1;

		float shown = math::saturate ( menu_show );

		c.push ( math::vec2 ( area.x, area.y ), 1.0f, shown );
		c.shadow ( area, metric::radius, 8.0f, ink ( tone::SHADOW ).fade ( 0.5f ), math::vec2 ( 0.0f, 3.0f ) );
		c.outline ( area, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "menu_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, area, surface_state );

		float y = area.y + 3.0f;
		bool ran = false;

		for ( int32_t i = 0; i < spec.count; i++ ) {

			command action = spec.items [ i ];

			if ( action == command::COUNT ) {

				c.fill ( math::rect ( area.x + 8.0f, y + 3.0f, area.w - 16.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );
				y += 7.0f;

				continue;

			}

			math::rect row ( area.x + 3.0f, y, area.w - 6.0f, row_h );
			uint64_t id = make_id ( "menu_item", static_cast < int64_t > ( action ) );
			widget& item = state ( id );
			bool usable = enabled ( action );
			bool clicked = interact ( id, row, item );
			render::color text = enabled ( action ) ? ink ( tone::INK ) : ink ( tone::INK_FAINT );

			c.fill ( row, 2.0f, ink ( tone::SELECT ).fade ( item.hover * ( usable ? 1.0f : 0.4f ) ) );

			if ( checked ( action ) )
				c.icon ( icons::CHECK, math::vec2 ( row.x + 13.0f, row.center ( ).y ), 10.0f, ink ( tone::INK ) );

			c.text ( command_label ( action ), render::font::UI, math::vec2 ( row.x + 28.0f, row.center ( ).y ), text );
			c.text ( command_keys ( action ), render::font::SMALL, math::vec2 ( row.right ( ) - 10.0f, row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

			if ( clicked && !ran ) {

				ran = true;
				menu_open = -1;
				run ( action );

			}

			y += row_h;

		}

		c.pop ( );

	}

}