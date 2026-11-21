#pragma once

#include "base/base.hh"
#include "base/math.hh"

struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11BlendState;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct IDXGISwapChain1;
struct IDXGIFactory2;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;
struct IDCompositionSurface;
struct ID2D1Factory1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IDWriteTextLayout;
struct IDWriteFontFace;
struct IDWriteRenderingParams;
struct IDWriteInlineObject;
struct IDWriteTypography;

namespace render {

	struct color {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		constexpr color ( ) = default;

		constexpr color ( float red, float green, float blue, float alpha = 1.0f ) : r ( red ), g ( green ), b ( blue ), a ( alpha ) { }

		constexpr color fade ( float amount ) const {

			return color ( r, g, b, a * amount );

		}

	};

	constexpr color rgb ( uint32_t hex, float alpha = 1.0f ) {

		return color ( static_cast < float > ( ( hex >> 16 ) & 0xFF ) / 255.0f, static_cast < float > ( ( hex >> 8 ) & 0xFF ) / 255.0f, static_cast < float > ( hex & 0xFF ) / 255.0f, alpha );

	}

	inline color mix ( color x, color y, float t ) {

		return color ( x.r + ( y.r - x.r ) * t, x.g + ( y.g - x.g ) * t, x.b + ( y.b - x.b ) * t, x.a + ( y.a - x.a ) * t );

	}

	enum class font : int32_t {

		UI,
		UI_STRONG,
		SMALL,
		SMALL_STRONG,
		TINY,
		HEADING,
		TITLE,
		DISPLAY,
		MONO,
		MONO_STRONG,
		MONO_SMALL,
		BRAND,
		CODE,
		COUNT,

	};

	enum class align : int32_t {

		LEFT,
		CENTER,
		RIGHT,

	};

	enum class kind : int32_t {

		FILL,
		SHADOW,
		IMAGE,
		TEXTURE,
		GLASS,
		GRADIENT,
		POLYGON,
		RING,

	};

	struct quad {

		float rect [ 4 ];
		float shape [ 4 ];
		float fill [ 4 ];
		float stroke [ 4 ];
		float data [ 4 ];
		float extra [ 4 ];
		float clip [ 4 ];

	};

	struct glass_style {

		color tint = color ( 1.0f, 1.0f, 1.0f, 0.62f );
		float frost = 1.0f;
		float refraction = 16.0f;
		float bevel = 14.0f;
		float dispersion = 0.18f;
		float highlight = 1.0f;

	};

	struct text_entry {

		uint64_t key = 0;
		IDWriteTextLayout* layout = nullptr;
		float width = 0.0f;
		float height = 0.0f;
		float baseline = 0.0f;
		float cap = 0.0f;
		uint32_t used = 0;

	};

	struct glyph_entry {

		uint32_t code = 0;
		uint16_t index = 0;
		float middle_x = 0.0f;
		float middle_y = 0.0f;

	};

	struct surface {

		ID3D11Texture2D* texture = nullptr;
		ID3D11RenderTargetView* target = nullptr;
		ID3D11ShaderResourceView* view = nullptr;
		ID2D1Bitmap1* bitmap = nullptr;
		int32_t width = 0;
		int32_t height = 0;

	};

	struct output {

		HWND__* window = nullptr;
		IDCompositionTarget* target = nullptr;
		IDCompositionVisual* visual = nullptr;
		IDCompositionSurface* content = nullptr;
		int32_t width = 0;
		int32_t height = 0;

	};

	struct text_item {

		IDWriteTextLayout* layout;
		float x;
		float y;
		float scale;
		color tint;
		float clip [ 4 ];
		float bounds [ 4 ];

	};

	struct glyph_item {

		uint16_t index;
		float size;
		float x;
		float y;
		float scale;
		color tint;
		float clip [ 4 ];
		float bounds [ 4 ];

	};

	struct command {

		int32_t type;
		int32_t first;
		int32_t count;

	};

	struct cover_spec {

		color colors [ 4 ];
		float seed;
		int32_t style;
		float grain;
		float vignette;

	};

	class canvas;

	class gpu {

	public:

		bool setup ( bool force_software = false );

		bool healthy ( ) const;

		void shutdown ( );

		bool attach ( output& target, HWND__* window, int32_t width, int32_t height );

		bool resize ( output& target, int32_t width, int32_t height );

		void release ( output& target );

		void present ( output& target, const surface& source );

		void commit ( );

		void finish ( );

		bool prepare ( int32_t width, int32_t height );

