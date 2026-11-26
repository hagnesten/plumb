#include "render/render.h"

namespace render {

	static void premultiply ( float* target, color tint, float alpha ) {

		float a = tint.a * alpha;

		target [ 0 ] = tint.r * a;
		target [ 1 ] = tint.g * a;
		target [ 2 ] = tint.b * a;
		target [ 3 ] = a;

	}

	void canvas::begin ( gpu* target_device, float dpi_scale, math::vec2 origin, float now ) {

		device = target_device;
		quads.clear ( );
		commands.clear ( );
		texts.clear ( );
		glyphs.clear ( );
		depth = 0;
		stack [ 0 ].scale = dpi_scale;
		stack [ 0 ].offset = origin;
		stack [ 0 ].alpha = 1.0f;
		clip_depth = 0;
		clips [ 0 ] [ 0 ] = -100000.0f;
		clips [ 0 ] [ 1 ] = -100000.0f;
		clips [ 0 ] [ 2 ] = 100000.0f;
		clips [ 0 ] [ 3 ] = 100000.0f;
		light_strength = 0.0f;
		snap = true;
		scale_glyphs = false;
		snap_origin = math::vec2 ( 0.0f, 0.0f );
		light_at = math::vec2 ( -10000.0f, -10000.0f );
		time = now;

	}

	void canvas::push ( math::vec2 pivot, float scale_by, float alpha_by, math::vec2 offset ) {

		const transform& top = stack [ depth ];
		transform next;
		float lx = pivot.x * ( 1.0f - scale_by ) + offset.x;
		float ly = pivot.y * ( 1.0f - scale_by ) + offset.y;

		next.scale = top.scale * scale_by;
		next.offset = math::vec2 ( lx * top.scale + top.offset.x, ly * top.scale + top.offset.y );
		next.alpha = top.alpha * alpha_by;

		if ( depth < 31 )
			depth++;

		stack [ depth ] = next;

	}

	void canvas::push_alpha ( float alpha_by ) {

		push ( math::vec2 ( 0.0f, 0.0f ), 1.0f, alpha_by );

	}

	void canvas::pop ( ) {

		if ( depth > 0 )
			depth--;

	}

	void canvas::push_clip ( math::rect area ) {

		math::vec2 low = screen ( math::vec2 ( area.x, area.y ) );
		math::vec2 high = screen ( math::vec2 ( area.right ( ), area.bottom ( ) ) );
		const float* current = clips [ clip_depth ];
		int32_t next = base::min ( clip_depth + 1, 31 );

		clips [ next ] [ 0 ] = base::max ( current [ 0 ], math::round ( low.x ) );
		clips [ next ] [ 1 ] = base::max ( current [ 1 ], math::round ( low.y ) );
		clips [ next ] [ 2 ] = base::min ( current [ 2 ], math::round ( high.x ) );
		clips [ next ] [ 3 ] = base::min ( current [ 3 ], math::round ( high.y ) );
		clip_depth = next;

	}

	void canvas::pop_clip ( ) {

		if ( clip_depth > 0 )
			clip_depth--;

	}

	math::vec2 canvas::screen ( math::vec2 p ) const {

		const transform& top = stack [ depth ];

		return math::vec2 ( p.x * top.scale + top.offset.x, p.y * top.scale + top.offset.y );

	}

	float canvas::scale ( ) const {

		return stack [ depth ].scale;

	}

	float canvas::alpha ( ) const {

		return stack [ depth ].alpha;

	}

	void canvas::add_command ( int32_t type, int32_t index ) {

		if ( type == 0 && !commands.empty ( ) ) {

			command& last = commands.back ( );

			if ( last.type == 0 && last.first + last.count == index ) {

				last.count++;

				return;

			}

		}

		command item = { type, index, 1 };

		commands.push ( item );

	}

