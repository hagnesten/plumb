#include "render/render.hh"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dwmapi.h>
#include <d2d1_1.h>
#include <dwrite_2.h>
#include <dcomp.h>
#include <wincodec.h>

namespace render {

	template < typename type >
	static void drop ( type*& value ) {

		if ( value ) {

			value->Release ( );
			value = nullptr;

		}

	}

	struct frame_data {

		float target_size [ 2 ];
		float source_size [ 2 ];
		float light [ 4 ];
		float tuning [ 4 ];

	};

	struct post_data {

		float texel [ 2 ];
		float spread;
		float unused;
		float colors [ 4 ] [ 4 ];
		float params [ 4 ];

	};

	static bool resource ( int32_t id, const void*& data, uint32_t& size ) {

		HMODULE module = GetModuleHandleW ( nullptr );
		HRSRC found = FindResourceW ( module, MAKEINTRESOURCEW ( id ), MAKEINTRESOURCEW ( 10 ) );

		if ( !found )
			return false;

		HGLOBAL loaded = LoadResource ( module, found );

		if ( !loaded )
			return false;

		data = LockResource ( loaded );
		size = SizeofResource ( module, found );

		return data && size;

	}

	bool gpu::setup ( bool force_software ) {

		if ( !force_software && create ( false ) )
			return true;

		shutdown ( );

		return create ( true );

	}

	bool gpu::healthy ( ) const {

		if ( !device || device->GetDeviceRemovedReason ( ) != S_OK )
			return false;

		BOOL valid = TRUE;

		return !composition || ( SUCCEEDED ( composition->CheckDeviceState ( &valid ) ) && valid );

	}

	bool gpu::create ( bool use_software ) {

		D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

		software = use_software;

		if ( FAILED ( D3D11CreateDevice ( nullptr, software ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context ) ) )
			return false;

		IDXGIDevice* dxgi = nullptr;
		IDXGIAdapter* adapter = nullptr;

		if ( FAILED ( device->QueryInterface ( __uuidof ( IDXGIDevice ), reinterpret_cast < void** > ( &dxgi ) ) ) )
			return false;

		dxgi->GetAdapter ( &adapter );

		if ( !adapter || FAILED ( adapter->GetParent ( __uuidof ( IDXGIFactory2 ), reinterpret_cast < void** > ( &factory ) ) ) ) {

			drop ( adapter );
			drop ( dxgi );

			return false;

		}

		drop ( adapter );

		if ( FAILED ( DCompositionCreateDevice ( dxgi, __uuidof ( IDCompositionDevice ), reinterpret_cast < void** > ( &composition ) ) ) ) {

			drop ( dxgi );

			return false;

		}

		D2D1_FACTORY_OPTIONS options = { };

		options.debugLevel = D2D1_DEBUG_LEVEL_NONE;

		if ( FAILED ( D2D1CreateFactory ( D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof ( ID2D1Factory1 ), &options, reinterpret_cast < void** > ( &d2d ) ) ) ) {

			drop ( dxgi );

			return false;

		}

		if ( FAILED ( d2d->CreateDevice ( dxgi, &d2d_device ) ) ) {

			drop ( dxgi );

			return false;

		}

		drop ( dxgi );

		if ( FAILED ( d2d_device->CreateDeviceContext ( D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context ) ) )
			return false;

		D2D1_COLOR_F white = { 1.0f, 1.0f, 1.0f, 1.0f };

		d2d_context->CreateSolidColorBrush ( &white, nullptr, &brush );
		d2d_context->SetTextAntialiasMode ( D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE );
		d2d_context->SetUnitMode ( D2D1_UNIT_MODE_PIXELS );

		if ( FAILED ( DWriteCreateFactory ( DWRITE_FACTORY_TYPE_SHARED, __uuidof ( IDWriteFactory ), reinterpret_cast < IUnknown** > ( &dwrite ) ) ) )
			return false;

		IDWriteFactory2* modern = nullptr;

		if ( SUCCEEDED ( dwrite->QueryInterface ( __uuidof ( IDWriteFactory2 ), reinterpret_cast < void** > ( &modern ) ) ) ) {

			IDWriteRenderingParams2* smooth = nullptr;

			if ( SUCCEEDED ( modern->CreateCustomRenderingParams ( 1.8f, 0.6f, 0.6f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_GRID_FIT_MODE_DISABLED, &smooth ) ) )
				text_params = smooth;

			modern->Release ( );

		}

		if ( !text_params )
			dwrite->CreateCustomRenderingParams ( 1.8f, 0.6f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &text_params );

		if ( text_params )
			d2d_context->SetTextRenderingParams ( text_params );

		dwrite->CreateCustomRenderingParams ( 1.8f, 0.6f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_OUTLINE, &icon_params );

		if ( !make_shaders ( ) || !make_fonts ( ) )
			return false;

		entries = new text_entry [ entry_room ];

		return true;

	}

