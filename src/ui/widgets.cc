#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	void app::draw_brand ( math::vec2 center, float height ) {

		render::canvas& c = canvas;
		float s = height;
		float top = center.y - s * 0.5f;
		render::color brass = ink ( tone::ACCENT );
		render::color deep = render::mix ( brass, render::rgb ( 0x000000 ), 0.32f );
		render::color line = ink ( tone::INK_SOFT );
		float body_top = top + s * 0.3f;
		float shoulder = body_top + s * 0.3f;
		float bottom = top + s;
		float half = s * 0.25f;

		c.fill ( math::rect ( center.x - s * 0.018f - 0.4f, top, s * 0.036f + 0.8f, s * 0.24f ), 0.5f, line );
		c.fill ( math::rect ( center.x - s * 0.1f, top + s * 0.2f, s * 0.2f, s * 0.085f ), s * 0.02f, deep );
		c.polygon ( math::vec2 ( center.x, body_top ), math::vec2 ( center.x + half, shoulder ), math::vec2 ( center.x, bottom ), math::vec2 ( center.x - half, shoulder ), s * 0.015f, brass );
		c.polygon ( math::vec2 ( center.x, body_top ), math::vec2 ( center.x + half, shoulder ), math::vec2 ( center.x, bottom ), math::vec2 ( center.x, shoulder ), s * 0.01f, deep );
		c.fill ( math::rect ( center.x - half * 0.82f, shoulder - s * 0.012f, half * 1.64f, s * 0.024f ), s * 0.012f, render::rgb ( 0xFFFFFF, 0.18f ) );

	}

	void app::draw_tip ( ) {

		bool showing_tip = tip_id && tip_text && tip_time > 0.55f;

		settle ( tip_show, showing_tip ? 1.0f : 0.0f );

		if ( tip_show <= 0.01f || !tip_text )
			return;

		render::canvas& c = canvas;
		float text_w = c.measure ( tip_text, render::font::SMALL );
		float bw = text_w + 14.0f;
		float bh = 20.0f;
		float x = base::clamp ( tip_anchor.center ( ).x - bw * 0.5f, 6.0f, w - bw - 6.0f );
		float y = tip_anchor.bottom ( ) + 4.0f;

		if ( y + bh > h - 4.0f )
			y = tip_anchor.y - bh - 4.0f;

		c.push_alpha ( tip_show );
		c.shadow ( math::rect ( x, y, bw, bh ), 2.0f, 4.0f, ink ( tone::SHADOW ).fade ( 0.5f ), math::vec2 ( 0.0f, 1.0f ) );
		c.outline ( math::rect ( x, y, bw, bh ), 2.0f, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );
		c.text ( tip_text, render::font::SMALL, math::vec2 ( x + 7.0f, y + bh * 0.5f ), ink ( tone::INK ) );
		c.pop ( );

	}

	void app::draw_scrollbar ( uint64_t id, math::rect track, scroller& item, float content, float page ) {

		if ( content <= page + 0.5f || track.h < 20.0f )
			return;

		render::canvas& c = canvas;
		widget& bar = state ( id );
		float ratio = base::clamp ( page / content, 0.04f, 1.0f );
		float thumb_h = base::max ( track.h * ratio, 24.0f );
		float limit = base::max ( content - page, 1.0f );
		float travel = track.h - thumb_h;
		float thumb_y = track.y + travel * base::clamp ( item.offset / limit, 0.0f, 1.0f );
		math::rect hit_area ( track.right ( ) - 12.0f, track.y, 12.0f, track.h );

		interact ( id, hit_area, bar );

		bool dragging = active_id == id && mouse_down;

		if ( dragging ) {

			if ( press_event )
				bar.value = mouse.y - thumb_y;

			float place = base::clamp ( ( mouse.y - bar.value - track.y ) / base::max ( travel, 1.0f ), 0.0f, 1.0f );

			item.target = place * limit;
			item.offset = item.target;

		} else if ( clicked_id == id && !( mouse.y >= thumb_y && mouse.y < thumb_y + thumb_h ) ) {

			item.target += mouse.y < thumb_y ? -page : page;

		}

		bool lit = bar.hover > 0.5f || dragging;
		render::color tint = ink ( tone::INK_FAINT ).fade ( lit ? 0.75f : 0.4f );

		c.fill ( math::rect ( track.right ( ) - 8.0f, thumb_y + 2.0f, 6.0f, thumb_h - 4.0f ), 1.0f, tint );

	}

	void app::draw_side_scrollbar ( uint64_t id, math::rect track, scroller& item, float content, float page ) {

		if ( content <= page + 0.5f || track.w < 20.0f )
			return;

		render::canvas& c = canvas;
		widget& bar = state ( id );
		float ratio = base::clamp ( page / content, 0.04f, 1.0f );
		float thumb_w = base::max ( track.w * ratio, 24.0f );
		float limit = base::max ( content - page, 1.0f );
		float travel = track.w - thumb_w;
		float thumb_x = track.x + travel * base::clamp ( item.offset / limit, 0.0f, 1.0f );

		interact ( id, track, bar );

		bool dragging = active_id == id && mouse_down;

		if ( dragging ) {

			if ( press_event )
				bar.value = mouse.x - thumb_x;

			float place = base::clamp ( ( mouse.x - bar.value - track.x ) / base::max ( travel, 1.0f ), 0.0f, 1.0f );

			item.target = place * limit;
			item.offset = item.target;

		} else if ( clicked_id == id && !( mouse.x >= thumb_x && mouse.x < thumb_x + thumb_w ) ) {

			item.target += mouse.x < thumb_x ? -page : page;

		}

		bool lit = bar.hover > 0.5f || dragging;
		render::color tint = ink ( tone::INK_FAINT ).fade ( lit ? 0.75f : 0.4f );

		c.fill ( math::rect ( thumb_x + 2.0f, track.bottom ( ) - 8.0f, thumb_w - 4.0f, 6.0f ), 1.0f, tint );

	}

	bool app::splitter ( uint64_t id, math::rect area, bool vertical ) {

		widget& item = state ( id );

		interact ( id, area, item );

		bool dragging = active_id == id && mouse_down;

		if ( item.hover > 0.5f || dragging )
			wanted_cursor = vertical ? pointer::SIZE_WE : pointer::SIZE_NS;

		float glow = base::max ( item.hover, dragging ? 1.0f : 0.0f );

		if ( glow > 0.01f ) {

			math::rect line = vertical ? math::rect ( area.center ( ).x - 1.0f, area.y, 2.0f, area.h ) : math::rect ( area.x, area.center ( ).y - 1.0f, area.w, 2.0f );

			canvas.fill ( line, 0.0f, ink ( tone::SELECT_LINE ).fade ( glow * 0.8f ) );

		}

		return dragging;

	}

	bool app::button ( uint64_t id, math::rect area, const wchar_t* label, uint32_t glyph, bool primary ) {

		render::canvas& c = canvas;
		widget& item = state ( id );
		bool clicked = interact ( id, area, item );
		render::color fill = primary ? ink ( tone::ACCENT ) : ink ( tone::RAISED );
		render::color text = primary ? ink ( tone::ACCENT_INK ) : ink ( tone::INK );

		if ( primary )
			fill = render::mix ( fill, render::rgb ( light ? 0x000000 : 0xFFFFFF ), 0.08f * item.hover - 0.1f * item.press );

		c.outline ( area, metric::radius, fill, 1.0f, primary ? fill : ink ( tone::LINE_STRONG ) );

		if ( !primary )
			c.fill ( area, metric::radius, ink ( tone::HOVER ).fade ( item.hover + item.press ) );

		float label_w = c.measure ( label, render::font::UI_STRONG );
		float glyph_w = glyph ? 22.0f : 0.0f;
		float x = area.center ( ).x - ( label_w + glyph_w ) * 0.5f;

		if ( glyph )
			c.icon ( glyph, math::vec2 ( x + 7.0f, area.center ( ).y ), 14.0f, text );

		c.text ( label, render::font::UI_STRONG, math::vec2 ( x + glyph_w, area.center ( ).y ), text );

		if ( item.hover > 0.5f )
			wanted_cursor = pointer::HAND;

		return clicked;

	}

	bool app::icon_button ( uint64_t id, math::vec2 center, float size, uint32_t glyph, float glyph_size, const wchar_t* text, hit kind ) {

		render::canvas& c = canvas;
		widget& item = state ( id );
		math::rect area ( center.x - size * 0.5f, center.y - size * 0.5f, size, size );
		bool clicked = interact ( id, area, item, kind );

		c.fill ( area, metric::radius, ink ( tone::HOVER ).fade ( item.hover + item.press ) );
		c.icon ( glyph, center, glyph_size, render::mix ( ink ( tone::INK_SOFT ), ink ( tone::INK ), item.hover ) );
		tip ( id, text, area );

		return clicked;

	}

	bool app::text_field ( uint64_t id, field& value, math::rect area, const wchar_t* placeholder, uint32_t glyph, render::font role ) {

		render::canvas& c = canvas;
		widget& item = state ( id );
		bool focused_now = focus_id == id;

		if ( focused_now )
			focus_drawn = true;

		interact ( id, area, item );

		if ( item.hover > 0.5f )
			wanted_cursor = pointer::TEXT;

		float pad = glyph ? 24.0f : 8.0f;
		float inner = area.w - pad - 10.0f;
		math::vec2 base_at ( area.x + pad, area.center ( ).y );

		if ( active_id == id && press_event ) {

			if ( !focused_now )
				focus_field ( id, value, false );

			float local = mouse.x - base_at.x + value.scroll;
			int32_t best = value.length;
			wchar_t prefix [ 256 ];

			for ( int32_t i = 0; i <= value.length; i++ ) {

				for ( int32_t k = 0; k < i; k++ )
					prefix [ k ] = value.text [ k ];

				prefix [ i ] = 0;

				float width_here = c.measure ( prefix, role );

				if ( width_here >= local ) {

					float before = 0.0f;

					if ( i > 0 ) {

						prefix [ i - 1 ] = 0;
						before = c.measure ( prefix, role );

					}

					best = local - before < width_here - local ? base::max ( i - 1, 0 ) : i;

					break;

				}

			}

			value.caret = best;

			if ( !key_shift )
				value.anchor = best;

			focused_now = true;
			caret_time = clock;

		}

		settle ( item.value, focused_now ? 1.0f : 0.0f );

		render::color border = render::mix ( ink ( tone::LINE ), ink ( tone::SELECT_LINE ), item.value );

		c.outline ( area, metric::radius, ink ( tone::SUNKEN ), 1.0f, border );

		if ( glyph )
			c.icon ( glyph, math::vec2 ( area.x + 12.0f, area.center ( ).y ), 11.0f, ink ( tone::INK_FAINT ) );

		c.push_clip ( math::rect ( area.x + pad - 2.0f, area.y, inner + 4.0f, area.h ) );

		if ( !value.length ) {

			c.text ( placeholder, render::font::UI, base_at, ink ( tone::INK_FAINT ) );

		} else {

			wchar_t prefix [ 256 ];
			auto width_to = [ & ] ( int32_t count ) {

				for ( int32_t k = 0; k < count; k++ )
					prefix [ k ] = value.text [ k ];

				prefix [ count ] = 0;

				return c.measure ( prefix, role );

			};

			float caret_x = focused_now ? width_to ( value.caret ) : 0.0f;

			if ( caret_x - value.scroll > inner )
				value.scroll = caret_x - inner;
			else if ( caret_x - value.scroll < 0.0f )
				value.scroll = caret_x;

			if ( !focused_now )
				value.scroll = 0.0f;

			if ( focused_now && value.caret != value.anchor ) {

				float a = width_to ( base::min ( value.caret, value.anchor ) );
				float b = width_to ( base::max ( value.caret, value.anchor ) );

				c.fill ( math::rect ( base_at.x + a - value.scroll, area.y + 4.0f, b - a, area.h - 8.0f ), 0.0f, ink ( tone::SELECT ) );

			}

			c.text ( value.text, role, math::vec2 ( base_at.x - value.scroll, base_at.y ), ink ( tone::INK ) );

		}

		if ( focused_now ) {

			float caret_x = 0.0f;

			if ( value.caret > 0 ) {

				wchar_t prefix [ 256 ];

				for ( int32_t k = 0; k < value.caret; k++ )
					prefix [ k ] = value.text [ k ];

				prefix [ value.caret ] = 0;
				caret_x = c.measure ( prefix, role );

			}

			double since = clock - caret_time;
			bool lit = since < 0.6 || static_cast < int64_t > ( ( since - 0.6 ) / 0.53 ) % 2 == 1;

			if ( lit )
				c.fill ( math::rect ( base_at.x + caret_x - value.scroll - 0.5f, area.y + 5.0f, 1.0f, area.h - 10.0f ), 0.0f, ink ( tone::INK ) );

		}

		c.pop_clip ( );

		if ( focus_id == id )
			focus_drawn = true;

		bool submitted = focused_now && field_submit;

		if ( submitted )
			field_submit = false;

		return submitted;

	}

	void app::section_heading ( math::rect area, const wchar_t* text, const wchar_t* detail ) {

		render::canvas& c = canvas;

		c.text ( text, render::font::TINY, math::vec2 ( area.x, area.center ( ).y ), ink ( tone::INK_SOFT ) );

		if ( detail )
			c.text ( detail, render::font::SMALL, math::vec2 ( area.right ( ), area.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

	}

	void app::key_cap ( math::vec2 at, const wchar_t* keys_text, render::color tint, bool right ) {

		if ( !keys_text || !keys_text [ 0 ] )
			return;

		render::canvas& c = canvas;
		float text_w = c.measure ( keys_text, render::font::SMALL );
		float x = right ? at.x - text_w - 12.0f : at.x;

		c.outline ( math::rect ( x, at.y - 8.0f, text_w + 10.0f, 16.0f ), 2.0f, ink ( tone::SUNKEN ).fade ( 0.6f ), 1.0f, ink ( tone::LINE ) );
		c.text ( keys_text, render::font::SMALL, math::vec2 ( x + 5.0f, at.y ), tint );

	}

}