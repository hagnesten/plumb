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

	if ( icon_button ( make_id ( "output_clear" ), math::vec2 ( bar.right ( ) - 16.0f, bar.center ( ).y ), 20.0f, icons::CANCEL, 9.0f, L"Clear output" ) ) {

		output.clear ( );
		output_anchor_line = -1;
		output_caret_line = -1;

	}

	if ( icon_button ( make_id ( "output_copy_all" ), math::vec2 ( bar.right ( ) - 40.0f, bar.center ( ).y ), 20.0f, icons::COPY, 9.0f, output_has_selection ( ) ? L"Copy the selection" : L"Copy all output" ) )
		copy_output ( !output_has_selection ( ) );

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
	widget& surface = state ( id );

	interact ( id, list, surface );

	if ( surface.hover > 0.5f )
		wanted_cursor = pointer::TEXT;

	output_scroll.target -= wheel * line_h * 3.0f;

	if ( press_event && !over ( area ) )
		output_focused = false;

	if ( active_id == id && press_event && !output.empty ( ) ) {

		int32_t spot_line = 0;
		int32_t spot_char = 0;

		output_focused = true;

		if ( output_spot ( list, columns, advance, spot_line, spot_char ) ) {

			if ( double_event ) {

				output_anchor_line = spot_line;
				output_anchor_char = 0;
				output_caret_line = spot_line;
				output_caret_char = output [ static_cast < size_t > ( spot_line ) ].text.size ( );
				output_selecting = false;

			} else {

				if ( !key_shift || output_anchor_line < 0 ) {

					output_anchor_line = spot_line;
					output_anchor_char = spot_char;

				}

				output_caret_line = spot_line;
				output_caret_char = spot_char;
				output_selecting = true;

			}

		}

	}

	if ( output_selecting && mouse_down ) {

		int32_t spot_line = 0;
		int32_t spot_char = 0;

		if ( mouse.y < list.y + 2.0f )
			output_scroll.target -= line_h;
		else if ( mouse.y > list.bottom ( ) - 2.0f )
			output_scroll.target += line_h;

		if ( output_spot ( list, columns, advance, spot_line, spot_char ) ) {

			output_caret_line = spot_line;
			output_caret_char = spot_char;

		}

		pending = base::max ( pending, 2 );

	} else
		output_selecting = false;

	if ( pinned && wheel == 0.0f && !output_selecting )
		output_scroll.target = 1e9f;

	glide ( output_scroll, content - list.h );
	c.push_clip ( list );

	int32_t first = base::max ( static_cast < int32_t > ( output_scroll.shown / line_h ), 0 );
	int32_t last = first + static_cast < int32_t > ( list.h / line_h ) + 2;
	int32_t row = 0;

	for ( int32_t i = 0; i < output.count ( ) && row < last; i++ ) {

		const log_line& line = output [ static_cast < size_t > ( i ) ];

		if ( row + line.rows > first )
			draw_log_line ( line, i, list, row, columns, advance );

		row += line.rows;

	}

	c.pop_clip ( );
	draw_scrollbar ( make_id ( "output_bar" ), list, output_scroll, content, list.h );

	math::rect line_area ( area.x + 6.0f, area.bottom ( ) - field_h + 2.0f, area.w - 12.0f, metric::field );

	if ( text_field ( make_id ( "command_line" ), command_field, line_area, L"Command: go 140001000, functions where calls VirtualAlloc and size > 0x100, help", icons::CHEVRON_RIGHT, render::font::MONO ) )
		run_command_line ( );

	c.pop_clip ( );

}