	quad& canvas::emit ( kind type, math::rect area, float radius ) {

		const transform& top = stack [ depth ];
		int32_t index = quads.count ( );
		quad& item = quads.emplace ( );
		math::vec2 at = screen ( math::vec2 ( area.x, area.y ) );

		item.rect [ 0 ] = at.x;
		item.rect [ 1 ] = at.y;
		item.rect [ 2 ] = area.w * top.scale;
		item.rect [ 3 ] = area.h * top.scale;
		item.shape [ 0 ] = radius * top.scale;
		item.shape [ 3 ] = static_cast < float > ( type );

		for ( int32_t i = 0; i < 4; i++ )
			item.clip [ i ] = clips [ clip_depth ] [ i ];

		add_command ( 0, index );

		return item;

	}

	void canvas::fill ( math::rect area, float radius, color tint ) {

		if ( tint.a * alpha ( ) <= 0.0f || area.w <= 0.0f || area.h <= 0.0f )
			return;

		quad& item = emit ( kind::FILL, area, radius );

		premultiply ( item.fill, tint, alpha ( ) );

	}

	void canvas::soft ( math::rect area, float radius, float softness, color tint ) {

		if ( tint.a * alpha ( ) <= 0.0f )
			return;

		quad& item = emit ( kind::FILL, area, radius );

		item.shape [ 1 ] = softness * scale ( );
		premultiply ( item.fill, tint, alpha ( ) );

	}

	void canvas::outline ( math::rect area, float radius, color inside, float width, color border ) {

		if ( ( inside.a + border.a ) * alpha ( ) <= 0.0f )
			return;

		quad& item = emit ( kind::FILL, area, radius );

		item.shape [ 2 ] = width * scale ( );
		premultiply ( item.fill, inside, alpha ( ) );
		premultiply ( item.stroke, border, alpha ( ) );

	}

	void canvas::shadow ( math::rect area, float radius, float blur, color tint, math::vec2 offset ) {

		if ( tint.a * alpha ( ) <= 0.0f )
			return;

		area.x += offset.x;
		area.y += offset.y;

		quad& item = emit ( kind::SHADOW, area, radius );

		item.shape [ 1 ] = base::max ( blur * scale ( ), 0.5f );
		premultiply ( item.fill, tint, alpha ( ) );

	}

	void canvas::glass ( math::rect area, float radius, const glass_style& style, float alpha_by ) {

		float a = alpha ( ) * alpha_by;

		if ( a <= 0.0f )
			return;

		quad& item = emit ( kind::GLASS, area, radius );

		premultiply ( item.fill, style.tint, 1.0f );
		item.data [ 0 ] = style.bevel * scale ( );
		item.data [ 1 ] = style.refraction * scale ( );
		item.data [ 2 ] = style.frost;
		item.data [ 3 ] = style.dispersion;
		item.extra [ 0 ] = style.highlight;
		item.extra [ 1 ] = a;

	}

	void canvas::image ( math::rect area, float radius, int32_t slice, float alpha_by, float angle ) {

		float a = alpha ( ) * alpha_by;

		if ( a <= 0.0f || slice < 0 )
			return;

		quad& item = emit ( kind::IMAGE, area, radius );

		item.fill [ 0 ] = a;
		item.fill [ 1 ] = a;
		item.fill [ 2 ] = a;
		item.fill [ 3 ] = a;
		item.data [ 0 ] = 0.0f;
		item.data [ 1 ] = 0.0f;
		item.data [ 2 ] = 1.0f;
		item.data [ 3 ] = 1.0f;
		item.extra [ 0 ] = static_cast < float > ( slice );
		item.extra [ 1 ] = angle;

	}

	void canvas::thumb ( math::rect area, float radius, int32_t slice, float alpha_by, float angle ) {

		float a = alpha ( ) * alpha_by;

		if ( a <= 0.0f || slice < 0 )
			return;

		quad& item = emit ( kind::IMAGE, area, radius );

		item.fill [ 0 ] = a;
		item.fill [ 1 ] = a;
		item.fill [ 2 ] = a;
		item.fill [ 3 ] = a;
		item.data [ 0 ] = 0.0f;
		item.data [ 1 ] = 0.0f;
		item.data [ 2 ] = 1.0f;
		item.data [ 3 ] = 1.0f;
		item.extra [ 0 ] = static_cast < float > ( slice );
		item.extra [ 1 ] = angle;
		item.extra [ 2 ] = 1.0f;

	}

