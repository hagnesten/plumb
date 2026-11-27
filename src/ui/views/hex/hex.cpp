#include "ui/views/hex/hex.h"
#include "ui/core/app/app.h"
#include "ui/style/icons/icons.h"
#include "ui/style/format/format.h"

namespace ui {

	bool app::live_address ( uint64_t offset, uint64_t& runtime ) const {

		uint64_t address = 0;

		if ( debug_state.state != debugger::status::PAUSED || !debug_state.image_base || !file.address_of ( offset, address ) )
			return false;

		runtime = address - file.base + debug_state.image_base;

		return true;

	}

	bool app::live_byte ( uint64_t offset, uint8_t& value ) const {

		uint64_t runtime = 0;

		return live_address ( offset, runtime ) && debugger::peek ( runtime, &value, 1 );

	}

	void app::hex_type ( int32_t nibble ) {

		uint64_t offset = now->hex_cursor;

		if ( offset >= file.size )
			return;

		uint64_t runtime = 0;
		bool live = live_address ( offset, runtime );
		uint8_t current = file.byte ( offset );

		if ( live && !debugger::peek ( runtime, &current, 1 ) )
			live = false;

		uint8_t value = now->hex_nibble == 0 ? static_cast < uint8_t > ( ( current & 0x0F ) | ( nibble << 4 ) ) : static_cast < uint8_t > ( ( current & 0xF0 ) | nibble );

		if ( live ) {

			if ( !debugger::poke ( runtime, &value, 1 ) ) {

				log ( log_kind::WARN, base::writer ( ).add ( L"Couldn't write to the process at " ).hex ( runtime ).text ( ) );

				return;

			}

		} else {

			uint64_t address = 0;

			if ( !file.address_of ( offset, address ) ) {

				log ( log_kind::WARN, L"Only bytes that load into memory can be patched here." );

				return;

			}

			if ( !patch_bytes ( address, &value, 1 ) )
				return;

		}

		if ( now->hex_nibble == 0 )
			now->hex_nibble = 1;
		else {

			now->hex_nibble = 0;

			if ( now->hex_cursor + 1 < file.size )
				now->hex_cursor++;

			now->hex_anchor = now->hex_cursor;

			uint64_t address = 0;

			if ( file.address_of ( now->hex_cursor, address ) )
				now->cursor_address = address;

		}

		pending = base::max ( pending, 2 );

	}