void ui::app::draw_log_line ( const log_line& line, int32_t index, math::rect list, int32_t row, int32_t columns, float advance ) {

	render::canvas& c = canvas;
	float line_h = metric::log_row;
	float top = list.y + 4.0f + static_cast < float > ( row ) * line_h - output_scroll.shown;
	render::color mark = line.kind == log_kind::GOOD ? ink ( tone::SUCCESS ) : ( line.kind == log_kind::WARN ? ink ( tone::WARNING ) : ( line.kind == log_kind::FAIL ? ink ( tone::DANGER ) : ink ( tone::INK_FAINT ) ) );
	const wchar_t* text = line.text.get ( );
	const wchar_t* begin = text;
	bool echo = text [ 0 ] == L'>' && text [ 1 ] == L' ';
	render::color tint = echo ? ink ( tone::ACCENT ) : ( line.kind == log_kind::FAIL ? ink ( tone::DANGER ) : ink ( tone::INK ) );
	int32_t total = line.text.size ( );
	int32_t left = total;
	int32_t select_from = -1;
	int32_t select_to = -1;
	base::list < wchar_t > piece;

	if ( output_has_selection ( ) ) {

		bool forward = output_anchor_line < output_caret_line || ( output_anchor_line == output_caret_line && output_anchor_char <= output_caret_char );
		int32_t low_line = forward ? output_anchor_line : output_caret_line;
		int32_t low_char = forward ? output_anchor_char : output_caret_char;
		int32_t high_line = forward ? output_caret_line : output_anchor_line;
		int32_t high_char = forward ? output_caret_char : output_anchor_char;

		if ( index >= low_line && index <= high_line ) {

			select_from = index == low_line ? low_char : 0;
			select_to = index == high_line ? high_char : total + 1;

		}

	}

	if ( line.kind != log_kind::INFO )
		c.fill ( math::rect ( list.x + 8.0f, top + line_h * 0.5f - 4.0f, 2.0f, 8.0f ), 0.0f, mark );

	for ( int32_t r = 0; r < line.rows; r++ ) {

		int32_t skip = 0;
		int32_t size = wrap_piece ( text, left, columns, skip );
		int32_t start = static_cast < int32_t > ( text - begin );
		float cy = top + ( static_cast < float > ( r ) + 0.5f ) * line_h;
		bool visible = cy > list.y - line_h && cy < list.bottom ( ) + line_h;

		if ( visible && select_from >= 0 ) {

			int32_t from = base::max ( select_from, start );
			int32_t to = base::min ( select_to, start + size + skip + ( r == line.rows - 1 ? 1 : 0 ) );

			if ( to > from )
				c.fill ( math::rect ( list.x + 16.0f + static_cast < float > ( from - start ) * advance, cy - line_h * 0.5f, static_cast < float > ( to - from ) * advance, line_h ), 0.0f, ink ( tone::SELECT ) );

		}

		if ( visible ) {

			piece.clear ( );

			for ( int32_t k = 0; k < size; k++ )
				piece.push ( text [ k ] );

			piece.push ( 0 );
			c.text ( piece.data ( ), render::font::MONO_SMALL, math::vec2 ( list.x + 16.0f, cy ), tint );

		}

		text += size + skip;
		left -= size + skip;

	}

}

bool ui::app::output_spot ( math::rect list, int32_t columns, float advance, int32_t& line_out, int32_t& offset_out ) const {

	if ( output.empty ( ) )
		return false;

	float line_h = metric::log_row;
	float y = mouse.y - ( list.y + 4.0f ) + output_scroll.shown;
	int32_t want = static_cast < int32_t > ( y >= 0.0f ? y / line_h : -1.0f );

	if ( want < 0 ) {

		line_out = 0;
		offset_out = 0;

		return true;

	}

	int32_t row = 0;

	for ( int32_t i = 0; i < output.count ( ); i++ ) {

		const log_line& line = output [ static_cast < size_t > ( i ) ];

		if ( want >= row + line.rows ) {

			row += line.rows;

			continue;

		}

		const wchar_t* begin = line.text.get ( );
		const wchar_t* text = begin;
		int32_t left = line.text.size ( );

		for ( int32_t r = 0; r < line.rows; r++ ) {

			int32_t skip = 0;
			int32_t size = wrap_piece ( text, left, columns, skip );

			if ( row + r == want ) {

				int32_t column = static_cast < int32_t > ( ( mouse.x - ( list.x + 16.0f ) ) / base::max ( advance, 1.0f ) + 0.5f );

				line_out = i;
				offset_out = static_cast < int32_t > ( text - begin ) + base::clamp ( column, 0, size );

				return true;

			}

			text += size + skip;
			left -= size + skip;

		}

		line_out = i;
		offset_out = line.text.size ( );

		return true;

	}

	line_out = output.count ( ) - 1;
	offset_out = output.back ( ).text.size ( );

	return true;

}

bool ui::app::output_has_selection ( ) const {

	return output_anchor_line >= 0 && output_caret_line >= 0 && output_anchor_line < output.count ( ) && output_caret_line < output.count ( ) && ( output_anchor_line != output_caret_line || output_anchor_char != output_caret_char );

}

void ui::app::copy_output ( bool everything ) {

	base::list < wchar_t > joined;
	int32_t low_line = 0;
	int32_t low_char = 0;
	int32_t high_line = output.count ( ) - 1;
	int32_t high_char = high_line >= 0 ? output [ static_cast < size_t > ( high_line ) ].text.size ( ) : 0;

	if ( !everything ) {

		if ( !output_has_selection ( ) )
			return;

		bool forward = output_anchor_line < output_caret_line || ( output_anchor_line == output_caret_line && output_anchor_char <= output_caret_char );

		low_line = forward ? output_anchor_line : output_caret_line;
		low_char = forward ? output_anchor_char : output_caret_char;
		high_line = forward ? output_caret_line : output_anchor_line;
		high_char = forward ? output_caret_char : output_anchor_char;

	}

	for ( int32_t i = low_line; i <= high_line && i < output.count ( ); i++ ) {

		const log_line& line = output [ static_cast < size_t > ( i ) ];
		const wchar_t* text = line.text.get ( );
		int32_t size = line.text.size ( );
		int32_t from = i == low_line ? base::min ( low_char, size ) : 0;
		int32_t to = i == high_line ? base::min ( high_char, size ) : size;

		for ( int32_t k = from; k < to; k++ )
			joined.push ( text [ k ] );

		if ( i != high_line ) {

			joined.push ( L'\r' );
			joined.push ( L'\n' );

		}

	}

	joined.push ( 0 );
	copy_text ( joined.data ( ) );

}