	bool gpu::make_shaders ( ) {

		const void* data = nullptr;
		uint32_t size = 0;

		if ( !resource ( 101, data, size ) || FAILED ( device->CreateVertexShader ( data, size, nullptr, &ui_vs ) ) )
			return false;

		D3D11_INPUT_ELEMENT_DESC elements [ ] = {
			{ "RECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "SHAPE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "FILL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "STROKE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "DATA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "EXTRA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "CLIP", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1 }
		};

		if ( FAILED ( device->CreateInputLayout ( elements, 7, data, size, &ui_layout ) ) )
			return false;

		if ( !resource ( 102, data, size ) || FAILED ( device->CreatePixelShader ( data, size, nullptr, &ui_ps ) ) )
			return false;

		if ( !resource ( 103, data, size ) || FAILED ( device->CreateVertexShader ( data, size, nullptr, &full_vs ) ) )
			return false;

		if ( !resource ( 104, data, size ) || FAILED ( device->CreatePixelShader ( data, size, nullptr, &down_ps ) ) )
			return false;

		if ( !resource ( 105, data, size ) || FAILED ( device->CreatePixelShader ( data, size, nullptr, &up_ps ) ) )
			return false;

		if ( !resource ( 106, data, size ) || FAILED ( device->CreatePixelShader ( data, size, nullptr, &cover_ps ) ) )
			return false;

		D3D11_BUFFER_DESC constants = { };

		constants.ByteWidth = sizeof ( frame_data );
		constants.Usage = D3D11_USAGE_DYNAMIC;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		if ( FAILED ( device->CreateBuffer ( &constants, nullptr, &frame_constants ) ) )
			return false;

		constants.ByteWidth = sizeof ( post_data );

		if ( FAILED ( device->CreateBuffer ( &constants, nullptr, &post_constants ) ) )
			return false;

		D3D11_BLEND_DESC blending = { };

		blending.RenderTarget [ 0 ].BlendEnable = TRUE;
		blending.RenderTarget [ 0 ].SrcBlend = D3D11_BLEND_ONE;
		blending.RenderTarget [ 0 ].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blending.RenderTarget [ 0 ].BlendOp = D3D11_BLEND_OP_ADD;
		blending.RenderTarget [ 0 ].SrcBlendAlpha = D3D11_BLEND_ONE;
		blending.RenderTarget [ 0 ].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blending.RenderTarget [ 0 ].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blending.RenderTarget [ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		if ( FAILED ( device->CreateBlendState ( &blending, &blend ) ) )
			return false;

		blending.RenderTarget [ 0 ].BlendEnable = FALSE;

		if ( FAILED ( device->CreateBlendState ( &blending, &opaque ) ) )
			return false;

		D3D11_RASTERIZER_DESC rasterizer = { };

		rasterizer.FillMode = D3D11_FILL_SOLID;
		rasterizer.CullMode = D3D11_CULL_NONE;
		rasterizer.DepthClipEnable = TRUE;

		if ( FAILED ( device->CreateRasterizerState ( &rasterizer, &raster ) ) )
			return false;

		D3D11_SAMPLER_DESC sampling = { };

		sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampling.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampling.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampling.MaxLOD = D3D11_FLOAT32_MAX;
		sampling.MipLODBias = -0.35f;
		sampling.MaxAnisotropy = 1;

		return SUCCEEDED ( device->CreateSamplerState ( &sampling, &sampler ) );

	}

	struct font_role {

		int32_t family;
		DWRITE_FONT_WEIGHT weight;
		float size;
		float tracking;

	};

	bool gpu::make_fonts ( ) {

		IDWriteFontCollection* collection = nullptr;

		if ( FAILED ( dwrite->GetSystemFontCollection ( &collection, FALSE ) ) )
			return false;

		const wchar_t* wanted [ 3 ] [ 2 ] = { { L"Segoe UI Variable Text", L"Segoe UI" }, { L"Segoe UI Variable Display", L"Segoe UI" }, { L"Consolas", L"Cascadia Mono" } };
		const wchar_t* families [ 3 ];

		for ( int32_t i = 0; i < 3; i++ ) {

			UINT32 index = 0;
			BOOL exists = FALSE;

			collection->FindFamilyName ( wanted [ i ] [ 0 ], &index, &exists );
			families [ i ] = exists ? wanted [ i ] [ 0 ] : wanted [ i ] [ 1 ];

		}

		font_role roles [ static_cast < int32_t > ( font::COUNT ) ] = {
			{ 0, DWRITE_FONT_WEIGHT_REGULAR, 12.0f, 0.0f },
			{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f, 0.0f },
			{ 0, DWRITE_FONT_WEIGHT_REGULAR, 11.0f, 0.0f },
			{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.0f, 0.0f },
			{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 10.0f, 0.05f },
			{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.5f, 0.0f },
			{ 1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 20.0f, -0.01f },
			{ 1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 28.0f, -0.015f },
			{ 2, DWRITE_FONT_WEIGHT_REGULAR, 13.0f, 0.0f },
			{ 2, DWRITE_FONT_WEIGHT_BOLD, 13.0f, 0.0f },
			{ 2, DWRITE_FONT_WEIGHT_REGULAR, 12.0f, 0.0f },
			{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f, 0.0f }
		};

		for ( int32_t i = 0; i < static_cast < int32_t > ( font::COUNT ); i++ ) {

			const font_role& role = roles [ i ];

			if ( FAILED ( dwrite->CreateTextFormat ( families [ role.family ], collection, role.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, role.size, L"en-us", &formats [ i ] ) ) ) {

				collection->Release ( );

				return false;

			}

			formats [ i ]->SetWordWrapping ( DWRITE_WORD_WRAPPING_NO_WRAP );

			if ( SUCCEEDED ( dwrite->CreateEllipsisTrimmingSign ( formats [ i ], &ellipsis [ i ] ) ) ) {

				DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };

				formats [ i ]->SetTrimming ( &trimming, ellipsis [ i ] );

			}

			tracking [ i ] = role.tracking * role.size;
			caps [ i ] = role.size * 0.7f;

			UINT32 index = 0;
			BOOL exists = FALSE;

			collection->FindFamilyName ( families [ role.family ], &index, &exists );

			if ( exists ) {

				IDWriteFontFamily* family = nullptr;
				IDWriteFont* face = nullptr;

				if ( SUCCEEDED ( collection->GetFontFamily ( index, &family ) ) && SUCCEEDED ( family->GetFirstMatchingFont ( role.weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &face ) ) ) {

					DWRITE_FONT_METRICS metrics = { };

					face->GetMetrics ( &metrics );

					if ( metrics.designUnitsPerEm )
						caps [ i ] = static_cast < float > ( metrics.capHeight ) / static_cast < float > ( metrics.designUnitsPerEm ) * role.size;

				}

				drop ( face );
				drop ( family );

			}

		}

		if ( SUCCEEDED ( dwrite->CreateTypography ( &tabular ) ) ) {

			DWRITE_FONT_FEATURE feature = { DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1 };

			tabular->AddFontFeature ( feature );

		}

		const wchar_t* icon_names [ 2 ] = { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" };

		for ( int32_t i = 0; i < 2 && !icon_face; i++ ) {

			UINT32 index = 0;
			BOOL exists = FALSE;

			collection->FindFamilyName ( icon_names [ i ], &index, &exists );

			if ( !exists )
				continue;

			IDWriteFontFamily* family = nullptr;
			IDWriteFont* face = nullptr;

			if ( SUCCEEDED ( collection->GetFontFamily ( index, &family ) ) && SUCCEEDED ( family->GetFirstMatchingFont ( DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &face ) ) )
				face->CreateFontFace ( &icon_face );

			drop ( face );
			drop ( family );

		}

		collection->Release ( );

		return true;

	}

	void gpu::shutdown ( ) {

		for ( int32_t i = 0; i < entry_room && entries; i++ )
			drop ( entries [ i ].layout );

		delete [ ] entries;
		entries = nullptr;
		entry_count = 0;
		glyph_count = 0;

		for ( int32_t i = 0; i < 4; i++ )
			free_surface ( down [ i ] );

		for ( int32_t i = 0; i < 3; i++ )
			free_surface ( up [ i ] );

		blurred = surface ( );
		free_surface ( scene );
		free_surface ( chrome );

		for ( int32_t i = 0; i < static_cast < int32_t > ( font::COUNT ); i++ ) {

			drop ( ellipsis [ i ] );
			drop ( formats [ i ] );

		}

		drop ( tabular );
		drop ( icon_face );
		drop ( icon_params );
		drop ( text_params );
		drop ( dwrite );
		drop ( thumb_view );
		drop ( thumbs );
		drop ( cover_view );
		drop ( covers );
		drop ( instances );
		instance_room = 0;
		drop ( frame_constants );
		drop ( post_constants );
		drop ( sampler );
		drop ( raster );
		drop ( opaque );
		drop ( blend );
		drop ( cover_ps );
		drop ( up_ps );
		drop ( down_ps );
		drop ( full_vs );
		drop ( ui_layout );
		drop ( ui_ps );
		drop ( ui_vs );
		drop ( brush );
		drop ( d2d_context );
		drop ( d2d_device );
		drop ( d2d );
		drop ( composition );
		drop ( factory );

		if ( context )
			context->ClearState ( );

		drop ( context );
		drop ( device );

	}

	bool gpu::make_surface ( surface& result, int32_t surface_width, int32_t surface_height, bool drawable ) {

		free_surface ( result );

		D3D11_TEXTURE2D_DESC desc = { };

		desc.Width = static_cast < UINT > ( base::max ( surface_width, 1 ) );
		desc.Height = static_cast < UINT > ( base::max ( surface_height, 1 ) );
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if ( FAILED ( device->CreateTexture2D ( &desc, nullptr, &result.texture ) ) )
			return false;

		if ( FAILED ( device->CreateRenderTargetView ( result.texture, nullptr, &result.target ) ) )
			return false;

		if ( FAILED ( device->CreateShaderResourceView ( result.texture, nullptr, &result.view ) ) )
			return false;

		if ( drawable ) {

			IDXGISurface* dxgi_surface = nullptr;
			D2D1_BITMAP_PROPERTIES1 properties = { };

			properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
			properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
			properties.dpiX = 96.0f;
			properties.dpiY = 96.0f;
			properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

			if ( FAILED ( result.texture->QueryInterface ( __uuidof ( IDXGISurface ), reinterpret_cast < void** > ( &dxgi_surface ) ) ) )
				return false;

			HRESULT made = d2d_context->CreateBitmapFromDxgiSurface ( dxgi_surface, &properties, &result.bitmap );

			dxgi_surface->Release ( );

			if ( FAILED ( made ) )
				return false;

		}

		result.width = static_cast < int32_t > ( desc.Width );
		result.height = static_cast < int32_t > ( desc.Height );

		return true;

	}

	void gpu::free_surface ( surface& target ) {

		drop ( target.bitmap );
		drop ( target.view );
		drop ( target.target );
		drop ( target.texture );
		target.width = 0;
		target.height = 0;

	}

	static int32_t round_up ( int32_t value, int32_t step ) {

		return ( ( value + step - 1 ) / step ) * step;

	}

	bool gpu::prepare ( int32_t target_width, int32_t target_height ) {

		width = target_width;
		height = target_height;

		if ( scene.texture && target_width <= scene.width && target_height <= scene.height && scene.width <= target_width * 2 + 256 && scene.height <= target_height * 2 + 256 )
			return true;

		int32_t w = round_up ( base::max ( target_width, 64 ), 128 );
		int32_t h = round_up ( base::max ( target_height, 64 ), 128 );

		d2d_context->SetTarget ( nullptr );

		if ( !make_surface ( scene, w, h, true ) || !make_surface ( chrome, w, h, true ) )
			return false;

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( !make_surface ( down [ i ], base::max ( w >> ( i + 1 ), 1 ), base::max ( h >> ( i + 1 ), 1 ), false ) )
				return false;

		}

		for ( int32_t i = 0; i < 3; i++ ) {

			if ( !make_surface ( up [ i ], down [ 2 - i ].width, down [ 2 - i ].height, false ) )
				return false;

		}

		blurred = up [ 2 ];

		return true;

	}

