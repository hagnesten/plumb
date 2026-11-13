#include "ui/app.hh"
#include "ui/icons.hh"
#include "ui/format.hh"
#include "ui/tokens.hh"

namespace ui {

	void app::draw_listing ( math::rect area ) {

		render::canvas& c = canvas;
		float row_h = metric::code_row;
		float adv = c.measure ( L"0000000000", render::font::MONO ) * 0.1f;
		uint64_t id = make_id ( "listing", drawing_serial );
		widget& surface = state ( id );
		float visible = area.h / row_h;
		bool analyzed = db && analysis_applied && !db->maps.empty ( );
		float x0 = area.x + 22.0f;

		c.fill ( area, 0.0f, ink ( tone::BASE ) );
		scroll_region ( id, area );
		now->listing_scroll.target -= take_wheel ( id ) * 3.0f;
		glide ( now->listing_scroll, static_cast < float > ( total_rows ) - visible + 3.0f, row_h );
		interact ( id, area, surface );

		float offset = base::max ( now->listing_scroll.shown, 0.0f );
		uint64_t first = static_cast < uint64_t > ( offset );
		float shift = ( offset - static_cast < float > ( first ) ) * row_h;
		int32_t count = static_cast < int32_t > ( visible ) + 2;

		listing_cols = listing_columns ( area.right ( ) - x0 - 12.0f, adv );

		const row_columns& cols = listing_cols;

		if ( total_rows && !now->selecting && ( now->row_build != listing_build || now->row_for != now->cursor_address ) ) {

			uint64_t address = now->cursor_address;
			uint64_t row = row_of ( address );
			float place = static_cast < float > ( row );
			bool outside = place < now->listing_scroll.target || place > now->listing_scroll.target + visible - 2.0f;

			set_cursor_row ( row, outside );

			if ( address )
				now->cursor_address = address;

			now->row_for = now->cursor_address;
			now->cursor_y = place;
			now->ranged = false;
			now->caret_col = cols.mnemonic;

		}

		if ( !now->ranged ) {

			now->select_anchor = now->cursor_row;
			now->anchor_col = now->caret_col;

		}

		if ( row_cache.count ( ) < count )
			row_cache.resize ( static_cast < size_t > ( count ) );

		for ( int32_t k = 0; k < count; k++ ) {

			uint64_t row = first + static_cast < uint64_t > ( k );

			if ( row < total_rows )
				compose_row ( row, cols, row_cache [ static_cast < size_t > ( k ) ] );
			else
				row_cache [ static_cast < size_t > ( k ) ].clear ( );

		}

		row_line scratch;
		uint64_t follow_target = 0;
		bool hand = false;

		auto line_for = [ & ] ( uint64_t row ) -> const row_line& {

			if ( row >= first && row < first + static_cast < uint64_t > ( count ) )
				return row_cache [ static_cast < size_t > ( row - first ) ];

			compose_row ( row, cols, scratch );

			return scratch;

		};

		auto column_at = [ & ] ( float x, const row_line& line ) {

			int32_t column = static_cast < int32_t > ( math::floor ( ( x - x0 ) / adv + 0.5f ) );

			return base::clamp ( column, 0, line.length );

		};

		if ( active_id == id && mouse_down && total_rows ) {

			float local = ( mouse.y - area.y + shift ) / row_h;
			int64_t pointed = static_cast < int64_t > ( first ) + static_cast < int64_t > ( local < 0.0f ? local - 1.0f : local );
			uint64_t row = static_cast < uint64_t > ( base::clamp < int64_t > ( pointed, 0, static_cast < int64_t > ( total_rows ) - 1 ) );
			const row_line& line = line_for ( row );
			int32_t column = column_at ( mouse.x, line );

			if ( press_event && over ( area ) ) {

				set_cursor_row ( row, false );
				now->caret_col = column;
				blink_from = clock;

				if ( key_shift )
					now->ranged = true;
				else {

					now->select_anchor = row;
					now->anchor_col = column;
					now->ranged = false;

				}

				now->selecting = double_id != id;

				if ( double_id == id ) {

					int32_t exact = static_cast < int32_t > ( math::floor ( ( mouse.x - x0 ) / adv ) );
					int32_t hit = line.run_at ( exact );
					int32_t from = 0;
					int32_t to = 0;

					if ( hit >= 0 && line.runs [ hit ].target && line.runs [ hit ].target != line.address ) {

						follow_target = line.runs [ hit ].target;

					} else if ( word_bounds ( line.text, line.length, base::clamp ( exact, 0, line.length ), from, to ) ) {

						now->select_anchor = row;
						now->anchor_col = from;
						now->caret_col = to;
						now->ranged = true;

					}

				}

			} else if ( now->selecting && ( row != now->cursor_row || column != now->caret_col ) ) {

				if ( row != now->cursor_row )
					set_cursor_row ( row, false );

				now->caret_col = column;
				now->ranged = now->select_anchor != now->cursor_row || now->anchor_col != now->caret_col;
				blink_from = clock;

				if ( mouse.y < area.y || mouse.y > area.bottom ( ) )
					animating = true;

			}

		}

		if ( !mouse_down )
			now->selecting = false;

		now->cursor_y = static_cast < float > ( now->cursor_row );
		now->cursor_ready = true;

		bool forward = now->select_anchor < now->cursor_row || ( now->select_anchor == now->cursor_row && now->anchor_col <= now->caret_col );
		uint64_t low = forward ? now->select_anchor : now->cursor_row;
		uint64_t high = forward ? now->cursor_row : now->select_anchor;
		int32_t low_col = forward ? now->anchor_col : now->caret_col;
		int32_t high_col = forward ? now->caret_col : now->anchor_col;
		bool ranged = now->ranged && ( low != high || low_col != high_col );

		c.push_clip ( area );

		for ( int32_t k = 0; k < count; k++ ) {

			uint64_t row = first + static_cast < uint64_t > ( k );

			if ( row >= total_rows )
				break;

			const row_line& line = row_cache [ static_cast < size_t > ( k ) ];
			float y = area.y + static_cast < float > ( k ) * row_h - shift;
			float cy = y + row_h * 0.5f;
			int32_t function = analyzed && line.address ? db->function_at ( line.address ) : -1;
			bool inside = function >= 0 && line.kind != line_kind::BANNER && line.kind != line_kind::BLANK;

			if ( line.kind == line_kind::BANNER && line.decor ) {

				c.fill ( math::rect ( area.x, y + 1.0f, area.w, row_h - 2.0f ), 0.0f, ink ( tone::PANEL ) );
				c.fill ( math::rect ( area.x, y + 1.0f, area.w, 1.0f ), 0.0f, ink ( tone::LINE ) );
				c.fill ( math::rect ( area.x, y + row_h - 2.0f, area.w, 1.0f ), 0.0f, ink ( tone::LINE ) );

			} else if ( line.kind == line_kind::FUNCTION ) {

				c.fill ( math::rect ( area.x, y, area.w, row_h ), 0.0f, ink ( tone::RAISED ) );
				c.fill ( math::rect ( area.x, y, area.w, 1.0f ), 0.0f, ink ( tone::LINE_STRONG ) );

			} else if ( line.kind == line_kind::END )
				c.fill ( math::rect ( x0, cy, area.right ( ) - x0 - 12.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );

			if ( inside ) {

				float top = line.kind == line_kind::FUNCTION ? cy : y;
				float bottom = line.kind == line_kind::END ? cy + 1.0f : y + row_h;

				c.fill ( math::rect ( area.x + 9.0f, top, 2.0f, bottom - top ), 0.0f, ink ( tone::GUIDE ) );

				if ( line.kind == line_kind::END )
					c.fill ( math::rect ( area.x + 9.0f, cy - 1.0f, x0 - area.x - 12.0f, 2.0f ), 0.0f, ink ( tone::GUIDE ) );

			}

			if ( line.instruction && debug_pc && line.address == debug_pc ) {

				c.fill ( math::rect ( area.x, y, area.w, row_h ), 0.0f, ink ( tone::WARNING ).fade ( 0.16f ) );
				c.fill ( math::rect ( area.x + 2.0f, cy - 5.0f, 6.0f, 10.0f ), 1.5f, ink ( tone::WARNING ) );

			}

			if ( line.instruction && !breakpoints.empty ( ) && is_breakpoint ( line.address ) )
				c.fill ( math::rect ( area.x + 11.0f, cy - 4.0f, 8.0f, 8.0f ), 4.0f, ink ( tone::DANGER ) );

			if ( ranged && row >= low && row <= high ) {

				int32_t from = row == low ? low_col : 0;
				int32_t to = row == high ? high_col : line.length + 1;

				if ( to > from )
					c.fill ( math::rect ( x0 + static_cast < float > ( from ) * adv, y, static_cast < float > ( to - from ) * adv, row_h ), 0.0f, ink ( tone::SELECT ) );

			}

		}

		float cursor_top = area.y + ( static_cast < float > ( now->cursor_row ) - offset ) * row_h;

		if ( cursor_top > area.y - row_h && cursor_top < area.bottom ( ) ) {

			c.fill ( math::rect ( area.x, cursor_top, area.w, row_h ), 0.0f, ink ( tone::HOVER ) );
			c.fill ( math::rect ( area.x, cursor_top, 2.0f, row_h ), 0.0f, ink ( tone::SELECT_LINE ) );

		}

		for ( int32_t k = 0; k < count; k++ ) {

			uint64_t row = first + static_cast < uint64_t > ( k );

			if ( row >= total_rows )
				break;

			const row_line& line = row_cache [ static_cast < size_t > ( k ) ];
			float y = area.y + static_cast < float > ( k ) * row_h - shift;
			float cy = y + row_h * 0.5f;

			if ( line.kind == line_kind::END && line.count ) {

				const token_run& run = line.runs [ 0 ];
				wchar_t tag [ 300 ];
				int32_t length = base::min ( run.length, 299 );

				for ( int32_t i = 0; i < length; i++ )
					tag [ i ] = line.text [ run.start + i ];

				tag [ length ] = 0;

				float tag_x = x0 + static_cast < float > ( run.start ) * adv;
				float tag_w = c.measure ( tag, render::font::SMALL );

				c.fill ( math::rect ( tag_x - 6.0f, cy - 7.0f, tag_w + 12.0f, 14.0f ), 0.0f, ink ( tone::BASE ) );
				c.text ( tag, render::font::SMALL, math::vec2 ( tag_x, cy ), ink ( tone::INK_FAINT ) );

				continue;

			}

			for ( int32_t r = 0; r < line.count; r++ ) {

				const token_run& run = line.runs [ r ];
				float rx = x0 + static_cast < float > ( run.start ) * adv;

				if ( rx > area.right ( ) )
					break;

				wchar_t piece [ 400 ];
				int32_t length = base::min ( run.length, 399 );

				for ( int32_t i = 0; i < length; i++ )
					piece [ i ] = line.text [ run.start + i ];

				piece [ length ] = 0;

				render::font role = line.decor && r > 0 ? render::font::MONO_SMALL : ( line.strong [ r ] ? render::font::MONO_STRONG : render::font::MONO );

				c.text ( piece, role, math::vec2 ( rx, cy ), ink ( run.color ) );

				if ( run.target && run.target != line.address && !line.decor ) {

					math::rect token_area ( rx, y, static_cast < float > ( length ) * adv, row_h );

					if ( over ( token_area ) && over ( area ) && !now->selecting ) {

						c.fill ( math::rect ( rx, cy + 6.0f, token_area.w, 1.0f ), 0.0f, ink ( run.color ).fade ( 0.7f ) );
						hand = true;

						if ( clicked_id == id && key_control )
							follow_target = run.target;

					}

				}

			}

		}

		bool focused_view = focused && !focused_field && pane_count >= 1 && pane_document ( focused_pane ) >= 0 && &documents [ pane_document ( focused_pane ) ].state == now;

		if ( focused_view && cursor_top > area.y - row_h && cursor_top < area.bottom ( ) ) {

			double since = clock - blink_from;
			bool lit = since < 0.6 || since > 60.0 || static_cast < int64_t > ( ( since - 0.6 ) / 0.53 ) % 2 == 1;

			caret_live = true;

			if ( lit || now->selecting )
				c.fill ( math::rect ( x0 + static_cast < float > ( now->caret_col ) * adv - 0.5f, cursor_top + 2.0f, 1.0f, row_h - 4.0f ), 0.0f, ink ( tone::INK ) );

		}

		c.pop_clip ( );

		if ( hand )
			wanted_cursor = pointer::HAND;
		else if ( surface.hover > 0.5f && over ( area ) )
			wanted_cursor = pointer::TEXT;

		if ( follow_target ) {

			now->ranged = false;
			go ( follow_target, true );

		}

		draw_listing_map ( area );
		draw_scrollbar ( make_id ( "listing_bar", drawing_serial ), area, now->listing_scroll, static_cast < float > ( total_rows ) + 3.0f, visible );

	}

	void app::draw_listing_map ( math::rect area ) {

		if ( !total_rows || area.h < 60.0f )
			return;

		render::canvas& c = canvas;
		math::rect track ( area.right ( ) - 11.0f, area.y, 11.0f, area.h );
		float total = static_cast < float > ( total_rows ) + 3.0f;

		auto y_of = [ & ] ( uint64_t address ) {

			return track.y + static_cast < float > ( row_of ( address ) ) / total * track.h;

		};

		auto tick = [ & ] ( uint64_t address, render::color tint, float thick, float inset ) {

			float y = math::round ( y_of ( address ) - thick * 0.5f );

			c.fill ( math::rect ( track.x + inset, y, track.w - inset - 1.0f, thick ), 0.0f, tint );

		};

		c.fill ( track, 0.0f, ink ( tone::PANEL ).fade ( 0.55f ) );
		c.fill ( math::rect ( track.x, track.y, 1.0f, track.h ), 0.0f, ink ( tone::LINE ) );

		for ( const loader::section& item : file.sections ) {

			if ( !item.size )
				continue;

			tone band = item.type == loader::kind::CODE ? tone::CODE_BAND : ( item.type == loader::kind::CONSTANT ? tone::CONST_BAND : ( item.type == loader::kind::EMPTY ? tone::EMPTY_BAND : ( item.type == loader::kind::HEADER ? tone::HEADER_BAND : tone::DATA_BAND ) ) );
			float top = y_of ( item.address );
			float bottom = y_of ( item.address + item.size - 1 ) + 1.0f;

			c.fill ( math::rect ( track.x + 1.0f, top, 2.0f, base::max ( bottom - top, 1.0f ) ), 0.0f, ink ( band ) );

		}

		int32_t hits = base::min ( search_hits.count ( ), 4000 );

		for ( int32_t i = 0; i < hits; i++ )
			tick ( search_hits [ static_cast < size_t > ( i ) ].address, ink ( tone::INK_SOFT ).fade ( 0.6f ), 1.0f, 4.0f );

		uint64_t last_patch = ~0ull;

		for ( const patched_byte& item : patches ) {

			uint64_t address = 0;

			if ( item.offset == last_patch + 1 ) {

				last_patch = item.offset;

				continue;

			}

			last_patch = item.offset;

			if ( file.address_of ( item.offset, address ) )
				tick ( address, ink ( tone::WARNING ), 2.0f, 4.0f );

		}

		for ( const user_name& item : bookmarks )
			tick ( item.address, ink ( tone::ACCENT ), 2.0f, 4.0f );

		for ( uint64_t address : breakpoints )
			tick ( address, ink ( tone::DANGER ), 2.0f, 4.0f );

		if ( debug_pc )
			tick ( debug_pc, ink ( tone::WARNING ), 3.0f, 1.0f );

		tick ( now->cursor_address, ink ( tone::INK ), 2.0f, 1.0f );

	}

}