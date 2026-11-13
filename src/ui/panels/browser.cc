#include "ui/app.hh"
#include "ui/icons.hh"

namespace ui {

	static const uint32_t panel_glyphs [ static_cast < int32_t > ( panel::COUNT ) ] = { icons::CODE, icons::IMPORT, icons::EXPORT, icons::FONT, icons::LAYERS, icons::PUZZLE, icons::FORM, icons::LINK, icons::SEARCH, icons::INFO };
	static const wchar_t* panel_titles [ static_cast < int32_t > ( panel::COUNT ) ] = { L"Functions", L"Imports", L"Exports", L"Strings", L"Sections", L"Classes", L"Structures", L"References", L"Search", L"Inspector" };
	static const wchar_t* panel_captions [ static_cast < int32_t > ( panel::COUNT ) ] = { L"FUNCTIONS", L"IMPORTS", L"EXPORTS", L"STRINGS", L"SECTIONS", L"CLASSES", L"STRUCTURES", L"REFERENCES", L"SEARCH", L"INSPECTOR" };
	static const wchar_t* panel_columns [ static_cast < int32_t > ( panel::COUNT ) ] [ 3 ] = {
		{ L"Name", L"Address", L"Size" }, { L"Name", nullptr, L"Slot" }, { L"Name", nullptr, L"Address" }, { L"Text", nullptr, L"Address" }, { L"Name", L"Access", L"Address" },
		{ L"Class", nullptr, L"Virtuals" }, { L"Struct", nullptr, L"Size" }, { L"Kind", L"From", nullptr }, { L"Address", L"Where", nullptr }, { nullptr, nullptr, nullptr }
	};
	static const wchar_t* panel_hints [ static_cast < int32_t > ( panel::COUNT ) ] = { L"Filter functions", L"Filter imports or modules", L"Filter exports", L"Search strings", L"Filter sections", L"Filter classes", L"Filter structures", L"Filter by function", L"Filter results", L"" };

	static const wchar_t* ref_word ( analysis::ref type ) {

		switch ( type ) {

			case analysis::ref::CALL:
				return L"call";

			case analysis::ref::JUMP:
				return L"jump";

			case analysis::ref::BRANCH:
				return L"branch";

			case analysis::ref::READ:
				return L"read";

			case analysis::ref::WRITE:
				return L"write";

			case analysis::ref::ADDRESS:
				return L"address";

			case analysis::ref::POINTER:
				return L"pointer";

			default:
				return L"case";

		}

	}

	void app::draw_rail ( ) {

		if ( rail_area.w < 1.0f )
			return;

		render::canvas& c = canvas;
		math::rect area = rail_area;

		c.fill ( area, 0.0f, ink ( tone::TITLE ) );
		c.fill ( math::rect ( area.right ( ) - 1.0f, area.y, 1.0f, area.h ), 0.0f, ink ( tone::LINE ) );

		float slot = metric::rail_slot;
		float top = area.y + 4.0f;

		if ( browser_open )
			c.fill ( math::rect ( area.x, top + static_cast < float > ( static_cast < int32_t > ( browsing ) ) * slot + 7.0f, 2.0f, slot - 14.0f ), 0.0f, ink ( tone::ACCENT ) );

		for ( int32_t i = 0; i < static_cast < int32_t > ( panel::COUNT ); i++ ) {

			math::vec2 center ( area.x + area.w * 0.5f, top + static_cast < float > ( i ) * slot + slot * 0.5f );
			uint64_t id = make_id ( "rail", i );
			widget& item = state ( id );
			math::rect hit_area ( area.x, center.y - slot * 0.5f, area.w, slot );
			bool selected = browser_open && browsing == static_cast < panel > ( i );

			if ( interact ( id, hit_area, item ) ) {

				if ( selected )
					browser_open = false;
				else {

					browsing = static_cast < panel > ( i );
					browser_open = true;

				}

			}

			c.fill ( math::rect ( center.x - 13.0f, center.y - 13.0f, 26.0f, 26.0f ), metric::radius, ink ( tone::HOVER ).fade ( item.hover * 1.5f ) );
			c.icon ( panel_glyphs [ i ], center, 14.0f, selected ? ink ( tone::INK ) : ( item.hover > 0.5f ? ink ( tone::INK_SOFT ) : ink ( tone::INK_FAINT ) ) );
			tip ( id, panel_titles [ i ], hit_area );

		}

		float bottom = area.bottom ( ) - 6.0f;

		if ( icon_button ( make_id ( "rail_theme" ), math::vec2 ( area.x + area.w * 0.5f, bottom - 16.0f ), 26.0f, light ? icons::MOON : icons::SUN, 13.0f, light ? L"Dark theme" : L"Light theme" ) )
			run ( command::THEME );

		if ( icon_button ( make_id ( "rail_palette" ), math::vec2 ( area.x + area.w * 0.5f, bottom - 48.0f ), 26.0f, icons::MENU, 13.0f, L"Command palette" ) )
			run ( command::PALETTE );

	}