	static bool make_content ( IDCompositionDevice* composition, output& target, int32_t target_width, int32_t target_height ) {

		IDCompositionSurface* fresh = nullptr;

		if ( FAILED ( composition->CreateSurface ( static_cast < UINT > ( target_width ), static_cast < UINT > ( target_height ), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &fresh ) ) )
			return false;

		target.visual->SetContent ( fresh );
		drop ( target.content );
		target.content = fresh;
		target.width = target_width;
		target.height = target_height;

		return true;

	}

	bool gpu::attach ( output& target, HWND__* window, int32_t target_width, int32_t target_height ) {

		if ( FAILED ( composition->CreateTargetForHwnd ( window, TRUE, &target.target ) ) )
			return false;

		if ( FAILED ( composition->CreateVisual ( &target.visual ) ) )
			return false;

		target.window = window;
		target.target->SetRoot ( target.visual );

		if ( !make_content ( composition, target, base::max ( target_width, 1 ), base::max ( target_height, 1 ) ) )
			return false;

		composition->Commit ( );

		return true;

	}

	bool gpu::resize ( output& target, int32_t target_width, int32_t target_height ) {

		target_width = base::max ( target_width, 1 );
		target_height = base::max ( target_height, 1 );

		if ( !target.visual )
			return false;

		if ( target.width == target_width && target.height == target_height )
			return true;

		return make_content ( composition, target, target_width, target_height );

	}

	void gpu::release ( output& target ) {

		drop ( target.content );
		drop ( target.visual );
		drop ( target.target );

	}

