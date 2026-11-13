#include "ui/app.hh"
#include "ui/icons.hh"
#include <windows.h>

namespace ui {

	void app::draw_dialogs ( ) {

		settle ( about_show, about_open ? 1.0f : 0.0f );
		settle ( keys_show, keys_open ? 1.0f : 0.0f );

		render::canvas& c = canvas;
		float show = math::saturate ( base::max ( about_show, keys_show ) );

		if ( show <= 0.004f )
			return;

		uint64_t blocker = make_id ( "dialog_blocker" );
		widget& block = state ( blocker );

		if ( interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) ) {

			about_open = false;
			keys_open = false;

		}

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.45f * show ) );

		bool about = about_show >= keys_show;
		float box_w = about ? 460.0f : 700.0f;
		float box_h = about ? 260.0f : base::min ( h - 60.0f, 640.0f );
		math::rect box ( w * 0.5f - box_w * 0.5f, h * 0.5f - box_h * 0.5f, box_w, box_h );

		c.push ( box.center ( ), 1.0f, show );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "dialog_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );

		if ( icon_button ( make_id ( "dialog_close" ), math::vec2 ( box.right ( ) - 22.0f, box.y + 22.0f ), 30.0f, icons::CANCEL, 11.0f, L"Close" ) ) {

			about_open = false;
			keys_open = false;

		}

		if ( about ) {

			draw_brand ( math::vec2 ( box.x + 44.0f, box.y + 50.0f ), 40.0f );
			c.text ( L"Plumb 0.3", render::font::HEADING, math::vec2 ( box.x + 76.0f, box.y + 42.0f ), ink ( tone::INK ) );
			c.text ( L"Disassembler, decompiler and debugger for Windows executables", render::font::SMALL, math::vec2 ( box.x + 76.0f, box.y + 62.0f ), ink ( tone::INK_SOFT ) );

			const wchar_t* lines [ ] = {
				L"x86 and x86-64 decoding, including AVX-512",
				L"Functions, references, switch tables and RTTI on every core",
				L"Text, graph and C views with editable names, types and prototypes",
				L"Debugger, assembler and patching, call graph and signatures"
			};

			for ( int32_t i = 0; i < 4; i++ ) {

				float y = box.y + 104.0f + static_cast < float > ( i ) * 22.0f;

				c.fill ( math::rect ( box.x + 30.0f, y - 2.0f, 4.0f, 4.0f ), 1.0f, ink ( tone::ACCENT ) );
				c.text ( lines [ i ], render::font::UI, math::vec2 ( box.x + 44.0f, y ), ink ( tone::INK ) );

			}

			c.fill ( math::rect ( box.x + 28.0f, box.bottom ( ) - 46.0f, box.w - 56.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );
			c.text ( L"No C runtime, no dependencies. Direct3D 11 and DirectWrite. Windows 10 and 11.", render::font::SMALL, math::vec2 ( box.x + 28.0f, box.bottom ( ) - 24.0f ), ink ( tone::INK_FAINT ) );

		} else {

			c.text ( L"Keyboard shortcuts", render::font::HEADING, math::vec2 ( box.x + 28.0f, box.y + 30.0f ), ink ( tone::INK ) );

			const command order [ ] = {
				command::OPEN, command::PALETTE, command::GO_ADDRESS, command::GO_ENTRY, command::BACK, command::FORWARD, command::FOLLOW,
				command::TOGGLE_GRAPH, command::VIEW_PSEUDOCODE, command::SWAP_VIEW, command::VIEW_LISTING, command::VIEW_HEX, command::SPLIT,
				command::CLOSE_TAB, command::NEXT_TAB, command::RENAME, command::SET_TYPE, command::COMMENT, command::BOOKMARK, command::NUMBER_RADIX, command::NUMBER_CHAR, command::SHOW_REFERENCES,
				command::MAKE_SIGNATURE, command::FIND_SIGNATURE, command::SEARCH_ALL, command::ASSEMBLE, command::PATCH_BYTES, command::REVERT_PATCH, command::XOR_DECODE, command::VIEW_CALLS,
				command::COPY, command::COPY_FUNCTION, command::FIND, command::SHOW_FUNCTIONS, command::SAVE_PROJECT, command::UNDO,
				command::DEBUG_RUN, command::DEBUG_STEP_INTO, command::DEBUG_STEP_OVER, command::DEBUG_BREAKPOINT, command::DEBUG_STOP,
				command::TOGGLE_BROWSER, command::TOGGLE_INSPECTOR, command::TOGGLE_OUTPUT, command::THEME, command::CLOSE
			};
			int32_t total = static_cast < int32_t > ( sizeof ( order ) / sizeof ( order [ 0 ] ) );
			int32_t half = ( total + 1 ) / 2;
			float column = ( box.w - 56.0f - 32.0f ) * 0.5f;

			for ( int32_t i = 0; i < total; i++ ) {

				float cx = box.x + 28.0f + ( i < half ? 0.0f : column + 32.0f );
				float cy = box.y + 70.0f + static_cast < float > ( i < half ? i : i - half ) * base::min ( 27.0f, ( box.h - 96.0f ) / static_cast < float > ( half ) );

				c.text ( command_label ( order [ i ] ), render::font::UI, math::vec2 ( cx, cy ), ink ( tone::INK ), render::align::LEFT, column - 110.0f );
				key_cap ( math::vec2 ( cx + column, cy ), command_keys ( order [ i ] ), ink ( tone::INK_SOFT ), true );

			}

		}

		c.pop ( );

	}

	void app::draw_prompt ( ) {

		settle ( prompt_show, prompt_open ? 1.0f : 0.0f );

		float show = math::saturate ( prompt_show );

		if ( show <= 0.004f )
			return;

		render::canvas& c = canvas;
		uint64_t blocker = make_id ( "prompt_blocker" );
		widget& block = state ( blocker );

		if ( interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) && prompt_open ) {

			prompt_open = false;
			focus_id = 0;
			focused_field = nullptr;

		}

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.3f * show ) );

		math::rect box ( w * 0.5f - 260.0f, title_area.bottom ( ) + 70.0f, 520.0f, 150.0f );
		const wchar_t* title = L"Rename";
		base::writer detail;

		if ( prompt_action == command::COMMENT )
			title = L"Comment";
		else if ( prompt_action == command::NOTE )
			title = L"Function note";
		else if ( prompt_action == command::DEBUG_WATCH )
			title = L"Add watch";
		else if ( prompt_action == command::DEBUG_CONDITION )
			title = L"Breakpoint condition";
		else if ( prompt_action == command::FIND_SIGNATURE )
			title = L"Find signature or bytes";
		else if ( prompt_action == command::SEARCH_ALL )
			title = L"Search everything";
		else if ( prompt_action == command::ASSEMBLE )
			title = L"Assemble";
		else if ( prompt_action == command::PATCH_BYTES )
			title = L"Patch bytes";
		else if ( prompt_action == command::XOR_DECODE )
			title = L"Decode XOR bytes";
		else if ( prompt_mode == 1 )
			title = L"Rename variable";
		else if ( prompt_mode == 2 )
			title = L"Set variable type";
		else if ( prompt_mode == 3 )
			title = L"Function prototype";
		else if ( prompt_mode == 4 )
			title = L"Rename field";
		else if ( prompt_mode == 5 )
			title = L"Field type";
		else if ( prompt_mode == 6 )
			title = prompt_from_var ? L"Make struct from variable" : L"New struct";
		else if ( prompt_mode == 7 )
			title = L"Edit field";
		else if ( prompt_mode == 8 )
			title = L"Add field";
		else if ( prompt_mode == 9 )
			title = L"Rename struct";

		if ( prompt_action == command::FIND_SIGNATURE )
			detail.add ( L"48 8B 05 ? ? ? ?   48 8B 05 ?? ??   \"\\x48\\x8B\" \"xx\"   or \"text\"" );
		else if ( prompt_action == command::SEARCH_ALL )
			detail.add ( L"Text in every instruction.  #0x40 finds immediates and offsets.  c:text searches the C." );
		else if ( prompt_action == command::XOR_DECODE )
			detail.add ( L"Key bytes in hex like 5A or DE AD BE EF, then an optional decimal length, or auto." );
		else if ( prompt_mode == 1 || prompt_mode == 2 ) {

			wchar_t name [ 200 ];

			detail.add ( prompt_key.get ( ) );

			if ( symbol_name ( prompt_function, true, name, 200 ) )
				detail.add ( L" in " ).add ( name );

		} else if ( prompt_mode == 3 )
			detail.add ( L"Return type, name and parameters. Empty resets to the inferred signature." );
		else if ( prompt_mode >= 4 && prompt_mode <= 9 ) {

			if ( prompt_mode == 6 )
				detail.add ( prompt_from_var ? L"Fields come from how the variable is used. The variable becomes a pointer to it." : L"Add fields from the Structures panel, then type a variable as Name* with Y." );
			else if ( prompt_mode == 7 )
				detail.add ( L"Type and name, like  float speed,  char name[32]  or  Player* owner. Empty deletes it." );
			else if ( prompt_mode == 8 )
				detail.add ( L"Optional +offset, then type and name. Built-in types or your own structs." );
			else if ( prompt_mode == 9 )
				detail.add ( L"Empty deletes the struct." );
			else if ( prompt_struct >= 0 && prompt_struct < structs.count ( ) )
				detail.add ( structs [ static_cast < size_t > ( prompt_struct ) ].name.get ( ) ).add ( L" +0x" ).hex ( static_cast < uint64_t > ( prompt_offset ) );

		}
		else {

			wchar_t name [ 200 ];

			detail.add ( L"at " ).hex ( prompt_address );

			if ( symbol_name ( prompt_address, true, name, 200 ) )
				detail.add ( L"  \x00B7  " ).add ( name );

		}

		c.push ( box.center ( ), 1.0f, show );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "prompt_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );
		c.text ( title, render::font::UI_STRONG, math::vec2 ( box.x + 22.0f, box.y + 26.0f ), ink ( tone::INK ) );
		c.text ( detail.text ( ), render::font::MONO_SMALL, math::vec2 ( box.x + 22.0f, box.y + 48.0f ), ink ( tone::INK_FAINT ), render::align::LEFT, box.w - 44.0f );

		uint64_t field_id = make_id ( "prompt" );

		const wchar_t* hint = prompt_action == command::FIND_SIGNATURE ? L"Pattern" : ( prompt_action == command::COMMENT ? L"Comment, empty to remove" : ( prompt_action == command::NOTE ? L"What this function does, empty to remove" : L"New name, empty to reset" ) );
		bool code_font = prompt_action == command::DEBUG_WATCH || prompt_action == command::DEBUG_CONDITION || prompt_action == command::FIND_SIGNATURE || prompt_action == command::SEARCH_ALL || prompt_action == command::ASSEMBLE || prompt_action == command::PATCH_BYTES || prompt_action == command::XOR_DECODE || prompt_mode >= 2;

		if ( prompt_action == command::SEARCH_ALL )
			hint = L"call rax,  #0x40,  c:GetProcAddress";
		else if ( prompt_action == command::ASSEMBLE )
			hint = L"mov eax, 1";
		else if ( prompt_action == command::PATCH_BYTES )
			hint = L"90 90 EB 05";
		else if ( prompt_action == command::XOR_DECODE )
			hint = L"auto";
		else if ( prompt_action == command::DEBUG_WATCH )
			hint = L"rcx,  [rsp+0x28],  g_name+8,  empty clears all";
		else if ( prompt_action == command::DEBUG_CONDITION )
			hint = L"rax == 0,  empty makes it unconditional";

		if ( prompt_mode == 2 )
			hint = L"int32_t, char*, DWORD, MyStruct*  (empty resets)";
		else if ( prompt_mode == 3 )
			hint = L"int64_t name(int32_t count, char* text)";
		else if ( prompt_mode == 6 || prompt_mode == 9 )
			hint = L"Struct name";
		else if ( prompt_mode == 7 || prompt_mode == 8 )
			hint = L"+0x10 int32_t health";

		bool apply = text_field ( field_id, prompt_field, math::rect ( box.x + 18.0f, box.y + 66.0f, box.w - 36.0f, 32.0f ), hint, 0, code_font ? render::font::MONO : render::font::UI );

		if ( field_submit && focus_id == field_id )
			apply = true;

		if ( button ( make_id ( "prompt_ok" ), math::rect ( box.right ( ) - 196.0f, box.bottom ( ) - 42.0f, 86.0f, 30.0f ), prompt_action == command::FIND_SIGNATURE || prompt_action == command::SEARCH_ALL ? L"Find" : ( prompt_action == command::ASSEMBLE || prompt_action == command::PATCH_BYTES ? L"Patch" : L"Apply" ), 0, true ) )
			apply = true;

		if ( button ( make_id ( "prompt_cancel" ), math::rect ( box.right ( ) - 102.0f, box.bottom ( ) - 42.0f, 84.0f, 30.0f ), L"Cancel", 0, false ) ) {

			prompt_open = false;
			focus_id = 0;
			focused_field = nullptr;

		}

		c.pop ( );

		if ( apply && prompt_open )
			apply_prompt ( );

	}

	void app::draw_signature ( ) {

		settle ( signature_show, signature_open ? 1.0f : 0.0f );

		float show = math::saturate ( signature_show );

		if ( show <= 0.004f )
			return;

		render::canvas& c = canvas;
		uint64_t blocker = make_id ( "signature_blocker" );
		widget& block = state ( blocker );

		if ( interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) )
			signature_open = false;

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.4f * show ) );

		float box_w = base::min ( 760.0f, w - 60.0f );
		math::rect box ( w * 0.5f - box_w * 0.5f, h * 0.5f - 190.0f, box_w, 380.0f );
		const analysis::signature& sig = signature_result;
		wchar_t name [ 200 ];
		base::writer heading;
		base::writer status;

		int32_t holes = 0;

		for ( uint8_t mask : sig.bytes.mask )
			holes += mask ? 0 : 1;

		heading.add ( L"Signature for " );

		if ( symbol_name ( sig.target, true, name, 200 ) )
			heading.add ( name );
		else
			heading.hex ( sig.target );

		if ( !sig.via_reference && sig.target_offset )
			heading.add ( L" (+" ).hex ( static_cast < uint64_t > ( sig.target_offset ) ).add ( L" into the pattern)" );

		if ( signature_ok && sig.via_reference )
			status.add ( L"Unique, through the call at " ).hex ( sig.reference ).add ( L". Read the rel32 at +" ).number ( sig.target_offset ).add ( L" and add it to the end of the call." );
		else if ( signature_ok )
			status.add ( L"Unique in the code sections, " ).number ( sig.bytes.bytes.count ( ) ).add ( L" bytes, " ).number ( holes ).add ( L" wildcards" );
		else
			status.add ( L"No unique pattern within the function. The best one still matches " ).number ( sig.matches ).add ( L" places." );

		status.add ( L"   \x00B7   " ).fixed ( signature_seconds * 1000.0, 1 ).add ( L" ms" );

		c.push ( box.center ( ), 1.0f, show );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "signature_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );
		c.text ( heading.text ( ), render::font::UI_STRONG, math::vec2 ( box.x + 24.0f, box.y + 28.0f ), ink ( tone::INK ), render::align::LEFT, box.w - 80.0f );
		c.icon ( signature_ok ? icons::CHECK : icons::WARNING, math::vec2 ( box.x + 31.0f, box.y + 52.0f ), 12.0f, signature_ok ? ink ( tone::SUCCESS ) : ink ( tone::WARNING ) );
		c.text ( status.text ( ), render::font::SMALL, math::vec2 ( box.x + 46.0f, box.y + 52.0f ), ink ( tone::INK_SOFT ), render::align::LEFT, box.w - 72.0f );

		if ( icon_button ( make_id ( "signature_close" ), math::vec2 ( box.right ( ) - 22.0f, box.y + 24.0f ), 30.0f, icons::CANCEL, 11.0f, L"Close (Esc)" ) )
			signature_open = false;

		const wchar_t* labels [ 4 ] = { L"IDA", L"x64dbg, Cheat Engine", L"Code bytes and mask", L"Byte array" };
		base::list < wchar_t > text;

		for ( int32_t i = 0; i < 4; i++ ) {

			math::rect row ( box.x + 18.0f, box.y + 80.0f + static_cast < float > ( i ) * 62.0f, box.w - 36.0f, 54.0f );

			analysis::format_pattern ( sig.bytes, static_cast < analysis::pattern_style > ( i ), text );
			c.outline ( row, 4.0f, ink ( tone::SUNKEN ), 1.0f, ink ( tone::LINE ) );
			c.text ( labels [ i ], render::font::TINY, math::vec2 ( row.x + 12.0f, row.y + 14.0f ), ink ( tone::INK_FAINT ) );
			c.text ( text.data ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 12.0f, row.y + 35.0f ), ink ( tone::INK ), render::align::LEFT, row.w - 110.0f );

			bool copied = signature_copied == i && clock - signature_copied_at < 1.4;

			if ( button ( make_id ( "signature_copy", i ), math::rect ( row.right ( ) - 86.0f, row.y + 12.0f, 76.0f, 30.0f ), copied ? L"Copied" : L"Copy", copied ? icons::CHECK : 0, false ) ) {

				copy_text ( text.data ( ) );
				signature_copied = i;
				signature_copied_at = clock;

			}

			if ( copied )
				animating = true;

		}

		if ( button ( make_id ( "signature_find" ), math::rect ( box.x + 18.0f, box.bottom ( ) - 46.0f, 150.0f, 30.0f ), L"Find all matches", icons::SEARCH, false ) ) {

			analysis::format_pattern ( sig.bytes, analysis::pattern_style::IDA, text );
			signature_open = false;
			find_signature ( text.data ( ) );

		}

		c.text ( L"RIP-relative addresses, branch targets and relocations are wildcarded.", render::font::SMALL, math::vec2 ( box.x + 182.0f, box.bottom ( ) - 31.0f ), ink ( tone::INK_FAINT ), render::align::LEFT, box.w - 200.0f );
		c.pop ( );

	}

}