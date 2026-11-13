#include "ui/app.hh"
#include "ui/icons.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	static bool read_clipboard ( void* window, wchar_t* out, int32_t room ) {

		out [ 0 ] = 0;

		if ( !IsClipboardFormatAvailable ( CF_UNICODETEXT ) || !OpenClipboard ( static_cast < HWND > ( window ) ) )
			return false;

		HANDLE block = GetClipboardData ( CF_UNICODETEXT );
		bool ok = false;

		if ( block ) {

			const wchar_t* source = static_cast < const wchar_t* > ( GlobalLock ( block ) );

			if ( source ) {

				int32_t i = 0;

				for ( ; source [ i ] && i < room - 1; i++ )
					out [ i ] = source [ i ] == L'\r' || source [ i ] == L'\n' || source [ i ] == L'\t' ? L' ' : source [ i ];

				out [ i ] = 0;
				ok = true;
				GlobalUnlock ( block );

			}

		}

		CloseClipboard ( );

		return ok;

	}

	static bool word_char ( wchar_t value ) {

		return ( value >= L'0' && value <= L'9' ) || ( value >= L'a' && value <= L'z' ) || ( value >= L'A' && value <= L'Z' ) || value == L'_';

	}

	static void field_erase ( field& value, int32_t from, int32_t to ) {

		if ( from > to ) {

			int32_t held = from;

			from = to;
			to = held;

		}

		from = base::clamp ( from, 0, value.length );
		to = base::clamp ( to, 0, value.length );

		for ( int32_t i = to; i <= value.length; i++ )
			value.text [ from + i - to ] = value.text [ i ];

		value.length -= to - from;
		value.caret = from;
		value.anchor = from;
		value.revision++;

	}

	static void field_insert ( field& value, const wchar_t* text ) {

		if ( value.caret != value.anchor )
			field_erase ( value, value.caret, value.anchor );

		int32_t count = base::text_length ( text );

		count = base::min ( count, 254 - value.length );

		if ( count <= 0 )
			return;

		for ( int32_t i = value.length; i >= value.caret; i-- )
			value.text [ i + count ] = value.text [ i ];

		for ( int32_t i = 0; i < count; i++ )
			value.text [ value.caret + i ] = text [ i ];

		value.length += count;
		value.caret += count;
		value.anchor = value.caret;
		value.revision++;

	}

	void app::focus_field ( uint64_t id, field& value, bool select ) {

		focus_id = id;
		focused_field = &value;
		caret_time = clock;
		value.caret = value.length;
		value.anchor = select ? 0 : value.length;
		pending = base::max ( pending, 2 );

	}

	bool app::edit_keys ( field& value, uint32_t code ) {

		int32_t low = base::min ( value.caret, value.anchor );
		int32_t high = base::max ( value.caret, value.anchor );

		auto move_to = [ & ] ( int32_t target ) {

			value.caret = base::clamp ( target, 0, value.length );

			if ( !key_shift )
				value.anchor = value.caret;

		};

		auto word_left = [ & ] ( int32_t at ) {

			while ( at > 0 && !word_char ( value.text [ at - 1 ] ) )
				at--;

			while ( at > 0 && word_char ( value.text [ at - 1 ] ) )
				at--;

			return at;

		};

		auto word_right = [ & ] ( int32_t at ) {

			while ( at < value.length && !word_char ( value.text [ at ] ) )
				at++;

			while ( at < value.length && word_char ( value.text [ at ] ) )
				at++;

			return at;

		};

		switch ( code ) {

			case VK_LEFT:
				move_to ( !key_shift && low != high ? low : ( key_control ? word_left ( value.caret ) : value.caret - 1 ) );
				return true;

			case VK_RIGHT:
				move_to ( !key_shift && low != high ? high : ( key_control ? word_right ( value.caret ) : value.caret + 1 ) );
				return true;

			case VK_HOME:
				move_to ( 0 );
				return true;

			case VK_END:
				move_to ( value.length );
				return true;

			case VK_BACK:

				if ( low != high )
					field_erase ( value, low, high );
				else if ( value.caret > 0 )
					field_erase ( value, key_control ? word_left ( value.caret ) : value.caret - 1, value.caret );

				return true;

			case VK_DELETE:

				if ( low != high )
					field_erase ( value, low, high );
				else if ( value.caret < value.length )
					field_erase ( value, value.caret, key_control ? word_right ( value.caret ) : value.caret + 1 );

				return true;

			case 'A':

				if ( !key_control )
					return false;

				value.anchor = 0;
				value.caret = value.length;

				return true;

			case 'C':
			case 'X':

				if ( !key_control )
					return false;

				if ( low != high ) {

					wchar_t piece [ 256 ];
					int32_t count = high - low;

					for ( int32_t i = 0; i < count; i++ )
						piece [ i ] = value.text [ low + i ];

					piece [ count ] = 0;
					copy_text ( piece );

					if ( code == 'X' )
						field_erase ( value, low, high );

				}

				return true;

			case 'V': {

				if ( !key_control )
					return false;

				wchar_t clip [ 256 ];

				if ( read_clipboard ( window, clip, 256 ) )
					field_insert ( value, clip );

				return true;

			}

			case VK_RETURN:
				field_submit = true;
				return true;

			case VK_ESCAPE:

				if ( palette_open || prompt_open )
					return false;

				focus_id = 0;
				focused_field = nullptr;

				return true;

			default:
				return false;

		}

	}

	void app::handle_keys ( ) {

		field_submit = false;

		if ( focused_field ) {

			for ( wchar_t value : typed ) {

				if ( value < 32 || value == 127 )
					continue;

				wchar_t single [ 2 ] = { value, 0 };

				field_insert ( *focused_field, single );

			}

		}

		typed.clear ( );

		for ( uint32_t code : keys ) {

			if ( focused_field && edit_keys ( *focused_field, code ) )
				continue;

			if ( code == VK_F9 || code == VK_F7 || code == VK_F8 || code == VK_F12 || code == VK_F2 ) {

				if ( code == VK_F9 )
					run ( key_shift && !debugger::active ( ) ? command::DEBUG_COVERAGE : command::DEBUG_RUN );
				else if ( code == VK_F7 )
					run ( command::DEBUG_STEP_INTO );
				else if ( code == VK_F8 )
					run ( command::DEBUG_STEP_OVER );
				else if ( code == VK_F12 )
					run ( command::DEBUG_PAUSE );
				else
					run ( key_control ? command::DEBUG_STOP : ( key_shift ? command::DEBUG_CONDITION : command::DEBUG_BREAKPOINT ) );

				continue;

			}

			if ( code == VK_ESCAPE ) {

				if ( menu_open >= 0 )
					menu_open = -1;
				else if ( palette_open ) {

					palette_open = false;
					focus_id = 0;
					focused_field = nullptr;

				} else if ( prompt_open ) {

					prompt_open = false;
					focus_id = 0;
					focused_field = nullptr;

				} else if ( signature_open )
					signature_open = false;
				else if ( about_open || keys_open ) {

					about_open = false;
					keys_open = false;

				} else if ( focused_field ) {

					focus_id = 0;
					focused_field = nullptr;

				} else
					run ( command::BACK );

				continue;

			}

			if ( palette_open ) {

				if ( code == VK_DOWN )
					palette_pick++;
				else if ( code == VK_UP )
					palette_pick--;

				continue;

			}

			if ( prompt_open || signature_open )
				continue;

			if ( key_control && key_alt ) {

				if ( code == 'S' )
					run ( command::MAKE_SIGNATURE );
				else if ( code == 'F' )
					run ( command::FIND_SIGNATURE );
				else if ( code == 'A' )
					run ( command::ASSEMBLE );
				else if ( code == 'B' )
					run ( command::PATCH_BYTES );
				else if ( code == 'R' )
					run ( command::REVERT_PATCH );
				else if ( code == 'X' )
					run ( command::XOR_DECODE );

				continue;

			}

			if ( key_control && key_shift ) {

				if ( code == 'P' )
					run ( command::PALETTE );
				else if ( code == 'L' )
					run ( command::THEME );
				else if ( code == 'W' )
					run ( command::CLOSE );
				else if ( code == 'C' )
					run ( command::COPY_FUNCTION );
				else if ( code == 'F' )
					run ( command::SEARCH_ALL );
				else if ( code == VK_TAB )
					run ( command::NEXT_TAB );

				continue;

			}

			if ( key_alt ) {

				if ( code == VK_LEFT )
					run ( command::BACK );
				else if ( code == VK_RIGHT )
					run ( command::FORWARD );
				else if ( code == '0' )
					run ( command::SHOW_NOTES );
				else if ( code >= '1' && code <= '9' )
					run ( static_cast < command > ( static_cast < int32_t > ( command::SHOW_FUNCTIONS ) + static_cast < int32_t > ( code - '1' ) ) );

				continue;

			}

			if ( key_control ) {

				switch ( code ) {

					case 'O':
						run ( command::OPEN );
						break;

					case 'S':
						run ( command::SAVE_PROJECT );
						break;

					case 'Z':
						run ( command::UNDO );
						break;

					case 'Y':
						run ( command::REDO );
						break;

					case 'W':
						run ( command::CLOSE_TAB );
						break;

					case 'C':
						run ( command::COPY );
						break;

					case 'E':
						run ( command::GO_ENTRY );
						break;

					case 'F':
						run ( command::FIND );
						break;

					case 'G':
						run ( command::GO_ADDRESS );
						break;

					case 'P':
						run ( command::PALETTE );
						break;

					case 'B':
						run ( command::TOGGLE_BROWSER );
						break;

					case 'I':
						run ( command::TOGGLE_INSPECTOR );
						break;

					case 'J':
						run ( command::TOGGLE_OUTPUT );
						break;

					case 'D':
						run ( command::BOOKMARK );
						break;

					case '1':
						run ( command::VIEW_LISTING );
						break;

					case '2':
						run ( command::VIEW_HEX );
						break;

					case '3':
						run ( command::VIEW_HEADERS );
						break;

					case '4':
						run ( command::VIEW_CALLS );
						break;

					case VK_OEM_5:
						run ( command::SPLIT );
						break;

					case VK_TAB:
						run ( command::NEXT_TAB );
						break;

					case VK_NEXT:
						run ( command::NEXT_SECTION );
						break;

					case VK_PRIOR:
						run ( command::PREVIOUS_SECTION );
						break;

					case VK_HOME:

						if ( file.loaded ( ) && showing == view::LISTING )
							set_cursor_row ( 0, false );

						break;

					case VK_END:

						if ( total_rows && showing == view::LISTING )
							set_cursor_row ( total_rows - 1, false );

						break;

					default:
						break;

				}

				continue;

			}

			if ( code == VK_F5 ) {

				run ( command::VIEW_PSEUDOCODE );

				continue;

			}

			if ( code == VK_F1 ) {

				run ( command::SHORTCUTS );

				continue;

			}

			if ( focused_field )
				continue;

			switch ( code ) {

				case 'G':
					run ( command::GO_ADDRESS );
					continue;

				case 'N':
					run ( key_shift ? command::NOTE : command::RENAME );
					continue;

				case 'H':
					run ( key_shift ? command::NUMBER_RESET : command::NUMBER_RADIX );
					continue;

				case 'R':
					run ( command::NUMBER_CHAR );
					continue;

				case 'Y':
					run ( command::SET_TYPE );
					continue;

				case 'S':

					if ( showing == view::PSEUDOCODE )
						run ( command::STRUCT_FROM_VAR );

					continue;

				case 'B':
					run ( command::NUMBER_BINARY );
					continue;

				case 'F':
					run ( command::NUMBER_FLOAT );
					continue;

				case 'X':
					run ( command::SHOW_REFERENCES );
					continue;

				case VK_OEM_1:
					run ( command::COMMENT );
					continue;

				case VK_TAB:
					run ( command::SWAP_VIEW );
					continue;

				case VK_SPACE:

					if ( showing == view::LISTING || showing == view::GRAPH )
						run ( command::TOGGLE_GRAPH );

					continue;

				case VK_RETURN:

					if ( showing == view::CALLS ) {

						run ( command::FOLLOW );

						continue;

					}

					break;

				case VK_BACK:
					run ( command::BACK );
					continue;

				default:
					break;

			}

			if ( !file.loaded ( ) )
				continue;

			if ( showing == view::LISTING ) {

				if ( code == VK_RETURN ) {

					run ( command::FOLLOW );

					continue;

				}

				int64_t page = static_cast < int64_t > ( panes [ focused_pane ].body.h / metric::code_row ) - 2;
				int64_t move = 0;

				if ( code == VK_LEFT || code == VK_RIGHT ) {

					row_line line;

					compose_row ( now->cursor_row, listing_cols, line );
					now->caret_col = step_caret ( line.text, line.length, base::min ( now->caret_col, line.length ), code == VK_RIGHT, key_control );
					blink_from = clock;

					if ( key_shift )
						now->ranged = true;
					else {

						now->anchor_col = now->caret_col;
						now->ranged = false;

					}

					continue;

				}

				if ( code == VK_DOWN )
					move = 1;
				else if ( code == VK_UP )
					move = -1;
				else if ( code == VK_NEXT )
					move = page;
				else if ( code == VK_PRIOR )
					move = -page;

				if ( move ) {

					int64_t target = static_cast < int64_t > ( now->cursor_row ) + move;

					set_cursor_row ( static_cast < uint64_t > ( base::clamp < int64_t > ( target, 0, static_cast < int64_t > ( total_rows ) - 1 ) ), false );
					blink_from = clock;

					if ( key_shift )
						now->ranged = true;
					else {

						now->select_anchor = now->cursor_row;
						now->anchor_col = now->caret_col;
						now->ranged = false;

					}

				}

			} else if ( showing == view::PSEUDOCODE ) {

				const decompiler::result* code_view = pseudocode_for ( now->function );

				if ( !code_view )
					continue;

				int32_t count = code_view->lines.count ( );
				int32_t page = static_cast < int32_t > ( panes [ focused_pane ].body.h / metric::code_row ) - 2;
				int32_t move = 0;

				if ( ( code == VK_LEFT || code == VK_RIGHT ) && count ) {

					const decompiler::line& item = code_view->lines [ static_cast < size_t > ( base::clamp ( now->code_line, 0, count - 1 ) ) ];
					wchar_t text [ 1024 ];
					int32_t length = 0;

					for ( int32_t k = 0; k < item.indent * 4 && length < 1000; k++ )
						text [ length++ ] = L' ';

					for ( int32_t k = 0; k < item.length && length < 1000; k++ )
						text [ length++ ] = code_view->text [ static_cast < size_t > ( item.text + k ) ];

					text [ length ] = 0;
					now->code_col = step_caret ( text, length, base::min ( now->code_col, length ), code == VK_RIGHT, key_control );
					blink_from = clock;

					if ( key_shift )
						now->code_ranged = true;
					else {

						now->code_anchor_col = now->code_col;
						now->code_ranged = false;

					}

					continue;

				}

				if ( code == VK_DOWN )
					move = 1;
				else if ( code == VK_UP )
					move = -1;
				else if ( code == VK_NEXT )
					move = page;
				else if ( code == VK_PRIOR )
					move = -page;
				else if ( code == VK_RETURN && now->code_line < count ) {

					const decompiler::line& item = code_view->lines [ static_cast < size_t > ( now->code_line ) ];

					for ( int32_t i = 0; i < item.span_count; i++ ) {

						const decompiler::span& part = code_view->spans [ static_cast < size_t > ( item.first_span + i ) ];

						if ( part.target && ( part.kind == decompiler::syntax::FUNCTION || part.kind == decompiler::syntax::GLOBAL || part.kind == decompiler::syntax::STRING ) ) {

							go ( part.target, true );

							break;

						}

					}

				}

				if ( move && count ) {

					now->code_line = base::clamp ( now->code_line + move, 0, count - 1 );
					blink_from = clock;

					if ( key_shift )
						now->code_ranged = true;
					else {

						now->code_anchor = now->code_line;
						now->code_anchor_col = now->code_col;
						now->code_ranged = false;

					}

					const decompiler::line& item = code_view->lines [ static_cast < size_t > ( now->code_line ) ];

					if ( item.address ) {

						now->cursor_address = item.address;
						sync_panes ( item.address );

					}

					float visible = panes [ focused_pane ].body.h / metric::code_row - 1.0f;
					float place = static_cast < float > ( now->code_line );

					if ( place < now->code_scroll.target )
						now->code_scroll.target = place;
					else if ( place > now->code_scroll.target + visible - 1.0f )
						now->code_scroll.target = place - visible + 1.0f;

				}

			} else if ( showing == view::HEX && file.size ) {

				int64_t page = ( static_cast < int64_t > ( panes [ focused_pane ].body.h / metric::code_row ) - 3 ) * 16;
				int64_t move = 0;

				if ( code == VK_RIGHT )
					move = 1;
				else if ( code == VK_LEFT )
					move = -1;
				else if ( code == VK_DOWN )
					move = 16;
				else if ( code == VK_UP )
					move = -16;
				else if ( code == VK_NEXT )
					move = page;
				else if ( code == VK_PRIOR )
					move = -page;

				if ( move ) {

					int64_t target = base::clamp < int64_t > ( static_cast < int64_t > ( now->hex_cursor ) + move, 0, static_cast < int64_t > ( file.size ) - 1 );

					now->hex_cursor = static_cast < uint64_t > ( target );

					if ( !key_shift )
						now->hex_anchor = now->hex_cursor;

					float row = static_cast < float > ( now->hex_cursor / 16 );
					float visible = base::max ( panes [ focused_pane ].body.h / metric::code_row - 3.0f, 1.0f );

					if ( row < now->hex_scroll.target )
						now->hex_scroll.target = row;
					else if ( row > now->hex_scroll.target + visible - 1.0f )
						now->hex_scroll.target = row - visible + 1.0f;

					uint64_t address = 0;

					if ( file.address_of ( now->hex_cursor, address ) )
						now->cursor_address = address;

				}

			}

		}

		keys.clear ( );

	}

}