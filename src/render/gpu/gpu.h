#pragma once

#include "base/list/list.h"
#include "base/math/math.h"
#include "render/colors/colors.h"
#include "render/fonts/fonts.h"
#include "render/commands/commands.h"

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

}