	void app::draw_browser ( ) {

		if ( browser_area.w < 2.0f )
			return;

		render::canvas& c = canvas;
		math::rect area = browser_area;
		int32_t slot = static_cast < int32_t > ( browsing );
		float inner_w = browser_fit;

		c.push_clip ( area );
		c.fill ( area, 0.0f, ink ( tone::PANEL ) );

		if ( browsing == panel::INSPECTOR ) {

			draw_inspector ( area, inner_w );
			c.pop_clip ( );

			return;

		}

		float x = area.right ( ) - inner_w;
		base::writer count;

		if ( filters [ slot ].revision != filter_seen [ slot ] )
			refresh_filter ( browsing );

		int32_t total = 0;

		switch ( browsing ) {

			case panel::FUNCTIONS:
				total = db && analysis_applied && !db->functions.empty ( ) ? db->functions.count ( ) : file.functions.count ( );
				break;

			case panel::CLASSES:
				total = db && analysis_applied ? db->classes.count ( ) : 0;
				break;

			case panel::STRUCTS:
				total = structs.count ( ) + 1;
				break;

			case panel::REFERENCES:
				total = reference_list.count ( );
				break;

			case panel::SEARCH:
				total = search_hits.count ( );
				break;

			case panel::IMPORTS:
				total = file.imports.count ( );
				break;

			case panel::EXPORTS:
				total = file.exports.count ( );
				break;

			case panel::STRINGS:
				total = file.strings.count ( );
				break;

			default:
				total = file.sections.count ( );
				break;

		}

		count.grouped ( static_cast < uint64_t > ( total ) );

		math::rect header ( x + 10.0f, area.y, inner_w - 20.0f, metric::header );
		bool subject_line = browsing == panel::REFERENCES || browsing == panel::SEARCH;

		c.text ( panel_captions [ slot ], render::font::TINY, math::vec2 ( header.x, header.center ( ).y ), ink ( tone::INK_SOFT ) );

		if ( browsing == panel::REFERENCES || browsing == panel::SEARCH ) {

			base::writer subject;
			wchar_t name [ 200 ];

			if ( browsing == panel::REFERENCES ) {

				subject.add ( L"to " );

				if ( references_for != ~0ull && symbol_name ( references_for, true, name, 200 ) )
					subject.add ( name );
				else if ( references_for != ~0ull )
					subject.hex ( references_for );
				else
					subject.add ( L"the cursor" );

			} else if ( !search_query.empty ( ) )
				subject.add ( search_query.get ( ) ).add ( L"  \x00B7  " ).fixed ( search_seconds * 1000.0, 1 ).add ( L" ms" );
			else
				subject.add ( L"Ctrl+Alt+F to search bytes or a signature" );

			c.text ( subject.text ( ), render::font::SMALL, math::vec2 ( header.x, header.bottom ( ) + 7.0f ), ink ( tone::INK_SOFT ), render::align::LEFT, header.w );

		}

		c.text ( count.text ( ), render::font::SMALL, math::vec2 ( header.right ( ), header.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

		float field_y = metric::header + ( subject_line ? 16.0f : 0.0f );

		text_field ( make_id ( "filter", slot ), filters [ slot ], math::rect ( x + 6.0f, area.y + field_y, inner_w - 12.0f, metric::field ), panel_hints [ slot ], icons::SEARCH, render::font::UI );

		float columns_top = field_y + metric::field + 6.0f;
		math::rect columns ( x, area.y + columns_top, inner_w, 20.0f );
		const wchar_t* const* names = panel_columns [ slot ];

		c.fill ( columns, 0.0f, ink ( tone::SUNKEN ) );
		c.fill ( math::rect ( x, columns.y, inner_w, 1.0f ), 0.0f, ink ( tone::LINE ) );
		c.fill ( math::rect ( x, columns.bottom ( ) - 1.0f, inner_w, 1.0f ), 0.0f, ink ( tone::LINE ) );

		bool sortable = browsing == panel::FUNCTIONS || browsing == panel::EXPORTS || browsing == panel::STRINGS || browsing == panel::SECTIONS;
		bool middle_right = browsing == panel::FUNCTIONS;
		float middle_x = browsing == panel::FUNCTIONS ? x + inner_w - 64.0f : ( browsing == panel::SECTIONS ? x + 106.0f : ( browsing == panel::REFERENCES ? x + 60.0f : x + 100.0f ) );
		float right_x = x + inner_w - 10.0f;
		float spots [ 3 ] = { x + 10.0f, middle_x, right_x };
		render::align aligns [ 3 ] = { render::align::LEFT, middle_right ? render::align::RIGHT : render::align::LEFT, render::align::RIGHT };
		float middle_left = names [ 1 ] ? ( middle_right ? middle_x - 64.0f : middle_x - 6.0f ) : right_x - 64.0f;
		math::rect cells [ 3 ] = {
			math::rect ( x, columns.y, middle_left - x, columns.h ),
			math::rect ( middle_left, columns.y, middle_right ? middle_x + 4.0f - middle_left : right_x - 70.0f - middle_left, columns.h ),
			math::rect ( right_x - 64.0f, columns.y, x + inner_w - ( right_x - 64.0f ), columns.h )
		};

		for ( int32_t k = 0; k < 3; k++ ) {

			if ( !names [ k ] )
				continue;

			bool active = sort_by [ slot ] == k + 1;
			float label_w = c.measure ( names [ k ], render::font::SMALL );

			if ( sortable ) {

				uint64_t id = make_id ( "sort_column", slot * 4 + k );
				widget& cell = state ( id );

				if ( interact ( id, cells [ k ], cell ) ) {

					if ( !active ) {

						sort_by [ slot ] = static_cast < int8_t > ( k + 1 );
						sort_down [ slot ] = k == 2 && browsing == panel::FUNCTIONS;

					} else if ( sort_down [ slot ] != ( k == 2 && browsing == panel::FUNCTIONS ) )
						sort_by [ slot ] = 0;
					else
						sort_down [ slot ] = !sort_down [ slot ];

					refresh_filter ( browsing );
					active = sort_by [ slot ] == k + 1;

				}

				c.fill ( cells [ k ], 0.0f, ink ( tone::HOVER ).fade ( cell.hover ) );
				tip ( id, L"Sort by this column", cells [ k ] );

			}

			c.text ( names [ k ], active ? render::font::SMALL_STRONG : render::font::SMALL, math::vec2 ( spots [ k ], columns.center ( ).y ), active ? ink ( tone::INK ) : ink ( tone::INK_SOFT ), aligns [ k ] );

			if ( active ) {

				float arrow_x = aligns [ k ] == render::align::RIGHT ? spots [ k ] - label_w - 8.0f : spots [ k ] + label_w + 8.0f;

				c.icon ( sort_down [ slot ] ? icons::CHEVRON_DOWN : icons::CHEVRON_UP, math::vec2 ( arrow_x, columns.center ( ).y ), 8.0f, ink ( tone::ACCENT ) );

			}

		}

		math::rect list ( x, columns.bottom ( ), inner_w, area.bottom ( ) - columns.bottom ( ) );

		draw_browser_rows ( browsing, list );
		c.pop_clip ( );

	}

	void app::draw_browser_rows ( panel which, math::rect area ) {

		render::canvas& c = canvas;
		int32_t slot = static_cast < int32_t > ( which );
		base::list < int32_t >& list = shown [ slot ];
		scroller& scroll = browser_scroll [ slot ];
		float row_h = metric::list_row;
		float content = static_cast < float > ( list.count ( ) ) * row_h + 4.0f;
		uint64_t list_id = make_id ( "browser_list", slot );

		scroll_region ( list_id, area );
		scroll.target -= take_wheel ( list_id ) * row_h * 3.0f;
		glide ( scroll, content - area.h );

		if ( list.empty ( ) ) {

			const wchar_t* empty = filters [ slot ].length ? L"No matches." : L"Nothing here in this image.";

			if ( which == panel::REFERENCES )
				empty = analysis_applied ? L"Nothing references this address." : L"Waiting for analysis.";
			else if ( which == panel::CLASSES )
				empty = analysis_applied ? L"No MSVC RTTI in this image." : L"Waiting for analysis.";
			else if ( which == panel::SEARCH )
				empty = search_query.empty ( ) ? L"No search yet." : L"No matches.";

			c.text ( empty, render::font::UI, math::vec2 ( area.x + 10.0f, area.y + 16.0f ), ink ( tone::INK_FAINT ) );

			return;

		}

		int32_t first = base::max ( static_cast < int32_t > ( scroll.shown / row_h ), 0 );
		int32_t last = base::min ( list.count ( ), first + static_cast < int32_t > ( area.h / row_h ) + 2 );
		bool wide = file.wide ( );

		c.push_clip ( area );

		for ( int32_t pass = 0; pass < 2; pass++ ) {

			for ( int32_t i = first; i < last; i++ ) {

				int32_t value = list [ static_cast < size_t > ( i ) ];
				math::rect row ( area.x + 2.0f, area.y + 2.0f + static_cast < float > ( i ) * row_h - scroll.shown, area.w - 4.0f, row_h );
				uint64_t id = make_id ( "browser_row", static_cast < int64_t > ( slot ) * 0x100000000ll + i );
				bool header = which == panel::IMPORTS && value < 0;
				float cy = row.center ( ).y;

				if ( pass == 0 ) {

					widget& item = state ( id );
					bool clicked = interact ( id, row, item );

					if ( header ) {

						c.fill ( math::rect ( area.x, row.y, area.w, row.h ), 0.0f, ink ( tone::BASE ).fade ( 0.55f ) );

						continue;

					}

					if ( browser_pick [ slot ] == i )
						c.fill ( row, 0.0f, ink ( tone::SELECT ) );

					c.fill ( row, 0.0f, ink ( tone::HOVER ).fade ( item.hover ) );

					if ( clicked ) {

						browser_pick [ slot ] = i;

						switch ( which ) {

							case panel::FUNCTIONS:

								if ( db && analysis_applied && !db->functions.empty ( ) )
									go ( db->functions [ static_cast < size_t > ( value ) ].start, true );
								else
									go ( file.functions [ static_cast < size_t > ( value ) ].address, true );

								break;

							case panel::CLASSES: {

								int32_t owner = value / 8192;
								int32_t child = value % 8192;
								const analysis::class_info& info = db->classes [ static_cast < size_t > ( owner ) ];

								if ( child == 0 ) {

									expanded_class = expanded_class == owner ? -1 : owner;
									go ( info.vtable, true );
									refresh_filter ( panel::CLASSES );

								} else if ( child <= info.base_count ) {

									const wchar_t* base_name = db->class_bases [ static_cast < size_t > ( info.first_base + child - 1 ) ].get ( );

									for ( int32_t k = 0; k < db->classes.count ( ); k++ ) {

										if ( base::same_text ( db->classes [ static_cast < size_t > ( k ) ].name.get ( ), base_name ) && db->classes [ static_cast < size_t > ( k ) ].offset == 0 ) {

											expanded_class = k;
											go ( db->classes [ static_cast < size_t > ( k ) ].vtable, true );
											refresh_filter ( panel::CLASSES );

											break;

										}

									}

								} else {

									uint64_t slot = info.vtable + static_cast < uint64_t > ( child - 1 - info.base_count ) * ( file.wide ( ) ? 8 : 4 );
									uint64_t offset = 0;

									if ( file.offset_of ( slot, offset ) )
										go ( file.wide ( ) ? file.read64 ( offset ) : file.read32 ( offset ), true );

								}

								return;

							}

							case panel::STRUCTS: {

								if ( value == -2 ) {

									open_struct_prompt ( 6, -1, 0, L"" );

									return;

								}

								int32_t owner = value / 4096;
								int32_t child = value % 4096;

								if ( owner >= structs.count ( ) )
									return;

								const user_struct& info = structs [ static_cast < size_t > ( owner ) ];

								if ( child == 0 ) {

									if ( mouse.x > row.right ( ) - 30.0f )
										open_struct_prompt ( 9, owner, 0, info.name.get ( ) );
									else {

										expanded_struct = expanded_struct == owner ? -1 : owner;
										refresh_filter ( panel::STRUCTS );

									}

								} else if ( child == 4095 ) {

									base::writer fill;

									fill.add ( L"+0x" ).hex ( static_cast < uint64_t > ( struct_size ( owner ) ) ).add ( L" int32_t field_" ).hex ( static_cast < uint64_t > ( struct_size ( owner ) ) );
									open_struct_prompt ( 8, owner, struct_size ( owner ), fill.text ( ) );

								} else if ( child - 1 < info.fields.count ( ) ) {

									const user_field& field = info.fields [ static_cast < size_t > ( child - 1 ) ];
									base::writer fill;

									fill.add ( field.type.get ( ) ).add ( L" " ).add ( field.name.get ( ) );
									open_struct_prompt ( 7, owner, field.offset, fill.text ( ) );

								}

								return;

							}

							case panel::REFERENCES:
								go ( file.base + reference_list [ static_cast < size_t > ( value ) ].source, true );
								break;

							case panel::SEARCH:
								go ( search_hits [ static_cast < size_t > ( value ) ].address, true );
								break;

							case panel::IMPORTS:
								go ( file.imports [ static_cast < size_t > ( value ) ].slot, true );
								break;

							case panel::EXPORTS:

								if ( file.exports [ static_cast < size_t > ( value ) ].address )
									go ( file.exports [ static_cast < size_t > ( value ) ].address, true );

								break;

							case panel::STRINGS: {

								uint64_t address = 0;
								uint64_t offset = file.strings [ static_cast < size_t > ( value ) ].offset;

								if ( file.address_of ( offset, address ) && showing != view::HEX )
									go ( address, true );
								else
									go_offset ( offset );

								break;

							}

							default:
								go ( file.sections [ static_cast < size_t > ( value ) ].address, true );
								break;

						}

					}

					continue;

				}

				base::writer where;

				switch ( which ) {

					case panel::FUNCTIONS: {

						if ( db && analysis_applied && !db->functions.empty ( ) ) {

							const analysis::function_info& info = db->functions [ static_cast < size_t > ( value ) ];
							wchar_t name [ 200 ];
							tone tag = ( info.flags & analysis::THUNK ) ? tone::IMPORT : ( ( info.flags & ( analysis::FROM_ENTRY | analysis::FROM_EXPORT | analysis::NAMED ) ) || user_text ( renamed, info.start ) ? tone::FUNCTION : tone::INK );
							base::writer size_text;

							function_name ( value, name, 200 );
							c.text ( name, render::font::MONO_SMALL, math::vec2 ( row.x + 8.0f, cy ), ink ( tag ), render::align::LEFT, row.w - 136.0f );
							where.hex ( info.start, wide ? 9 : 8 );
							c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 62.0f, cy ), ink ( tone::ADDRESS ), render::align::RIGHT );
							size_text.add ( L"0x" ).hex ( info.end - info.start );
							c.text ( size_text.text ( ), render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT );

							break;

						}

						const loader::symbol& item = file.functions [ static_cast < size_t > ( value ) ];
						render::color tag = item.type == loader::symbol_kind::ENTRY ? ink ( tone::ACCENT ) : ( item.type == loader::symbol_kind::TLS ? ink ( tone::WARNING ) : ink ( tone::NAME ) );

						c.fill ( math::rect ( row.x + 8.0f, cy - 3.0f, 6.0f, 6.0f ), 1.0f, tag );
						where.hex ( item.address, wide ? 9 : 8 );
						c.text ( item.name.get ( ), render::font::UI, math::vec2 ( row.x + 22.0f, cy ), ink ( tone::INK ), render::align::LEFT, row.w - 100.0f );
						c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::ADDRESS ), render::align::RIGHT );

						break;

					}

					case panel::IMPORTS: {

						if ( header ) {

							const loader::module& owner = file.modules [ static_cast < size_t > ( -value - 1 ) ];

							where.number ( owner.count );
							c.icon ( icons::LIBRARY, math::vec2 ( row.x + 11.0f, cy ), 12.0f, ink ( tone::INK_SOFT ) );
							c.text ( owner.name.get ( ), render::font::UI_STRONG, math::vec2 ( row.x + 26.0f, cy ), ink ( tone::INK ), render::align::LEFT, row.w - 70.0f );
							c.text ( where.text ( ), render::font::SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT );

							break;

						}

						const loader::import& item = file.imports [ static_cast < size_t > ( value ) ];
						base::writer label;

						if ( item.by_ordinal || item.name.empty ( ) )
							label.add ( L"#" ).number ( item.ordinal );
						else
							label.add ( item.name.get ( ) );

						where.hex ( item.slot, wide ? 9 : 8 );
						c.text ( label.text ( ), render::font::UI, math::vec2 ( row.x + 26.0f, cy ), item.by_ordinal ? ink ( tone::INK_SOFT ) : ink ( tone::INK ), render::align::LEFT, row.w - 110.0f );
						c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::ADDRESS ), render::align::RIGHT );

						break;

					}