	void canvas::texture ( math::rect area, float radius, math::rect uv, float alpha_by ) {

		float a = alpha ( ) * alpha_by;

		if ( a <= 0.0f )
			return;

		quad& item = emit ( kind::TEXTURE, area, radius );

		item.fill [ 3 ] = a;
		item.data [ 0 ] = uv.x;
		item.data [ 1 ] = uv.y;
		item.data [ 2 ] = uv.right ( );
		item.data [ 3 ] = uv.bottom ( );

	}

	void canvas::gradient ( math::rect area, float radius, math::vec2 from, math::vec2 to, color start, color end ) {

		if ( ( start.a + end.a ) * alpha ( ) <= 0.0f )
			return;

		quad& item = emit ( kind::GRADIENT, area, radius );
		math::vec2 a = screen ( from );
		math::vec2 b = screen ( to );

		premultiply ( item.fill, start, alpha ( ) );
		premultiply ( item.stroke, end, alpha ( ) );
		item.data [ 0 ] = a.x;
		item.data [ 1 ] = a.y;
		item.data [ 2 ] = b.x;
		item.data [ 3 ] = b.y;

	}

	void canvas::polygon ( math::vec2 a, math::vec2 b, math::vec2 c, math::vec2 d, float rounding, color tint ) {

		if ( tint.a * alpha ( ) <= 0.0f )
			return;

		float x0 = base::min ( base::min ( a.x, b.x ), base::min ( c.x, d.x ) ) - rounding - 1.0f;
		float y0 = base::min ( base::min ( a.y, b.y ), base::min ( c.y, d.y ) ) - rounding - 1.0f;
		float x1 = base::max ( base::max ( a.x, b.x ), base::max ( c.x, d.x ) ) + rounding + 1.0f;
		float y1 = base::max ( base::max ( a.y, b.y ), base::max ( c.y, d.y ) ) + rounding + 1.0f;
		quad& item = emit ( kind::POLYGON, math::rect ( x0, y0, x1 - x0, y1 - y0 ), rounding );
		math::vec2 sa = screen ( a );
		math::vec2 sb = screen ( b );
		math::vec2 sc = screen ( c );
		math::vec2 sd = screen ( d );

		premultiply ( item.fill, tint, alpha ( ) );
		item.data [ 0 ] = sa.x;
		item.data [ 1 ] = sa.y;
		item.data [ 2 ] = sb.x;
		item.data [ 3 ] = sb.y;
		item.extra [ 0 ] = sc.x;
		item.extra [ 1 ] = sc.y;
		item.extra [ 2 ] = sd.x;
		item.extra [ 3 ] = sd.y;

	}

	void canvas::ring ( math::vec2 center, float radius, float width, float start, float sweep, color tint ) {

		if ( tint.a * alpha ( ) <= 0.0f )
			return;

		quad& item = emit ( kind::RING, math::rect ( center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f ), radius );

		item.shape [ 2 ] = width * scale ( );
		premultiply ( item.fill, tint, alpha ( ) );
		item.data [ 0 ] = start;
		item.data [ 1 ] = sweep;

	}

	void canvas::circle ( math::vec2 center, float radius, color tint ) {

		fill ( math::rect ( center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f ), radius, tint );

	}

