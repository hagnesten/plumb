#include "ui/panels/output/output.h"
#include "ui/core/app/app.h"
#include "ui/style/icons/icons.h"

int32_t ui::wrap_piece ( const wchar_t* text, int32_t left, int32_t columns, int32_t& skip ) {

	skip = 0;

	if ( left <= columns )
		return left;

	int32_t cut = columns;

	while ( cut > 0 && text [ cut ] != L' ' )
		cut--;

	if ( cut <= 0 )
		return columns;

	skip = 1;

	return cut;

}

int32_t ui::wrap_rows ( const wchar_t* text, int32_t length, int32_t columns ) {

	int32_t rows = 1;
	int32_t left = length;

	while ( left > columns ) {

		int32_t skip = 0;
		int32_t size = wrap_piece ( text, left, columns, skip );

		text += size + skip;
		left -= size + skip;

		if ( left <= 0 )
			break;

		rows++;

	}

	return rows;

}

void ui::app::draw_output ( ) {

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
	float advance = c.measure ( L"0000000000", render::font::MONO_SMALL ) / 10.0f;
	int32_t columns = base::max ( static_cast < int32_t > ( ( list.w - 30.0f ) / base::max ( advance, 1.0f ) ), 8 );
	int32_t total = 0;

	for ( log_line& line : output ) {

		if ( line.columns != columns ) {

			line.rows = wrap_rows ( line.text.get ( ), line.text.size ( ), columns );
			line.columns = columns;

		}

		total += line.rows;

	}

	float content = static_cast < float > ( total ) * line_h + 8.0f;
	uint64_t id = make_id ( "output_list" );

	scroll_region ( id, list );

	float wheel = take_wheel ( id );
	bool pinned = output_scroll.target >= output_scroll.limit - 1.0f;

	output_scroll.target -= wheel * line_h * 3.0f;

	if ( pinned && wheel == 0.0f )
		output_scroll.target = 1e9f;

	glide ( output_scroll, content - list.h );
	c.push_clip ( list );

	int32_t first = base::max ( static_cast < int32_t > ( output_scroll.shown / line_h ), 0 );
	int32_t last = first + static_cast < int32_t > ( list.h / line_h ) + 2;
	int32_t row = 0;

	for ( int32_t i = 0; i < output.count ( ) && row < last; i++ ) {

		const log_line& line = output [ static_cast < size_t > ( i ) ];

		if ( row + line.rows > first )
			draw_log_line ( line, list, row, columns );

		row += line.rows;

	}

	c.pop_clip ( );
	draw_scrollbar ( make_id ( "output_bar" ), list, output_scroll, content, list.h );

	math::rect line_area ( area.x + 6.0f, area.bottom ( ) - field_h + 2.0f, area.w - 12.0f, metric::field );

	if ( text_field ( make_id ( "command_line" ), command_field, line_area, L"Command: go 140001000, functions where calls VirtualAlloc and size > 0x100, help", icons::CHEVRON_RIGHT, render::font::MONO ) )
		run_command_line ( );

	c.pop_clip ( );

}

void ui::app::draw_log_line ( const log_line& line, math::rect list, int32_t row, int32_t columns ) {

	render::canvas& c = canvas;
	float line_h = metric::log_row;
	float top = list.y + 4.0f + static_cast < float > ( row ) * line_h - output_scroll.shown;
	render::color mark = line.kind == log_kind::GOOD ? ink ( tone::SUCCESS ) : ( line.kind == log_kind::WARN ? ink ( tone::WARNING ) : ( line.kind == log_kind::FAIL ? ink ( tone::DANGER ) : ink ( tone::INK_FAINT ) ) );
	const wchar_t* text = line.text.get ( );
	bool echo = text [ 0 ] == L'>' && text [ 1 ] == L' ';
	render::color tint = echo ? ink ( tone::ACCENT ) : ( line.kind == log_kind::FAIL ? ink ( tone::DANGER ) : ink ( tone::INK ) );
	int32_t left = line.text.size ( );
	wchar_t piece [ 1024 ];

	if ( line.kind != log_kind::INFO )
		c.fill ( math::rect ( list.x + 8.0f, top + line_h * 0.5f - 4.0f, 2.0f, 8.0f ), 0.0f, mark );

	for ( int32_t r = 0; r < line.rows; r++ ) {

		int32_t skip = 0;
		int32_t size = wrap_piece ( text, left, columns, skip );
		int32_t shown = base::min ( size, 1023 );
		float cy = top + ( static_cast < float > ( r ) + 0.5f ) * line_h;

		for ( int32_t k = 0; k < shown; k++ )
			piece [ k ] = text [ k ];

		piece [ shown ] = 0;

		if ( cy > list.y - line_h && cy < list.bottom ( ) + line_h )
			c.text ( piece, render::font::MONO_SMALL, math::vec2 ( list.x + 16.0f, cy ), tint );

		text += size + skip;
		left -= size + skip;

	}

}