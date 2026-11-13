#include "ui/app.hh"
#include "ui/icons.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	uint64_t make_id ( const char* name, int64_t index ) {

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( ; *name; name++ ) {

			hash ^= static_cast < uint8_t > ( *name );
			hash *= 0x100000001B3ull;

		}

		hash ^= static_cast < uint64_t > ( index ) * 0x9E3779B97F4A7C15ull;
		hash *= 0x100000001B3ull;

		return hash ? hash : 1;

	}

	static const uint32_t dark_tones [ static_cast < int32_t > ( tone::COUNT ) ] = {
		0x1C1D20, 0x222327, 0x2A2C30, 0x17181A, 0x26272B, 0x2E3035, 0x3B3E44, 0xD3D5D9, 0x9A9EA5, 0x666B73,
		0x3D8FD6, 0x3D8FD6, 0xFFFFFF, 0x24405E, 0x3D8FD6, 0xFFFFFF, 0xFFFFFF, 0x000000, 0x6F757E, 0x5A5F67,
		0xC9B37A, 0x8DB4D9, 0xA9C48E, 0xD4987A, 0x8FC7BC, 0x6E8E68, 0x4A7FB3, 0x86785A, 0x6D6A8C, 0x34373C,
		0x535862, 0xD9675C, 0x6FB46C, 0xD6A84A, 0x6FA2D3, 0xC08FD0, 0x5DB3A6, 0xDDCB8E, 0xA6C6E2, 0x86C2D6,
		0xAFCB9A, 0x9EA2A8, 0xA2B8CF, 0xD79BBE, 0x4A4E55
	};

	static const uint32_t light_tones [ static_cast < int32_t > ( tone::COUNT ) ] = {
		0xFCFCFD, 0xF2F3F5, 0xFFFFFF, 0xEBECEF, 0xE8EAED, 0xDCDFE3, 0xC4C8CE, 0x1F2328, 0x4F5862, 0x878E97,
		0x1F6CB8, 0x1F6CB8, 0xFFFFFF, 0xCFE1F5, 0x1F6CB8, 0x000000, 0x000000, 0x000000, 0x6A727C, 0x8C929A,
		0x86580E, 0x1E4F8F, 0x2B7A3B, 0xA04722, 0x1E6D63, 0x5B7A4E, 0x6A8DB8, 0xAE9D74, 0x938FAE, 0xD5D8DD,
		0x9AA0A8, 0xC23B30, 0x2F8A3A, 0x9A6D08, 0x1F5FB0, 0x7C3E9E, 0x23768C, 0x6A4E12, 0x1E3F75, 0x266782,
		0x4E6B1C, 0x50565E, 0x2F4A72, 0x9A3F74, 0xC4C8CE
	};

	static const float tone_alpha [ static_cast < int32_t > ( tone::COUNT ) ] [ 2 ] = {
		{ 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f },
		{ 1.0f, 1.0f }, { 0.16f, 0.12f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.04f, 0.04f }, { 0.075f, 0.07f }, { 0.45f, 0.14f }, { 1.0f, 1.0f }, { 1.0f, 1.0f },
		{ 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f },
		{ 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f },
		{ 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }
	};

	render::color app::ink ( tone value ) const {

		return tones [ static_cast < int32_t > ( value ) ];

	}

	void app::update_theme ( ) {

		settle ( theme_mix, light ? 1.0f : 0.0f );

		float t = math::saturate ( theme_mix );

		for ( int32_t i = 0; i < static_cast < int32_t > ( tone::COUNT ); i++ ) {

			render::color dark = render::rgb ( dark_tones [ i ], tone_alpha [ i ] [ 0 ] );
			render::color bright = render::rgb ( light_tones [ i ], tone_alpha [ i ] [ 1 ] );

			tones [ i ] = render::mix ( dark, bright, t );

		}

	}

	void app::set_theme ( bool value ) {

		light = value;
		pending = base::max ( pending, 3 );

	}

	bool app::setup ( render::gpu* device, void* window_handle ) {

		gpu = device;
		window = window_handle;
		widgets = new widget [ widget_room ];
		x86::prepare ( );
		theme_mix = light ? 1.0f : 0.0f;
		update_theme ( );
		log ( log_kind::INFO, L"Plumb is ready. Open a binary with Ctrl+O or drop one on the window." );

		return true;

	}

	void app::rebuild_gpu ( render::gpu* device ) {

		gpu = device;
		pending = base::max ( pending, 3 );

	}

	void app::shutdown ( ) {

		if ( persist )
			save_settings ( );

		wait_analysis ( );

		if ( autosave )
			save_project ( );

		files::settle ( );
		file.close ( );

		if ( db ) {

			delete db;
			db = nullptr;

		}

		if ( building ) {

			delete building;
			building = nullptr;

		}

	}

	void app::resize ( int32_t width_px, int32_t height_px, float dpi_scale ) {

		width = base::max ( width_px, 1 );
		height = base::max ( height_px, 1 );
		dpi = dpi_scale;
		pending = base::max ( pending, 2 );

	}

	void app::set_maximized ( bool value ) {

		maximized = value;
		pending = base::max ( pending, 2 );

	}

	void app::set_focus ( bool value ) {

		focused = value;
		pending = base::max ( pending, 2 );

	}

	bool app::busy ( ) const {

		return animating || pending > 0 || ( analysis_thread && !analysis_done );

	}

	double app::wake_after ( ) const {

		if ( focused_field )
			return 0.5;

		if ( debug_state.state == debugger::status::RUNNING )
			return 0.1;

		if ( caret_live && focused ) {

			double since = clock - blink_from;

			if ( since > 60.0 )
				return -1.0;

			if ( since < 0.6 )
				return 0.6 - since + 0.005;

			double phase = since - 0.6;

			return 0.53 - ( phase - static_cast < double > ( static_cast < int64_t > ( phase / 0.53 ) ) * 0.53 ) + 0.005;

		}

		return -1.0;

	}

	request app::take_request ( ) {

		request value = pending_request;

		pending_request = request::NONE;

		return value;

	}

	pointer app::cursor ( ) const {

		return wanted_cursor;

	}

	void app::mouse_move ( float x, float y ) {

		mouse = math::vec2 ( x / dpi, y / dpi );
		mouse_inside = true;
		pending = base::max ( pending, 2 );

	}

	void app::mouse_button ( int32_t button, bool down ) {

		if ( button == 1 ) {

			if ( down )
				right_event = true;

			pending = base::max ( pending, 2 );

			return;

		}

		if ( button == 2 ) {

			if ( down )
				middle_event = true;

			pending = base::max ( pending, 2 );

			return;

		}

		if ( button == 3 || button == 4 ) {

			if ( down )
				run ( button == 3 ? command::BACK : command::FORWARD );

			return;

		}

		if ( down ) {

			double now_time = base::seconds ( );
			float dx = mouse.x - press_at.x;
			float dy = mouse.y - press_at.y;

			press_event = true;
			mouse_down = true;

			if ( now_time - last_press < 0.45 && dx * dx + dy * dy < 25.0f && !double_event_last )
				double_event = true;

			double_event_last = double_event;
			press_at = mouse;
			last_press = now_time;

		} else {

			release_event = true;
			mouse_down = false;

		}

		pending = base::max ( pending, 2 );

	}

	void app::mouse_leave ( ) {

		mouse_inside = false;

		if ( !mouse_down )
			mouse = math::vec2 ( -1000.0f, -1000.0f );

		pending = base::max ( pending, 2 );

	}

	void app::mouse_wheel ( float steps, bool horizontal ) {

		if ( horizontal )
			wheel_side += steps;
		else
			wheel_steps += steps;

		pending = base::max ( pending, 2 );

	}

	void app::key ( uint32_t code, bool control, bool shift, bool alt ) {

		keys.push ( code );
		key_control = control;
		key_shift = shift;
		key_alt = alt;
		caret_time = clock;
		pending = base::max ( pending, 2 );

	}

	void app::character ( wchar_t value ) {

		typed.push ( value );
		caret_time = clock;
		pending = base::max ( pending, 2 );

	}

	hit app::hit_test ( float x, float y ) const {

		math::vec2 at ( x / dpi, y / dpi );

		for ( int32_t i = hits.count ( ) - 1; i >= 0; i-- ) {

			const hit_rect& item = hits [ static_cast < size_t > ( i ) ];

			if ( item.area.contains ( at ) )
				return item.kind;

		}

		return title_area.contains ( at ) ? hit::CAPTION : hit::CLIENT;

	}

	widget& app::state ( uint64_t id ) {

		uint32_t slot = static_cast < uint32_t > ( id % static_cast < uint64_t > ( widget_room ) );

		for ( int32_t probe = 0; probe < widget_room; probe++ ) {

			widget& item = widgets [ slot ];

			if ( item.id == id ) {

				item.seen = frame_index;

				return item;

			}

			if ( item.id == 0 || item.seen + 600 < frame_index ) {

				item = widget ( );
				item.id = id;
				item.seen = frame_index;

				return item;

			}

			slot = ( slot + 1 ) % static_cast < uint32_t > ( widget_room );

		}

		return widgets [ id % static_cast < uint64_t > ( widget_room ) ];

	}

	bool app::over ( math::rect area ) const {

		return mouse_inside && area.contains ( mouse );

	}

	bool app::interact ( uint64_t id, math::rect area, widget& item, hit kind ) {

		hit_rect entry = { id, area, kind };

		next_hits.push ( entry );
		settle ( item.hover, hot == id ? 1.0f : 0.0f );
		settle ( item.press, active_id == id && mouse_down && hot == id ? 1.0f : 0.0f );

		return clicked_id == id;

	}

	void app::scroll_region ( uint64_t id, math::rect area ) {

		hit_rect entry = { id, area, hit::CLIENT };

		next_scrolls.push ( entry );

	}

	float app::take_wheel ( uint64_t id ) {

		if ( scroll_hot != id || wheel_steps == 0.0f )
			return 0.0f;

		float value = wheel_steps;

		wheel_steps = 0.0f;

		return value;

	}

	void app::tip ( uint64_t id, const wchar_t* text, math::rect anchor ) {

		if ( hot != id || !text || mouse_down )
			return;

		tip_candidate = id;
		tip_candidate_text = text;
		tip_candidate_anchor = anchor;

	}

	void app::settle ( float& value, float target ) {

		value = target;

	}

	void app::glide ( scroller& item, float limit, float unit ) {

		float pixels = base::max ( unit * dpi, 0.001f );

		item.limit = base::max ( limit, 0.0f );
		item.target = base::clamp ( item.target, 0.0f, item.limit );
		item.offset = item.target;
		item.shown = math::round ( item.offset * pixels ) / pixels;

	}

	bool app::begin_frame ( double seconds ) {

		if ( last < 0.0 )
			last = seconds;

		dt = fixed_step ? 1.0f / 60.0f : static_cast < float > ( base::clamp ( seconds - last, 0.0, 0.05 ) );
		last = seconds;
		clock = seconds;
		animating = false;
		wanted_cursor = pointer::ARROW;
		w = static_cast < float > ( width ) / dpi;
		h = static_cast < float > ( height ) / dpi;
		frame_index++;

		return true;

	}

	void app::begin_input ( ) {

		hot = 0;
		scroll_hot = 0;

		if ( mouse_inside || mouse_down ) {

			for ( int32_t i = hits.count ( ) - 1; i >= 0; i-- ) {

				if ( hits [ static_cast < size_t > ( i ) ].area.contains ( mouse ) ) {

					hot = hits [ static_cast < size_t > ( i ) ].id;

					break;

				}

			}

			for ( int32_t i = scrolls.count ( ) - 1; i >= 0; i-- ) {

				if ( scrolls [ static_cast < size_t > ( i ) ].area.contains ( mouse ) ) {

					scroll_hot = scrolls [ static_cast < size_t > ( i ) ].id;

					break;

				}

			}

		}

		clicked_id = 0;
		double_id = 0;
		right_id = 0;
		middle_id = 0;

		if ( press_event ) {

			active_id = hot;

			if ( double_event )
				double_id = hot;

			if ( focus_id && hot != focus_id && !palette_open && !prompt_open && !signature_open ) {

				focus_id = 0;
				focused_field = nullptr;

			}

		}

		if ( right_event )
			right_id = hot;

		if ( middle_event )
			middle_id = hot;

		if ( release_event && active_id && active_id == hot )
			clicked_id = active_id;

	}

	void app::end_input ( ) {

		if ( release_event ) {

			active_id = 0;
			dragging_split = 0;
			now->hex_selecting = false;

			for ( document& item : documents )
				item.state.hex_selecting = false;

		}

		press_event = false;
		release_event = false;
		right_event = false;
		double_event = false;
		middle_event = false;
		wheel_steps = 0.0f;
		wheel_side = 0.0f;
		keys.clear ( );
		typed.clear ( );
		hits = base::move ( next_hits );
		next_hits.clear ( );
		scrolls = base::move ( next_scrolls );
		next_scrolls.clear ( );

		if ( tip_candidate && tip_candidate == tip_id )
			tip_time += dt;
		else {

			tip_time = 0.0f;
			tip_id = tip_candidate;

		}

		if ( tip_candidate ) {

			tip_text = tip_candidate_text;
			tip_anchor = tip_candidate_anchor;

			if ( tip_time < 0.6f )
				animating = true;

		}

		tip_candidate = 0;

		bool shown_peek = peek_target && peek_time >= 0.45f;

		if ( peek_candidate && peek_candidate == peek_target ) {

			peek_time += dt;
			peek_anchor = peek_candidate_anchor;

			if ( peek_time < 0.45f )
				animating = true;

		} else if ( peek_candidate && !( shown_peek && over ( peek_box ) ) ) {

			peek_target = peek_candidate;
			peek_anchor = peek_candidate_anchor;
			peek_time = 0.0f;
			animating = true;

		} else if ( !peek_candidate && !( shown_peek && over ( peek_box ) ) ) {

			peek_target = 0;
			peek_time = 0.0f;

		}

		peek_candidate = 0;

	}

	void app::log ( log_kind kind, const wchar_t* text ) {

		log_line& line = output.emplace ( );

		line.kind = kind;
		line.text.set ( text );
		line.time = base::seconds ( );

		if ( output.count ( ) > 2000 )
			output.remove_at ( 0 );

		output_scroll.target = 1e9f;
		pending = base::max ( pending, 2 );

	}

	void app::copy_text ( const wchar_t* text ) {

		if ( !text || !OpenClipboard ( static_cast < HWND > ( window ) ) )
			return;

		EmptyClipboard ( );

		size_t length = static_cast < size_t > ( base::text_length ( text ) ) + 1;
		HGLOBAL block = GlobalAlloc ( GMEM_MOVEABLE, length * sizeof ( wchar_t ) );

		if ( block ) {

			wchar_t* target = static_cast < wchar_t* > ( GlobalLock ( block ) );

			for ( size_t i = 0; i < length; i++ )
				target [ i ] = text [ i ];

			GlobalUnlock ( block );

			if ( !SetClipboardData ( CF_UNICODETEXT, block ) )
				GlobalFree ( block );

		}

		CloseClipboard ( );

	}

	void app::layout ( ) {

		bool has = file.loaded ( );
		float top = metric::title;
		float bottom = h - metric::status;

		title_area = math::rect ( 0.0f, 0.0f, w, top );
		status_area = math::rect ( 0.0f, bottom, w, metric::status );
		settle ( welcome_show, has ? 0.0f : 1.0f );
		settle ( browser_show, browser_open && has ? 1.0f : 0.0f );
		settle ( output_show, output_open && has ? 1.0f : 0.0f );

		float file_show = 1.0f - welcome_show;
		float rail = metric::rail * file_show;

		float tall = bottom - top;

		dock_size [ 0 ] = base::clamp ( dock_size [ 0 ], 200.0f, base::max ( w * 0.45f, 200.0f ) );
		dock_size [ 1 ] = base::clamp ( dock_size [ 1 ], 200.0f, base::max ( w * 0.45f, 200.0f ) );
		dock_size [ 2 ] = base::clamp ( dock_size [ 2 ], 90.0f, base::max ( tall * 0.6f, 90.0f ) );

		float shows [ 2 ] = { has ? math::saturate ( browser_show ) : 0.0f, has ? math::saturate ( output_show ) : 0.0f };
		float side_show [ 3 ] = { 0.0f, 0.0f, 0.0f };
		float side_fit [ 3 ] = { dock_size [ 0 ], dock_size [ 1 ], dock_size [ 2 ] };

		for ( int32_t p = 0; p < 2; p++ )
			side_show [ static_cast < int32_t > ( docks [ p ] ) ] = base::max ( side_show [ static_cast < int32_t > ( docks [ p ] ) ], shows [ p ] );

		float excess = ( side_show [ 0 ] > 0.0f ? side_fit [ 0 ] : 0.0f ) + ( side_show [ 1 ] > 0.0f ? side_fit [ 1 ] : 0.0f ) + 420.0f - ( w - rail );

		if ( excess > 0.0f && side_show [ 1 ] > 0.0f ) {

			float cut = base::min ( excess, base::max ( side_fit [ 1 ] - 210.0f, 0.0f ) );

			side_fit [ 1 ] -= cut;
			excess -= cut;

		}

		if ( excess > 0.0f && side_show [ 0 ] > 0.0f ) {

			float cut = base::min ( excess, base::max ( side_fit [ 0 ] - 210.0f, 0.0f ) );

			side_fit [ 0 ] -= cut;
			excess -= cut;

		}

		float left = side_fit [ 0 ] * side_show [ 0 ];
		float right = excess > 0.0f ? 0.0f : side_fit [ 1 ] * side_show [ 1 ];
		float lower = side_fit [ 2 ] * side_show [ 2 ];

		rail_area = math::rect ( 0.0f, top, rail, tall );
		dock_areas [ 0 ] = math::rect ( rail, top, left, tall );
		dock_areas [ 1 ] = math::rect ( w - right, top, right, tall );
		center_area = math::rect ( dock_areas [ 0 ].right ( ), top, dock_areas [ 1 ].x - dock_areas [ 0 ].right ( ), tall );
		dock_areas [ 2 ] = math::rect ( center_area.x, bottom - lower, center_area.w, lower );
		view_area = math::rect ( center_area.x, top, center_area.w, dock_areas [ 2 ].y - top );

		for ( int32_t s = 0; s < 3; s++ )
			dock_shows [ s ] = side_show [ s ];

		math::rect* targets [ 2 ] = { &browser_area, &output_area };

		for ( int32_t s = 0; s < 3; s++ ) {

			float weight = 0.0f;
			float at = 0.0f;
			const math::rect& side = dock_areas [ s ];

			for ( int32_t p = 0; p < 2; p++ ) {

				if ( static_cast < int32_t > ( docks [ p ] ) == s )
					weight += shows [ p ];

			}

			for ( int32_t p = 0; p < 2; p++ ) {

				if ( static_cast < int32_t > ( docks [ p ] ) != s )
					continue;

				float share = weight > 0.0f ? shows [ p ] / weight : 0.0f;

				if ( s == 2 ) {

					*targets [ p ] = math::rect ( side.x + at, side.y, side.w * share, side.h );
					at += side.w * share;

				} else {

					*targets [ p ] = math::rect ( side.x, side.y + at, side.w, side.h * share );
					at += side.h * share;

				}

				if ( shows [ p ] <= 0.0f )
					*targets [ p ] = math::rect ( side.x, side.y, 0.0f, 0.0f );

			}

		}

		browser_fit = docks [ 0 ] == dock_side::BOTTOM ? browser_area.w : side_fit [ static_cast < int32_t > ( docks [ 0 ] ) ];

		float strip = has ? metric::strip : 0.0f;

		if ( pane_count == 2 ) {

			pane_split = base::clamp ( pane_split, 0.2f, 0.8f );

			float left = base::max ( view_area.w * pane_split, 200.0f );

			panes [ 0 ].area = math::rect ( view_area.x, view_area.y, left - 1.0f, view_area.h );
			panes [ 1 ].area = math::rect ( view_area.x + left, view_area.y, view_area.w - left, view_area.h );

		} else
			panes [ 0 ].area = view_area;

		auto snap = [ & ] ( math::rect value ) {

			float left = math::round ( value.x * dpi ) / dpi;
			float top_edge = math::round ( value.y * dpi ) / dpi;
			float right = math::round ( value.right ( ) * dpi ) / dpi;
			float bottom_edge = math::round ( value.bottom ( ) * dpi ) / dpi;

			return math::rect ( left, top_edge, right - left, bottom_edge - top_edge );

		};

		browser_area = snap ( browser_area );
		center_area = snap ( center_area );

		for ( int32_t s = 0; s < 3; s++ )
			dock_areas [ s ] = snap ( dock_areas [ s ] );
		output_area = snap ( output_area );
		view_area = snap ( view_area );

		for ( int32_t i = 0; i < pane_count; i++ ) {

			pane& item = panes [ i ];

			item.area = snap ( item.area );
			item.strip = snap ( math::rect ( item.area.x, item.area.y, item.area.w, strip ) );
			item.body = snap ( math::rect ( item.area.x, item.area.y + strip, item.area.w, item.area.h - strip ) );

		}

	}

	void app::frame ( double seconds ) {

		begin_frame ( seconds );
		begin_input ( );
		caret_live = false;

		if ( analysis_done && ( !analysis_applied || building ) )
			analysis_finished ( );

		poll_debugger ( );

		select_state ( current_document ( ) );
		handle_keys ( );
		select_state ( current_document ( ) );

		if ( browser_open && browsing == panel::REFERENCES && file.loaded ( ) )
			refresh_references ( );
		update_theme ( );
		layout ( );
		canvas.begin ( gpu, dpi, math::vec2 ( 0.0f, 0.0f ), static_cast < float > ( clock ) );
		canvas.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::BASE ) );

		if ( welcome_show > 0.01f )
			draw_welcome ( math::rect ( 0.0f, title_area.bottom ( ), w, status_area.y - title_area.bottom ( ) ) );

		if ( file.loaded ( ) ) {

			canvas.push_alpha ( 1.0f - welcome_show );
			draw_panes ( );
			dock_handles ( );
			draw_output ( );
			draw_browser ( );
			draw_docks ( );
			draw_rail ( );
			draw_peeks ( );
			canvas.pop ( );

		}

		draw_status ( );
		draw_title ( );
		draw_menu ( );
		draw_palette ( );
		draw_dialogs ( );
		draw_prompt ( );
		draw_signature ( );
		draw_tip ( );

		if ( focus_id && !focus_drawn && !palette_open && !prompt_open && !signature_open ) {

			focus_id = 0;
			focused_field = nullptr;

		}

		focus_drawn = false;

		if ( focused_field && clock - caret_time < 30.0 )
			pending = base::max ( pending, 1 );

		end_input ( );
		gpu->next_frame ( );
		gpu->prepare ( width, height );
		gpu->draw ( canvas, gpu->chrome, nullptr, true, ink ( tone::BASE ) );

		if ( pending > 0 )
			pending--;

	}

}