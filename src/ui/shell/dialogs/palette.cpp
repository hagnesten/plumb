#include "ui/app.h"
#include "base/strings/strings.h"
#include "ui/icons.h"
#include <windows.h>

namespace ui {

	static int32_t fuzzy_score ( const wchar_t* text, const wchar_t* query ) {

		if ( !query [ 0 ] )
			return 1;

		int32_t score = 0;
		int32_t q = 0;
		bool boundary = true;

		for ( int32_t i = 0; text [ i ] && query [ q ]; i++ ) {

			wchar_t a = text [ i ] >= L'A' && text [ i ] <= L'Z' ? static_cast < wchar_t > ( text [ i ] + 32 ) : text [ i ];
			wchar_t b = query [ q ] >= L'A' && query [ q ] <= L'Z' ? static_cast < wchar_t > ( query [ q ] + 32 ) : query [ q ];

			if ( a == b ) {

				score += boundary ? 8 : 2;
				q++;

			}

			boundary = text [ i ] == L' ';

		}

		return query [ q ] ? 0 : score;

	}

	static void keep_best ( base::list < palette_entry >& items, const palette_entry& entry, int32_t room ) {

		if ( items.count ( ) >= room && items.back ( ).score >= entry.score )
			return;

		int32_t at = items.count ( );

		items.push ( entry );

		while ( at > 0 && items [ static_cast < size_t > ( at - 1 ) ].score < entry.score ) {

			items [ static_cast < size_t > ( at ) ] = items [ static_cast < size_t > ( at - 1 ) ];
			at--;

		}

		items [ static_cast < size_t > ( at ) ] = entry;

		if ( items.count ( ) > room )
			items.pop ( );

	}

	static int32_t name_score ( const wchar_t* text, const wchar_t* query, int32_t bonus ) {

		if ( !text || !text [ 0 ] )
			return 0;

		if ( base::compare_folded ( text, query ) == 0 )
			return 400 + bonus;

		if ( base::contains_folded ( text, query ) ) {

			int32_t length = 0;

			while ( text [ length ] )
				length++;

			return 200 + bonus - base::min ( length, 80 );

		}

		int32_t fuzzy = fuzzy_score ( text, query );

		return fuzzy ? fuzzy + bonus / 2 : 0;

	}

	void app::fill_palette ( const wchar_t* query ) {

		const int32_t room = 300;

		for ( int32_t i = 0; i < static_cast < int32_t > ( command::COUNT ); i++ ) {

			int32_t score = fuzzy_score ( command_label ( static_cast < command > ( i ) ), query );

			if ( !score )
				continue;

			palette_entry entry;

			entry.action = static_cast < command > ( i );
			entry.score = score + ( enabled ( static_cast < command > ( i ) ) ? 100 : 0 );
			keep_best ( palette_items, entry, room );

		}

		int32_t length = 0;

		while ( query [ length ] )
			length++;

		if ( length < 2 || !file.loaded ( ) )
			return;

		wchar_t name [ 400 ];

		auto offer = [ & ] ( jump_kind kind, const wchar_t* text, uint64_t address, int32_t index, int32_t bonus ) {

			int32_t score = name_score ( text, query, bonus );

			if ( !score )
				return;

			palette_entry entry;

			entry.kind = kind;
			entry.score = score;
			entry.address = address;
			entry.index = index;
			keep_best ( palette_items, entry, room );

		};

		if ( db && analysis_applied ) {

			for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

				function_name ( i, name, 400 );
				offer ( jump_kind::FUNCTION, name, db->functions [ static_cast < size_t > ( i ) ].start, i, 60 );

			}

		}

		for ( int32_t i = 0; i < file.imports.count ( ); i++ ) {

			const loader::import& item = file.imports [ static_cast < size_t > ( i ) ];

			if ( !item.name.empty ( ) )
				offer ( jump_kind::IMPORT, item.name.get ( ), item.slot, i, 40 );

		}

		for ( int32_t i = 0; i < file.exports.count ( ); i++ ) {

			const loader::exported& item = file.exports [ static_cast < size_t > ( i ) ];

			if ( !item.name.empty ( ) && ( !db || db->function_starting ( item.address ) < 0 ) )
				offer ( jump_kind::EXPORT, item.name.get ( ), item.address, i, 50 );

		}

		for ( int32_t i = 0; i < renamed.count ( ); i++ ) {

			const user_name& item = renamed [ static_cast < size_t > ( i ) ];

			if ( !db || db->function_starting ( item.address ) < 0 )
				offer ( jump_kind::NAME, item.text.get ( ), item.address, i, 55 );

		}

		for ( int32_t i = 0; i < structs.count ( ); i++ )
			offer ( jump_kind::STRUCT, structs [ static_cast < size_t > ( i ) ].name.get ( ), 0, i, 50 );

		for ( int32_t i = 0; i < bookmarks.count ( ); i++ )
			offer ( jump_kind::BOOKMARK, bookmarks [ static_cast < size_t > ( i ) ].text.get ( ), bookmarks [ static_cast < size_t > ( i ) ].address, i, 70 );

