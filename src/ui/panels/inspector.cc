#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	static void float_text ( base::writer& out, double value ) {

		if ( value != value ) {

			out.add ( L"NaN" );

			return;

		}

		double magnitude = value < 0.0 ? -value : value;

		if ( magnitude > 1.7976931348623157e308 ) {

			out.add ( value < 0.0 ? L"\x2212\x221E" : L"\x221E" );

			return;

		}

		if ( magnitude == 0.0 || ( magnitude >= 1e-3 && magnitude < 1e7 ) ) {

			out.fixed ( value, 4 );

			return;

		}

		int32_t exponent = 0;

		while ( magnitude >= 10.0 && exponent < 400 ) {

			magnitude /= 10.0;
			exponent++;

		}

		while ( magnitude < 1.0 && exponent > -400 ) {

			magnitude *= 10.0;
			exponent--;

		}

		if ( value < 0.0 )
			out.add ( L'\x2212' );

		out.fixed ( magnitude, 4 ).add ( L'e' ).number ( exponent );

	}

	void app::draw_inspector ( math::rect area, float inner ) {

		render::canvas& c = canvas;
		float x = area.right ( ) - inner + 10.0f;
		float value_x = x + 96.0f;
		uint64_t offset = 0;
		bool mapped = file.offset_of ( now->cursor_address, offset );
		int32_t owner = file.section_at ( now->cursor_address );
		uint32_t digits = file.wide ( ) ? 16 : 8;

		if ( showing == view::HEX ) {

			offset = now->hex_cursor;
			mapped = now->hex_cursor < file.size;

		}

		uint64_t id = make_id ( "inspector" );

		scroll_region ( id, area );
		inspector_scroll.target -= take_wheel ( id ) * 60.0f;

		float y = area.y + metric::header - inspector_scroll.shown;
		float row = metric::list_row;

		auto pair = [ & ] ( const wchar_t* key, const wchar_t* value, render::font role, render::color tint ) {

			c.text ( key, render::font::SMALL, math::vec2 ( x, y + row * 0.5f ), ink ( tone::INK_SOFT ) );
			c.text ( value, role, math::vec2 ( value_x, y + row * 0.5f ), tint, render::align::LEFT, inner - 116.0f );
			y += row;

		};

		auto heading = [ & ] ( const wchar_t* title ) {

			y += 6.0f;
			section_heading ( math::rect ( x, y, inner - 20.0f, 18.0f ), title, nullptr );
			y += 20.0f;
			c.fill ( math::rect ( x, y - 2.0f, inner - 20.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );
			y += 2.0f;

		};


		if ( debug_state.state != debugger::status::IDLE ) {

			static const wchar_t* const states [ 5 ] = { L"Idle", L"Running", L"Paused", L"Exited", L"Failed to start" };
			base::writer value;

			heading ( L"DEBUGGER" );
			value.add ( states [ static_cast < int32_t > ( debug_state.state ) ] );

			if ( debug_state.state == debugger::status::PAUSED && debug_state.note [ 0 ] )
				value.add ( L"  \x00B7  " ).add ( debug_state.note );

			pair ( L"State", value.text ( ), render::font::SMALL, ink ( debug_state.state == debugger::status::PAUSED ? tone::WARNING : ( debug_state.state == debugger::status::RUNNING ? tone::SUCCESS : tone::INK_SOFT ) ) );

			if ( debug_state.state == debugger::status::PAUSED ) {

				int32_t shown = debug_state.wide ? 18 : 10;
				uint32_t width = debug_state.wide ? 16 : 8;

				for ( int32_t i = 0; i < shown; i++ ) {

					int32_t index = debug_state.wide ? i : ( i < 8 ? i : i + 8 );
					base::writer number;

					number.hex ( debug_state.registers [ index ], static_cast < int32_t > ( width ) );
					pair ( debugger::register_name ( index, debug_state.wide ), number.text ( ), render::font::MONO, ink ( index == 16 ? tone::WARNING : tone::INK ) );

				}

			}

		}
		heading ( L"LOCATION" );

		base::writer text;

		text.hex ( now->cursor_address, static_cast < int32_t > ( digits ) );
		pair ( L"Address", text.text ( ), render::font::MONO, ink ( tone::INK ) );

		if ( file.type != loader::format::RAW ) {

			text.clear ( );
			text.hex ( now->cursor_address - file.base );
			pair ( L"RVA", text.text ( ), render::font::MONO, ink ( tone::INK ) );

		}

		text.clear ( );

		if ( mapped )
			text.hex ( offset );
		else
			text.add ( L"not in the file" );

		pair ( L"File offset", text.text ( ), mapped ? render::font::MONO : render::font::SMALL, mapped ? ink ( tone::INK ) : ink ( tone::INK_FAINT ) );

		if ( owner >= 0 ) {

			const loader::section& part = file.sections [ static_cast < size_t > ( owner ) ];

			text.clear ( );
			text.add ( part.name ).add ( L"  \x00B7  " ).add ( loader::kind_name ( part.type ) );
			pair ( L"Section", text.text ( ), render::font::UI, ink ( tone::INK ) );

		}

		int32_t index = showing == view::LISTING ? item_at ( now->cursor_row ) : -1;

		if ( index >= 0 ) {

			const line_item& item = items [ static_cast < size_t > ( index ) ];
			const wchar_t* what = L"Bytes";

			switch ( item.kind ) {

				case line_kind::BANNER:
					what = L"Section start";
					break;

				case line_kind::LABEL:
					what = item.function ? L"Function entry" : L"Label";
					break;

				case line_kind::CODE:
					what = db && analysis_applied ? ( db->function_at ( now->cursor_address ) >= 0 ? L"Instruction" : L"Instruction outside functions" ) : L"Code, analysis running";
					break;

				case line_kind::FUNCTION:
					what = L"Function start";
					break;

				case line_kind::END:
					what = L"Function end";
					break;

				case line_kind::ALIGN:
					what = L"Padding";
					break;

				case line_kind::TABLE:
					what = L"Switch table";
					break;

				case line_kind::STRING:
					what = file.strings [ static_cast < size_t > ( item.ref ) ].wide ? L"UTF-16 string" : L"ASCII string";
					break;

				case line_kind::POINTER:
					what = L"Import address slot";
					break;

				case line_kind::EMPTY:
					what = L"Uninitialized space";
					break;

				default:
					break;

			}

			pair ( L"Item", what, render::font::UI, ink ( tone::INK ) );

		}

		heading ( L"BYTES" );

		float adv = c.measure ( L"0000000000", render::font::MONO ) * 0.1f;
		float ascii_x = x + adv * 24.0f + 12.0f;
		bool ascii_fits = ascii_x + adv * 8.0f < area.right ( ) - 10.0f;

		for ( int32_t line = 0; line < 2; line++ ) {

			base::writer bytes;
			wchar_t ascii [ 9 ];
			int32_t filled = 0;

			for ( int32_t i = 0; i < 8; i++ ) {

				uint64_t at = offset + static_cast < uint64_t > ( line * 8 + i );

				if ( !mapped || at >= file.size )
					break;

				uint8_t value = file.byte ( at );

				if ( i )
					bytes.add ( L' ' );

				bytes.hex ( value, 2 );
				ascii [ i ] = value >= 0x20 && value < 0x7F ? static_cast < wchar_t > ( value ) : L'.';
				filled = i + 1;

			}

			ascii [ filled ] = 0;
			c.text ( filled ? bytes.text ( ) : L"\x2014", render::font::MONO, math::vec2 ( x, y + row * 0.5f ), ink ( tone::INK ) );
			if ( ascii_fits )
				c.text ( ascii, render::font::MONO, math::vec2 ( ascii_x, y + row * 0.5f ), ink ( tone::INK_SOFT ) );
			y += row;

		}

		heading ( L"AS VALUES" );

		uint64_t raw = 0;
		int32_t available = 0;

		for ( int32_t i = 0; i < 8 && mapped && offset + static_cast < uint64_t > ( i ) < file.size; i++ ) {

			raw |= static_cast < uint64_t > ( file.byte ( offset + static_cast < uint64_t > ( i ) ) ) << ( i * 8 );
			available++;

		}

		struct value_row {

			const wchar_t* name;
			int32_t bytes;

		};

		const value_row values [ ] = { { L"int8", 1 }, { L"uint8", 1 }, { L"int16", 2 }, { L"uint16", 2 }, { L"int32", 4 }, { L"uint32", 4 }, { L"int64", 8 }, { L"uint64", 8 }, { L"float", 4 }, { L"double", 8 } };

		for ( int32_t i = 0; i < 10; i++ ) {

			base::writer value;

			if ( available < values [ i ].bytes )
				value.add ( L"\x2014" );
			else {

				switch ( i ) {

					case 0:
						value.number ( static_cast < int8_t > ( raw & 0xFF ) );
						break;

					case 1:
						value.number ( static_cast < int64_t > ( raw & 0xFF ) );
						break;

					case 2:
						value.number ( static_cast < int16_t > ( raw & 0xFFFF ) );
						break;

					case 3:
						value.number ( static_cast < int64_t > ( raw & 0xFFFF ) );
						break;

					case 4:
						value.number ( static_cast < int32_t > ( raw & 0xFFFFFFFF ) );
						break;

					case 5:
						value.number ( static_cast < int64_t > ( raw & 0xFFFFFFFF ) );
						break;

					case 6:
						value.number ( static_cast < int64_t > ( raw ) );
						break;

					case 7:
						value.add ( L"0x" ).hex ( raw, 16 );
						break;

					case 8:
						float_text ( value, static_cast < double > ( __builtin_bit_cast ( float, static_cast < uint32_t > ( raw & 0xFFFFFFFF ) ) ) );
						break;

					default:
						float_text ( value, __builtin_bit_cast ( double, raw ) );
						break;

				}

			}

			pair ( values [ i ].name, value.text ( ), render::font::MONO, available < values [ i ].bytes ? ink ( tone::INK_FAINT ) : ink ( tone::NUMBER ) );

		}

		heading ( L"IMAGE" );
		pair ( L"Format", file.format_name ( ), render::font::UI, ink ( tone::INK ) );
		pair ( L"Machine", file.machine_name ( ), render::font::UI, ink ( tone::INK ) );

		if ( file.type != loader::format::RAW ) {

			text.clear ( );
			text.hex ( file.base, static_cast < int32_t > ( digits ) );
			pair ( L"Image base", text.text ( ), render::font::MONO, ink ( tone::INK ) );
			text.clear ( );

			if ( file.entry )
				text.hex ( file.entry, static_cast < int32_t > ( digits ) );
			else
				text.add ( L"none" );

			pair ( L"Entry point", text.text ( ), render::font::MONO, ink ( tone::INK ) );
			pair ( L"Kind", file.dll ? L"Dynamic-link library" : L"Executable", render::font::UI, ink ( tone::INK ) );

		}

		text.clear ( );
		text.grouped ( file.size ).add ( L" bytes" );
		pair ( L"Size", text.text ( ), render::font::UI, ink ( tone::INK ) );
		text.clear ( );
		text.number ( file.sections.count ( ) ).add ( L" sections, " ).number ( file.modules.count ( ) ).add ( L" modules" );
		pair ( L"Layout", text.text ( ), render::font::UI, ink ( tone::INK ) );

		if ( !file.debug_path.empty ( ) ) {

			c.text ( L"PDB", render::font::SMALL, math::vec2 ( x, y + row * 0.5f ), ink ( tone::INK_SOFT ) );
			y += c.paragraph ( file.debug_path.get ( ), render::font::SMALL, math::vec2 ( value_x, y + 3.0f ), inner - 128.0f, ink ( tone::INK ) ) + 8.0f;

		}

		float content = y + inspector_scroll.shown - area.y + 12.0f;

		glide ( inspector_scroll, content - area.h );
		c.fill ( math::rect ( area.x, area.y, area.w, metric::header ), 0.0f, ink ( tone::PANEL ) );
		c.text ( L"INSPECTOR", render::font::TINY, math::vec2 ( x, area.y + metric::header * 0.5f ), ink ( tone::INK_SOFT ) );
		c.fill ( math::rect ( area.x, area.y + metric::header - 1.0f, area.w, 1.0f ), 0.0f, ink ( tone::LINE ) );
		draw_scrollbar ( make_id ( "inspector_bar" ), area, inspector_scroll, content, area.h );

	}

}