	float canvas::text ( const wchar_t* value, font role, math::vec2 at, color tint, align placement, float max_width ) {

		if ( !value || !value [ 0 ] )
			return 0.0f;

		text_entry* entry = device->layout ( value, role, max_width );

		if ( !entry->layout )
			return 0.0f;

		float a = tint.a * alpha ( );

		if ( a <= 0.003f )
			return entry->width;

		const transform& rest = stack [ 0 ];
		float glyph = scale_glyphs ? stack [ depth ].scale : rest.scale;
		float lead = placement == align::CENTER ? entry->width * 0.5f : ( placement == align::RIGHT ? entry->width : 0.0f );
		float rise = entry->baseline - entry->cap * 0.5f;
		math::vec2 anchor = screen ( at );
		math::vec2 p ( anchor.x - lead * glyph, anchor.y - rise * glyph );
		text_item item;

		if ( snap ) {

			float rest_x = ( at.x - lead ) * rest.scale + rest.offset.x - snap_origin.x;
			float rest_y = ( at.y - rise ) * rest.scale + rest.offset.y - snap_origin.y;

			p.x += math::round ( rest_x ) - rest_x;
			p.y += math::round ( rest_y ) - rest_y;

		}

		item.layout = entry->layout;
		item.x = p.x;
		item.y = p.y;
		item.scale = glyph;
		item.tint = color ( tint.r, tint.g, tint.b, a );

		for ( int32_t i = 0; i < 4; i++ )
			item.clip [ i ] = clips [ clip_depth ] [ i ];

		float spill = entry->height * glyph * 0.3f + 3.0f;

		item.bounds [ 0 ] = p.x - spill;
		item.bounds [ 1 ] = p.y - spill;
		item.bounds [ 2 ] = p.x + entry->width * glyph + spill;
		item.bounds [ 3 ] = p.y + entry->height * glyph + spill;
		texts.push ( item );
		add_command ( 1, texts.count ( ) - 1 );

		return entry->width;

	}

	float canvas::paragraph ( const wchar_t* value, font role, math::vec2 at, float width, color tint ) {

		if ( !value || !value [ 0 ] )
			return 0.0f;

		text_entry* entry = device->layout ( value, role, width, true );

		if ( !entry->layout )
			return 0.0f;

		float a = tint.a * alpha ( );

		if ( a <= 0.003f )
			return entry->height;

		math::vec2 p = screen ( at );
		float glyph = scale_glyphs ? stack [ depth ].scale : stack [ 0 ].scale;
		text_item item;

		item.layout = entry->layout;
		item.x = p.x;
		item.y = p.y;
		item.scale = glyph;
		item.tint = color ( tint.r, tint.g, tint.b, a );

		for ( int32_t i = 0; i < 4; i++ )
			item.clip [ i ] = clips [ clip_depth ] [ i ];

		float spill = 8.0f * glyph + 3.0f;

		item.bounds [ 0 ] = p.x - spill;
		item.bounds [ 1 ] = p.y - spill;
		item.bounds [ 2 ] = p.x + base::max ( width, entry->width ) * glyph + spill;
		item.bounds [ 3 ] = p.y + entry->height * glyph + spill;
		texts.push ( item );
		add_command ( 1, texts.count ( ) - 1 );

		return entry->height;

	}

	float canvas::measure ( const wchar_t* value, font role ) {

		if ( !value || !value [ 0 ] )
			return 0.0f;

		return device->layout ( value, role, 0.0f )->width;

	}

	void canvas::icon ( uint32_t code, math::vec2 center, float size, color tint ) {

		float a = tint.a * alpha ( );

		if ( a <= 0.003f )
			return;

		const glyph_entry* entry = device->glyph ( code );

		if ( !entry )
			return;

		math::vec2 p = screen ( math::vec2 ( center.x - entry->middle_x * size, center.y + entry->middle_y * size ) );
		glyph_item item;

		item.index = entry->index;
		item.size = size;
		item.x = p.x;
		item.y = p.y;
		item.scale = scale ( );
		item.tint = color ( tint.r, tint.g, tint.b, a );

		for ( int32_t i = 0; i < 4; i++ )
			item.clip [ i ] = clips [ clip_depth ] [ i ];

		math::vec2 middle = screen ( center );
		float reach = size * item.scale * 0.85f + 3.0f;

		item.bounds [ 0 ] = middle.x - reach;
		item.bounds [ 1 ] = middle.y - reach;
		item.bounds [ 2 ] = middle.x + reach;
		item.bounds [ 3 ] = middle.y + reach;
		glyphs.push ( item );
		add_command ( 2, glyphs.count ( ) - 1 );

	}

	void canvas::light ( math::vec2 at, float strength ) {

		light_at = screen ( at );
		light_strength = strength;

	}

}