	void gpu::present ( output& target, const surface& source ) {

		if ( !target.content || !source.texture )
			return;

		ID3D11Texture2D* texture = nullptr;
		POINT offset = { };

		if ( FAILED ( target.content->BeginDraw ( nullptr, __uuidof ( ID3D11Texture2D ), reinterpret_cast < void** > ( &texture ), &offset ) ) )
			return;

		D3D11_BOX box = { 0, 0, 0, static_cast < UINT > ( base::min ( source.width, target.width ) ), static_cast < UINT > ( base::min ( source.height, target.height ) ), 1 };

		context->CopySubresourceRegion ( texture, 0, static_cast < UINT > ( offset.x ), static_cast < UINT > ( offset.y ), 0, source.texture, 0, &box );
		texture->Release ( );
		target.content->EndDraw ( );
		composition->Commit ( );

	}

	void gpu::commit ( ) {

		if ( composition )
			composition->Commit ( );

	}

	void gpu::finish ( ) {

		D3D11_QUERY_DESC description = { D3D11_QUERY_EVENT, 0 };
		ID3D11Query* query = nullptr;

		if ( FAILED ( device->CreateQuery ( &description, &query ) ) )
			return;

		context->End ( query );

		BOOL done = FALSE;

		while ( context->GetData ( query, &done, sizeof ( done ), 0 ) == S_FALSE )
			YieldProcessor ( );

		query->Release ( );

	}

	void gpu::bind_quads ( ID3D11RenderTargetView* target, int32_t target_width, int32_t target_height, const surface* backdrop, canvas& source ) {

		UINT stride = sizeof ( quad );
		UINT offset = 0;
		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast < float > ( target_width ), static_cast < float > ( target_height ), 0.0f, 1.0f };
		ID3D11ShaderResourceView* views [ 4 ] = { backdrop ? backdrop->view : nullptr, backdrop ? blurred.view : nullptr, cover_view, thumb_view };
		float factor [ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };

		context->IASetInputLayout ( ui_layout );
		context->IASetPrimitiveTopology ( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
		context->IASetVertexBuffers ( 0, 1, &instances, &stride, &offset );
		context->VSSetShader ( ui_vs, nullptr, 0 );
		context->VSSetConstantBuffers ( 0, 1, &frame_constants );
		context->PSSetShader ( ui_ps, nullptr, 0 );
		context->PSSetConstantBuffers ( 0, 1, &frame_constants );
		context->PSSetSamplers ( 0, 1, &sampler );
		context->OMSetRenderTargets ( 1, &target, nullptr );
		context->PSSetShaderResources ( 0, 4, views );
		context->OMSetBlendState ( blend, factor, 0xFFFFFFFF );
		context->OMSetDepthStencilState ( nullptr, 0 );
		context->RSSetState ( raster );
		context->RSSetViewports ( 1, &viewport );

	}

