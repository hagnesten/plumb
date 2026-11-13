#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	void app::draw_welcome ( math::rect area ) {

		render::canvas& c = canvas;
		float shown = math::saturate ( welcome_show );
		float column = base::min ( 560.0f, area.w - 64.0f );
		float x = area.center ( ).x - column * 0.5f;
		float top = area.y + base::max ( area.h * 0.5f - 240.0f, 32.0f );

		c.push ( math::vec2 ( area.center ( ).x, top ), 1.0f, shown );

		draw_brand ( math::vec2 ( x + 14.0f, top + 18.0f ), 34.0f );
		c.text ( L"Plumb", render::font::TITLE, math::vec2 ( x + 40.0f, top + 12.0f ), ink ( tone::INK ) );
		c.text ( L"x86 and x64 disassembler for PE images", render::font::SMALL, math::vec2 ( x + 40.0f, top + 30.0f ), ink ( tone::INK_FAINT ) );

		if ( button ( make_id ( "welcome_open" ), math::rect ( x + column - 132.0f, top + 4.0f, 132.0f, 30.0f ), L"Open file", icons::FOLDER_OPEN, true ) )
			run ( command::OPEN );

		float y = top + 64.0f;

		if ( !load_problem.empty ( ) ) {

			math::rect warning ( x, y, column, 36.0f );

			c.outline ( warning, 4.0f, ink ( tone::DANGER ).fade ( 0.1f ), 1.0f, ink ( tone::DANGER ).fade ( 0.5f ) );
			c.icon ( icons::FAILURE, math::vec2 ( warning.x + 16.0f, warning.center ( ).y ), 12.0f, ink ( tone::DANGER ) );
			c.text ( load_problem.get ( ), render::font::SMALL, math::vec2 ( warning.x + 32.0f, warning.center ( ).y ), ink ( tone::INK ), render::align::LEFT, column - 44.0f );
			y += 48.0f;

		}

		c.text ( L"Recent", render::font::UI_STRONG, math::vec2 ( x, y + 8.0f ), ink ( tone::INK_SOFT ) );
		y += 22.0f;
		c.fill ( math::rect ( x, y, column, 1.0f ), 0.0f, ink ( tone::LINE ) );
		y += 6.0f;

		if ( recent.empty ( ) )
			c.text ( L"No files opened yet", render::font::UI, math::vec2 ( x, y + 16.0f ), ink ( tone::INK_FAINT ) );

		int32_t forget = -1;

		for ( int32_t i = 0; i < recent.count ( ); i++ ) {

			const wchar_t* path = recent [ static_cast < size_t > ( i ) ].get ( );
			const wchar_t* tail = path;

			for ( const wchar_t* at = path; *at; at++ ) {

				if ( *at == L'\\' || *at == L'/' )
					tail = at + 1;

			}

			wchar_t folder [ 520 ];
			int32_t folder_length = base::min ( static_cast < int32_t > ( tail - path ), 519 );

			for ( int32_t k = 0; k < folder_length; k++ )
				folder [ k ] = path [ k ];

			if ( folder_length > 1 && folder [ folder_length - 1 ] == L'\\' )
				folder_length--;

			folder [ folder_length ] = 0;

			math::rect row ( x - 8.0f, y, column + 16.0f, 30.0f );
			uint64_t id = make_id ( "recent", i );
			widget& item = state ( id );

			if ( interact ( id, row, item ) )
				open_file ( path );

			bool inside = over ( row ) && menu_open < 0;

			settle ( item.value, inside ? 1.0f : 0.0f );

			float name_w = base::min ( c.measure ( tail, render::font::UI ), column * 0.45f );

			c.fill ( row, 4.0f, ink ( tone::HOVER ).fade ( item.value * 1.6f ) );
			c.text ( tail, render::font::UI, math::vec2 ( x, row.center ( ).y ), ink ( tone::INK ), render::align::LEFT, column * 0.45f );
			c.text ( folder, render::font::SMALL, math::vec2 ( x + name_w + 14.0f, row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::LEFT, column - name_w - 48.0f );

			if ( item.value > 0.02f ) {

				c.push_alpha ( item.value );

				if ( icon_button ( make_id ( "recent_forget", i ), math::vec2 ( row.right ( ) - 16.0f, row.center ( ).y ), 22.0f, icons::CANCEL, 9.0f, L"Remove from recent" ) )
					forget = i;

				c.pop ( );

			}

			y += 30.0f;

		}

		if ( forget >= 0 )
			forget_recent ( forget );

		y += recent.empty ( ) ? 52.0f : 26.0f;

		c.text ( L"Keys", render::font::UI_STRONG, math::vec2 ( x, y + 8.0f ), ink ( tone::INK_SOFT ) );
		y += 22.0f;
		c.fill ( math::rect ( x, y, column, 1.0f ), 0.0f, ink ( tone::LINE ) );
		y += 18.0f;

		const command hints [ 8 ] = { command::OPEN, command::GO_ADDRESS, command::PALETTE, command::TOGGLE_GRAPH, command::VIEW_PSEUDOCODE, command::SWAP_VIEW, command::SHOW_REFERENCES, command::MAKE_SIGNATURE };

		for ( int32_t i = 0; i < 8; i++ ) {

			float hx = x + static_cast < float > ( i % 2 ) * ( column * 0.5f + 8.0f );
			float hy = y + static_cast < float > ( i / 2 ) * 28.0f;
			const wchar_t* keys = command_keys ( hints [ i ] );

			key_cap ( math::vec2 ( hx, hy ), keys, ink ( tone::INK_SOFT ), false );
			c.text ( command_label ( hints [ i ] ), render::font::SMALL, math::vec2 ( hx + c.measure ( keys, render::font::SMALL ) + 22.0f, hy ), ink ( tone::INK_FAINT ) );

		}

		c.pop ( );

	}

}