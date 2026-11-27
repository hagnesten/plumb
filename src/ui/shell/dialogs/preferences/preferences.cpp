#include "ui/shell/dialogs/preferences/preferences.h"
#include "ui/core/app/app.h"
#include "base/strings/strings.h"
#include "ui/style/icons/icons.h"

namespace ui {

	static const sample_token listing_sample [ ] = {
		{ L"140001370", tone::ADDRESS }, { L"  ", tone::INK }, { L"fn_140001370", tone::NAME }, { L":", tone::INK }, { L"\n", tone::INK },
		{ L"140001370", tone::ADDRESS }, { L"    ", tone::INK }, { L"sub", tone::MNEMONIC }, { L"     ", tone::INK }, { L"rsp", tone::REGISTER }, { L", ", tone::INK }, { L"0x28", tone::NUMBER }, { L"\n", tone::INK },
		{ L"140001374", tone::ADDRESS }, { L"    ", tone::INK }, { L"lea", tone::MNEMONIC }, { L"     ", tone::INK }, { L"rcx", tone::REGISTER }, { L", ", tone::INK }, { L"[rip+0x2C85]", tone::INK }, { L"  ", tone::INK }, { L"; \"hello\"", tone::STRING }, { L"\n", tone::INK },
		{ L"14000137B", tone::ADDRESS }, { L"    ", tone::INK }, { L"call", tone::MNEMONIC }, { L"    ", tone::INK }, { L"GetModuleHandleW", tone::IMPORT }, { L"\n", tone::INK },
		{ L"140001381", tone::ADDRESS }, { L"    ", tone::INK }, { L"test", tone::MNEMONIC }, { L"    ", tone::INK }, { L"rax", tone::REGISTER }, { L", ", tone::INK }, { L"rax", tone::REGISTER }, { L"  ", tone::INK }, { L"; module", tone::COMMENT }, { L"\n", tone::INK },
		{ L"140001384", tone::ADDRESS }, { L"    ", tone::INK }, { L"jz", tone::MNEMONIC }, { L"      ", tone::INK }, { L"loc_1400013A0", tone::LABEL }
	};

	static const sample_token code_sample [ ] = {
		{ L"__int64", tone::TYPE }, { L" ", tone::INK }, { L"__fastcall", tone::KEYWORD }, { L" ", tone::INK }, { L"fn_140001370", tone::FUNCTION }, { L"(", tone::INK }, { L"__int64", tone::TYPE }, { L" ", tone::INK }, { L"a1", tone::PARAMETER }, { L")", tone::INK }, { L"\n", tone::INK },
		{ L"{", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"__int64", tone::TYPE }, { L" ", tone::INK }, { L"result", tone::LOCAL }, { L"; ", tone::INK }, { L"// rax", tone::COMMENT }, { L"\n", tone::INK },
		{ L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"result", tone::LOCAL }, { L" = ", tone::OPERATOR }, { L"GetModuleHandleW", tone::IMPORT }, { L"(", tone::INK }, { L"\"hello\"", tone::STRING }, { L");", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"if", tone::CONTROL }, { L" ( ", tone::INK }, { L"!", tone::OPERATOR }, { L"result", tone::LOCAL }, { L" )", tone::INK }, { L"\n", tone::INK },
		{ L"    ", tone::INK }, { L"return", tone::CONTROL }, { L" ", tone::INK }, { L"0i64", tone::NUMBER }, { L";", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"g_140006000", tone::GLOBAL }, { L" = ", tone::OPERATOR }, { L"*(", tone::INK }, { L"_DWORD", tone::TYPE }, { L" *)(", tone::INK }, { L"a1", tone::PARAMETER }, { L" + ", tone::OPERATOR }, { L"60", tone::NUMBER }, { L");", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"return", tone::CONTROL }, { L" ", tone::INK }, { L"result", tone::LOCAL }, { L";", tone::INK }, { L"\n", tone::INK },
		{ L"}", tone::INK }
	};

	static void set_field ( field& target, const wchar_t* text ) {

		int32_t i = 0;

		for ( ; text [ i ] && i < 250; i++ )
			target.text [ i ] = text [ i ];

		target.text [ i ] = 0;
		target.length = i;
		target.caret = i;
		target.anchor = i;

	}

