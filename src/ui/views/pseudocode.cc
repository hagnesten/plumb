#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	static tone syntax_tone ( decompiler::syntax kind ) {

		switch ( kind ) {

			case decompiler::syntax::KEYWORD:
				return tone::KEYWORD;

			case decompiler::syntax::CONTROL:
				return tone::CONTROL;

			case decompiler::syntax::TYPE:
				return tone::TYPE;

			case decompiler::syntax::FUNCTION:
				return tone::FUNCTION;

			case decompiler::syntax::LOCAL:
				return tone::LOCAL;

			case decompiler::syntax::PARAMETER:
				return tone::PARAMETER;

			case decompiler::syntax::GLOBAL:
				return tone::GLOBAL;

			case decompiler::syntax::NUMBER:
				return tone::NUMBER;

			case decompiler::syntax::STRING:
				return tone::STRING;

			case decompiler::syntax::COMMENT:
				return tone::COMMENT;

			case decompiler::syntax::OPERATOR:
				return tone::OPERATOR;

			case decompiler::syntax::LABEL:
				return tone::LABEL;

			case decompiler::syntax::IMPORT:
				return tone::IMPORT;

			default:
				return tone::INK;

		}

	}

	void app::draw_pseudocode ( math::rect area ) {

		render::canvas& c = canvas;

		c.fill ( area, 0.0f, ink ( tone::BASE ) );

		if ( !db || !analysis_applied ) {

			c.text ( L"Analyzing\x2026 the C view opens as soon as functions are known.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

			return;

		}

		const decompiler::result* code = pseudocode_for ( now->function );

		if ( !code || !code->ok ) {

			c.text ( L"Couldn't build C for this function.", render::font::UI, math::vec2 ( area.x + 24.0f, area.y + 28.0f ), ink ( tone::INK_SOFT ) );

			return;

		}

		float row_h = metric::code_row;
		float adv = c.measure ( L"0000000000", render::font::MONO ) * 0.1f;
		uint64_t id = make_id ( "code", drawing_serial );
		widget& surface = state ( id );
		int32_t total = code->lines.count ( );
		float side_bar = 12.0f;
		int32_t widest = 0;

		for ( const decompiler::line& item : code->lines )
			widest = base::max ( widest, item.indent * 4 + item.length );

		if ( now->code_seen != code->function || now->code_seen_revision != names_revision ) {

			now->code_seen = code->function;
			now->code_seen_revision = names_revision;
			now->code_ranged = false;
			now->code_token = -1;

		}

		if ( total )
			now->code_line = base::clamp ( now->code_line, 0, total - 1 );

		if ( !now->code_ranged )
			now->code_anchor = now->code_line;

		int32_t digits = 8;
		uint64_t highest = file.base;

		for ( const loader::section& part : file.sections )
			highest = base::max ( highest, part.address + base::max ( part.size, part.raw ) );

		while ( digits < 16 && ( highest >> ( digits * 4 ) ) )
			digits++;

		bool gutter = area.w > adv * 40.0f;
		bool addresses = area.w > adv * 100.0f;
		float small_adv = c.measure ( L"0000000000", render::font::MONO_SMALL ) * 0.1f;
		int32_t number_digits = total >= 10000 ? 5 : ( total >= 1000 ? 4 : 3 );
		float number_right = area.x + 10.0f + adv * static_cast < float > ( number_digits );
		float address_x = number_right + adv * 1.5f;
		float x_base = gutter ? ( addresses ? address_x + small_adv * static_cast < float > ( digits ) + adv * 2.5f : number_right + adv * 2.5f ) : area.x + 18.0f;
		float columns = ( area.right ( ) - x_base - 16.0f ) / adv;
		bool sideways = static_cast < float > ( widest + 2 ) > columns;
		math::rect text_area ( area.x, area.y, area.w, area.h - ( sideways ? side_bar : 0.0f ) );
		float visible = text_area.h / row_h;
		float wheel = 0.0f;

		scroll_region ( id, area );
		wheel = take_wheel ( id );

		if ( key_shift && sideways )
			now->code_side.target -= wheel * 8.0f;
		else
			now->code_scroll.target -= wheel * 3.0f;

		if ( scroll_hot == id && wheel_side != 0.0f && sideways ) {

			now->code_side.target += wheel_side * 8.0f;
			wheel_side = 0.0f;

		}

		glide ( now->code_scroll, static_cast < float > ( total + 3 ) - visible, row_h );
		glide ( now->code_side, sideways ? static_cast < float > ( widest + 4 ) - columns : 0.0f, adv );
		interact ( id, text_area, surface );

		float x_text = x_base - base::max ( now->code_side.shown, 0.0f ) * adv;
		math::rect inner ( x_base - adv, text_area.y, text_area.right ( ) - x_base + adv, text_area.h );
		float offset = base::max ( now->code_scroll.shown, 0.0f );
		int32_t first = static_cast < int32_t > ( offset );
		float shift = ( offset - static_cast < float > ( first ) ) * row_h;

		auto line_text = [ & ] ( int32_t row, wchar_t* out, int32_t& length ) {

			const decompiler::line& item = code->lines [ static_cast < size_t > ( row ) ];

			length = 0;

			for ( int32_t k = 0; k < item.indent * 4 && length < 1000; k++ )
				out [ length++ ] = L' ';

			for ( int32_t k = 0; k < item.length && length < 1000; k++ )
				out [ length++ ] = code->text [ static_cast < size_t > ( item.text + k ) ];

			out [ length ] = 0;

		};

		uint64_t jump = 0;
		uint64_t open_function = 0;

		if ( active_id == id && mouse_down && total ) {

			float local = ( mouse.y - area.y + shift ) / row_h;
			int32_t row = base::clamp ( first + static_cast < int32_t > ( local < 0.0f ? local - 1.0f : local ), 0, total - 1 );
			wchar_t line [ 1024 ];
			int32_t length = 0;

			line_text ( row, line, length );

			int32_t column = base::clamp ( static_cast < int32_t > ( math::floor ( ( mouse.x - x_text ) / adv + 0.5f ) ), 0, length );

			if ( press_event && over ( text_area ) ) {

				now->code_line = row;
				now->code_col = column;
				now->code_selecting = double_id != id;
				blink_from = clock;

				if ( key_shift )
					now->code_ranged = true;
				else {

					now->code_anchor = row;
					now->code_anchor_col = column;
					now->code_ranged = false;

				}

				if ( double_id == id ) {

					const decompiler::line& item = code->lines [ static_cast < size_t > ( row ) ];
					int32_t exact = static_cast < int32_t > ( math::floor ( ( mouse.x - x_text ) / adv ) );
					int32_t inside = exact - item.indent * 4;
					const decompiler::span* link = nullptr;
					int32_t from = 0;
					int32_t to = 0;

					for ( int32_t s = 0; s < item.span_count; s++ ) {

						const decompiler::span& part = code->spans [ static_cast < size_t > ( item.first_span + s ) ];
						int32_t begin = part.start - item.text;

						if ( part.target && inside >= begin && inside < begin + part.length )
							link = &part;

					}

					if ( link && link->target != code->function ) {

						if ( link->kind == decompiler::syntax::FUNCTION && db->function_starting ( link->target ) >= 0 )
							open_function = link->target;
						else
							jump = link->target;

					} else if ( word_bounds ( line, length, base::clamp ( exact, 0, length ), from, to ) ) {

						now->code_anchor = row;
						now->code_anchor_col = from;
						now->code_col = to;
						now->code_ranged = true;

					}

				}

			} else if ( now->code_selecting && ( row != now->code_line || column != now->code_col ) ) {

				now->code_line = row;
				now->code_col = column;
				now->code_ranged = now->code_anchor != row || now->code_anchor_col != column;
				blink_from = clock;

			}

			if ( now->code_selecting ) {

				float visible_rows = area.h / row_h;

				if ( static_cast < float > ( row ) < now->code_scroll.target )
					now->code_scroll.target = static_cast < float > ( row );
				else if ( static_cast < float > ( row ) > now->code_scroll.target + visible_rows - 1.0f )
					now->code_scroll.target = static_cast < float > ( row ) - visible_rows + 1.0f;

			}

			if ( code->lines [ static_cast < size_t > ( now->code_line ) ].address ) {

				now->cursor_address = code->lines [ static_cast < size_t > ( now->code_line ) ].address;
				sync_panes ( now->cursor_address );

			}

		}

		if ( !mouse_down )
			now->code_selecting = false;

		now->code_y = static_cast < float > ( now->code_line );
		now->code_ready = true;

		uint64_t follow = 0;

		for ( int32_t p = 0; p < pane_count; p++ ) {

			int32_t other = pane_document ( p );

			if ( other < 0 || documents [ other ].kind != view::LISTING )
				continue;

			uint64_t address = documents [ other ].state.cursor_address;

			if ( db->function_at ( address ) >= 0 && db->functions [ static_cast < size_t > ( db->function_at ( address ) ) ].start == code->function )
				follow = address;

		}

		int32_t synced = -1;

		if ( follow ) {

			uint64_t best = 0;

			for ( int32_t i = 0; i < total; i++ ) {

				uint64_t address = code->lines [ static_cast < size_t > ( i ) ].address;

				if ( address && address <= follow && address >= best ) {

					best = address;
					synced = i;

				}

			}

		}

		bool forward = now->code_anchor < now->code_line || ( now->code_anchor == now->code_line && now->code_anchor_col <= now->code_col );
		int32_t low = forward ? now->code_anchor : now->code_line;
		int32_t high = forward ? now->code_line : now->code_anchor;
		int32_t low_col = forward ? now->code_anchor_col : now->code_col;
		int32_t high_col = forward ? now->code_col : now->code_anchor_col;
		bool ranged = now->code_ranged && ( low != high || low_col != high_col );
		float cursor_y = area.y + ( now->code_y - offset ) * row_h;
		int32_t count = static_cast < int32_t > ( visible ) + 2;
		bool hand = false;

		c.push_clip ( text_area );
		c.fill ( math::rect ( area.x, cursor_y, 2.5f, row_h ), 0.0f, ink ( tone::SELECT_LINE ) );

		if ( synced >= 0 && synced != now->code_line ) {

			float sy = area.y + ( static_cast < float > ( synced ) - offset ) * row_h;

			c.fill ( math::rect ( area.x, sy, area.w, row_h ), 0.0f, ink ( tone::ACCENT_SOFT ) );

		}

		for ( int32_t k = 0; k < count; k++ ) {

			int32_t row = first + k;

			if ( row >= total )
				break;

			const decompiler::line& item = code->lines [ static_cast < size_t > ( row ) ];
			float y = area.y + static_cast < float > ( k ) * row_h - shift;

			c.push_clip ( inner );

			for ( int32_t guide = 1; guide <= item.indent; guide++ ) {

				float gx = x_text + static_cast < float > ( guide * 4 - 4 ) * adv + adv * 0.5f;

				c.fill ( math::rect ( gx, y, 1.0f, row_h ), 0.0f, ink ( tone::LINE ).fade ( 0.7f ) );

			}

			if ( ranged && row >= low && row <= high ) {

				int32_t length = item.indent * 4 + item.length;
				int32_t from = row == low ? low_col : 0;
				int32_t to = row == high ? high_col : length + 1;

				if ( to > from )
					c.fill ( math::rect ( x_text + static_cast < float > ( from ) * adv, y + 1.0f, static_cast < float > ( to - from ) * adv, row_h - 2.0f ), 2.0f, ink ( tone::SELECT ) );

			}

			c.pop_clip ( );

		}

		for ( int32_t k = 0; k < count; k++ ) {

			int32_t row = first + k;

			if ( row >= total )
				break;

			const decompiler::line& item = code->lines [ static_cast < size_t > ( row ) ];
			float y = area.y + static_cast < float > ( k ) * row_h - shift;
			float cy = y + row_h * 0.5f;
			float x = x_text + static_cast < float > ( item.indent * 4 ) * adv;

			if ( gutter ) {

				base::writer number;

				number.number ( row + 1 );
				c.text ( number.text ( ), render::font::MONO, math::vec2 ( number_right, cy ), ink ( row == now->code_line ? tone::INK_SOFT : tone::INK_FAINT ).fade ( row == now->code_line ? 1.0f : 0.75f ), render::align::RIGHT );

				if ( addresses && item.address ) {

					base::writer where;

					where.hex ( item.address, digits );
					c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( address_x, cy ), ink ( row == synced ? tone::ADDRESS : tone::INK_FAINT ).fade ( row == now->code_line || row == synced ? 1.0f : 0.6f ) );

				}

			}

			c.push_clip ( inner );

			int32_t at = item.text;
			int32_t end = item.text + item.length;
			int32_t span_index = 0;
			wchar_t piece [ 400 ];

			while ( at < end ) {

				const decompiler::span* part = span_index < item.span_count ? &code->spans [ static_cast < size_t > ( item.first_span + span_index ) ] : nullptr;
				int32_t stop = part ? ( part->start > at ? part->start : part->start + part->length ) : end;
				tone color = part && part->start <= at ? syntax_tone ( part->kind ) : tone::INK;

				stop = base::min ( stop, end );

				if ( stop <= at )
					stop = at + 1;

				int32_t length = base::min ( stop - at, 399 );

				for ( int32_t i = 0; i < length; i++ )
					piece [ i ] = code->text [ static_cast < size_t > ( at + i ) ];

				piece [ length ] = 0;

				float px = x + static_cast < float > ( at - item.text ) * adv;

				if ( px > area.right ( ) )
					break;

				c.text ( piece, render::font::MONO, math::vec2 ( px, cy ), ink ( color ) );

				if ( part && part->start <= at ) {

					if ( part->target && part->target != code->function && !now->code_selecting && over ( math::rect ( px, y, static_cast < float > ( length ) * adv, row_h ) ) && over ( text_area ) ) {

						hand = true;
						c.fill ( math::rect ( px, cy + 7.0f, static_cast < float > ( length ) * adv, 1.0f ), 0.0f, ink ( color ).fade ( 0.7f ) );

						if ( clicked_id == id && key_control ) {

							if ( part->kind == decompiler::syntax::FUNCTION && db->function_starting ( part->target ) >= 0 )
								open_function = part->target;
							else
								jump = part->target;

						}

					}

					span_index++;

				}

				at = stop;

			}

			c.pop_clip ( );

		}

		bool focused_view = focused && !focused_field && pane_document ( focused_pane ) >= 0 && &documents [ pane_document ( focused_pane ) ].state == now;

		if ( focused_view && cursor_y > area.y - row_h && cursor_y < text_area.bottom ( ) ) {

			double since = clock - blink_from;
			bool lit = since < 0.6 || since > 60.0 || static_cast < int64_t > ( ( since - 0.6 ) / 0.53 ) % 2 == 1;

			caret_live = true;

			if ( lit || now->code_selecting ) {

				c.push_clip ( inner );
				c.fill ( math::rect ( x_text + static_cast < float > ( now->code_col ) * adv - 0.75f, cursor_y + 3.0f, 1.5f, row_h - 6.0f ), 0.5f, ink ( tone::INK ) );
				c.pop_clip ( );

			}

		}

		if ( hand )
			wanted_cursor = pointer::HAND;
		else if ( surface.hover > 0.5f && over ( text_area ) )
			wanted_cursor = pointer::TEXT;

		if ( open_function ) {

			now->back_stack.push ( now->function );
			now->forward_stack.clear ( );
			now->function = open_function;
			now->code_scroll = scroller ( );
			now->code_line = 0;
			now->code_col = 0;
			now->code_ranged = false;
			now->code_ready = false;

		} else if ( jump ) {

			now->code_ranged = false;
			go ( jump, true );

		}

		float end_y = area.y + ( static_cast < float > ( total ) - offset ) * row_h + 6.0f;

		if ( end_y > area.y && end_y < area.bottom ( ) ) {

			wchar_t name [ 200 ];
			int32_t owner = db->function_starting ( code->function );
			base::writer tag;

			if ( owner >= 0 )
				function_name ( owner, name, 200 );
			else
				name [ 0 ] = 0;

			tag.add ( L" end of " ).add ( name ).add ( L"  \x00B7  " ).number ( code->instructions ).add ( L" instructions, " ).number ( code->blocks ).add ( L" blocks" );

			if ( code->gotos )
				tag.add ( L", " ).number ( code->gotos ).add ( code->gotos == 1 ? L" goto" : L" gotos" );

			tag.add ( L"  \x00B7  " ).fixed ( code->seconds * 1000.0, 2 ).add ( L" ms " );
			c.fill ( math::rect ( x_base, end_y + 9.0f, area.right ( ) - x_base - 16.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );
			c.fill ( math::rect ( x_base + 8.0f, end_y + 1.0f, c.measure ( tag.text ( ), render::font::SMALL ) + 8.0f, 16.0f ), 3.0f, ink ( tone::BASE ) );
			c.text ( tag.text ( ), render::font::SMALL, math::vec2 ( x_base + 12.0f, end_y + 9.0f ), ink ( tone::INK_FAINT ) );

		}

		c.pop_clip ( );
		draw_scrollbar ( make_id ( "code_bar", drawing_serial ), text_area, now->code_scroll, static_cast < float > ( total + 3 ), visible );

		if ( sideways )
			draw_side_scrollbar ( make_id ( "code_side", drawing_serial ), math::rect ( x_base, text_area.bottom ( ), area.right ( ) - x_base - 12.0f, side_bar ), now->code_side, static_cast < float > ( widest + 4 ), columns );

	}

}