#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	void app::draw_output ( ) {

		if ( output_area.h < 2.0f )
			return;

		render::canvas& c = canvas;
		math::rect area = output_area;
		float bar_h = metric::header;
		float field_h = metric::field + 8.0f;

		c.push_clip ( area );
		c.fill ( area, 0.0f, ink ( tone::PANEL ) );

		math::rect bar ( area.x, area.y + 1.0f, area.w, bar_h );

		c.text ( L"OUTPUT", render::font::TINY, math::vec2 ( bar.x + 10.0f, bar.center ( ).y ), ink ( tone::INK_SOFT ) );

		base::writer count;

		count.grouped ( static_cast < uint64_t > ( output.count ( ) ) ).add ( output.count ( ) == 1 ? L" message" : L" messages" );
		c.text ( count.text ( ), render::font::SMALL, math::vec2 ( bar.x + 66.0f, bar.center ( ).y ), ink ( tone::INK_FAINT ) );

		if ( icon_button ( make_id ( "output_clear" ), math::vec2 ( bar.right ( ) - 16.0f, bar.center ( ).y ), 20.0f, icons::CANCEL, 9.0f, L"Clear output" ) )
			output.clear ( );

		c.fill ( math::rect ( area.x, bar.bottom ( ), area.w, 1.0f ), 0.0f, ink ( tone::LINE ) );

		math::rect list ( area.x, bar.bottom ( ) + 1.0f, area.w, area.h - bar_h - field_h - 3.0f );
		float line_h = metric::log_row;
		float content = static_cast < float > ( output.count ( ) ) * line_h + 8.0f;
		uint64_t id = make_id ( "output_list" );

		scroll_region ( id, list );
		output_scroll.target -= take_wheel ( id ) * line_h * 3.0f;
		glide ( output_scroll, content - list.h );
		c.push_clip ( list );

		int32_t first = base::max ( static_cast < int32_t > ( output_scroll.shown / line_h ), 0 );
		int32_t last = base::min ( output.count ( ), first + static_cast < int32_t > ( list.h / line_h ) + 2 );

		for ( int32_t i = first; i < last; i++ ) {

			const log_line& line = output [ static_cast < size_t > ( i ) ];
			float cy = list.y + 4.0f + static_cast < float > ( i ) * line_h - output_scroll.shown + line_h * 0.5f;
			render::color mark = line.kind == log_kind::GOOD ? ink ( tone::SUCCESS ) : ( line.kind == log_kind::WARN ? ink ( tone::WARNING ) : ( line.kind == log_kind::FAIL ? ink ( tone::DANGER ) : ink ( tone::INK_FAINT ) ) );
			bool echo = line.text.get ( ) [ 0 ] == L'>' && line.text.get ( ) [ 1 ] == L' ';

			if ( line.kind != log_kind::INFO )
				c.fill ( math::rect ( list.x + 8.0f, cy - 4.0f, 2.0f, 8.0f ), 0.0f, mark );

			c.text ( line.text.get ( ), render::font::MONO_SMALL, math::vec2 ( list.x + 16.0f, cy ), echo ? ink ( tone::ACCENT ) : ( line.kind == log_kind::FAIL ? ink ( tone::DANGER ) : ink ( tone::INK ) ), render::align::LEFT, list.w - 30.0f );

		}

		c.pop_clip ( );
		draw_scrollbar ( make_id ( "output_bar" ), list, output_scroll, content, list.h );

		math::rect line_area ( area.x + 6.0f, area.bottom ( ) - field_h + 2.0f, area.w - 12.0f, metric::field );

		if ( text_field ( make_id ( "command_line" ), command_field, line_area, L"Command: go 140001000, functions where calls VirtualAlloc and size > 0x100, help", icons::CHEVRON_RIGHT, render::font::MONO ) )
			run_command_line ( );

		c.pop_clip ( );

	}

}