		for ( int32_t i = 0; i < notes.count ( ); i++ )
			offer ( jump_kind::NOTE, notes [ static_cast < size_t > ( i ) ].text.get ( ), notes [ static_cast < size_t > ( i ) ].address, i, 30 );

		for ( int32_t i = 0; i < comments.count ( ); i++ )
			offer ( jump_kind::COMMENT, comments [ static_cast < size_t > ( i ) ].text.get ( ), comments [ static_cast < size_t > ( i ) ].address, i, 20 );

		if ( length >= 3 ) {

			for ( int32_t i = 0; i < file.strings.count ( ); i++ ) {

				uint64_t address = 0;

				if ( !file.address_of ( file.strings [ static_cast < size_t > ( i ) ].offset, address ) )
					continue;

				string_label ( i, name, 400 );

				if ( base::contains_folded ( name, query ) )
					offer ( jump_kind::STRING, name, address, i, 10 );

			}

		}

	}

	void app::palette_label ( const palette_entry& entry, wchar_t* out, int32_t room ) const {

		out [ 0 ] = 0;

		auto copy = [ & ] ( const wchar_t* text ) {

			int32_t k = 0;

			for ( ; text && text [ k ] && k < room - 1; k++ )
				out [ k ] = text [ k ] == L'\n' || text [ k ] == L'\r' ? L' ' : text [ k ];

			out [ k ] = 0;

		};

		switch ( entry.kind ) {

			case jump_kind::COMMAND:
				copy ( command_label ( entry.action ) );
				break;

			case jump_kind::FUNCTION:
				function_name ( entry.index, out, room );
				break;

			case jump_kind::IMPORT:
				copy ( file.imports [ static_cast < size_t > ( entry.index ) ].name.get ( ) );
				break;

			case jump_kind::EXPORT:
				copy ( file.exports [ static_cast < size_t > ( entry.index ) ].name.get ( ) );
				break;

			case jump_kind::NAME:
				copy ( renamed [ static_cast < size_t > ( entry.index ) ].text.get ( ) );
				break;

			case jump_kind::STRUCT:
				copy ( structs [ static_cast < size_t > ( entry.index ) ].name.get ( ) );
				break;

			case jump_kind::BOOKMARK:
				copy ( bookmarks [ static_cast < size_t > ( entry.index ) ].text.get ( ) );
				break;

			case jump_kind::NOTE:
				copy ( notes [ static_cast < size_t > ( entry.index ) ].text.get ( ) );
				break;

			case jump_kind::COMMENT:
				copy ( comments [ static_cast < size_t > ( entry.index ) ].text.get ( ) );
				break;

			case jump_kind::STRING: {

				wchar_t inner [ 380 ];

				string_label ( entry.index, inner, 380 );
				copy ( base::writer ( ).add ( L"\"" ).add ( inner ).add ( L"\"" ).text ( ) );
				break;

			}

		}

	}

	void app::draw_palette ( ) {

		settle ( palette_show, palette_open ? 1.0f : 0.0f );

		if ( palette_show <= 0.004f && !palette_open )
			return;

		render::canvas& c = canvas;
		float shown = math::saturate ( palette_show );
		float width_box = base::min ( 600.0f, w - 40.0f );
		float x = w * 0.5f - width_box * 0.5f;
		float y = title_area.bottom ( ) + 40.0f;
		uint64_t blocker = make_id ( "palette_blocker" );
		widget& block = state ( blocker );

		if ( palette_open && interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) ) {

			palette_open = false;
			focus_id = 0;
			focused_field = nullptr;

		}

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.2f * shown ) );

		if ( palette_open && palette_field.revision != palette_seen ) {

			palette_seen = palette_field.revision;
			palette_items.clear ( );
			palette_pick = 0;

			if ( !palette_address )
				fill_palette ( palette_field.text );

		}

		int32_t visible = base::min ( palette_items.count ( ), 14 );
		float row_h = metric::palette_row;
		float head_h = metric::field + 12.0f;
		float list_h = palette_address ? 40.0f : static_cast < float > ( visible ) * row_h + ( visible ? 8.0f : 0.0f );
		math::rect box ( x, y, width_box, head_h + list_h );

		palette_pick = palette_items.count ( ) ? base::clamp ( palette_pick, 0, palette_items.count ( ) - 1 ) : 0;
		c.push ( math::vec2 ( box.center ( ).x, box.y ), 1.0f, shown );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "palette_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );

		bool submitted = text_field ( make_id ( "palette" ), palette_field, math::rect ( box.x + 6.0f, box.y + 6.0f, box.w - 12.0f, metric::field ), palette_address ? L"Address, RVA (+1A30), symbol or section" : ( file.loaded ( ) ? L"Command, function, name, string, struct or bookmark" : L"Type a command" ), palette_address ? icons::FORWARD : icons::SEARCH, palette_address ? render::font::MONO : render::font::UI );

		c.fill ( math::rect ( box.x, box.y + head_h - 1.0f, box.w, 1.0f ), 0.0f, ink ( tone::LINE ) );

		if ( palette_address ) {

			uint64_t address = 0;
			bool valid = file.loaded ( ) && parse_address ( palette_field.text, address );
			base::writer hint;

			if ( !file.loaded ( ) )
				hint.add ( L"Open a file first." );
			else if ( !palette_field.length )
				hint.add ( L"Enter jumps.  Plain hex is a virtual address, +hex is relative to the image base." );
			else if ( valid ) {

				int32_t owner = file.section_at ( address );

				hint.add ( L"Go to " ).add ( owner >= 0 ? file.sections [ static_cast < size_t > ( owner ) ].name : L"(outside the image)" ).add ( L":" ).hex ( address, file.wide ( ) ? 16 : 8 );

			} else
				hint.add ( L"That isn't an address or a known name." );

			c.text ( hint.text ( ), render::font::SMALL, math::vec2 ( box.x + 12.0f, box.y + head_h + 20.0f ), valid ? ink ( tone::INK_SOFT ) : ink ( tone::INK_FAINT ) );

			if ( submitted && valid ) {

				palette_open = false;
				focus_id = 0;
				focused_field = nullptr;
				go ( address, true );

			}

		} else {

			float target = static_cast < float > ( palette_pick );

			if ( palette_pick_y < -0.5f )
				palette_pick_y = target;

			settle ( palette_pick_y, target );

			float list_top = box.y + head_h + 4.0f;

			if ( visible )
				c.fill ( math::rect ( box.x + 4.0f, list_top + palette_pick_y * row_h, box.w - 8.0f, row_h ), 2.0f, ink ( tone::SELECT ) );

			for ( int32_t i = 0; i < visible; i++ ) {

				const palette_entry& entry = palette_items [ static_cast < size_t > ( i ) ];
				math::rect row ( box.x + 4.0f, list_top + static_cast < float > ( i ) * row_h, box.w - 8.0f, row_h );
				uint64_t id = make_id ( "palette_row", i );
				widget& item = state ( id );
				bool is_command = entry.kind == jump_kind::COMMAND;
				bool usable = !is_command || enabled ( entry.action );
				wchar_t label [ 400 ];

				if ( interact ( id, row, item ) ) {

					palette_pick = i;
					submitted = true;

				}

				if ( item.hover > 0.5f && mouse_inside )
					palette_pick = i;

				palette_label ( entry, label, 400 );

				if ( is_command ) {

					c.text ( label, render::font::UI, math::vec2 ( row.x + 10.0f, row.center ( ).y ), usable ? ink ( tone::INK ) : ink ( tone::INK_FAINT ) );

					if ( !usable )
						c.text ( L"unavailable", render::font::SMALL, math::vec2 ( row.right ( ) - 14.0f - ( command_keys ( entry.action ) [ 0 ] ? c.measure ( command_keys ( entry.action ), render::font::SMALL ) + 24.0f : 0.0f ), row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

					key_cap ( math::vec2 ( row.right ( ) - 10.0f, row.center ( ).y ), command_keys ( entry.action ), ink ( tone::INK_SOFT ), true );

					continue;

				}

				static const wchar_t* const kinds [ ] = { L"", L"function", L"import", L"export", L"name", L"struct", L"bookmark", L"note", L"comment", L"string" };
				static const tone colors [ ] = { tone::INK, tone::FUNCTION, tone::IMPORT, tone::FUNCTION, tone::GLOBAL, tone::TYPE, tone::INK, tone::COMMENT, tone::COMMENT, tone::STRING };
				int32_t slot = static_cast < int32_t > ( entry.kind );
				base::writer tag;

				tag.add ( kinds [ slot ] );

				if ( entry.kind != jump_kind::STRUCT )
					tag.add ( L"  " ).hex ( entry.address, file.wide ( ) ? 12 : 8 );

				float tag_w = c.measure ( tag.text ( ), render::font::SMALL ) + 24.0f;

				c.text ( label, entry.kind == jump_kind::FUNCTION || entry.kind == jump_kind::IMPORT || entry.kind == jump_kind::EXPORT || entry.kind == jump_kind::NAME ? render::font::MONO : render::font::UI, math::vec2 ( row.x + 10.0f, row.center ( ).y ), ink ( colors [ slot ] ), render::align::LEFT, row.w - tag_w - 20.0f );
				c.text ( tag.text ( ), render::font::SMALL, math::vec2 ( row.right ( ) - 12.0f, row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

			}

			if ( submitted && palette_items.count ( ) ) {

				palette_entry chosen = palette_items [ static_cast < size_t > ( palette_pick ) ];

				palette_open = false;
				focus_id = 0;
				focused_field = nullptr;

				if ( chosen.kind == jump_kind::COMMAND )
					run ( chosen.action );
				else if ( chosen.kind == jump_kind::STRUCT ) {

					browsing = panel::STRUCTS;
					browser_open = true;
					expanded_struct = chosen.index;
					refresh_filter ( panel::STRUCTS );

				} else
					go ( chosen.address, true );

			}

		}

		c.pop ( );

	}

}