		void draw ( canvas& source, surface& target, const surface* backdrop, bool clear, color clear_color );


		void blur ( );

		bool make_covers ( const cover_spec* specs, int32_t count, int32_t total, int32_t size );

		bool upload_cover ( int32_t slot, const uint8_t* pixels );

		bool make_thumbs ( int32_t total, int32_t size );

		bool upload_thumb ( int32_t slot, const uint8_t* pixels );

		text_entry* layout ( const wchar_t* text, font role, float max_width, bool wrap = false );

		const glyph_entry* glyph ( uint32_t code );

		float cap_height ( font role ) const;

		void next_frame ( );

		bool make_surface ( surface& result, int32_t width, int32_t height, bool drawable );

		void free_surface ( surface& target );

		bool save ( const surface& source, const wchar_t* path, color underlay, int32_t width, int32_t height );

		bool set_fonts ( );

		bool has_family ( const wchar_t* name );

		wchar_t font_names [ 3 ] [ 64 ] = { };
		float font_sizes [ 3 ] = { };
		wchar_t family_used [ 3 ] [ 64 ] = { };
		surface scene;
		surface chrome;
		surface blurred;
		int32_t width = 0;
		int32_t height = 0;
		bool software = false;

	private:

		bool create ( bool use_software );

		bool make_shaders ( );

		bool make_fonts ( );

		void bind_quads ( ID3D11RenderTargetView* target, int32_t target_width, int32_t target_height, const surface* backdrop, canvas& source );

		void run ( canvas& source, ID3D11RenderTargetView* target, ID2D1Bitmap1* bitmap, int32_t target_width, int32_t target_height, const surface* backdrop );

		void words ( canvas& source, const command& item, bool& clipped, float* active );

		base::list < int32_t > held_commands;

		void post ( ID3D11PixelShader* shader, const surface& source, const surface& target, float spread );

		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGIFactory2* factory = nullptr;
		IDCompositionDevice* composition = nullptr;
		ID2D1Factory1* d2d = nullptr;
		ID2D1Device* d2d_device = nullptr;
		ID2D1DeviceContext* d2d_context = nullptr;
		ID2D1SolidColorBrush* brush = nullptr;
		IDWriteFactory* dwrite = nullptr;
		IDWriteRenderingParams* text_params = nullptr;
		IDWriteRenderingParams* icon_params = nullptr;
		IDWriteTextFormat* formats [ static_cast < int32_t > ( font::COUNT ) ] = { };
		IDWriteInlineObject* ellipsis [ static_cast < int32_t > ( font::COUNT ) ] = { };
		float caps [ static_cast < int32_t > ( font::COUNT ) ] = { };
		float tracking [ static_cast < int32_t > ( font::COUNT ) ] = { };
		IDWriteTypography* tabular = nullptr;
		IDWriteFontFace* icon_face = nullptr;
		ID3D11VertexShader* ui_vs = nullptr;
		ID3D11PixelShader* ui_ps = nullptr;
		ID3D11InputLayout* ui_layout = nullptr;
		ID3D11VertexShader* full_vs = nullptr;
		ID3D11PixelShader* down_ps = nullptr;
		ID3D11PixelShader* up_ps = nullptr;
		ID3D11PixelShader* cover_ps = nullptr;
		ID3D11Buffer* instances = nullptr;
		int32_t instance_room = 0;
		ID3D11Buffer* frame_constants = nullptr;
		ID3D11Buffer* post_constants = nullptr;
		ID3D11BlendState* blend = nullptr;
		ID3D11BlendState* opaque = nullptr;
		ID3D11RasterizerState* raster = nullptr;
		ID3D11SamplerState* sampler = nullptr;
		ID3D11Texture2D* covers = nullptr;
		ID3D11ShaderResourceView* cover_view = nullptr;
		int32_t cover_size = 0;
		int32_t cover_slots = 0;
		int32_t cover_mips = 0;
		ID3D11Texture2D* thumbs = nullptr;
		ID3D11ShaderResourceView* thumb_view = nullptr;
		int32_t thumb_size = 0;
		int32_t thumb_slots = 0;
		int32_t thumb_mips = 0;
		surface down [ 4 ];
		surface up [ 3 ];
		text_entry* entries = nullptr;
		int32_t entry_count = 0;
		static constexpr int32_t entry_room = 16384;
		glyph_entry glyphs [ 64 ];
		int32_t glyph_count = 0;
		uint32_t frame = 1;

	};

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