					case panel::EXPORTS: {

						const loader::exported& item = file.exports [ static_cast < size_t > ( value ) ];
						base::writer label;

						if ( item.name.empty ( ) )
							label.add ( L"#" ).number ( item.ordinal );
						else
							label.add ( item.name.get ( ) );

						if ( item.forward.empty ( ) )
							where.hex ( item.address, wide ? 9 : 8 );
						else
							where.add ( L"\x2192 " ).add ( item.forward.get ( ) );

						c.text ( label.text ( ), render::font::UI, math::vec2 ( row.x + 10.0f, cy ), ink ( tone::INK ), render::align::LEFT, row.w - 110.0f );
						c.text ( where.text ( ), item.forward.empty ( ) ? render::font::MONO_SMALL : render::font::SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), item.forward.empty ( ) ? ink ( tone::ADDRESS ) : ink ( tone::INK_SOFT ), render::align::RIGHT, 140.0f );

						break;

					}

					case panel::STRINGS: {

						const loader::found_string& item = file.strings [ static_cast < size_t > ( value ) ];
						wchar_t preview [ 120 ];
						int32_t length = base::min ( static_cast < int32_t > ( item.length ), 119 );
						uint64_t address = 0;

						for ( int32_t k = 0; k < length; k++ )
							preview [ k ] = static_cast < wchar_t > ( file.byte ( item.offset + static_cast < uint64_t > ( k ) * ( item.wide ? 2 : 1 ) ) );

						preview [ length ] = 0;

						if ( file.address_of ( item.offset, address ) )
							where.hex ( address, wide ? 9 : 8 );
						else
							where.add ( L"@" ).hex ( item.offset );

						c.text ( item.wide ? L"W" : L"A", render::font::TINY, math::vec2 ( row.x + 8.0f, cy ), item.wide ? ink ( tone::NAME ) : ink ( tone::INK_FAINT ) );
						c.text ( preview, render::font::MONO_SMALL, math::vec2 ( row.x + 24.0f, cy ), ink ( tone::INK ), render::align::LEFT, row.w - 104.0f );
						c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::ADDRESS ), render::align::RIGHT );

						break;

					}

					case panel::CLASSES: {

						int32_t owner = value / 8192;
						int32_t child = value % 8192;
						const analysis::class_info& info = db->classes [ static_cast < size_t > ( owner ) ];

						if ( child == 0 ) {

							base::writer count_text;

							c.icon ( owner == expanded_class ? icons::CHEVRON_DOWN : icons::CHEVRON_RIGHT, math::vec2 ( row.x + 11.0f, cy ), 9.0f, ink ( owner == expanded_class ? tone::ACCENT : tone::INK_FAINT ) );
							c.text ( info.name.get ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 24.0f, cy ), ink ( tone::TYPE ), render::align::LEFT, row.w - 96.0f );
							count_text.number ( info.methods ).add ( info.methods == 1 ? L" virtual" : L" virtuals" );
							c.text ( count_text.text ( ), render::font::SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT );

						} else if ( child <= info.base_count ) {

							c.text ( L":", render::font::MONO_SMALL, math::vec2 ( row.x + 26.0f, cy ), ink ( tone::INK_FAINT ) );
							c.text ( db->class_bases [ static_cast < size_t > ( info.first_base + child - 1 ) ].get ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 38.0f, cy ), ink ( tone::TYPE ).fade ( 0.75f ), render::align::LEFT, row.w - 46.0f );

						} else {

							int32_t slot_index = child - 1 - info.base_count;
							uint64_t slot = info.vtable + static_cast < uint64_t > ( slot_index ) * ( wide ? 8 : 4 );
							uint64_t offset = 0;
							uint64_t target = 0;
							wchar_t name [ 200 ];
							base::writer label;

							if ( file.offset_of ( slot, offset ) )
								target = wide ? file.read64 ( offset ) : file.read32 ( offset );

							label.add ( L"[" ).number ( slot_index ).add ( L"]" );
							c.text ( label.text ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 26.0f, cy ), ink ( tone::INK_FAINT ) );

							if ( symbol_name ( target, true, name, 200 ) )
								c.text ( name, render::font::MONO_SMALL, math::vec2 ( row.x + 62.0f, cy ), ink ( tone::FUNCTION ), render::align::LEFT, row.w - 70.0f );

						}

						break;

					}

					case panel::STRUCTS: {

						if ( value == -2 ) {

							c.icon ( icons::ADD, math::vec2 ( row.x + 12.0f, cy ), 10.0f, ink ( tone::ACCENT ) );
							c.text ( L"New struct", render::font::UI, math::vec2 ( row.x + 26.0f, cy ), ink ( tone::ACCENT ) );

							break;

						}

						int32_t owner = value / 4096;
						int32_t child = value % 4096;

						if ( owner >= structs.count ( ) )
							break;

						const user_struct& info = structs [ static_cast < size_t > ( owner ) ];

						if ( child == 0 ) {

							base::writer size_text;

							c.icon ( owner == expanded_struct ? icons::CHEVRON_DOWN : icons::CHEVRON_RIGHT, math::vec2 ( row.x + 11.0f, cy ), 9.0f, ink ( owner == expanded_struct ? tone::ACCENT : tone::INK_FAINT ) );
							c.text ( info.name.get ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 24.0f, cy ), ink ( tone::TYPE ), render::align::LEFT, row.w - 120.0f );
							size_text.add ( L"0x" ).hex ( static_cast < uint64_t > ( struct_size ( owner ) ) ).add ( L" bytes" );
							c.text ( size_text.text ( ), render::font::SMALL, math::vec2 ( row.right ( ) - 34.0f, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT );
							c.icon ( icons::PENCIL, math::vec2 ( row.right ( ) - 16.0f, cy ), 10.0f, ink ( tone::INK_FAINT ) );

						} else if ( child == 4095 ) {

							c.icon ( icons::ADD, math::vec2 ( row.x + 30.0f, cy ), 9.0f, ink ( tone::INK_FAINT ) );
							c.text ( L"add field", render::font::SMALL, math::vec2 ( row.x + 42.0f, cy ), ink ( tone::INK_FAINT ) );

						} else if ( child - 1 < info.fields.count ( ) ) {

							const user_field& field = info.fields [ static_cast < size_t > ( child - 1 ) ];
							base::writer where;

							where.add ( L"+" ).hex ( static_cast < uint64_t > ( field.offset ) );
							c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 26.0f, cy ), ink ( tone::INK_FAINT ) );
							c.text ( field.type.get ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 70.0f, cy ), ink ( tone::TYPE ), render::align::LEFT, 100.0f );
							c.text ( field.name.get ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 176.0f, cy ), ink ( tone::GLOBAL ), render::align::LEFT, row.w - 184.0f );

						}

						break;

					}

					case panel::REFERENCES: {

						const analysis::xref& item = reference_list [ static_cast < size_t > ( value ) ];
						uint64_t source = file.base + item.source;
						int32_t owner = db ? db->function_at ( source ) : -1;
						wchar_t name [ 200 ];
						base::writer label;
						tone kind_tone = item.type == analysis::ref::WRITE ? tone::DANGER : ( item.type == analysis::ref::CALL ? tone::FUNCTION : ( item.type == analysis::ref::READ ? tone::SUCCESS : tone::INK_SOFT ) );

						if ( owner >= 0 ) {

							function_name ( owner, name, 200 );
							label.add ( name );

							if ( source != db->functions [ static_cast < size_t > ( owner ) ].start )
								label.add ( L"+" ).hex ( source - db->functions [ static_cast < size_t > ( owner ) ].start );

						} else
							label.hex ( source );

						c.text ( ref_word ( item.type ), render::font::TINY, math::vec2 ( row.x + 8.0f, cy ), ink ( kind_tone ) );
						c.text ( label.text ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 58.0f, cy ), ink ( tone::INK ), render::align::LEFT, row.w - 66.0f );

						break;

					}

					case panel::SEARCH: {

						const search_hit& item = search_hits [ static_cast < size_t > ( value ) ];
						int32_t owner = db && analysis_applied ? db->function_at ( item.address ) : -1;
						wchar_t name [ 200 ];
						base::writer label;

						where.hex ( item.address, wide ? 9 : 8 );

						if ( !item.note.empty ( ) )
							label.add ( item.note.get ( ) );
						else if ( owner >= 0 ) {

							function_name ( owner, name, 200 );
							label.add ( name ).add ( L"+" ).hex ( item.address - db->functions [ static_cast < size_t > ( owner ) ].start );

						} else {

							int32_t section = file.section_at ( item.address );

							label.add ( section >= 0 ? file.sections [ static_cast < size_t > ( section ) ].name : L"?" );

						}

						c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 8.0f, cy ), ink ( tone::ADDRESS ) );
						c.text ( label.text ( ), render::font::MONO_SMALL, math::vec2 ( row.x + 98.0f, cy ), ink ( item.note.empty ( ) ? tone::INK : tone::INK_SOFT ), render::align::LEFT, row.w - 106.0f );


						break;

					}

					default: {

						const loader::section& item = file.sections [ static_cast < size_t > ( value ) ];
						render::color chip = item.type == loader::kind::CODE ? ink ( tone::CODE_BAND ) : ( item.type == loader::kind::CONSTANT ? ink ( tone::CONST_BAND ) : ( item.type == loader::kind::EMPTY ? ink ( tone::EMPTY_BAND ) : ( item.type == loader::kind::HEADER ? ink ( tone::HEADER_BAND ) : ink ( tone::DATA_BAND ) ) ) );
						wchar_t perms [ 4 ] = { item.read ? L'R' : L'\x00B7', item.write ? L'W' : L'\x00B7', item.execute ? L'X' : L'\x00B7', 0 };

						c.fill ( math::rect ( row.x + 8.0f, cy - 6.0f, 3.0f, 12.0f ), 1.0f, chip );
						c.text ( item.name, render::font::MONO_STRONG, math::vec2 ( row.x + 20.0f, cy ), ink ( tone::INK ) );
						c.text ( perms, render::font::MONO_SMALL, math::vec2 ( row.x + 104.0f, cy ), ink ( tone::INK_SOFT ) );
						where.hex ( item.address, wide ? 9 : 8 );
						c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( row.right ( ) - 8.0f, cy ), ink ( tone::ADDRESS ), render::align::RIGHT );

						break;

					}

				}

			}

		}

		c.pop_clip ( );
		draw_scrollbar ( make_id ( "browser_bar", slot ), area, scroll, content, area.h );

	}

}