	bool app::settings_check ( uint64_t id, math::rect area, const wchar_t* label, bool value ) {

		render::canvas& c = canvas;
		widget& item = state ( id );
		bool clicked = interact ( id, area, item );
		math::rect box ( area.x, area.center ( ).y - 7.0f, 14.0f, 14.0f );

		c.fill ( area, metric::radius, ink ( tone::HOVER ).fade ( item.hover ) );
		c.outline ( box, 3.0f, value ? ink ( tone::ACCENT ) : ink ( tone::SUNKEN ), 1.0f, value ? ink ( tone::ACCENT ) : ink ( tone::LINE_STRONG ) );

		if ( value )
			c.icon ( icons::CHECK, box.center ( ), 9.0f, ink ( tone::ACCENT_INK ) );

		c.text ( label, render::font::UI, math::vec2 ( area.x + 24.0f, area.center ( ).y ), ink ( tone::INK ) );

		if ( item.hover > 0.5f )
			wanted_cursor = pointer::HAND;

		return clicked;

	}

	void app::draw_sample ( math::rect area, const sample_token* tokens, int32_t count, render::font role, float advance ) {

		render::canvas& c = canvas;
		float x = area.x;
		float y = area.y + advance * 0.5f;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( tokens [ i ].text [ 0 ] == L'\n' ) {

				x = area.x;
				y += advance;

				continue;

			}

			c.text ( tokens [ i ].text, role, math::vec2 ( x, y ), ink ( tokens [ i ].kind ) );
			x += c.measure ( tokens [ i ].text, role );

		}

	}

	void app::draw_settings ( ) {

		settle ( settings_show, settings_open ? 1.0f : 0.0f );

		float show = math::saturate ( settings_show );

		if ( show <= 0.004f ) {

			font_fields_ready = false;

			return;

		}

		render::canvas& c = canvas;
		uint64_t blocker = make_id ( "settings_blocker" );
		widget& block = state ( blocker );

		if ( interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) )
			settings_open = false;

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.45f * show ) );

		float box_w = base::min ( w - 40.0f, 900.0f );
		float box_h = base::min ( h - 40.0f, 690.0f );
		math::rect box ( math::floor ( w * 0.5f - box_w * 0.5f ), math::floor ( h * 0.5f - box_h * 0.5f ), box_w, box_h );

		c.push ( box.center ( ), 1.0f, show );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "settings_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );

		if ( icon_button ( make_id ( "settings_close" ), math::vec2 ( box.right ( ) - 22.0f, box.y + 22.0f ), 30.0f, icons::CANCEL, 11.0f, L"Close" ) )
			settings_open = false;

		c.text ( L"Settings", render::font::HEADING, math::vec2 ( box.x + 28.0f, box.y + 30.0f ), ink ( tone::INK ) );

		const float base_size [ 3 ] = { 12.0f, 13.0f, 13.0f };
		const wchar_t* font_labels [ 3 ] = { L"Interface", L"Disassembly", L"C pseudocode" };

		if ( !font_fields_ready ) {

			for ( int32_t g = 0; g < 3; g++ ) {

				set_field ( font_fields [ g ], font_names [ g ] [ 0 ] ? font_names [ g ] : ( gpu ? gpu->family_used [ g ] : L"" ) );
				font_seen [ g ] = font_fields [ g ].revision;

			}

			font_fields_ready = true;

		}

		float left = box.x + 28.0f;
		float column = 400.0f;
		float y = box.y + 62.0f;

		section_heading ( math::rect ( left, y, column, 20.0f ), L"FONTS", nullptr );
		y += 26.0f;

		for ( int32_t g = 0; g < 3; g++ ) {

			math::rect row ( left, y, column, 28.0f );
			math::rect name_area ( left + 110.0f, y + 1.0f, 186.0f, 26.0f );

			c.text ( font_labels [ g ], render::font::UI, math::vec2 ( left, row.center ( ).y ), ink ( tone::INK_SOFT ) );
			text_field ( make_id ( "settings_font", g ), font_fields [ g ], name_area, L"Default", 0, render::font::UI );

			if ( font_fields [ g ].revision != font_seen [ g ] ) {

				font_seen [ g ] = font_fields [ g ].revision;

				const wchar_t* typed = font_fields [ g ].text;

				if ( !typed [ 0 ] || ( gpu && gpu->has_family ( typed ) ) ) {

					int32_t k = 0;

					for ( ; typed [ k ] && k < 63; k++ )
						font_names [ g ] [ k ] = typed [ k ];

					font_names [ g ] [ k ] = 0;
					fonts_dirty = true;

				}

			}

			if ( font_fields [ g ].text [ 0 ] && gpu && !base::same_text ( font_fields [ g ].text, gpu->family_used [ g ] ) && !gpu->has_family ( font_fields [ g ].text ) )
				c.text ( L"not installed", render::font::SMALL, math::vec2 ( name_area.x, name_area.bottom ( ) + 6.0f ), ink ( tone::WARNING ) );

			float size = font_sizes [ g ] >= 6.0f ? font_sizes [ g ] : base_size [ g ];
			base::writer size_text;

			size_text.number ( static_cast < int64_t > ( size + 0.5f ) ).add ( L" px" );

			if ( icon_button ( make_id ( "settings_smaller", g ), math::vec2 ( left + 318.0f, row.center ( ).y ), 24.0f, icons::REMOVE, 10.0f, L"Smaller" ) && size > 9.0f ) {

				font_sizes [ g ] = size - 1.0f;
				fonts_dirty = true;

			}

			c.text ( size_text.text ( ), render::font::UI, math::vec2 ( left + 350.0f, row.center ( ).y ), ink ( tone::INK ), render::align::CENTER );

			if ( icon_button ( make_id ( "settings_larger", g ), math::vec2 ( left + 382.0f, row.center ( ).y ), 24.0f, icons::ADD, 10.0f, L"Larger" ) && size < 20.0f ) {

				font_sizes [ g ] = size + 1.0f;
				fonts_dirty = true;

			}

			y += 38.0f;

		}

		y += 6.0f;
		section_heading ( math::rect ( left, y, column, 20.0f ), L"DECOMPILER", nullptr );
		y += 24.0f;

		{

			bool* flags [ 7 ] = { &c_spaced, &c_no_ternary, &c_hide_casts, &c_collapse_locals, &name_virtuals, &c_guess_structs, &c_index_pointers };
			const wchar_t* labels [ 7 ] = { L"Spaces inside parentheses, like  fn ( a1 )", L"Write ternaries as if and else", L"Hide casts", L"Collapse local declarations", L"Name virtual functions by vtable slot", L"Guess structs for pointer parameters", L"Index pointers like arrays, like  a3[128]" };

			for ( int32_t i = 0; i < 7; i++ ) {

				if ( settings_check ( make_id ( "settings_check", i ), math::rect ( left, y, column, 24.0f ), labels [ i ], *flags [ i ] ) ) {

					*flags [ i ] = !*flags [ i ];
					names_revision++;
					annotation_revision++;

				}

				y += 26.0f;

			}

		}

		y += 6.0f;
		section_heading ( math::rect ( left, y, column, 20.0f ), L"THEME", nullptr );
		y += 24.0f;

		if ( settings_check ( make_id ( "settings_light" ), math::rect ( left, y, column, 24.0f ), L"Light theme", light ) )
			set_theme ( !light );

		float right = left + column + 32.0f;
		float right_w = box.right ( ) - 28.0f - right;
		float top = box.y + 62.0f;
		float mono_adv = metric::code_row;
		float code_adv = metric::code_row;

		section_heading ( math::rect ( right, top, right_w, 20.0f ), L"PREVIEW", L"updates as you edit" );

		math::rect preview ( right, top + 26.0f, right_w, 8.0f * mono_adv + 9.0f * code_adv + 34.0f );

		c.outline ( preview, metric::radius, ink ( tone::BASE ), 1.0f, ink ( tone::LINE ) );
		c.push_clip ( preview );
		draw_sample ( math::rect ( preview.x + 12.0f, preview.y + 10.0f, preview.w - 24.0f, 6.0f * mono_adv ), listing_sample, static_cast < int32_t > ( sizeof ( listing_sample ) / sizeof ( listing_sample [ 0 ] ) ), render::font::MONO, mono_adv );
		c.fill ( math::rect ( preview.x, preview.y + 6.0f * mono_adv + 18.0f, preview.w, 1.0f ), 0.0f, ink ( tone::LINE ) );
		draw_sample ( math::rect ( preview.x + 12.0f, preview.y + 6.0f * mono_adv + 26.0f, preview.w - 24.0f, 9.0f * code_adv ), code_sample, static_cast < int32_t > ( sizeof ( code_sample ) / sizeof ( code_sample [ 0 ] ) ), render::font::CODE, code_adv );
		c.pop_clip ( );

		float buttons_y = box.bottom ( ) - 44.0f;

		if ( button ( make_id ( "settings_colors" ), math::rect ( box.right ( ) - 28.0f - 130.0f, buttons_y, 130.0f, 28.0f ), L"Edit colors\x2026", 0, true ) ) {

			open_colors ( );
			c.pop ( );

			return;

		}

		if ( button ( make_id ( "settings_reset_fonts" ), math::rect ( box.right ( ) - 28.0f - 270.0f, buttons_y, 130.0f, 28.0f ), L"Reset fonts", 0, false ) ) {

			for ( int32_t g = 0; g < 3; g++ ) {

				font_names [ g ] [ 0 ] = 0;
				font_sizes [ g ] = 0.0f;

			}

			fonts_dirty = true;
			font_fields_ready = false;

		}

		c.pop ( );

	}

}