	void app::draw_hex ( math::rect area ) {

		render::canvas& c = canvas;
		float row_h = metric::code_row;
		float adv = c.measure ( L"0000000000", render::font::MONO ) * 0.1f;
		uint64_t id = make_id ( "hex" );
		widget& surface = state ( id );
		uint64_t rows = ( file.size + 15 ) / 16;
		math::rect header ( area.x, area.y, area.w, 26.0f );
		math::rect body ( area.x, area.y + 26.0f, area.w, area.h - 26.0f );
		float visible = body.h / row_h;

		c.fill ( area, 0.0f, ink ( tone::BASE ) );
		scroll_region ( id, body );
		now->hex_scroll.target -= take_wheel ( id ) * 3.0f;
		glide ( now->hex_scroll, static_cast < float > ( rows ) - visible + 2.0f, row_h );
		interact ( id, body, surface );

		float x_off = area.x + 14.0f;
		float x_hex = x_off + adv * 12.0f;
		float x_ascii = x_hex + adv * 50.0f;
		auto byte_x = [ & ] ( int32_t column ) {

			return x_hex + static_cast < float > ( column ) * adv * 3.0f + ( column >= 8 ? adv : 0.0f );

		};

		float offset = base::max ( now->hex_scroll.shown, 0.0f );
		uint64_t first = static_cast < uint64_t > ( offset );
		float shift = ( offset - static_cast < float > ( first ) ) * row_h;

		if ( active_id == id && mouse_down && file.size ) {

			float local_y = mouse.y - body.y + shift;
			uint64_t row = first + static_cast < uint64_t > ( base::max ( local_y / row_h, 0.0f ) );
			int32_t column = -1;

			for ( int32_t i = 0; i < 16; i++ ) {

				if ( mouse.x >= byte_x ( i ) - adv * 0.5f && mouse.x < byte_x ( i ) + adv * 2.5f )
					column = i;

				if ( mouse.x >= x_ascii + static_cast < float > ( i ) * adv && mouse.x < x_ascii + static_cast < float > ( i + 1 ) * adv )
					column = i;

			}

			if ( column >= 0 ) {

				uint64_t target = base::min ( row * 16 + static_cast < uint64_t > ( column ), file.size - 1 );

				if ( press_event ) {

					now->hex_cursor = target;
					now->hex_nibble = 0;

					if ( !key_shift )
						now->hex_anchor = target;

					now->hex_selecting = true;

				} else if ( now->hex_selecting )
					now->hex_cursor = target;

				uint64_t address = 0;

				if ( file.address_of ( now->hex_cursor, address ) )
					now->cursor_address = address;

			}

		}

		c.fill ( header, 0.0f, ink ( tone::PANEL ) );
		c.fill ( math::rect ( header.x, header.bottom ( ) - 1.0f, header.w, 1.0f ), 0.0f, ink ( tone::LINE ) );
		c.text ( L"Offset", render::font::SMALL_STRONG, math::vec2 ( x_off, header.center ( ).y ), ink ( tone::INK_SOFT ) );

		for ( int32_t i = 0; i < 16; i++ ) {

			base::writer column;

			column.hex ( static_cast < uint64_t > ( i ), 2 );
			c.text ( column.text ( ), render::font::MONO_SMALL, math::vec2 ( byte_x ( i ), header.center ( ).y ), ink ( tone::INK_FAINT ) );

		}

		c.text ( L"ASCII", render::font::SMALL_STRONG, math::vec2 ( x_ascii, header.center ( ).y ), ink ( tone::INK_SOFT ) );

		bool live = debug_state.state == debugger::status::PAUSED && debug_state.image_base;

		if ( live )
			c.text ( L"Live process memory  \x00B7  typing writes to the process", render::font::SMALL, math::vec2 ( area.right ( ) - 22.0f, header.center ( ).y ), ink ( tone::WARNING ), render::align::RIGHT );

		uint64_t low = base::min ( now->hex_cursor, now->hex_anchor );
		uint64_t high = base::max ( now->hex_cursor, now->hex_anchor );
		int32_t count = static_cast < int32_t > ( visible ) + 2;

		c.push_clip ( body );

		for ( int32_t pass = 0; pass < 2; pass++ ) {

			for ( int32_t k = 0; k < count; k++ ) {

				uint64_t row = first + static_cast < uint64_t > ( k );

				if ( row >= rows )
					break;

				float y = body.y + static_cast < float > ( k ) * row_h - shift;
				float cy = y + row_h * 0.5f;
				uint64_t start = row * 16;

				if ( pass == 0 ) {

					for ( int32_t i = 0; i < 16 && start + static_cast < uint64_t > ( i ) < file.size; i++ ) {

						uint64_t at = start + static_cast < uint64_t > ( i );

						if ( at < low || at > high )
							continue;

						bool edge = at == now->hex_cursor;
						render::color tint = edge ? ink ( tone::SELECT_LINE ).fade ( 0.55f ) : ink ( tone::SELECT );

						c.fill ( math::rect ( byte_x ( i ) - adv * 0.5f, y + 1.0f, adv * 3.0f, row_h - 2.0f ), 2.0f, tint );
						c.fill ( math::rect ( x_ascii + static_cast < float > ( i ) * adv, y + 1.0f, adv, row_h - 2.0f ), 1.0f, tint );

					}

					for ( const loader::section& part : file.sections ) {

						if ( part.raw && part.offset >= start && part.offset < start + 16 && part.type != loader::kind::HEADER ) {

							int32_t column = static_cast < int32_t > ( part.offset - start );

							c.fill ( math::rect ( byte_x ( column ) - adv * 0.5f, y, 1.5f, row_h ), 0.0f, ink ( tone::ACCENT ) );

						}

					}

					continue;

				}

				base::writer position;
				wchar_t ascii [ 17 ];

				position.hex ( start, file.size > 0xFFFFFFFFull ? 12 : 8 );
				c.text ( position.text ( ), render::font::MONO, math::vec2 ( x_off, cy ), ink ( tone::ADDRESS ) );

				int32_t filled = 0;
				uint8_t row_live [ 16 ] = { };
				bool row_known [ 16 ] = { };

				if ( live ) {

					uint64_t first_runtime = 0;
					uint64_t last_runtime = 0;
					uint64_t span = base::min ( file.size - start, 16ull );

					if ( live_address ( start, first_runtime ) && live_address ( start + span - 1, last_runtime ) && last_runtime == first_runtime + span - 1 && debugger::peek ( first_runtime, row_live, static_cast < size_t > ( span ) ) ) {

						for ( uint64_t i = 0; i < span; i++ )
							row_known [ i ] = true;

					} else {

						for ( uint64_t i = 0; i < span; i++ )
							row_known [ i ] = live_byte ( start + i, row_live [ i ] );

					}

				}

				for ( int32_t i = 0; i < 16 && start + static_cast < uint64_t > ( i ) < file.size; i++ ) {

					uint8_t stored = file.byte ( start + static_cast < uint64_t > ( i ) );
					uint8_t value = row_known [ i ] ? row_live [ i ] : stored;
					base::writer pair;
					render::color tint = value == 0 ? ink ( tone::INK_FAINT ) : ( value >= 0x20 && value < 0x7F ? ink ( tone::INK ) : ( value == 0xFF ? ink ( tone::NUMBER ) : ink ( tone::INK_SOFT ) ) );

					if ( row_known [ i ] && value != stored )
						tint = ink ( tone::WARNING );
					else if ( live && !row_known [ i ] )
						tint = ink ( tone::INK_FAINT ).fade ( 0.5f );

					pair.hex ( value, 2 );
					c.text ( pair.text ( ), render::font::MONO, math::vec2 ( byte_x ( i ), cy ), tint );
					ascii [ i ] = printable ( value );
					filled = i + 1;

				}

				ascii [ filled ] = 0;
				c.text ( ascii, render::font::MONO, math::vec2 ( x_ascii, cy ), ink ( tone::INK_SOFT ) );

				for ( const loader::section& part : file.sections ) {

					if ( part.raw && part.offset >= start && part.offset < start + 16 && part.type != loader::kind::HEADER )
						c.text ( part.name, render::font::SMALL_STRONG, math::vec2 ( area.right ( ) - 22.0f, cy ), ink ( tone::ACCENT ), render::align::RIGHT );

				}

			}

		}

		c.pop_clip ( );
		draw_scrollbar ( make_id ( "hex_bar" ), body, now->hex_scroll, static_cast < float > ( rows ) + 2.0f, visible );

	}

}