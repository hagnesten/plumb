#pragma once

#include "render/gpu/gpu.h"

namespace render {

	class canvas {

	public:

		void begin ( gpu* device, float dpi_scale, math::vec2 origin, float time );

		void push ( math::vec2 pivot, float scale, float alpha, math::vec2 offset = math::vec2 ( 0.0f, 0.0f ) );

		void push_alpha ( float alpha );

		void pop ( );

		void push_clip ( math::rect area );

		void pop_clip ( );

		void fill ( math::rect area, float radius, color tint );

		void soft ( math::rect area, float radius, float softness, color tint );

		void outline ( math::rect area, float radius, color inside, float width, color border );

		void shadow ( math::rect area, float radius, float blur, color tint, math::vec2 offset = math::vec2 ( 0.0f, 0.0f ) );

		void glass ( math::rect area, float radius, const glass_style& style, float alpha = 1.0f );

		void image ( math::rect area, float radius, int32_t slice, float alpha = 1.0f, float angle = 0.0f );

		void thumb ( math::rect area, float radius, int32_t slice, float alpha = 1.0f, float angle = 0.0f );

		void texture ( math::rect area, float radius, math::rect uv, float alpha );

		void gradient ( math::rect area, float radius, math::vec2 from, math::vec2 to, color start, color end );

		void polygon ( math::vec2 a, math::vec2 b, math::vec2 c, math::vec2 d, float rounding, color tint );

		void ring ( math::vec2 center, float radius, float width, float start, float sweep, color tint );

		void circle ( math::vec2 center, float radius, color tint );

		float text ( const wchar_t* value, font role, math::vec2 at, color tint, align placement = align::LEFT, float max_width = 0.0f );

		float measure ( const wchar_t* value, font role );

		float paragraph ( const wchar_t* value, font role, math::vec2 at, float width, color tint );

		void icon ( uint32_t code, math::vec2 center, float size, color tint );

		void light ( math::vec2 at, float strength );

		math::vec2 screen ( math::vec2 p ) const;

		float scale ( ) const;

		float alpha ( ) const;

		base::list < quad > quads;
		base::list < command > commands;
		base::list < text_item > texts;
		base::list < glyph_item > glyphs;
		math::vec2 light_at;
		float light_strength = 0.0f;
		float time = 0.0f;
		bool snap = true;
		bool scale_glyphs = false;
		math::vec2 snap_origin;

	private:

		struct transform {

			float scale;
			math::vec2 offset;
			float alpha;

		};

		quad& emit ( kind type, math::rect area, float radius );

		void add_command ( int32_t type, int32_t index );

		gpu* device = nullptr;
		transform stack [ 32 ];
		int32_t depth = 0;
		float clips [ 32 ] [ 4 ];
		int32_t clip_depth = 0;

	};

}