#pragma once

#include "base/list/list.h"
#include "base/math/math.h"
#include "render/colors/colors.h"
#include "render/fonts/fonts.h"

struct HWND__;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID2D1Bitmap1;
struct IDCompositionTarget;
struct IDCompositionVisual;
struct IDCompositionSurface;

namespace render {

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

}