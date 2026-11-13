#include "ui/app.hh"
#include "ui/icons.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	static int32_t lower_name ( wchar_t value ) {

		return value >= L'A' && value <= L'Z' ? value + 32 : value;

	}

	static bool contains_text ( const wchar_t* haystack, const wchar_t* needle ) {

		if ( !needle [ 0 ] )
			return true;

		for ( int32_t i = 0; haystack [ i ]; i++ ) {

			int32_t k = 0;

			while ( needle [ k ] && haystack [ i + k ] && lower_name ( haystack [ i + k ] ) == lower_name ( needle [ k ] ) )
				k++;

			if ( !needle [ k ] )
				return true;

		}

		return false;

	}

	static void string_preview ( const loader::binary& source, const loader::found_string& item, wchar_t* out, int32_t room ) {

		int32_t length = base::min ( static_cast < int32_t > ( item.length ), room - 1 );

		for ( int32_t i = 0; i < length; i++ )
			out [ i ] = static_cast < wchar_t > ( source.byte ( item.offset + static_cast < uint64_t > ( i ) * ( item.wide ? 2 : 1 ) ) );

		out [ length ] = 0;

	}

	void app::refresh_filter ( panel which ) {

		int32_t slot = static_cast < int32_t > ( which );
		base::list < int32_t >& list = shown [ slot ];
		const wchar_t* needle = filters [ slot ].text;
		wchar_t buffer [ 200 ];

		list.clear ( );
		filter_seen [ slot ] = filters [ slot ].revision;
		browser_pick [ slot ] = -1;

		if ( !file.loaded ( ) )
			return;

		switch ( which ) {

			case panel::FUNCTIONS:

				if ( db && analysis_applied && !db->functions.empty ( ) ) {

					for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

						if ( needle [ 0 ] ) {

							function_name ( i, buffer, 200 );

							if ( !contains_text ( buffer, needle ) )
								continue;

						}

						list.push ( i );

					}

				} else {

					for ( int32_t i = 0; i < file.functions.count ( ); i++ ) {

						if ( contains_text ( file.functions [ static_cast < size_t > ( i ) ].name.get ( ), needle ) )
							list.push ( i );

					}

				}

				break;

			case panel::IMPORTS:

				for ( int32_t m = 0; m < file.modules.count ( ); m++ ) {

					const loader::module& owner = file.modules [ static_cast < size_t > ( m ) ];
					bool header = false;
					bool module_match = contains_text ( owner.name.get ( ), needle );

					for ( int32_t i = owner.first; i < owner.first + owner.count; i++ ) {

						const loader::import& item = file.imports [ static_cast < size_t > ( i ) ];

						if ( !module_match && !contains_text ( item.name.get ( ), needle ) )
							continue;

						if ( !header ) {

							list.push ( -( m + 1 ) );
							header = true;

						}

						list.push ( i );

					}

					if ( !header && module_match )
						list.push ( -( m + 1 ) );

				}

				break;

			case panel::EXPORTS:

				for ( int32_t i = 0; i < file.exports.count ( ); i++ ) {

					if ( contains_text ( file.exports [ static_cast < size_t > ( i ) ].name.get ( ), needle ) )
						list.push ( i );

				}

				break;

			case panel::STRINGS:

				for ( int32_t i = 0; i < file.strings.count ( ); i++ ) {

					if ( needle [ 0 ] ) {

						string_preview ( file, file.strings [ static_cast < size_t > ( i ) ], buffer, 160 );

						if ( !contains_text ( buffer, needle ) )
							continue;

					}

					list.push ( i );

				}

				break;

			case panel::SECTIONS:

				for ( int32_t i = 0; i < file.sections.count ( ); i++ ) {

					if ( contains_text ( file.sections [ static_cast < size_t > ( i ) ].name, needle ) )
						list.push ( i );

				}

				break;

			case panel::CLASSES:

				if ( !db || !analysis_applied )
					break;

				for ( int32_t i = 0; i < db->classes.count ( ); i++ ) {

					const analysis::class_info& item = db->classes [ static_cast < size_t > ( i ) ];

					if ( !contains_text ( item.name.get ( ), needle ) )
						continue;

					list.push ( i * 8192 );

					if ( i != expanded_class )
						continue;

					for ( int32_t b = 0; b < item.base_count; b++ )
						list.push ( i * 8192 + 1 + b );

					for ( int32_t m = 0; m < item.methods && m < 4096; m++ )
						list.push ( i * 8192 + 1 + item.base_count + m );

				}

				break;

			case panel::STRUCTS:

				list.push ( -2 );

				for ( int32_t i = 0; i < structs.count ( ); i++ ) {

					const user_struct& item = structs [ static_cast < size_t > ( i ) ];

					if ( !contains_text ( item.name.get ( ), needle ) )
						continue;

					list.push ( i * 4096 );

					if ( i != expanded_struct )
						continue;

					for ( int32_t f = 0; f < item.fields.count ( ) && f < 4000; f++ )
						list.push ( i * 4096 + 1 + f );

					list.push ( i * 4096 + 4095 );

				}

				break;

			case panel::REFERENCES:

				for ( int32_t i = 0; i < reference_list.count ( ); i++ ) {

					if ( needle [ 0 ] ) {

						uint64_t source = file.base + reference_list [ static_cast < size_t > ( i ) ].source;
						int32_t owner = db ? db->function_at ( source ) : -1;

						if ( owner < 0 )
							continue;

						function_name ( owner, buffer, 200 );

						if ( !contains_text ( buffer, needle ) )
							continue;

					}

					list.push ( i );

				}

				break;

			case panel::SEARCH:

				for ( int32_t i = 0; i < search_hits.count ( ); i++ )
					list.push ( i );

				break;

			case panel::NOTES: {

				const base::list < user_name >* sources [ 3 ] = { &notes, &bookmarks, &comments };

				notes_seen = annotation_revision;

				for ( int32_t kind = 0; kind < 3; kind++ ) {

					const base::list < user_name >& source = *sources [ kind ];

					for ( int32_t i = 0; i < source.count ( ) && i < 0x100000; i++ ) {

						const user_name& item = source [ static_cast < size_t > ( i ) ];

						if ( needle [ 0 ] && !contains_text ( item.text.get ( ), needle ) ) {

							int32_t owner = db && analysis_applied ? db->function_at ( item.address ) : -1;

							if ( owner < 0 )
								continue;

							function_name ( owner, buffer, 200 );

							if ( !contains_text ( buffer, needle ) )
								continue;

						}

						list.push ( kind * 0x100000 + i );

					}

				}

				break;

			}

			default:
				break;

		}

		sort_rows ( which );

	}

	void app::sort_rows ( panel which ) {

		int32_t slot = static_cast < int32_t > ( which );
		int32_t column = sort_by [ slot ];
		base::list < int32_t >& list = shown [ slot ];

		if ( !column || list.count ( ) < 2 )
			return;

		bool analyzed = db && analysis_applied && !db->functions.empty ( );
		bool by_text = column == 1;
		base::list < uint64_t > numbers;
		base::list < base::text > texts;
		base::list < int32_t > order;
		wchar_t buffer [ 200 ];

		if ( which == panel::FUNCTIONS && !analyzed )
			return;

		if ( which != panel::FUNCTIONS && which != panel::EXPORTS && which != panel::STRINGS && which != panel::SECTIONS )
			return;

		order.resize ( list.size ( ) );

		if ( by_text )
			texts.resize ( list.size ( ) );
		else
			numbers.resize ( list.size ( ) );

		for ( int32_t i = 0; i < list.count ( ); i++ ) {

			int32_t value = list [ static_cast < size_t > ( i ) ];

			order [ static_cast < size_t > ( i ) ] = i;

			switch ( which ) {

				case panel::FUNCTIONS: {

					const analysis::function_info& info = db->functions [ static_cast < size_t > ( value ) ];

					if ( by_text ) {

						function_name ( value, buffer, 200 );
						texts [ static_cast < size_t > ( i ) ].set ( buffer );

					} else
						numbers [ static_cast < size_t > ( i ) ] = column == 2 ? info.start : info.end - info.start;

					break;

				}

				case panel::EXPORTS: {

					const loader::exported& item = file.exports [ static_cast < size_t > ( value ) ];

					if ( by_text )
						texts [ static_cast < size_t > ( i ) ].set ( item.name.get ( ) );
					else
						numbers [ static_cast < size_t > ( i ) ] = item.address;

					break;

				}

				case panel::STRINGS: {

					const loader::found_string& item = file.strings [ static_cast < size_t > ( value ) ];

					if ( by_text ) {

						string_preview ( file, item, buffer, 160 );
						texts [ static_cast < size_t > ( i ) ].set ( buffer );

					} else
						numbers [ static_cast < size_t > ( i ) ] = item.offset;

					break;

				}

				default: {

					const loader::section& item = file.sections [ static_cast < size_t > ( value ) ];

					if ( by_text )
						texts [ static_cast < size_t > ( i ) ].set ( item.name );
					else
						numbers [ static_cast < size_t > ( i ) ] = column == 2 ? ( item.read ? 4u : 0u ) | ( item.write ? 2u : 0u ) | ( item.execute ? 1u : 0u ) : item.address;

					break;

				}

			}

		}

		bool down = sort_down [ slot ];

		base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t a, int32_t b ) {

			if ( by_text ) {

				int32_t compared = base::compare_folded ( texts [ static_cast < size_t > ( a ) ].get ( ), texts [ static_cast < size_t > ( b ) ].get ( ) );

				return down ? compared > 0 : compared < 0;

			}

			uint64_t x = numbers [ static_cast < size_t > ( a ) ];
			uint64_t y = numbers [ static_cast < size_t > ( b ) ];

			return down ? x > y : x < y;

		} );

		base::list < int32_t > sorted;

		sorted.resize ( list.size ( ) );

		for ( int32_t i = 0; i < list.count ( ); i++ )
			sorted [ static_cast < size_t > ( i ) ] = list [ static_cast < size_t > ( order [ static_cast < size_t > ( i ) ] ) ];

		list = base::move ( sorted );

	}

	void app::refresh_references ( ) {

		if ( !db || !analysis_applied || !file.loaded ( ) )
			return;

		uint64_t subject = reference_target != ~0ull ? reference_target : now->cursor_address;
		int32_t first = 0;
		int32_t count = 0;

		if ( showing == view::PSEUDOCODE && reference_target == ~0ull )
			subject = now->function;

		db->refs_to ( subject, first, count );

		if ( !count && reference_target == ~0ull ) {

			int32_t owner = db->function_at ( subject );

			if ( owner >= 0 ) {

				subject = db->functions [ static_cast < size_t > ( owner ) ].start;
				db->refs_to ( subject, first, count );

			}

		}

		if ( subject == references_for && references_revision == names_revision )
			return;

		references_for = subject;
		references_revision = names_revision;
		reference_list.clear ( );

		for ( int32_t i = 0; i < count && i < 20000; i++ )
			reference_list.push ( db->refs [ static_cast < size_t > ( first + i ) ] );

		browser_scroll [ static_cast < int32_t > ( panel::REFERENCES ) ] = scroller ( );
		refresh_filter ( panel::REFERENCES );

	}

	static unsigned long __stdcall analysis_entry ( void* parameter ) {

		app* owner = static_cast < app* > ( parameter );
		analysis::database* target = owner->building ? owner->building : owner->db;

		target->build ( owner->file, &owner->analysis_state );
		__atomic_store_n ( &owner->analysis_done, 1, __ATOMIC_RELEASE );

		if ( owner->window )
			PostMessageW ( static_cast < HWND > ( owner->window ), WM_APP + 1, 0, 0 );

		return 0;

	}

	void app::start_analysis ( ) {

		if ( !db )
			db = new analysis::database ( );

		analysis_done = 0;
		analysis_applied = false;
		analysis_clock = base::seconds ( );
		analysis_state.stage = 0;
		analysis_state.done = 0;
		analysis_state.total = 0;

		if ( !analysis_sync && window )
			analysis_thread = CreateThread ( nullptr, 0, analysis_entry, this, 0, nullptr );

		if ( !analysis_thread ) {

			db->build ( file, &analysis_state );
			analysis_done = 1;
			analysis_finished ( );

		}

	}

	void app::reanalyze ( ) {

		code_cache.clear ( );
		stack_seen.clear ( );
		stack_texts.clear ( );
		calls_for = ~0ull;
		names_revision++;

		if ( !db || !file.loaded ( ) || ( !analysis_applied && !analysis_thread ) )
			return;

		if ( building || !analysis_applied ) {

			rebuild_pending = true;

			return;

		}

		wait_analysis ( );
		building = new analysis::database ( );
		analysis_done = 0;
		analysis_state.stage = 0;
		analysis_state.done = 0;
		analysis_state.total = 0;

		if ( !analysis_sync )
			analysis_thread = CreateThread ( nullptr, 0, analysis_entry, this, 0, nullptr );

		if ( !analysis_thread ) {

			building->build ( file, &analysis_state );
			analysis_done = 1;
			analysis_finished ( );

		}

	}

	void app::wait_analysis ( ) {

		if ( !analysis_thread )
			return;

		WaitForSingleObject ( static_cast < HANDLE > ( analysis_thread ), INFINITE );
		CloseHandle ( static_cast < HANDLE > ( analysis_thread ) );
		analysis_thread = nullptr;

	}

	void app::analysis_finished ( ) {

		if ( !__atomic_load_n ( &analysis_done, __ATOMIC_ACQUIRE ) || !file.loaded ( ) )
			return;

		if ( building ) {

			wait_analysis ( );

			base::list < uint64_t > rows;

			for ( document& item : documents )
				rows.push ( item.used && item.kind == view::LISTING ? item.state.cursor_row : 0 );

			delete db;
			db = building;
			building = nullptr;
			code_cache.clear ( );
			stack_seen.clear ( );
			stack_texts.clear ( );
			calls_for = ~0ull;
			names_revision++;
			references_for = ~0ull;
			build_listing ( );
			refresh_filter ( panel::FUNCTIONS );
			refresh_filter ( panel::CLASSES );

			view_state* held = now;

			for ( int32_t i = 0; i < 16; i++ ) {

				document& item = documents [ i ];

				if ( !item.used || item.kind != view::LISTING )
					continue;

				now = &item.state;

				float shift = static_cast < float > ( static_cast < int64_t > ( row_of ( now->cursor_address ) ) - static_cast < int64_t > ( rows [ static_cast < size_t > ( i ) ] ) );

				now->listing_scroll.offset += shift;
				now->listing_scroll.target += shift;
				now->listing_scroll.shown += shift;

			}

			now = held;
			log ( log_kind::INFO, base::writer ( ).add ( L"Re-analyzed in " ).fixed ( db->seconds * 1000.0, 1 ).add ( L" ms." ).text ( ) );
			pending = base::max ( pending, 4 );

			if ( rebuild_pending ) {

				rebuild_pending = false;
				reanalyze ( );

			}

			return;

		}

		if ( analysis_applied )
			return;

		wait_analysis ( );
		analysis_applied = true;
		build_listing ( );
		refresh_filter ( panel::FUNCTIONS );
		refresh_filter ( panel::CLASSES );
		references_for = ~0ull;

		view_state* held = now;

		for ( int32_t i = 0; i < 16; i++ ) {

			document& item = documents [ i ];

			if ( !item.used || item.kind != view::LISTING )
				continue;

			now = &item.state;

			uint64_t address = now->cursor_address;

			set_cursor_row ( row_of ( address ), true );
			now->cursor_address = db->is_covered ( address ) ? db->instruction_at ( address ) : address;
			now->listing_scroll.offset = now->listing_scroll.target;
			now->cursor_ready = false;

		}

		now = held;

		base::writer line;

		if ( db->maps.empty ( ) )
			line.add ( L"No x86 code to analyze in this image." );
		else
			line.add ( L"Analyzed " ).grouped ( db->decoded ).add ( L" instructions into " ).grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L" functions in " ).fixed ( db->seconds * 1000.0, 1 ).add ( L" ms on " ).number ( db->threads ).add ( db->threads == 1 ? L" thread" : L" threads" );

		log ( db->maps.empty ( ) ? log_kind::WARN : log_kind::GOOD, line.text ( ) );

		if ( !db->maps.empty ( ) ) {

			uint64_t cases = 0;

			for ( const analysis::jump_table& item : db->tables )
				cases += item.count;

			line.clear ( );
			line.grouped ( static_cast < uint64_t > ( db->refs.count ( ) ) ).add ( L" cross-references, " ).grouped ( static_cast < uint64_t > ( db->tables.count ( ) ) ).add ( L" switch tables with " ).grouped ( cases ).add ( L" cases, " ).grouped ( total_rows ).add ( L" listing rows" );
			log ( log_kind::INFO, line.text ( ) );

		}

		pending = base::max ( pending, 4 );

		if ( rebuild_pending ) {

			rebuild_pending = false;
			reanalyze ( );

		}

	}

	int32_t app::pane_document ( int32_t index ) const {

		if ( index < 0 || index >= pane_count )
			return -1;

		const pane& item = panes [ index ];

		return item.count ? item.tabs [ item.active ] : -1;

	}

	int32_t app::current_document ( ) const {

		return pane_document ( focused_pane );

	}

	void app::select_state ( int32_t document_index ) {

		if ( document_index < 0 ) {

			now = &spare_state;
			showing = view::LISTING;

			return;

		}

		now = &documents [ document_index ].state;
		showing = documents [ document_index ].kind;

	}

	void app::focus_document ( int32_t target_pane, int32_t tab ) {

		if ( target_pane < 0 || target_pane >= pane_count )
			return;

		pane& item = panes [ target_pane ];
		int32_t before = pane_document ( target_pane );

		focused_pane = target_pane;
		item.active = base::clamp ( tab, 0, base::max ( item.count - 1, 0 ) );

		int32_t after = pane_document ( target_pane );

		if ( after >= 0 && after != before )

		select_state ( after );
		pending = base::max ( pending, 2 );

	}

	int32_t app::open_document ( view kind, int32_t target_pane, uint64_t address ) {

		pane& item = panes [ target_pane ];
		int32_t slot = -1;

		for ( int32_t i = 0; i < 16 && slot < 0; i++ ) {

			if ( !documents [ i ].used )
				slot = i;

		}

		if ( slot < 0 || item.count >= 16 ) {

			log ( log_kind::WARN, L"That's the tab limit. Close a tab first (Ctrl+W)." );

			return -1;

		}

		document& made = documents [ slot ];

		made = document ( );
		made.used = true;
		made.kind = kind;
		made.serial = next_serial++;
		made.state.cursor_address = address;

		if ( db && analysis_applied ) {

			int32_t owner = db->function_at ( address );

			if ( owner >= 0 )
				made.state.function = db->functions [ static_cast < size_t > ( owner ) ].start;

		}

		item.tabs [ item.count++ ] = slot;
		focus_document ( target_pane, item.count - 1 );

		if ( kind == view::LISTING && total_rows ) {

			set_cursor_row ( row_of ( address ), true );
			now->cursor_address = address;
			now->listing_scroll.offset = now->listing_scroll.target;

		} else if ( kind == view::HEX ) {

			uint64_t offset = 0;

			if ( file.offset_of ( address, offset ) ) {

				go_offset ( offset );
				now->hex_scroll.offset = now->hex_scroll.target;

			}

		}

		return slot;

	}

	void app::close_document ( int32_t target_pane, int32_t tab ) {

		pane& item = panes [ target_pane ];

		if ( tab < 0 || tab >= item.count )
			return;

		int32_t open = 0;

		for ( int32_t p = 0; p < pane_count; p++ )
			open += panes [ p ].count;

		if ( open <= 1 )
			return;

		documents [ item.tabs [ tab ] ].used = false;

		for ( int32_t i = tab; i + 1 < item.count; i++ )
			item.tabs [ i ] = item.tabs [ i + 1 ];

		item.count--;

		if ( item.active > tab || item.active >= item.count )
			item.active = base::max ( item.active - 1, 0 );

		if ( !item.count && pane_count == 2 ) {

			if ( target_pane == 0 )
				panes [ 0 ] = panes [ 1 ];

			panes [ 1 ] = pane ( );
			pane_count = 1;
			focused_pane = 0;

		}

		focus_document ( base::min ( focused_pane, pane_count - 1 ), panes [ base::min ( focused_pane, pane_count - 1 ) ].active );

	}

	void app::open_pseudocode ( uint64_t address, bool other_pane ) {

		if ( !db || !analysis_applied )
			return;

		int32_t owner = db->function_at ( address );

		if ( owner < 0 )
			return;

		uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;
		int32_t target = other_pane && pane_count > 1 ? 1 - focused_pane : focused_pane;
		pane& item = panes [ target ];

		for ( int32_t i = 0; i < item.count; i++ ) {

			document& candidate = documents [ item.tabs [ i ] ];

			if ( candidate.kind == view::PSEUDOCODE && candidate.state.function == start ) {

				focus_document ( target, i );
				now->cursor_address = address;

				return;

			}

		}

		int32_t slot = open_document ( view::PSEUDOCODE, target, address );

		if ( slot >= 0 ) {

			documents [ slot ].state.function = start;
			documents [ slot ].state.cursor_address = address;

		}

	}

	struct code_names : decompiler::namer {

		app* owner = nullptr;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

			return owner->symbol_name ( address, code, out, room );

		}

		bool literal ( uint64_t address, wchar_t* out, int32_t room ) override {

			uint64_t offset = 0;
			int32_t length = 0;

			if ( !owner->file.offset_of ( address, offset ) )
				return false;

			bool wide = offset + 3 < owner->file.size && owner->file.byte ( offset + 1 ) == 0 && owner->file.byte ( offset ) >= 0x20 && owner->file.byte ( offset + 3 ) == 0;
			uint64_t step = wide ? 2 : 1;

			for ( uint64_t i = 0; length < room - 8 && i < 400; i++ ) {

				uint64_t at = offset + i * step;

				if ( at >= owner->file.size )
					return false;

				wchar_t c = wide ? static_cast < wchar_t > ( owner->file.read16 ( at ) ) : static_cast < wchar_t > ( owner->file.byte ( at ) );

				if ( !c )
					break;

				if ( c == L'\n' ) {

					out [ length++ ] = L'\\';
					out [ length++ ] = L'n';

				} else if ( c == L'\r' ) {

					out [ length++ ] = L'\\';
					out [ length++ ] = L'r';

				} else if ( c == L'\t' ) {

					out [ length++ ] = L'\\';
					out [ length++ ] = L't';

				} else if ( c == L'"' || c == L'\\' ) {

					out [ length++ ] = L'\\';
					out [ length++ ] = c;

				} else if ( c < 0x20 || c > 0x7E )
					return false;
				else
					out [ length++ ] = c;

			}

			if ( length < 2 )
				return false;

			out [ length ] = 0;

			return true;

		}

		const wchar_t* local_name ( uint64_t function, const wchar_t* key ) override {

			const local_entry* found = owner->local_for ( function, key );

			return found && !found->name.empty ( ) ? found->name.get ( ) : nullptr;

		}

		const wchar_t* local_type ( uint64_t function, const wchar_t* key ) override {

			const local_entry* found = owner->local_for ( function, key );

			return found && !found->type.empty ( ) ? found->type.get ( ) : nullptr;

		}

		const wchar_t* prototype ( uint64_t function ) override {

			return owner->prototype_for ( function );

		}

		bool spaced ( ) override {

			return owner->c_spaced;

		}

		bool expand_ternary ( ) override {

			return owner->c_no_ternary;

		}

		bool struct_field ( const wchar_t* type, int64_t offset, wchar_t* out, int32_t room, uint64_t& tag, int32_t& size ) override {

			int32_t index = owner->struct_named ( type );

			if ( index < 0 )
				return false;

			for ( const user_field& item : owner->structs [ static_cast < size_t > ( index ) ].fields ) {

				if ( item.offset != offset )
					continue;

				int32_t k = 0;

				for ( const wchar_t* at = item.name.get ( ); *at && *at != L'[' && k < room - 1; at++ )
					out [ k++ ] = *at;

				out [ k ] = 0;
				size = item.size;
				tag = ( 1ull << 63 ) | ( static_cast < uint64_t > ( index ) << 32 ) | static_cast < uint32_t > ( offset );

				return true;

			}

			return false;

		}

		const wchar_t* comment ( uint64_t address ) override {

			return owner->comment_at ( address );

		}

	};

	const decompiler::result* app::pseudocode_for ( uint64_t function ) {

		if ( !db || !analysis_applied )
			return nullptr;

		decompiler::result* found = code_cache.find ( function, names_revision );

		if ( found )
			return found;

		decompiler::result* made = code_cache.make ( function );
		code_names bridge;

		bridge.owner = this;
		made->revision = names_revision;
		decompiler::decompile ( *db, function, bridge, *made );

		return made;

	}

	bool app::open_file ( const wchar_t* path ) {

		base::writer problem;
		double started = base::seconds ( );
		int32_t length = 0;
		wchar_t binary [ 600 ];
		base::text from_project;

		while ( path [ length ] )
			length++;

		if ( length > 6 && base::same_text ( path + length - 6, L".plumb" ) ) {

			base::list < char > data;
			int32_t made = 0;

			binary [ 0 ] = 0;

			if ( files::read ( path, data ) && data.count ( ) > 5 && data [ 0 ] == 'f' && data [ 1 ] == 'i' && data [ 2 ] == 'l' && data [ 3 ] == 'e' && data [ 4 ] == ' ' ) {

				int32_t end = 5;

				while ( end < data.count ( ) && data [ static_cast < size_t > ( end ) ] != '\n' && data [ static_cast < size_t > ( end ) ] != '\r' && data [ static_cast < size_t > ( end ) ] )
					end++;

				made = MultiByteToWideChar ( CP_UTF8, 0, data.data ( ) + 5, end - 5, binary, 599 );
				binary [ made > 0 ? made : 0 ] = 0;

			}

			if ( !binary [ 0 ] || GetFileAttributesW ( binary ) == INVALID_FILE_ATTRIBUTES ) {

				int32_t k = 0;

				for ( ; k < length - 6 && k < 590; k++ )
					binary [ k ] = path [ k ];

				binary [ k ] = 0;

				if ( GetFileAttributesW ( binary ) == INVALID_FILE_ATTRIBUTES ) {

					static const wchar_t* const kinds [ 8 ] = { L".exe", L".dll", L".sys", L".ocx", L".cpl", L".scr", L".efi", L".bin" };

					for ( const wchar_t* kind : kinds ) {

						int32_t at = k;

						for ( const wchar_t* c = kind; *c; c++ )
							binary [ at++ ] = *c;

						binary [ at ] = 0;

						if ( GetFileAttributesW ( binary ) != INVALID_FILE_ATTRIBUTES )
							break;

						binary [ k ] = 0;

					}

				}

			}

			from_project.set ( path );
			path = binary;

		}

		if ( !confirm_discard ( ) )
			return false;

		close_file ( );

		if ( !file.open ( path, problem ) ) {

			load_problem.set ( problem.text ( ) );
			load_time = clock;
			log ( log_kind::FAIL, base::writer ( ).add ( L"Couldn't load " ).add ( path ).add ( L". " ).add ( problem.text ( ) ).text ( ) );

			return false;

		}

		load_problem.set ( L"" );
		string_addresses.clear ( );
		string_order.clear ( );
		analysis_applied = false;
		build_listing ( );

		for ( int32_t i = 0; i < static_cast < int32_t > ( panel::COUNT ); i++ ) {

			filters [ i ] = field ( );
			browser_scroll [ i ] = scroller ( );
			refresh_filter ( static_cast < panel > ( i ) );

		}

		for ( document& item : documents )
			item = document ( );

		panes [ 0 ] = pane ( );
		panes [ 1 ] = pane ( );
		pane_count = 1;
		focused_pane = 0;
		open_document ( view::LISTING, 0, file.entry ? file.entry : file.low ( ) );
		remember_recent ( path );
		project_file.set ( from_project.empty ( ) ? L"" : from_project.get ( ) );
		load_project ( );

		base::writer line;

		line.add ( L"Loaded " ).add ( file.name.get ( ) ).add ( L" (" ).grouped ( file.size ).add ( L" bytes) in " ).fixed ( ( base::seconds ( ) - started ) * 1000.0, 1 ).add ( L" ms" );
		log ( log_kind::GOOD, line.text ( ) );

		for ( const base::text& note : file.notes )
			log ( log_kind::INFO, note.get ( ) );

		line.clear ( );
		line.grouped ( static_cast < uint64_t > ( file.imports.count ( ) ) ).add ( L" imports from " ).number ( file.modules.count ( ) ).add ( L" modules, " ).grouped ( static_cast < uint64_t > ( file.exports.count ( ) ) ).add ( L" exports, " ).grouped ( static_cast < uint64_t > ( file.strings.count ( ) ) ).add ( L" strings" );
		log ( log_kind::INFO, line.text ( ) );

		if ( !file.debug_path.empty ( ) )
			log ( log_kind::INFO, base::writer ( ).add ( L"Debug info points to " ).add ( file.debug_path.get ( ) ).text ( ) );

		if ( file.arch == loader::machine::ARM64 || file.arch == loader::machine::ARM )
			log ( log_kind::WARN, L"This is an ARM image. Plumb decodes x86 and x86-64 only, so code is shown as bytes." );

		if ( file.entry )
			go ( file.entry, false );
		else
			set_cursor_row ( 0, false );

		now->listing_scroll.offset = now->listing_scroll.target;
		now->hex_scroll.offset = now->hex_scroll.target;
		start_analysis ( );
		pending = base::max ( pending, 4 );

		return true;

	}

	void app::close_file ( ) {

		if ( !file.loaded ( ) )
			return;

		wait_analysis ( );

		if ( building ) {

			delete building;
			building = nullptr;

		}

		rebuild_pending = false;
		patches.clear ( );
		stack_seen.clear ( );
		stack_texts.clear ( );
		call_nodes.clear ( );
		calls_for = ~0ull;

		if ( autosave )
			save_project ( );

		dirty = false;
		log ( log_kind::INFO, base::writer ( ).add ( L"Closed " ).add ( file.name.get ( ) ).text ( ) );

		if ( db )
			db->clear ( );

		code_cache.clear ( );
		analysis_applied = false;
		analysis_done = 0;
		file.close ( );
		items.clear ( );
		names.clear ( );
		string_addresses.clear ( );
		string_order.clear ( );
		renamed.clear ( );
		comments.clear ( );
		bookmarks.clear ( );
		notes.clear ( );
		reference_list.clear ( );
		search_hits.clear ( );
		total_rows = 0;

		for ( document& item : documents )
			item = document ( );

		panes [ 0 ] = pane ( );
		panes [ 1 ] = pane ( );
		pane_count = 1;
		focused_pane = 0;
		select_state ( -1 );
		spare_state = view_state ( );

		for ( int32_t i = 0; i < static_cast < int32_t > ( panel::COUNT ); i++ )
			shown [ i ].clear ( );

	}

}