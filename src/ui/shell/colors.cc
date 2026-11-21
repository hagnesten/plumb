#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	static const tone choices [ ] = {
		tone::BASE, tone::PANEL, tone::INK, tone::ACCENT, tone::SELECT, tone::ADDRESS, tone::MNEMONIC, tone::REGISTER,
		tone::NUMBER, tone::STRING, tone::NAME, tone::LABEL, tone::COMMENT, tone::KEYWORD, tone::CONTROL, tone::TYPE,
		tone::FUNCTION, tone::LOCAL, tone::PARAMETER, tone::GLOBAL, tone::OPERATOR, tone::IMPORT, tone::RAISED, tone::SUNKEN,
		tone::TITLE, tone::LINE, tone::LINE_STRONG, tone::INK_SOFT, tone::INK_FAINT, tone::ACCENT_INK, tone::SELECT_LINE, tone::HOVER,
		tone::GUIDE, tone::DANGER, tone::SUCCESS, tone::WARNING, tone::BYTES
	};

	static const char* choice_keys [ ] = {
		"background", "panels", "text", "accent", "selection", "addresses", "mnemonics", "registers",
		"numbers", "strings", "names", "labels", "comments", "keywords", "control", "types",
		"functions", "locals", "parameters", "globals", "operators", "imports", "raised", "sunken",
		"title", "lines", "strong_lines", "soft_text", "faint_text", "accent_text", "selection_edge", "hover",
		"guides", "danger", "success", "warning", "bytes"
	};

	static const wchar_t* choice_labels [ ] = {
		L"Background", L"Panels", L"Text", L"Accent", L"Selection", L"Addresses", L"Mnemonics", L"Registers",
		L"Numbers", L"Strings", L"Names", L"Labels", L"Comments", L"Keywords", L"Control flow", L"Types",
		L"Functions", L"Locals", L"Parameters", L"Globals", L"Operators", L"Imports", L"Raised surfaces", L"Sunken fields",
		L"Title bar", L"Lines", L"Strong lines", L"Soft text", L"Faint text", L"Text on accent", L"Selection edge", L"Hover",
		L"Indent guides", L"Errors", L"Success", L"Warnings", L"Opcode bytes"
	};

	static constexpr int32_t choice_count = 37;

	static const tone listing_items [ ] = { tone::BASE, tone::INK, tone::ADDRESS, tone::BYTES, tone::NAME, tone::LABEL, tone::MNEMONIC, tone::REGISTER, tone::NUMBER, tone::STRING, tone::COMMENT, tone::IMPORT, tone::SELECT, tone::HOVER };
	static const tone code_items [ ] = { tone::BASE, tone::INK, tone::KEYWORD, tone::CONTROL, tone::TYPE, tone::FUNCTION, tone::LOCAL, tone::PARAMETER, tone::GLOBAL, tone::NUMBER, tone::STRING, tone::OPERATOR, tone::LABEL, tone::COMMENT, tone::IMPORT, tone::GUIDE };
	static const tone interface_items [ ] = { tone::BASE, tone::PANEL, tone::RAISED, tone::SUNKEN, tone::TITLE, tone::LINE, tone::LINE_STRONG, tone::INK, tone::INK_SOFT, tone::INK_FAINT, tone::ACCENT, tone::ACCENT_INK, tone::SELECT, tone::SELECT_LINE, tone::HOVER, tone::DANGER, tone::SUCCESS, tone::WARNING };

	static const sample_token listing_sample [ ] = {
		{ L"0000000140001370", tone::ADDRESS }, { L"  ", tone::INK }, { L"fn_140001370", tone::NAME }, { L"  ", tone::INK }, { L"; 6 instructions, 2 callers", tone::COMMENT }, { L"\n", tone::INK },
		{ L"0000000140001370", tone::ADDRESS }, { L"  ", tone::INK }, { L"48 83 EC 28", tone::BYTES }, { L"     ", tone::INK }, { L"sub", tone::MNEMONIC }, { L"     ", tone::INK }, { L"rsp", tone::REGISTER }, { L", ", tone::INK }, { L"0x28", tone::NUMBER }, { L"\n", tone::INK },
		{ L"0000000140001374", tone::ADDRESS }, { L"  ", tone::INK }, { L"48 8D 0D 85", tone::BYTES }, { L"     ", tone::INK }, { L"lea", tone::MNEMONIC }, { L"     ", tone::INK }, { L"rcx", tone::REGISTER }, { L", ", tone::INK }, { L"[rip+0x2C85]", tone::INK }, { L"  ", tone::INK }, { L"\"hello\"", tone::STRING }, { L"\n", tone::INK },
		{ L"000000014000137B", tone::ADDRESS }, { L"  ", tone::INK }, { L"FF 15 7F 2E", tone::BYTES }, { L"     ", tone::INK }, { L"call", tone::MNEMONIC }, { L"    ", tone::INK }, { L"GetModuleHandleW", tone::IMPORT }, { L"\n", tone::INK },
		{ L"0000000140001381", tone::ADDRESS }, { L"  ", tone::INK }, { L"48 85 C0", tone::BYTES }, { L"        ", tone::INK }, { L"test", tone::MNEMONIC }, { L"    ", tone::INK }, { L"rax", tone::REGISTER }, { L", ", tone::INK }, { L"rax", tone::REGISTER }, { L"\n", tone::INK },
		{ L"0000000140001384", tone::ADDRESS }, { L"  ", tone::INK }, { L"74 1A", tone::BYTES }, { L"           ", tone::INK }, { L"jz", tone::MNEMONIC }, { L"      ", tone::INK }, { L".L30", tone::LABEL }, { L"\n", tone::INK },
		{ L"0000000140001386", tone::ADDRESS }, { L"  ", tone::INK }, { L"8B 48 3C", tone::BYTES }, { L"        ", tone::INK }, { L"mov", tone::MNEMONIC }, { L"     ", tone::INK }, { L"ecx", tone::REGISTER }, { L", ", tone::INK }, { L"dword ptr [rax+0x3C]", tone::INK }, { L"\n", tone::INK },
		{ L".L30", tone::LABEL }, { L":", tone::INK }, { L"\n", tone::INK },
		{ L"00000001400013A0", tone::ADDRESS }, { L"  ", tone::INK }, { L"33 C0", tone::BYTES }, { L"           ", tone::INK }, { L"xor", tone::MNEMONIC }, { L"     ", tone::INK }, { L"eax", tone::REGISTER }, { L", ", tone::INK }, { L"eax", tone::REGISTER }, { L"\n", tone::INK },
		{ L"00000001400013A2", tone::ADDRESS }, { L"  ", tone::INK }, { L"48 83 C4 28", tone::BYTES }, { L"     ", tone::INK }, { L"add", tone::MNEMONIC }, { L"     ", tone::INK }, { L"rsp", tone::REGISTER }, { L", ", tone::INK }, { L"0x28", tone::NUMBER }, { L"\n", tone::INK },
		{ L"00000001400013A6", tone::ADDRESS }, { L"  ", tone::INK }, { L"C3", tone::BYTES }, { L"              ", tone::INK }, { L"ret", tone::MNEMONIC }
	};

	static const sample_token code_sample [ ] = {
		{ L"__int64", tone::TYPE }, { L" ", tone::INK }, { L"__fastcall", tone::KEYWORD }, { L" ", tone::INK }, { L"fn_140001370", tone::FUNCTION }, { L"(", tone::INK }, { L"__int64", tone::TYPE }, { L" ", tone::INK }, { L"a1", tone::PARAMETER }, { L", ", tone::INK }, { L"int", tone::TYPE }, { L" ", tone::INK }, { L"a2", tone::PARAMETER }, { L")", tone::INK }, { L"\n", tone::INK },
		{ L"{", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"__int64", tone::TYPE }, { L" ", tone::INK }, { L"result", tone::LOCAL }, { L"; ", tone::INK }, { L"// rax", tone::COMMENT }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"_DWORD", tone::TYPE }, { L" *", tone::INK }, { L"v3", tone::LOCAL }, { L"; ", tone::INK }, { L"// rcx", tone::COMMENT }, { L"\n", tone::INK },
		{ L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"result", tone::LOCAL }, { L" = ", tone::OPERATOR }, { L"GetModuleHandleW", tone::IMPORT }, { L"(", tone::INK }, { L"L\"kernel32\"", tone::STRING }, { L");", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"if", tone::CONTROL }, { L" ( ", tone::INK }, { L"!", tone::OPERATOR }, { L"result", tone::LOCAL }, { L" )", tone::INK }, { L"\n", tone::INK },
		{ L"    ", tone::INK }, { L"goto", tone::CONTROL }, { L" ", tone::INK }, { L"LABEL_4", tone::LABEL }, { L";", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"v3", tone::LOCAL }, { L" = ", tone::OPERATOR }, { L"(", tone::INK }, { L"_DWORD", tone::TYPE }, { L" *)(", tone::INK }, { L"a1", tone::PARAMETER }, { L" + ", tone::OPERATOR }, { L"0x3C", tone::NUMBER }, { L");", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"g_140006000", tone::GLOBAL }, { L" = ", tone::OPERATOR }, { L"*", tone::OPERATOR }, { L"v3", tone::LOCAL }, { L" + ", tone::OPERATOR }, { L"a2", tone::PARAMETER }, { L";", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"return", tone::CONTROL }, { L" ", tone::INK }, { L"fn_1400014B0", tone::FUNCTION }, { L"(", tone::INK }, { L"result", tone::LOCAL }, { L");", tone::INK }, { L"\n", tone::INK },
		{ L"LABEL_4", tone::LABEL }, { L":", tone::INK }, { L"\n", tone::INK },
		{ L"  ", tone::INK }, { L"return", tone::CONTROL }, { L" ", tone::INK }, { L"0i64", tone::NUMBER }, { L";", tone::INK }, { L"\n", tone::INK },
		{ L"}", tone::INK }
	};

	int32_t tone_choice_count ( ) {

		return choice_count;

	}

	tone tone_choice ( int32_t index ) {

		return choices [ index ];

	}

	const char* tone_choice_key ( int32_t index ) {

		return choice_keys [ index ];

	}

	const wchar_t* tone_choice_label ( int32_t index ) {

		return choice_labels [ index ];

	}

	static const wchar_t* label_of ( tone value ) {

		for ( int32_t i = 0; i < choice_count; i++ ) {

			if ( choices [ i ] == value )
				return choice_labels [ i ];

		}

		return L"";

	}

	static void put_hex ( field& target, uint32_t value ) {

		base::writer text;

		text.hex ( value, 6 );

		int32_t i = 0;

		for ( ; text.text ( ) [ i ] && i < 250; i++ )
			target.text [ i ] = text.text ( ) [ i ];

		target.text [ i ] = 0;
		target.length = i;
		target.caret = i;
		target.anchor = i;

	}

	void app::open_colors ( ) {

		for ( int32_t t = 0; t < 2; t++ ) {

			for ( int32_t i = 0; i < static_cast < int32_t > ( tone::COUNT ); i++ ) {

				colors_saved [ t ] [ i ] = custom_colors [ t ] [ i ];
				colors_saved_set [ t ] [ i ] = custom_set [ t ] [ i ];

			}

		}

		colors_open = true;
		colors_picker_for = -1;
		settings_open = false;

	}

	void app::close_colors ( bool keep ) {

		if ( !keep ) {

			for ( int32_t t = 0; t < 2; t++ ) {

				for ( int32_t i = 0; i < static_cast < int32_t > ( tone::COUNT ); i++ ) {

					custom_colors [ t ] [ i ] = colors_saved [ t ] [ i ];
					custom_set [ t ] [ i ] = colors_saved_set [ t ] [ i ];

				}

			}

		} else if ( persist )
			save_settings ( );

		colors_open = false;
		focus_id = 0;
		focused_field = nullptr;
		pending = base::max ( pending, 3 );

	}

	void app::set_custom_color ( int32_t slot, uint32_t value ) {

		int32_t theme = light ? 1 : 0;

		custom_colors [ theme ] [ slot ] = value & 0xFFFFFF;
		custom_set [ theme ] [ slot ] = true;
		colors_picker_hex = value & 0xFFFFFF;
		pending = base::max ( pending, 2 );

	}

	void app::draw_colors ( ) {

		settle ( colors_show, colors_open ? 1.0f : 0.0f );

		float show = math::saturate ( colors_show );

		if ( show <= 0.004f )
			return;

		render::canvas& c = canvas;
		uint64_t blocker = make_id ( "colors_blocker" );
		widget& block = state ( blocker );

		interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block );
		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.45f * show ) );

		float box_w = base::min ( w - 40.0f, 1000.0f );
		float box_h = base::min ( h - 40.0f, 640.0f );
		math::rect box ( math::floor ( w * 0.5f - box_w * 0.5f ), math::floor ( h * 0.5f - box_h * 0.5f ), box_w, box_h );

		c.push ( box.center ( ), 1.0f, show );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "colors_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );

		if ( icon_button ( make_id ( "colors_close" ), math::vec2 ( box.right ( ) - 22.0f, box.y + 22.0f ), 30.0f, icons::CANCEL, 11.0f, L"Close without keeping changes" ) ) {

			close_colors ( false );
			c.pop ( );

			return;

		}

		c.text ( L"Colors", render::font::HEADING, math::vec2 ( box.x + 28.0f, box.y + 30.0f ), ink ( tone::INK ) );

		int32_t theme = light ? 1 : 0;
		float left = box.x + 28.0f;
		float y = box.y + 58.0f;

		c.text ( L"Theme", render::font::UI, math::vec2 ( left, y + 13.0f ), ink ( tone::INK_SOFT ) );

		const wchar_t* theme_names [ 2 ] = { L"Dark", L"Light" };

		for ( int32_t i = 0; i < 2; i++ ) {

			math::rect slot ( left + 60.0f + static_cast < float > ( i ) * 76.0f, y, 76.0f, 26.0f );
			uint64_t id = make_id ( "colors_theme", i );
			widget& item = state ( id );
			bool chosen = theme == i;

			if ( interact ( id, slot, item ) && !chosen ) {

				set_theme ( i == 1 );
				colors_picker_for = -1;

			}

			c.outline ( slot, metric::radius, chosen ? ink ( tone::SELECT ) : ink ( tone::SUNKEN ), 1.0f, chosen ? ink ( tone::SELECT_LINE ) : ink ( tone::LINE ) );
			c.fill ( slot, metric::radius, ink ( tone::HOVER ).fade ( item.hover ) );
			c.text ( theme_names [ i ], render::font::UI, slot.center ( ), chosen ? ink ( tone::INK ) : ink ( tone::INK_SOFT ), render::align::CENTER );

		}

		c.text ( L"Changes show everywhere as you make them. Cancel puts the colors back.", render::font::SMALL, math::vec2 ( left + 236.0f, y + 13.0f ), ink ( tone::INK_FAINT ) );

		y += 42.0f;

		const wchar_t* tab_names [ 3 ] = { L"Disassembly", L"C pseudocode", L"Interface" };
		float tab_x = left;

		for ( int32_t i = 0; i < 3; i++ ) {

			float tab_w = c.measure ( tab_names [ i ], render::font::UI_STRONG ) + 24.0f;
			math::rect slot ( tab_x, y, tab_w, 30.0f );
			uint64_t id = make_id ( "colors_tab", i );
			widget& item = state ( id );

			if ( interact ( id, slot, item ) )
				colors_tab = i;

			c.fill ( slot, metric::radius, ink ( tone::HOVER ).fade ( item.hover ) );
			c.text ( tab_names [ i ], render::font::UI_STRONG, slot.center ( ), colors_tab == i ? ink ( tone::INK ) : ink ( tone::INK_SOFT ), render::align::CENTER );

			if ( colors_tab == i )
				c.fill ( math::rect ( slot.x + 6.0f, slot.bottom ( ) - 2.0f, slot.w - 12.0f, 2.0f ), 0.0f, ink ( tone::ACCENT ) );

			tab_x += tab_w + 4.0f;

		}

		c.fill ( math::rect ( left, y + 30.0f, box.w - 56.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );

		float top = y + 44.0f;
		float bottom = box.bottom ( ) - 62.0f;
		math::rect sample ( left, top, 520.0f, bottom - top );
		math::rect hit_rects [ 160 ];
		tone hit_tones [ 160 ];
		int32_t hit_count = 0;

		auto add_hit = [ & ] ( math::rect area, tone kind ) {

			if ( hit_count < 160 ) {

				hit_rects [ hit_count ] = area;
				hit_tones [ hit_count ] = kind;
				hit_count++;

			}

		};

		uint64_t sample_id = make_id ( "colors_sample" );
		widget& sample_state = state ( sample_id );
		bool sample_clicked = interact ( sample_id, sample, sample_state );

		c.outline ( sample, metric::radius, ink ( tone::BASE ), 1.0f, ink ( tone::LINE_STRONG ) );
		add_hit ( sample, tone::BASE );
		c.push_clip ( sample );

		if ( colors_tab < 2 ) {

			bool code = colors_tab == 1;
			const sample_token* tokens = code ? code_sample : listing_sample;
			int32_t count = code ? static_cast < int32_t > ( sizeof ( code_sample ) / sizeof ( code_sample [ 0 ] ) ) : static_cast < int32_t > ( sizeof ( listing_sample ) / sizeof ( listing_sample [ 0 ] ) );
			render::font role = code ? render::font::CODE : render::font::MONO;
			float row_h = metric::code_row + 2.0f;
			float x = sample.x + 14.0f;
			float line_y = sample.y + 12.0f;
			int32_t row = 0;
			float adv = c.measure ( L"0000000000", role ) * 0.1f;

			math::rect selected_row ( sample.x + 1.0f, line_y + row_h * 3.0f, sample.w - 2.0f, row_h );
			math::rect hover_row ( sample.x + 1.0f, line_y + row_h * 6.0f, sample.w - 2.0f, row_h );

			c.fill ( selected_row, 0.0f, ink ( tone::SELECT ) );
			c.fill ( math::rect ( selected_row.x, selected_row.y, 2.0f, row_h ), 0.0f, ink ( tone::SELECT_LINE ) );
			c.fill ( hover_row, 0.0f, ink ( tone::HOVER ) );
			add_hit ( selected_row, tone::SELECT );
			add_hit ( hover_row, tone::HOVER );

			if ( code ) {

				for ( int32_t guide = 0; guide < 2; guide++ )
					c.fill ( math::rect ( x + adv * ( 0.5f + 2.0f * static_cast < float > ( guide ) ), line_y + row_h * ( guide ? 7.0f : 2.0f ), 1.0f, row_h * ( guide ? 1.0f : 10.0f ) ), 0.0f, ink ( tone::GUIDE ) );

				add_hit ( math::rect ( x, line_y + row_h * 2.0f, adv * 1.5f, row_h * 10.0f ), tone::GUIDE );

			}

			for ( int32_t i = 0; i < count; i++ ) {

				if ( tokens [ i ].text [ 0 ] == L'\n' ) {

					x = sample.x + 14.0f;
					row++;

					continue;

				}

				float width = c.measure ( tokens [ i ].text, role );
				float cy = line_y + row_h * static_cast < float > ( row ) + row_h * 0.5f;
				bool spaces = true;

				for ( const wchar_t* at = tokens [ i ].text; *at; at++ )
					spaces = spaces && *at == L' ';

				c.text ( tokens [ i ].text, role, math::vec2 ( x, cy ), ink ( tokens [ i ].kind ) );

				if ( !spaces )
					add_hit ( math::rect ( x, cy - row_h * 0.5f, width, row_h ), tokens [ i ].kind );

				if ( !spaces && static_cast < int32_t > ( tokens [ i ].kind ) == colors_pick )
					c.fill ( math::rect ( x, cy + row_h * 0.5f - 2.0f, width, 1.0f ), 0.0f, ink ( tone::ACCENT ).fade ( 0.8f ) );

				x += width;

			}

		} else {

			math::rect title_bar ( sample.x + 1.0f, sample.y + 1.0f, sample.w - 2.0f, 30.0f );

			c.fill ( title_bar, 0.0f, ink ( tone::TITLE ) );
			c.fill ( math::rect ( title_bar.x, title_bar.bottom ( ), title_bar.w, 1.0f ), 0.0f, ink ( tone::LINE ) );
			c.text ( L"Plumb   File   Edit   View", render::font::UI, math::vec2 ( title_bar.x + 14.0f, title_bar.center ( ).y ), ink ( tone::INK_SOFT ) );
			add_hit ( title_bar, tone::TITLE );

			math::rect side ( sample.x + 1.0f, title_bar.bottom ( ) + 1.0f, 200.0f, sample.h - 32.0f );

			c.fill ( side, 0.0f, ink ( tone::PANEL ) );
			c.fill ( math::rect ( side.right ( ), side.y, 1.0f, side.h ), 0.0f, ink ( tone::LINE_STRONG ) );
			add_hit ( side, tone::PANEL );
			add_hit ( math::rect ( side.right ( ) - 2.0f, side.y, 5.0f, side.h ), tone::LINE_STRONG );
			c.text ( L"FUNCTIONS", render::font::TINY, math::vec2 ( side.x + 12.0f, side.y + 16.0f ), ink ( tone::INK_SOFT ) );
			add_hit ( math::rect ( side.x + 12.0f, side.y + 8.0f, 70.0f, 16.0f ), tone::INK_SOFT );

			const wchar_t* names [ 5 ] = { L"fn_140001010", L"fn_1400010F0", L"start", L"fn_140001330", L"fn_140001370" };

			for ( int32_t i = 0; i < 5; i++ ) {

				math::rect row ( side.x, side.y + 30.0f + static_cast < float > ( i ) * 22.0f, side.w, 22.0f );

				if ( i == 1 ) {

					c.fill ( row, 0.0f, ink ( tone::SELECT ) );
					c.fill ( math::rect ( row.x, row.y, 2.0f, row.h ), 0.0f, ink ( tone::SELECT_LINE ) );
					add_hit ( row, tone::SELECT );
					add_hit ( math::rect ( row.x, row.y, 4.0f, row.h ), tone::SELECT_LINE );

				} else if ( i == 3 ) {

					c.fill ( row, 0.0f, ink ( tone::HOVER ) );
					add_hit ( row, tone::HOVER );

				}

				c.text ( names [ i ], render::font::MONO_SMALL, math::vec2 ( row.x + 12.0f, row.center ( ).y ), ink ( tone::INK ) );
				c.text ( L"0x46", render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 12.0f, row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );
				add_hit ( math::rect ( row.x + 12.0f, row.y + 3.0f, 100.0f, 16.0f ), tone::INK );
				add_hit ( math::rect ( row.right ( ) - 44.0f, row.y + 3.0f, 32.0f, 16.0f ), tone::INK_FAINT );

			}

			float px = side.right ( ) + 24.0f;
			float py = side.y + 20.0f;
			math::rect raised ( px, py, 260.0f, 96.0f );

			c.outline ( raised, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );
			add_hit ( raised, tone::RAISED );
			c.text ( L"A dialog on a raised surface", render::font::UI, math::vec2 ( raised.x + 14.0f, raised.y + 20.0f ), ink ( tone::INK ) );

			math::rect sunken ( raised.x + 14.0f, raised.y + 38.0f, 150.0f, 24.0f );

			c.outline ( sunken, metric::radius, ink ( tone::SUNKEN ), 1.0f, ink ( tone::LINE ) );
			c.text ( L"Sunken field", render::font::UI, math::vec2 ( sunken.x + 8.0f, sunken.center ( ).y ), ink ( tone::INK_FAINT ) );
			add_hit ( sunken, tone::SUNKEN );
			add_hit ( math::rect ( sunken.x + 8.0f, sunken.y + 4.0f, 80.0f, 16.0f ), tone::INK_FAINT );

			math::rect accent ( raised.x + 172.0f, raised.y + 38.0f, 74.0f, 24.0f );

			c.fill ( accent, metric::radius, ink ( tone::ACCENT ) );
			c.text ( L"OK", render::font::UI_STRONG, accent.center ( ), ink ( tone::ACCENT_INK ), render::align::CENTER );
			add_hit ( accent, tone::ACCENT );
			add_hit ( math::rect ( accent.center ( ).x - 10.0f, accent.y + 4.0f, 20.0f, 16.0f ), tone::ACCENT_INK );

			c.fill ( math::rect ( raised.x + 14.0f, raised.y + 76.0f, 232.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );
			add_hit ( math::rect ( raised.x + 14.0f, raised.y + 73.0f, 232.0f, 7.0f ), tone::LINE );

			py = raised.bottom ( ) + 28.0f;

			const wchar_t* states [ 3 ] = { L"Couldn't open the file.", L"Saved the project.", L"Still analyzing." };
			const tone state_tones [ 3 ] = { tone::DANGER, tone::SUCCESS, tone::WARNING };

			for ( int32_t i = 0; i < 3; i++ ) {

				float sy = py + static_cast < float > ( i ) * 24.0f;

				c.fill ( math::rect ( px, sy - 1.0f, 3.0f, 14.0f ), 0.0f, ink ( state_tones [ i ] ) );
				c.text ( states [ i ], render::font::UI, math::vec2 ( px + 12.0f, sy + 6.0f ), ink ( state_tones [ i ] ) );
				add_hit ( math::rect ( px, sy - 4.0f, 200.0f, 20.0f ), state_tones [ i ] );

			}

			c.text ( L"Soft text for captions and secondary labels", render::font::UI, math::vec2 ( px, py + 90.0f ), ink ( tone::INK_SOFT ) );
			c.text ( L"Faint text for hints", render::font::UI, math::vec2 ( px, py + 112.0f ), ink ( tone::INK_FAINT ) );
			add_hit ( math::rect ( px, py + 82.0f, 280.0f, 18.0f ), tone::INK_SOFT );
			add_hit ( math::rect ( px, py + 104.0f, 140.0f, 18.0f ), tone::INK_FAINT );

		}

		c.pop_clip ( );

		if ( sample_clicked ) {

			for ( int32_t i = hit_count - 1; i >= 0; i-- ) {

				if ( hit_rects [ i ].contains ( mouse ) ) {

					colors_pick = static_cast < int32_t > ( hit_tones [ i ] );

					break;

				}

			}

		}

		const tone* items = colors_tab == 0 ? listing_items : ( colors_tab == 1 ? code_items : interface_items );
		int32_t item_count = colors_tab == 0 ? static_cast < int32_t > ( sizeof ( listing_items ) / sizeof ( listing_items [ 0 ] ) ) : ( colors_tab == 1 ? static_cast < int32_t > ( sizeof ( code_items ) / sizeof ( code_items [ 0 ] ) ) : static_cast < int32_t > ( sizeof ( interface_items ) / sizeof ( interface_items [ 0 ] ) ) );
		float list_x = sample.right ( ) + 20.0f;
		float list_w = 200.0f;

		section_heading ( math::rect ( list_x, top - 4.0f, list_w, 16.0f ), L"ITEMS", nullptr );

		for ( int32_t i = 0; i < item_count; i++ ) {

			int32_t slot_index = static_cast < int32_t > ( items [ i ] );
			math::rect row ( list_x, top + 16.0f + static_cast < float > ( i ) * 24.0f, list_w, 24.0f );
			uint64_t id = make_id ( "colors_item", i );
			widget& item = state ( id );

			if ( interact ( id, row, item ) )
				colors_pick = slot_index;

			if ( slot_index == colors_pick )
				c.fill ( row, metric::radius, ink ( tone::SELECT ) );

			c.fill ( row, metric::radius, ink ( tone::HOVER ).fade ( item.hover ) );
			c.outline ( math::rect ( row.x + 6.0f, row.y + 5.0f, 14.0f, 14.0f ), 3.0f, render::rgb ( tone_hex ( items [ i ], light ) ), 1.0f, ink ( tone::LINE_STRONG ) );
			c.text ( label_of ( items [ i ] ), render::font::UI, math::vec2 ( row.x + 28.0f, row.center ( ).y ), ink ( tone::INK ) );

			if ( custom_set [ theme ] [ slot_index ] )
				c.fill ( math::rect ( row.right ( ) - 12.0f, row.center ( ).y - 2.0f, 4.0f, 4.0f ), 2.0f, ink ( tone::ACCENT ) );

		}

		float pick_x = list_x + list_w + 20.0f;
		float pick_w = box.right ( ) - 28.0f - pick_x;

		if ( colors_pick >= 0 && colors_pick < static_cast < int32_t > ( tone::COUNT ) ) {

			tone picked = static_cast < tone > ( colors_pick );
			uint32_t current = tone_hex ( picked, light );

			if ( colors_picker_for != colors_pick || colors_picker_hex != current ) {

				base::rgb_to_hsv ( current, colors_hue, colors_saturation, colors_value );
				colors_picker_for = colors_pick;
				colors_picker_hex = current;

				if ( focus_id != make_id ( "colors_hex" ) ) {

					put_hex ( colors_hex, current );
					colors_hex_seen = colors_hex.revision;

				}

			}

			c.text ( label_of ( picked ), render::font::UI_STRONG, math::vec2 ( pick_x, top + 4.0f ), ink ( tone::INK ) );

			float square = base::min ( pick_w - 26.0f, 170.0f );
			math::rect field_area ( pick_x, top + 20.0f, square, square );
			math::rect hue_area ( field_area.right ( ) + 10.0f, field_area.y, 16.0f, square );
			c.gradient ( field_area, 0.0f, math::vec2 ( field_area.x, field_area.y ), math::vec2 ( field_area.right ( ), field_area.y ), render::rgb ( 0xFFFFFF ), render::rgb ( base::hsv_to_rgb ( colors_hue, 1.0f, 1.0f ) ) );
			c.gradient ( field_area, 0.0f, math::vec2 ( field_area.x, field_area.y ), math::vec2 ( field_area.x, field_area.bottom ( ) ), render::rgb ( 0x000000, 0.0f ), render::rgb ( 0x000000 ) );

			for ( int32_t k = 0; k < 6; k++ ) {

				float segment = square / 6.0f;
				float top_y = hue_area.y + static_cast < float > ( k ) * segment;

				c.gradient ( math::rect ( hue_area.x, top_y, hue_area.w, segment + ( k < 5 ? 0.5f : 0.0f ) ), 0.0f, math::vec2 ( hue_area.x, top_y ), math::vec2 ( hue_area.x, top_y + segment ), render::rgb ( base::hsv_to_rgb ( static_cast < float > ( k ) / 6.0f, 1.0f, 1.0f ) ), render::rgb ( base::hsv_to_rgb ( static_cast < float > ( k + 1 ) / 6.0f, 1.0f, 1.0f ) ) );

			}

			math::vec2 marker ( field_area.x + colors_saturation * square, field_area.y + ( 1.0f - colors_value ) * square );

			c.outline ( math::rect ( marker.x - 5.0f, marker.y - 5.0f, 10.0f, 10.0f ), 5.0f, render::rgb ( current ), 2.0f, colors_value > 0.5f ? render::rgb ( 0x000000 ) : render::rgb ( 0xFFFFFF ) );
			c.outline ( math::rect ( hue_area.x - 2.0f, hue_area.y + colors_hue * square - 2.0f, hue_area.w + 4.0f, 4.0f ), 1.0f, render::rgb ( 0xFFFFFF ), 1.0f, render::rgb ( 0x000000 ) );

			uint64_t field_id = make_id ( "colors_field" );
			uint64_t hue_id = make_id ( "colors_hue" );
			widget& field_state = state ( field_id );
			widget& hue_state = state ( hue_id );

			interact ( field_id, field_area, field_state );
			interact ( hue_id, math::rect ( hue_area.x - 4.0f, hue_area.y, hue_area.w + 8.0f, hue_area.h ), hue_state );

			if ( active_id == field_id && mouse_down ) {

				colors_saturation = math::saturate ( ( mouse.x - field_area.x ) / square );
				colors_value = 1.0f - math::saturate ( ( mouse.y - field_area.y ) / square );
				set_custom_color ( colors_pick, base::hsv_to_rgb ( colors_hue, colors_saturation, colors_value ) );
				put_hex ( colors_hex, colors_picker_hex );
				colors_hex_seen = colors_hex.revision;

			}

			if ( active_id == hue_id && mouse_down ) {

				colors_hue = math::saturate ( ( mouse.y - hue_area.y ) / square );

				if ( colors_hue >= 1.0f )
					colors_hue = 0.999f;

				set_custom_color ( colors_pick, base::hsv_to_rgb ( colors_hue, colors_saturation, colors_value ) );
				put_hex ( colors_hex, colors_picker_hex );
				colors_hex_seen = colors_hex.revision;

			}

			float below = field_area.bottom ( ) + 14.0f;

			c.text ( L"Hex", render::font::UI, math::vec2 ( pick_x, below + 13.0f ), ink ( tone::INK_SOFT ) );
			text_field ( make_id ( "colors_hex" ), colors_hex, math::rect ( pick_x + 36.0f, below, 90.0f, 26.0f ), L"RRGGBB", 0, render::font::MONO );
			c.outline ( math::rect ( pick_x + 134.0f, below, 26.0f, 26.0f ), metric::radius, render::rgb ( current ), 1.0f, ink ( tone::LINE_STRONG ) );

			if ( colors_hex.revision != colors_hex_seen ) {

				colors_hex_seen = colors_hex.revision;

				uint64_t value = 0;

				if ( colors_hex.length == 6 && base::parse_hex ( colors_hex.text, value ) ) {

					set_custom_color ( colors_pick, static_cast < uint32_t > ( value ) );
					base::rgb_to_hsv ( static_cast < uint32_t > ( value ), colors_hue, colors_saturation, colors_value );

				}

			}

			bool edited = custom_set [ theme ] [ colors_pick ];
			uint32_t original = 0;

			custom_set [ theme ] [ colors_pick ] = false;
			original = tone_hex ( picked, light );
			custom_set [ theme ] [ colors_pick ] = edited;

			base::writer note;

			note.add ( L"Default " ).hex ( original, 6 );
			c.text ( note.text ( ), render::font::SMALL, math::vec2 ( pick_x, below + 46.0f ), ink ( tone::INK_FAINT ) );

			if ( edited && button ( make_id ( "colors_reset_item" ), math::rect ( pick_x, below + 62.0f, 120.0f, 26.0f ), L"Use default", 0, false ) ) {

				custom_set [ theme ] [ colors_pick ] = false;
				colors_picker_for = -1;
				pending = base::max ( pending, 2 );

			}

		}

		float buttons_y = box.bottom ( ) - 46.0f;

		c.fill ( math::rect ( box.x + 1.0f, buttons_y - 14.0f, box.w - 2.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );

		if ( button ( make_id ( "colors_reset" ), math::rect ( left, buttons_y, 150.0f, 30.0f ), light ? L"Reset light theme" : L"Reset dark theme", 0, false ) ) {

			for ( int32_t i = 0; i < static_cast < int32_t > ( tone::COUNT ); i++ )
				custom_set [ theme ] [ i ] = false;

			colors_picker_for = -1;
			pending = base::max ( pending, 2 );

		}

		float right = box.right ( ) - 28.0f;

		if ( button ( make_id ( "colors_ok" ), math::rect ( right - 96.0f, buttons_y, 96.0f, 30.0f ), L"OK", 0, true ) ) {

			close_colors ( true );
			c.pop ( );

			return;

		}

		if ( button ( make_id ( "colors_apply" ), math::rect ( right - 200.0f, buttons_y, 96.0f, 30.0f ), L"Apply", 0, false ) ) {

			for ( int32_t t = 0; t < 2; t++ ) {

				for ( int32_t i = 0; i < static_cast < int32_t > ( tone::COUNT ); i++ ) {

					colors_saved [ t ] [ i ] = custom_colors [ t ] [ i ];
					colors_saved_set [ t ] [ i ] = custom_set [ t ] [ i ];

				}

			}

			if ( persist )
				save_settings ( );

		}

		if ( button ( make_id ( "colors_cancel" ), math::rect ( right - 304.0f, buttons_y, 96.0f, 30.0f ), L"Cancel", 0, false ) ) {

			close_colors ( false );
			c.pop ( );

			return;

		}

		c.pop ( );

	}

}