	void gpu::run ( canvas& source, ID3D11RenderTargetView* target, ID2D1Bitmap1* bitmap, int32_t target_width, int32_t target_height, const surface* backdrop ) {

		int32_t count = source.quads.count ( );

		if ( count > instance_room ) {

			drop ( instances );
			instance_room = base::max ( count, base::max ( instance_room * 2, 2048 ) );

			D3D11_BUFFER_DESC desc = { };

			desc.ByteWidth = static_cast < UINT > ( instance_room * static_cast < int32_t > ( sizeof ( quad ) ) );
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			if ( FAILED ( device->CreateBuffer ( &desc, nullptr, &instances ) ) ) {

				instance_room = 0;

				return;

			}

		}

		D3D11_MAPPED_SUBRESOURCE mapped = { };

		if ( count > 0 && SUCCEEDED ( context->Map ( instances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

			__builtin_memcpy ( mapped.pData, source.quads.data ( ), sizeof ( quad ) * static_cast < size_t > ( count ) );
			context->Unmap ( instances, 0 );

		}

		if ( SUCCEEDED ( context->Map ( frame_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

			frame_data* constants = static_cast < frame_data* > ( mapped.pData );

			constants->target_size [ 0 ] = static_cast < float > ( target_width );
			constants->target_size [ 1 ] = static_cast < float > ( target_height );
			constants->source_size [ 0 ] = backdrop ? static_cast < float > ( backdrop->width ) : 1.0f;
			constants->source_size [ 1 ] = backdrop ? static_cast < float > ( backdrop->height ) : 1.0f;
			constants->light [ 0 ] = source.light_at.x;
			constants->light [ 1 ] = source.light_at.y;
			constants->light [ 2 ] = source.light_strength;
			constants->light [ 3 ] = source.time;
			constants->tuning [ 0 ] = 0.0f;
			constants->tuning [ 1 ] = 0.0f;
			constants->tuning [ 2 ] = 0.0f;
			constants->tuning [ 3 ] = 0.0f;
			context->Unmap ( frame_constants, 0 );

		}

		bool clipped = false;
		bool bound = false;
		float active [ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float reach [ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
		base::list < int32_t >& waiting = held_commands;

		waiting.clear ( );

		auto bounds_of = [ & ] ( const command& item ) {

			const float* box = item.type == 1 ? source.texts [ item.first ].bounds : source.glyphs [ item.first ].bounds;
			const float* clip = item.type == 1 ? source.texts [ item.first ].clip : source.glyphs [ item.first ].clip;

			return math::rect ( base::max ( box [ 0 ], clip [ 0 ] ), base::max ( box [ 1 ], clip [ 1 ] ), base::min ( box [ 2 ], clip [ 2 ] ) - base::max ( box [ 0 ], clip [ 0 ] ), base::min ( box [ 3 ], clip [ 3 ] ) - base::max ( box [ 1 ], clip [ 1 ] ) );

		};

		auto draw_quads = [ & ] ( int32_t first, int32_t total ) {

			if ( total <= 0 )
				return;

			if ( !bound ) {

				bind_quads ( target, target_width, target_height, backdrop, source );
				bound = true;

			}

			context->DrawInstanced ( 4, static_cast < UINT > ( total ), 0, static_cast < UINT > ( first ) );

		};

		auto draw_words = [ & ] ( ) {

			if ( waiting.empty ( ) )
				return;

			ID3D11ShaderResourceView* empty [ 4 ] = { nullptr, nullptr, nullptr, nullptr };

			context->PSSetShaderResources ( 0, 4, empty );
			d2d_context->SetTarget ( bitmap );
			d2d_context->BeginDraw ( );
			clipped = false;

			for ( int32_t at : waiting )
				words ( source, source.commands [ static_cast < size_t > ( at ) ], clipped, active );

			if ( clipped )
				d2d_context->PopAxisAlignedClip ( );

			d2d_context->EndDraw ( );
			waiting.clear ( );
			bound = false;

		};

		for ( int32_t c = 0; c < source.commands.count ( ); c++ ) {

			const command& item = source.commands [ static_cast < size_t > ( c ) ];

			if ( item.type != 0 ) {

				if ( !bitmap )
					continue;

				math::rect box = bounds_of ( item );

				if ( box.w <= 0.0f || box.h <= 0.0f )
					continue;

				if ( waiting.empty ( ) ) {

					reach [ 0 ] = box.x;
					reach [ 1 ] = box.y;
					reach [ 2 ] = box.right ( );
					reach [ 3 ] = box.bottom ( );

				} else {

					reach [ 0 ] = base::min ( reach [ 0 ], box.x );
					reach [ 1 ] = base::min ( reach [ 1 ], box.y );
					reach [ 2 ] = base::max ( reach [ 2 ], box.right ( ) );
					reach [ 3 ] = base::max ( reach [ 3 ], box.bottom ( ) );

				}

				waiting.push ( c );

				continue;

			}

			int32_t start = item.first;
			int32_t end = item.first + item.count;

			for ( int32_t q = item.first; q < end && !waiting.empty ( ); q++ ) {

				const quad& shape = source.quads [ static_cast < size_t > ( q ) ];
				float pad = static_cast < int32_t > ( shape.shape [ 3 ] ) == static_cast < int32_t > ( kind::SHADOW ) ? shape.shape [ 1 ] * 3.0f + 2.0f : 1.5f;
				float low_x = base::max ( shape.rect [ 0 ] - pad, shape.clip [ 0 ] - 1.0f );
				float low_y = base::max ( shape.rect [ 1 ] - pad, shape.clip [ 1 ] - 1.0f );
				float high_x = base::min ( shape.rect [ 0 ] + shape.rect [ 2 ] + pad, shape.clip [ 2 ] + 1.0f );
				float high_y = base::min ( shape.rect [ 1 ] + shape.rect [ 3 ] + pad, shape.clip [ 3 ] + 1.0f );

				if ( high_x <= reach [ 0 ] || low_x >= reach [ 2 ] || high_y <= reach [ 1 ] || low_y >= reach [ 3 ] || high_x <= low_x || high_y <= low_y )
					continue;

				bool touches = false;

				for ( int32_t k = 0; k < waiting.count ( ) && !touches; k++ ) {

					math::rect box = bounds_of ( source.commands [ static_cast < size_t > ( waiting [ static_cast < size_t > ( k ) ] ) ] );

					touches = high_x > box.x && low_x < box.right ( ) && high_y > box.y && low_y < box.bottom ( );

				}

				if ( !touches )
					continue;

				draw_quads ( start, q - start );
				draw_words ( );
				start = q;

			}

			draw_quads ( start, end - start );

		}

		draw_words ( );

		ID3D11ShaderResourceView* empty [ 4 ] = { nullptr, nullptr, nullptr, nullptr };

		context->PSSetShaderResources ( 0, 4, empty );

	}

	void gpu::words ( canvas& source, const command& item, bool& clipped, float* active ) {

		const float* clip = item.type == 1 ? source.texts [ item.first ].clip : source.glyphs [ item.first ].clip;

		if ( !clipped || clip [ 0 ] != active [ 0 ] || clip [ 1 ] != active [ 1 ] || clip [ 2 ] != active [ 2 ] || clip [ 3 ] != active [ 3 ] ) {

			D2D1_MATRIX_3X2_F identity = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
			D2D1_RECT_F area = { base::max ( clip [ 0 ], -16384.0f ), base::max ( clip [ 1 ], -16384.0f ), base::min ( clip [ 2 ], 16384.0f ), base::min ( clip [ 3 ], 16384.0f ) };

			if ( clipped )
				d2d_context->PopAxisAlignedClip ( );

			d2d_context->SetTransform ( &identity );
			d2d_context->PushAxisAlignedClip ( &area, D2D1_ANTIALIAS_MODE_ALIASED );
			clipped = true;

			for ( int32_t i = 0; i < 4; i++ )
				active [ i ] = clip [ i ];

		}

		if ( item.type == 1 ) {

			const text_item& text = source.texts [ item.first ];
			D2D1_MATRIX_3X2_F place = { text.scale, 0.0f, 0.0f, text.scale, text.x, text.y };
			D2D1_COLOR_F tint = { text.tint.r, text.tint.g, text.tint.b, text.tint.a };
			D2D1_POINT_2F origin = { 0.0f, 0.0f };

			d2d_context->SetTransform ( &place );
			brush->SetColor ( &tint );
			d2d_context->DrawTextLayout ( origin, text.layout, brush, D2D1_DRAW_TEXT_OPTIONS_NO_SNAP );

			return;

		}

		if ( !icon_face )
			return;

		const glyph_item& glyph = source.glyphs [ item.first ];
		D2D1_MATRIX_3X2_F place = { glyph.scale, 0.0f, 0.0f, glyph.scale, glyph.x, glyph.y };
		D2D1_COLOR_F tint = { glyph.tint.r, glyph.tint.g, glyph.tint.b, glyph.tint.a };
		D2D1_POINT_2F origin = { 0.0f, 0.0f };
		UINT16 index = glyph.index;
		FLOAT advance = 0.0f;
		DWRITE_GLYPH_RUN run = { };

		run.fontFace = icon_face;
		run.fontEmSize = glyph.size;
		run.glyphCount = 1;
		run.glyphIndices = &index;
		run.glyphAdvances = &advance;
		d2d_context->SetTransform ( &place );
		brush->SetColor ( &tint );

		if ( icon_params )
			d2d_context->SetTextRenderingParams ( icon_params );

		d2d_context->DrawGlyphRun ( origin, &run, brush, DWRITE_MEASURING_MODE_NATURAL );

		if ( icon_params && text_params )
			d2d_context->SetTextRenderingParams ( text_params );

	}

	void gpu::draw ( canvas& source, surface& target, const surface* backdrop, bool clear, color clear_color ) {

		if ( clear ) {

			float value [ 4 ] = { clear_color.r * clear_color.a, clear_color.g * clear_color.a, clear_color.b * clear_color.a, clear_color.a };

			context->ClearRenderTargetView ( target.target, value );

		}

		run ( source, target.target, target.bitmap, target.width, target.height, backdrop );

	}

	void gpu::post ( ID3D11PixelShader* shader, const surface& source, const surface& target, float spread ) {

		D3D11_MAPPED_SUBRESOURCE mapped = { };

		if ( SUCCEEDED ( context->Map ( post_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

			post_data* constants = static_cast < post_data* > ( mapped.pData );

			__builtin_memset ( constants, 0, sizeof ( post_data ) );
			constants->texel [ 0 ] = 1.0f / static_cast < float > ( source.width );
			constants->texel [ 1 ] = 1.0f / static_cast < float > ( source.height );
			constants->spread = spread;
			context->Unmap ( post_constants, 0 );

		}

		ID3D11ShaderResourceView* empty = nullptr;
		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast < float > ( target.width ), static_cast < float > ( target.height ), 0.0f, 1.0f };
		float factor [ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };

		context->PSSetShaderResources ( 0, 1, &empty );
		context->OMSetRenderTargets ( 1, &target.target, nullptr );
		context->IASetInputLayout ( nullptr );
		context->IASetPrimitiveTopology ( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		context->VSSetShader ( full_vs, nullptr, 0 );
		context->PSSetShader ( shader, nullptr, 0 );
		context->PSSetConstantBuffers ( 0, 1, &post_constants );
		context->PSSetSamplers ( 0, 1, &sampler );
		context->PSSetShaderResources ( 0, 1, &source.view );
		context->OMSetBlendState ( opaque, factor, 0xFFFFFFFF );
		context->RSSetState ( raster );
		context->RSSetViewports ( 1, &viewport );
		context->Draw ( 3, 0 );
		context->PSSetShaderResources ( 0, 1, &empty );

	}

	void gpu::blur ( ) {

		post ( down_ps, scene, down [ 0 ], 1.0f );
		post ( down_ps, down [ 0 ], down [ 1 ], 1.0f );
		post ( down_ps, down [ 1 ], down [ 2 ], 1.0f );
		post ( down_ps, down [ 2 ], down [ 3 ], 1.0f );
		post ( up_ps, down [ 3 ], up [ 0 ], 1.0f );
		post ( up_ps, up [ 0 ], up [ 1 ], 1.0f );
		post ( up_ps, up [ 1 ], up [ 2 ], 1.0f );

	}

	bool gpu::make_covers ( const cover_spec* specs, int32_t count, int32_t total, int32_t size ) {

		drop ( cover_view );
		drop ( covers );

		D3D11_TEXTURE2D_DESC desc = { };

		desc.Width = static_cast < UINT > ( size );
		desc.Height = static_cast < UINT > ( size );
		desc.MipLevels = 0;
		desc.ArraySize = static_cast < UINT > ( base::max ( total, count ) );
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		if ( FAILED ( device->CreateTexture2D ( &desc, nullptr, &covers ) ) )
			return false;

		if ( FAILED ( device->CreateShaderResourceView ( covers, nullptr, &cover_view ) ) )
			return false;

		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast < float > ( size ), static_cast < float > ( size ), 0.0f, 1.0f };

		cover_size = size;
		cover_slots = base::max ( total, count );
		cover_mips = 1;

		for ( int32_t edge = size; edge > 1; edge >>= 1 )
			cover_mips++;
		float factor [ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
		ID3D11ShaderResourceView* empty = nullptr;

		for ( int32_t i = 0; i < count; i++ ) {

			D3D11_RENDER_TARGET_VIEW_DESC view = { };
			ID3D11RenderTargetView* target = nullptr;

			view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			view.Texture2DArray.MipSlice = 0;
			view.Texture2DArray.FirstArraySlice = static_cast < UINT > ( i );
			view.Texture2DArray.ArraySize = 1;

			if ( FAILED ( device->CreateRenderTargetView ( covers, &view, &target ) ) )
				return false;

			D3D11_MAPPED_SUBRESOURCE mapped = { };

			if ( SUCCEEDED ( context->Map ( post_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

				post_data* constants = static_cast < post_data* > ( mapped.pData );

				__builtin_memset ( constants, 0, sizeof ( post_data ) );

				for ( int32_t c = 0; c < 4; c++ ) {

					constants->colors [ c ] [ 0 ] = specs [ i ].colors [ c ].r;
					constants->colors [ c ] [ 1 ] = specs [ i ].colors [ c ].g;
					constants->colors [ c ] [ 2 ] = specs [ i ].colors [ c ].b;
					constants->colors [ c ] [ 3 ] = 1.0f;

				}

				constants->params [ 0 ] = specs [ i ].seed;
				constants->params [ 1 ] = static_cast < float > ( specs [ i ].style );
				constants->params [ 2 ] = specs [ i ].grain;
				constants->params [ 3 ] = specs [ i ].vignette;
				context->Unmap ( post_constants, 0 );

			}

			context->PSSetShaderResources ( 0, 1, &empty );
			context->OMSetRenderTargets ( 1, &target, nullptr );
			context->IASetInputLayout ( nullptr );
			context->IASetPrimitiveTopology ( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			context->VSSetShader ( full_vs, nullptr, 0 );
			context->PSSetShader ( cover_ps, nullptr, 0 );
			context->PSSetConstantBuffers ( 0, 1, &post_constants );
			context->OMSetBlendState ( opaque, factor, 0xFFFFFFFF );
			context->RSSetState ( raster );
			context->RSSetViewports ( 1, &viewport );
			context->Draw ( 3, 0 );
			target->Release ( );

		}

		context->OMSetRenderTargets ( 0, nullptr, nullptr );
		context->GenerateMips ( cover_view );

		return true;

	}

	bool gpu::upload_cover ( int32_t slot, const uint8_t* pixels ) {

		if ( !covers || slot < 0 || slot >= cover_slots || !pixels )
			return false;

		UINT resource = D3D11CalcSubresource ( 0, static_cast < UINT > ( slot ), static_cast < UINT > ( cover_mips ) );

		context->UpdateSubresource ( covers, resource, nullptr, pixels, static_cast < UINT > ( cover_size * 4 ), 0 );

		D3D11_SHADER_RESOURCE_VIEW_DESC view = { };
		ID3D11ShaderResourceView* single = nullptr;

		view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		view.Texture2DArray.MostDetailedMip = 0;
		view.Texture2DArray.MipLevels = static_cast < UINT > ( -1 );
		view.Texture2DArray.FirstArraySlice = static_cast < UINT > ( slot );
		view.Texture2DArray.ArraySize = 1;

		if ( FAILED ( device->CreateShaderResourceView ( covers, &view, &single ) ) )
			return false;

		context->GenerateMips ( single );
		single->Release ( );

		return true;

	}

	bool gpu::make_thumbs ( int32_t total, int32_t size ) {

		drop ( thumb_view );
		drop ( thumbs );

		D3D11_TEXTURE2D_DESC desc = { };

		desc.Width = static_cast < UINT > ( size );
		desc.Height = static_cast < UINT > ( size );
		desc.MipLevels = 0;
		desc.ArraySize = static_cast < UINT > ( total );
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		if ( FAILED ( device->CreateTexture2D ( &desc, nullptr, &thumbs ) ) )
			return false;

		if ( FAILED ( device->CreateShaderResourceView ( thumbs, nullptr, &thumb_view ) ) )
			return false;

		thumb_size = size;
		thumb_slots = total;
		thumb_mips = 1;

		for ( int32_t edge = size; edge > 1; edge >>= 1 )
			thumb_mips++;

		return true;

	}

	bool gpu::upload_thumb ( int32_t slot, const uint8_t* pixels ) {

		if ( !thumbs || slot < 0 || slot >= thumb_slots || !pixels )
			return false;

		UINT resource = D3D11CalcSubresource ( 0, static_cast < UINT > ( slot ), static_cast < UINT > ( thumb_mips ) );

		context->UpdateSubresource ( thumbs, resource, nullptr, pixels, static_cast < UINT > ( thumb_size * 4 ), 0 );

		D3D11_SHADER_RESOURCE_VIEW_DESC view = { };
		ID3D11ShaderResourceView* single = nullptr;

		view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		view.Texture2DArray.MostDetailedMip = 0;
		view.Texture2DArray.MipLevels = static_cast < UINT > ( -1 );
		view.Texture2DArray.FirstArraySlice = static_cast < UINT > ( slot );
		view.Texture2DArray.ArraySize = 1;

		if ( FAILED ( device->CreateShaderResourceView ( thumbs, &view, &single ) ) )
			return false;

		context->GenerateMips ( single );
		single->Release ( );

		return true;

	}

	static uint64_t text_key ( const wchar_t* text, font role, float max_width ) {

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( int32_t i = 0; text [ i ]; i++ ) {

			hash ^= static_cast < uint64_t > ( text [ i ] );
			hash *= 0x100000001B3ull;

		}

		hash ^= static_cast < uint64_t > ( role ) * 0x9E3779B97F4A7C15ull;
		hash *= 0x100000001B3ull;
		hash ^= static_cast < uint64_t > ( static_cast < int64_t > ( max_width * 2.0f ) + 7 ) * 0xC2B2AE3D27D4EB4Full;
		hash *= 0x100000001B3ull;

		return hash ? hash : 1;

	}

	text_entry* gpu::layout ( const wchar_t* text, font role, float max_width, bool wrap ) {

		static text_entry blank;

		if ( !text || !text [ 0 ] )
			return &blank;

		max_width = max_width > 0.0f ? math::floor ( max_width * 2.0f ) * 0.5f : 0.0f;

		uint64_t key = text_key ( text, role, max_width ) ^ ( wrap ? 0x5DEECE66Dull : 0ull );
		int32_t slot = static_cast < int32_t > ( key & static_cast < uint64_t > ( entry_room - 1 ) );

		for ( int32_t probe = 0; probe < entry_room; probe++ ) {

			text_entry& entry = entries [ slot ];

			if ( !entry.layout )
				break;

			if ( entry.key == key ) {

				entry.used = frame;

				return &entry;

			}

			slot = ( slot + 1 ) & ( entry_room - 1 );

		}

		if ( entry_count >= entry_room - 64 )
			return &blank;

		int32_t index = static_cast < int32_t > ( role );
		UINT32 length = static_cast < UINT32 > ( base::text_length ( text ) );
		IDWriteTextLayout* created = nullptr;

		if ( FAILED ( dwrite->CreateTextLayout ( text, length, formats [ index ], max_width > 0.0f ? max_width : 100000.0f, 1000.0f, &created ) ) )
			return &blank;

		DWRITE_TEXT_RANGE range = { 0, length };

		if ( wrap ) {

			DWRITE_TRIMMING none = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };

			created->SetWordWrapping ( DWRITE_WORD_WRAPPING_WRAP );
			created->SetTrimming ( &none, nullptr );

		}

		if ( tracking [ index ] != 0.0f ) {

			IDWriteTextLayout1* spaced = nullptr;

			if ( SUCCEEDED ( created->QueryInterface ( __uuidof ( IDWriteTextLayout1 ), reinterpret_cast < void** > ( &spaced ) ) ) ) {

				spaced->SetCharacterSpacing ( 0.0f, tracking [ index ], 0.0f, range );
				spaced->Release ( );

			}

		}

		if ( role == font::SMALL && tabular )
			created->SetTypography ( tabular, range );

		DWRITE_TEXT_METRICS metrics = { };
		DWRITE_LINE_METRICS line = { };
		UINT32 lines = 0;

		created->GetMetrics ( &metrics );
		created->GetLineMetrics ( &line, 1, &lines );

		text_entry& entry = entries [ slot ];

		entry.key = key;
		entry.layout = created;
		entry.width = metrics.widthIncludingTrailingWhitespace;
		entry.height = metrics.height;
		entry.baseline = line.baseline;
		entry.cap = caps [ index ];
		entry.used = frame;
		entry_count++;

		return &entry;

	}

	void gpu::next_frame ( ) {

		frame++;

		if ( entry_count < entry_room / 2 )
			return;

		text_entry* kept = new text_entry [ entry_room ];
		int32_t count = 0;

		for ( int32_t i = 0; i < entry_room; i++ ) {

			text_entry& entry = entries [ i ];

			if ( !entry.layout )
				continue;

			if ( entry.used + 20 < frame ) {

				drop ( entry.layout );

				continue;

			}

			int32_t slot = static_cast < int32_t > ( entry.key & static_cast < uint64_t > ( entry_room - 1 ) );

			while ( kept [ slot ].layout )
				slot = ( slot + 1 ) & ( entry_room - 1 );

			kept [ slot ] = entry;
			count++;

		}

		delete [ ] entries;
		entries = kept;
		entry_count = count;

	}

	const glyph_entry* gpu::glyph ( uint32_t code ) {

		for ( int32_t i = 0; i < glyph_count; i++ ) {

			if ( glyphs [ i ].code == code )
				return &glyphs [ i ];

		}

		if ( !icon_face || glyph_count >= 64 )
			return nullptr;

		glyph_entry& entry = glyphs [ glyph_count++ ];
		UINT32 point = code;
		UINT16 index = 0;
		DWRITE_GLYPH_METRICS metrics = { };
		DWRITE_FONT_METRICS font_metrics = { };

		icon_face->GetGlyphIndices ( &point, 1, &index );
		icon_face->GetDesignGlyphMetrics ( &index, 1, &metrics, FALSE );
		icon_face->GetMetrics ( &font_metrics );

		float em = static_cast < float > ( font_metrics.designUnitsPerEm ? font_metrics.designUnitsPerEm : 2048 );
		float left = static_cast < float > ( metrics.leftSideBearing );
		float right = static_cast < float > ( metrics.advanceWidth ) - static_cast < float > ( metrics.rightSideBearing );
		float top = static_cast < float > ( metrics.verticalOriginY ) - static_cast < float > ( metrics.topSideBearing );
		float bottom = static_cast < float > ( metrics.verticalOriginY ) - ( static_cast < float > ( metrics.advanceHeight ) - static_cast < float > ( metrics.bottomSideBearing ) );

		entry.code = code;
		entry.index = index;
		entry.middle_x = ( left + right ) * 0.5f / em;
		entry.middle_y = ( top + bottom ) * 0.5f / em;

		return &entry;

	}

	float gpu::cap_height ( font role ) const {

		return caps [ static_cast < int32_t > ( role ) ];

	}

	bool gpu::save ( const surface& source, const wchar_t* path, color underlay, int32_t save_width, int32_t save_height ) {

		save_width = base::min ( save_width, source.width );
		save_height = base::min ( save_height, source.height );

		D3D11_TEXTURE2D_DESC desc = { };
		ID3D11Texture2D* staging = nullptr;

		desc.Width = static_cast < UINT > ( save_width );
		desc.Height = static_cast < UINT > ( save_height );
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		if ( FAILED ( device->CreateTexture2D ( &desc, nullptr, &staging ) ) )
			return false;

		D3D11_BOX box = { 0, 0, 0, desc.Width, desc.Height, 1 };

		context->CopySubresourceRegion ( staging, 0, 0, 0, 0, source.texture, 0, &box );

		D3D11_MAPPED_SUBRESOURCE mapped = { };

		if ( FAILED ( context->Map ( staging, 0, D3D11_MAP_READ, 0, &mapped ) ) ) {

			staging->Release ( );

			return false;

		}

		base::list < uint8_t > pixels;

		pixels.resize ( static_cast < size_t > ( save_width ) * static_cast < size_t > ( save_height ) * 4 );

		for ( int32_t y = 0; y < save_height; y++ ) {

			const uint8_t* row = static_cast < const uint8_t* > ( mapped.pData ) + static_cast < size_t > ( y ) * mapped.RowPitch;
			uint8_t* out = pixels.data ( ) + static_cast < size_t > ( y ) * static_cast < size_t > ( save_width ) * 4;

			for ( int32_t x = 0; x < save_width; x++ ) {

				float b = static_cast < float > ( row [ x * 4 + 0 ] ) / 255.0f;
				float g = static_cast < float > ( row [ x * 4 + 1 ] ) / 255.0f;
				float r = static_cast < float > ( row [ x * 4 + 2 ] ) / 255.0f;
				float a = static_cast < float > ( row [ x * 4 + 3 ] ) / 255.0f;

				if ( underlay.a > 0.0f ) {

					b += underlay.b * ( 1.0f - a );
					g += underlay.g * ( 1.0f - a );
					r += underlay.r * ( 1.0f - a );
					a = 1.0f;

				} else if ( a > 0.0f ) {

					b /= a;
					g /= a;
					r /= a;

				}

				out [ x * 4 + 0 ] = static_cast < uint8_t > ( math::saturate ( b ) * 255.0f + 0.5f );
				out [ x * 4 + 1 ] = static_cast < uint8_t > ( math::saturate ( g ) * 255.0f + 0.5f );
				out [ x * 4 + 2 ] = static_cast < uint8_t > ( math::saturate ( r ) * 255.0f + 0.5f );
				out [ x * 4 + 3 ] = static_cast < uint8_t > ( math::saturate ( a ) * 255.0f + 0.5f );

			}

		}

		context->Unmap ( staging, 0 );
		staging->Release ( );

		IWICImagingFactory* imaging = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame_encode = nullptr;
		bool ok = false;

		if ( SUCCEEDED ( CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof ( IWICImagingFactory ), reinterpret_cast < void** > ( &imaging ) ) ) && SUCCEEDED ( imaging->CreateStream ( &stream ) ) && SUCCEEDED ( stream->InitializeFromFilename ( path, GENERIC_WRITE ) ) && SUCCEEDED ( imaging->CreateEncoder ( GUID_ContainerFormatPng, nullptr, &encoder ) ) && SUCCEEDED ( encoder->Initialize ( stream, WICBitmapEncoderNoCache ) ) && SUCCEEDED ( encoder->CreateNewFrame ( &frame_encode, nullptr ) ) && SUCCEEDED ( frame_encode->Initialize ( nullptr ) ) ) {

			WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

			frame_encode->SetSize ( static_cast < UINT > ( save_width ), static_cast < UINT > ( save_height ) );
			frame_encode->SetPixelFormat ( &format );
			ok = SUCCEEDED ( frame_encode->WritePixels ( static_cast < UINT > ( save_height ), static_cast < UINT > ( save_width * 4 ), static_cast < UINT > ( pixels.size ( ) ), pixels.data ( ) ) ) && SUCCEEDED ( frame_encode->Commit ( ) ) && SUCCEEDED ( encoder->Commit ( ) );

		}

		drop ( frame_encode );
		drop ( encoder );
		drop ( stream );
		drop ( imaging );

		return ok;

	}

}