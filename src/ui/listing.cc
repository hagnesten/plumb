#include "ui/app.hh"
#include "ui/icons.hh"
#include "ui/format.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	struct mark {

		uint64_t address;
		uint64_t size;
		int32_t kind;
		int32_t ref;

	};

	static void sort_marks ( base::list < mark >& items ) {

		size_t count = items.size ( );

		if ( count < 2 )
			return;

		base::list < mark > scratch;

		scratch.resize ( count );

		mark* from = items.data ( );
		mark* to = scratch.data ( );

		for ( size_t width_run = 1; width_run < count; width_run *= 2 ) {

			for ( size_t low = 0; low < count; low += width_run * 2 ) {

				size_t middle = base::min ( low + width_run, count );
				size_t high = base::min ( low + width_run * 2, count );
				size_t a = low;
				size_t b = middle;
				size_t out = low;

				while ( a < middle && b < high ) {

					bool take_a = from [ a ].address < from [ b ].address || ( from [ a ].address == from [ b ].address && from [ a ].kind <= from [ b ].kind );

					to [ out++ ] = take_a ? from [ a++ ] : from [ b++ ];

				}

				while ( a < middle )
					to [ out++ ] = from [ a++ ];

				while ( b < high )
					to [ out++ ] = from [ b++ ];

			}

			mark* held = from;

			from = to;
			to = held;

		}

		if ( from != items.data ( ) ) {

			for ( size_t i = 0; i < count; i++ )
				items [ i ] = from [ i ];

		}

	}

	static bool padding_value ( uint8_t value ) {

		return value == 0xCC || value == 0x90 || value == 0x00;

	}

	void app::push_item ( line_kind kind, int32_t section, uint64_t address, uint64_t size, uint32_t rows, uint8_t stride, int32_t ref, uint64_t& total ) {

		line_item& item = items.emplace ( );

		item.kind = kind;
		item.section = static_cast < uint8_t > ( section );
		item.address = address;
		item.size = size;
		item.stride = stride;
		item.ref = ref;
		item.first_row = total;
		item.rows = rows;
		total += rows;

	}

	void app::build_code_items ( int32_t section, uint64_t& rows ) {

		const analysis::code_map* found = nullptr;

		for ( const analysis::code_map& map : db->maps ) {

			if ( map.section == section )
				found = &map;

		}

		if ( !found )
			return;

		const analysis::code_map& map = *found;
		uint64_t raw_end = map.start + map.decodable;
		uint64_t all_end = map.start + map.size;
		const base::list < analysis::function_info >& functions = db->functions;
		const base::list < analysis::chunk_info >& chunks = db->chunks;
		const base::list < analysis::jump_table >& tables = db->tables;
		int32_t next_function = 0;
		int32_t next_chunk = 0;
		int32_t next_table = 0;
		int32_t open = -1;
		uint64_t pos = map.start;

		while ( next_function < functions.count ( ) && functions [ static_cast < size_t > ( next_function ) ].start < map.start )
			next_function++;

		while ( next_chunk < chunks.count ( ) && chunks [ static_cast < size_t > ( next_chunk ) ].start < map.start )
			next_chunk++;

		auto fill = [ & ] ( uint64_t from, uint64_t to ) {

			while ( from < to ) {

				uint64_t offset = 0;

				if ( !file.offset_of ( from, offset ) ) {

					push_item ( line_kind::EMPTY, section, from, to - from, 1, 1, -1, rows );

					return;

				}

				uint8_t value = file.byte ( offset );

				if ( padding_value ( value ) ) {

					uint64_t run = from;

					while ( run < to && file.byte ( offset + ( run - from ) ) == value )
						run++;

					if ( run - from >= 2 || run == to ) {

						push_item ( line_kind::ALIGN, section, from, run - from, 1, 1, value, rows );
						from = run;

						continue;

					}

				}

				uint64_t probe = from;
				bool nops = db != nullptr;

				while ( nops && probe < to ) {

					x86::instruction ins;

					if ( !db->decode ( probe, ins ) || ins.next ( ) > to || ( ins.name != x86::mnemonic::nop && ins.name != x86::mnemonic::int3 ) )
						nops = false;
					else
						probe = ins.next ( );

				}

				if ( nops && probe == to ) {

					push_item ( line_kind::ALIGN, section, from, to - from, 1, 1, 0x90, rows );

					return;

				}

				uint64_t stop = from + 1;

				while ( stop < to ) {

					uint8_t here = file.byte ( offset + ( stop - from ) );

					if ( padding_value ( here ) && stop + 1 < to && file.byte ( offset + ( stop - from ) + 1 ) == here )
						break;

					stop++;

				}

				push_item ( line_kind::DATA, section, from, stop - from, static_cast < uint32_t > ( ( stop - from + 7 ) / 8 ), 8, -1, rows );
				from = stop;

			}

		};

		auto gap = [ & ] ( uint64_t from, uint64_t to ) {

			while ( from < to ) {

				while ( next_table < tables.count ( ) && tables [ static_cast < size_t > ( next_table ) ].table < from )
					next_table++;

				uint64_t limit = to;

				if ( next_table < tables.count ( ) && tables [ static_cast < size_t > ( next_table ) ].table < to )
					limit = tables [ static_cast < size_t > ( next_table ) ].table;

				if ( from < limit ) {

					fill ( from, limit );
					from = limit;

					continue;

				}

				const analysis::jump_table& table = tables [ static_cast < size_t > ( next_table ) ];
				uint64_t span = base::min ( static_cast < uint64_t > ( table.count ) * table.entry_size, to - from );

				push_item ( line_kind::TABLE, section, from, span, static_cast < uint32_t > ( ( span + table.entry_size - 1 ) / table.entry_size ), table.entry_size, next_table, rows );
				from += span;

				if ( table.index_table == from && table.index_count && from < to ) {

					uint64_t index_span = base::min ( static_cast < uint64_t > ( table.index_count ), to - from );

					push_item ( line_kind::TABLE, section, from, index_span, static_cast < uint32_t > ( ( index_span + 7 ) / 8 ), 8, -next_table - 2, rows );
					from += index_span;

				}

				next_table++;

			}

		};

		while ( pos < raw_end ) {

			if ( open >= 0 && pos >= functions [ static_cast < size_t > ( open ) ].end ) {

				push_item ( line_kind::END, section, pos, 0, 1, 1, open, rows );
				push_item ( line_kind::BLANK, section, pos, 0, 1, 1, -1, rows );
				open = -1;

			}

			while ( next_function < functions.count ( ) && functions [ static_cast < size_t > ( next_function ) ].start < pos )
				next_function++;

			if ( next_function < functions.count ( ) && functions [ static_cast < size_t > ( next_function ) ].start == pos ) {

				if ( open >= 0 ) {

					push_item ( line_kind::END, section, pos, 0, 1, 1, open, rows );
					push_item ( line_kind::BLANK, section, pos, 0, 1, 1, -1, rows );

				} else if ( !items.empty ( ) && items.back ( ).kind != line_kind::BLANK && items.back ( ).kind != line_kind::BANNER )
					push_item ( line_kind::BLANK, section, pos, 0, 1, 1, -1, rows );

				push_item ( line_kind::FUNCTION, section, pos, 0, 1, 1, next_function, rows );
				open = next_function;
				next_function++;

			}

			while ( next_chunk < chunks.count ( ) && chunks [ static_cast < size_t > ( next_chunk ) ].start < pos )
				next_chunk++;

			if ( next_chunk < chunks.count ( ) && chunks [ static_cast < size_t > ( next_chunk ) ].start == pos ) {

				push_item ( line_kind::CHUNK, section, pos, 0, 1, 1, next_chunk, rows );
				next_chunk++;

			}

			uint64_t stop = raw_end;

			if ( next_function < functions.count ( ) )
				stop = base::min ( stop, functions [ static_cast < size_t > ( next_function ) ].start );

			if ( open >= 0 && functions [ static_cast < size_t > ( open ) ].end > pos )
				stop = base::min ( stop, functions [ static_cast < size_t > ( open ) ].end );

			if ( next_chunk < chunks.count ( ) )
				stop = base::min ( stop, chunks [ static_cast < size_t > ( next_chunk ) ].start );

			if ( stop <= pos )
				stop = pos + 1;

			if ( db->is_covered ( pos ) ) {

				uint64_t end = db->covered_end ( pos, stop );

				if ( end <= pos )
					end = pos + 1;

				uint64_t count = db->row_rank ( end ) - db->row_rank ( pos );

				push_item ( line_kind::CODE, section, pos, end - pos, static_cast < uint32_t > ( base::max ( count, 1ull ) ), 1, -1, rows );
				pos = end;

			} else {

				uint64_t end = db->uncovered_end ( pos, stop );

				if ( end <= pos )
					end = pos + 1;

				gap ( pos, end );
				pos = end;

			}

		}

		if ( open >= 0 )
			push_item ( line_kind::END, section, pos, 0, 1, 1, open, rows );

		if ( all_end > raw_end )
			push_item ( line_kind::EMPTY, section, raw_end, all_end - raw_end, 1, 1, -1, rows );

	}

	void app::build_listing ( ) {

		listing_build++;
		items.clear ( );
		names.clear ( );
		total_rows = 0;

		if ( !file.loaded ( ) )
			return;

		bool analyzed = db && analysis_applied && !db->maps.empty ( );

		if ( string_addresses.empty ( ) && !file.strings.empty ( ) ) {

			base::list < mark > order;

			for ( int32_t i = 0; i < file.strings.count ( ); i++ ) {

				uint64_t address = 0;

				if ( file.address_of ( file.strings [ static_cast < size_t > ( i ) ].offset, address ) ) {

					mark entry = { address, 0, 0, i };

					order.push ( entry );

				}

			}

			sort_marks ( order );

			for ( const mark& entry : order ) {

				string_addresses.push ( entry.address );
				string_order.push ( entry.ref );

			}

		}

		base::list < mark > marks;

		if ( !analyzed ) {

			for ( const loader::symbol& item : file.functions ) {

				mark entry = { item.address, 0, 0, names.count ( ) };

				names.push ( item.name );
				marks.push ( entry );

			}

		}

		for ( const loader::exported& item : file.exports ) {

			if ( !item.address || !item.forward.empty ( ) )
				continue;

			int32_t owner = file.section_at ( item.address );

			if ( owner >= 0 && file.sections [ static_cast < size_t > ( owner ) ].type == loader::kind::CODE )
				continue;

			base::writer label;

			if ( item.name.empty ( ) )
				label.add ( L"Ordinal_" ).number ( item.ordinal );
			else
				label.add ( item.name.get ( ) );

			mark entry = { item.address, 0, 1, names.count ( ) };

			names.push ( base::text ( label.text ( ) ) );
			marks.push ( entry );

		}

		uint64_t pointer = file.wide ( ) ? 8 : 4;

		for ( int32_t i = 0; i < file.imports.count ( ); i++ ) {

			mark entry = { file.imports [ static_cast < size_t > ( i ) ].slot, pointer, 3, i };

			marks.push ( entry );

		}

		for ( size_t k = 0; k < string_addresses.size ( ); k++ ) {

			int32_t i = string_order [ k ];
			const loader::found_string& item = file.strings [ static_cast < size_t > ( i ) ];
			uint64_t address = string_addresses [ k ];
			int32_t owner = file.section_at ( address );

			if ( owner < 0 || file.sections [ static_cast < size_t > ( owner ) ].type == loader::kind::CODE )
				continue;

			uint64_t bytes = static_cast < uint64_t > ( item.length ) * ( item.wide ? 2 : 1 );
			uint64_t end = item.offset + bytes;

			if ( end < file.size && file.byte ( end ) == 0 && ( !item.wide || file.byte ( end + 1 ) == 0 ) )
				bytes += item.wide ? 2 : 1;

			mark entry = { address, bytes, 2, i };

			marks.push ( entry );

		}

		if ( analyzed ) {

			for ( int32_t i = 0; i < db->classes.count ( ); i++ ) {

				const analysis::class_info& item = db->classes [ static_cast < size_t > ( i ) ];

				if ( item.methods <= 0 )
					continue;

				mark entry = { item.vtable, static_cast < uint64_t > ( item.methods ) * pointer, 5, i };

				marks.push ( entry );

			}

			uint32_t previous = 0xFFFFFFFFu;

			for ( const analysis::xref& item : db->refs ) {

				if ( item.target == previous )
					continue;

				previous = item.target;

				uint64_t address = file.base + item.target;
				int32_t owner = file.section_at ( address );

				if ( owner <= 0 || file.sections [ static_cast < size_t > ( owner ) ].type == loader::kind::CODE )
					continue;

				mark entry = { address, 0, 4, -1 };

				marks.push ( entry );

			}

		}

		sort_marks ( marks );

		base::list < int32_t > order;

		for ( int32_t i = 0; i < file.sections.count ( ); i++ ) {

			int32_t at = order.count ( );

			order.push ( i );

			while ( at > 0 && file.sections [ static_cast < size_t > ( order [ at - 1 ] ) ].address > file.sections [ static_cast < size_t > ( i ) ].address ) {

				order [ at ] = order [ at - 1 ];
				at--;

			}

			order [ at ] = i;

		}

		uint64_t rows = 0;
		size_t next_mark = 0;

		auto fill = [ & ] ( int32_t section, uint64_t from, uint64_t to ) {

			const loader::section& owner = file.sections [ static_cast < size_t > ( section ) ];
			uint64_t raw_end = owner.address + owner.raw;

			while ( from < to ) {

				if ( from >= raw_end ) {

					push_item ( line_kind::EMPTY, section, from, to - from, 1, 1, -1, rows );

					return;

				}

				uint64_t end = base::min ( to, raw_end );
				uint64_t chunk = base::min ( end - from, 0x100000ull * 8 );

				push_item ( line_kind::DATA, section, from, chunk, static_cast < uint32_t > ( ( chunk + 7 ) / 8 ), 8, -1, rows );
				from += chunk;

			}

		};

		for ( int32_t index : order ) {

			const loader::section& owner = file.sections [ static_cast < size_t > ( index ) ];
			uint64_t start = owner.address;
			uint64_t end = owner.address + base::max ( owner.size, owner.raw );
			uint64_t at = start;

			push_item ( line_kind::BANNER, index, start, end - start, 2, 1, -1, rows );

			while ( next_mark < marks.size ( ) && marks [ next_mark ].address < start )
				next_mark++;

			if ( analyzed && owner.type == loader::kind::CODE ) {

				build_code_items ( index, rows );

				while ( next_mark < marks.size ( ) && marks [ next_mark ].address < end )
					next_mark++;

				continue;

			}

			while ( at < end ) {

				uint64_t target = end;

				while ( next_mark < marks.size ( ) && marks [ next_mark ].address < at )
					next_mark++;

				if ( next_mark < marks.size ( ) && marks [ next_mark ].address < end )
					target = marks [ next_mark ].address;

				if ( target > at )
					fill ( index, at, target );

				at = target;

				if ( at >= end )
					break;

				bool consumed = false;
				bool labeled = false;

				while ( next_mark < marks.size ( ) && marks [ next_mark ].address == at ) {

					const mark& item = marks [ next_mark++ ];

					if ( item.kind <= 1 ) {

						line_item& label = items.emplace ( );

						label.kind = line_kind::LABEL;
						label.section = static_cast < uint8_t > ( index );
						label.address = at;
						label.ref = item.ref;
						label.function = item.kind == 0;
						label.first_row = rows;
						rows++;
						labeled = true;

						continue;

					}

					if ( item.kind == 4 ) {

						if ( !labeled )
							push_item ( line_kind::LABEL, index, at, 0, 1, 1, -1, rows );

						labeled = true;

						continue;

					}

					if ( consumed )
						continue;

					uint64_t span = base::min ( item.size, end - at );

					if ( item.kind == 5 ) {

						if ( !labeled )
							push_item ( line_kind::LABEL, index, at, 0, 1, 1, -1, rows );

						push_item ( line_kind::VTABLE, index, at, span, static_cast < uint32_t > ( span / pointer ), static_cast < uint8_t > ( pointer ), item.ref, rows );
						labeled = true;

					} else if ( item.kind == 2 ) {

						if ( !labeled )
							push_item ( line_kind::LABEL, index, at, 0, 1, 1, -1, rows );

						push_item ( line_kind::STRING, index, at, span, 1, 1, item.ref, rows );
						labeled = true;

					} else
						push_item ( line_kind::POINTER, index, at, span, 1, 1, item.ref, rows );

					at += span;
					consumed = true;

				}

			}

		}

		total_rows = rows;

	}

	int32_t app::item_at ( uint64_t row ) const {

		int32_t low = 0;
		int32_t high = items.count ( ) - 1;

		if ( high < 0 )
			return -1;

		while ( low < high ) {

			int32_t middle = ( low + high + 1 ) / 2;

			if ( items [ static_cast < size_t > ( middle ) ].first_row <= row )
				low = middle;
			else
				high = middle - 1;

		}

		return low;

	}

	uint64_t app::address_at_row ( uint64_t row ) const {

		int32_t index = item_at ( row );

		if ( index < 0 )
			return 0;

		const line_item& item = items [ static_cast < size_t > ( index ) ];
		uint64_t sub = row - item.first_row;

		switch ( item.kind ) {

			case line_kind::CODE: {

				bool label = false;

				return db ? db->row_address ( item.address, item.address + item.size, sub, label ) : item.address;

			}

			case line_kind::DATA:
			case line_kind::TABLE:
			case line_kind::VTABLE:
				return item.address + sub * item.stride;

			default:
				return item.address;

		}

	}

	static bool content_kind ( line_kind kind ) {

		return kind != line_kind::BANNER && kind != line_kind::BLANK && kind != line_kind::FUNCTION && kind != line_kind::END && kind != line_kind::CHUNK && kind != line_kind::LABEL;

	}

	uint64_t app::row_of ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = items.count ( ) - 1;

		if ( high < 0 || items [ 0 ].address > address )
			return 0;

		while ( low < high ) {

			int32_t middle = ( low + high + 1 ) / 2;

			if ( items [ static_cast < size_t > ( middle ) ].address <= address )
				low = middle;
			else
				high = middle - 1;

		}

		if ( items [ static_cast < size_t > ( low ) ].address == address ) {

			while ( low > 0 && items [ static_cast < size_t > ( low - 1 ) ].address == address )
				low--;

			int32_t best = low;

			for ( int32_t i = low; i < items.count ( ) && items [ static_cast < size_t > ( i ) ].address == address; i++ ) {

				if ( content_kind ( items [ static_cast < size_t > ( i ) ].kind ) ) {

					best = i;

					break;

				}

				best = i;

			}

			const line_item& item = items [ static_cast < size_t > ( best ) ];

			if ( item.kind == line_kind::CODE && db && db->is_label ( address ) )
				return item.first_row + 1;

			return item.first_row;

		}

		const line_item& item = items [ static_cast < size_t > ( low ) ];

		if ( address >= item.address + base::max ( item.size, 1ull ) )
			return item.first_row;

		switch ( item.kind ) {

			case line_kind::CODE: {

				uint64_t start = db->instruction_at ( address );
				uint64_t local = db->row_rank ( start ) - db->row_rank ( item.address );

				if ( db->is_label ( start ) )
					local++;

				return item.first_row + base::min ( local, static_cast < uint64_t > ( item.rows - 1 ) );

			}

			case line_kind::DATA:
			case line_kind::TABLE:
			case line_kind::VTABLE:
				return item.first_row + base::min ( ( address - item.address ) / item.stride, static_cast < uint64_t > ( item.rows - 1 ) );

			default:
				return item.first_row;

		}

	}

	void app::set_cursor_row ( uint64_t row, bool center ) {

		if ( !total_rows )
			return;

		now->cursor_row = base::min ( row, total_rows - 1 );
		now->cursor_address = address_at_row ( now->cursor_row );
		now->row_for = now->cursor_address;
		now->row_build = listing_build;

		float visible = view_area.h > 40.0f ? view_area.h / metric::code_row - 1.0f : 30.0f;
		float place = static_cast < float > ( now->cursor_row );

		if ( center )
			now->listing_scroll.target = place - visible * 0.35f;
		else if ( place < now->listing_scroll.target )
			now->listing_scroll.target = place;
		else if ( place > now->listing_scroll.target + visible - 1.0f )
			now->listing_scroll.target = place - visible + 1.0f;

		uint64_t offset = 0;

		if ( file.offset_of ( now->cursor_address, offset ) ) {

			now->hex_cursor = offset;
			now->hex_anchor = offset;

		}

		pending = base::max ( pending, 2 );

	}

	void app::go ( uint64_t address, bool remember ) {

		if ( !file.loaded ( ) )
			return;

		if ( showing == view::PSEUDOCODE ) {

			int32_t owner = db && analysis_applied ? db->function_at ( address ) : -1;

			if ( owner >= 0 ) {

				uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;

				if ( start != now->function ) {

					if ( remember ) {

						now->back_stack.push ( now->function );
						now->forward_stack.clear ( );

					}

					now->function = start;
					now->code_scroll = scroller ( );
					now->code_line = 0;
					now->code_ready = false;

				}

				now->cursor_address = address;

				return;

			}

			int32_t listing = -1;
			pane& item = panes [ focused_pane ];

			for ( int32_t i = 0; i < item.count; i++ ) {

				if ( documents [ item.tabs [ i ] ].kind == view::LISTING )
					listing = i;

			}

			if ( listing >= 0 )
				focus_document ( focused_pane, listing );
			else
				open_document ( view::LISTING, focused_pane, address );

		} else if ( showing == view::GRAPH ) {

			int32_t owner = db && analysis_applied ? db->function_at ( address ) : -1;

			if ( owner >= 0 ) {

				uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;

				if ( remember && now->cursor_address != address ) {

					now->back_stack.push ( now->cursor_address );
					now->forward_stack.clear ( );

				}

				if ( start != now->graph_function ) {

					now->graph_function = start;
					now->graph_ready = false;

				}

				now->cursor_address = address;
				now->graph_block = -1;
				pending = base::max ( pending, 2 );

				return;

			}

			int32_t current = current_document ( );

			if ( current >= 0 ) {

				documents [ current ].kind = view::LISTING;
				select_state ( current );

			}

		} else if ( showing == view::CALLS ) {

			int32_t owner = db && analysis_applied ? db->function_at ( address ) : -1;

			if ( owner >= 0 ) {

				uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;

				if ( remember && now->function != start ) {

					now->back_stack.push ( now->function );
					now->forward_stack.clear ( );

				}

				now->function = start;
				now->cursor_address = start;
				pending = base::max ( pending, 2 );

				return;

			}

			int32_t listing = -1;
			pane& item = panes [ focused_pane ];

			for ( int32_t i = 0; i < item.count; i++ ) {

				if ( documents [ item.tabs [ i ] ].kind == view::LISTING )
					listing = i;

			}

			if ( listing >= 0 )
				focus_document ( focused_pane, listing );
			else
				open_document ( view::LISTING, focused_pane, address );

		} else if ( showing == view::HEADERS ) {

			int32_t listing = -1;
			pane& item = panes [ focused_pane ];

			for ( int32_t i = 0; i < item.count; i++ ) {

				if ( documents [ item.tabs [ i ] ].kind == view::LISTING )
					listing = i;

			}

			if ( listing >= 0 )
				focus_document ( focused_pane, listing );
			else
				open_document ( view::LISTING, focused_pane, address );

		}

		if ( remember && now->cursor_address != address ) {

			now->back_stack.push ( now->cursor_address );
			now->forward_stack.clear ( );

			if ( now->back_stack.count ( ) > 256 )
				now->back_stack.remove_at ( 0 );

		}

		set_cursor_row ( row_of ( address ), true );
		now->select_anchor = now->cursor_row;
		now->ranged = false;
		now->caret_col = listing_cols.mnemonic;
		now->anchor_col = listing_cols.mnemonic;
		blink_from = clock;

		if ( !db || !db->is_covered ( address ) ) {

			now->cursor_address = address;
			now->row_for = address;

		}

		if ( showing == view::HEX ) {

			uint64_t offset = 0;

			if ( file.offset_of ( address, offset ) )
				go_offset ( offset );

		}

	}

	void app::go_offset ( uint64_t offset ) {

		now->hex_cursor = offset;
		now->hex_anchor = offset;
		now->hex_scroll.target = static_cast < float > ( offset / 16 ) - 6.0f;
		pending = base::max ( pending, 2 );

	}

	static wchar_t lower_char ( wchar_t value ) {

		return value >= L'A' && value <= L'Z' ? static_cast < wchar_t > ( value + 32 ) : value;

	}

	static bool same_name ( const wchar_t* a, const wchar_t* b ) {

		while ( *a && *b && lower_char ( *a ) == lower_char ( *b ) ) {

			a++;
			b++;

		}

		return !*a && !*b;

	}

	bool app::parse_address ( const wchar_t* text, uint64_t& result ) const {

		while ( *text == L' ' )
			text++;

		if ( !*text )
			return false;

		for ( const user_name& item : renamed ) {

			if ( same_name ( item.text.get ( ), text ) ) {

				result = item.address;

				return true;

			}

		}

		if ( db && analysis_applied ) {

			wchar_t name [ 200 ];

			for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

				function_name ( i, name, 200 );

				if ( same_name ( name, text ) ) {

					result = db->functions [ static_cast < size_t > ( i ) ].start;

					return true;

				}

			}

		}

		for ( const loader::symbol& item : file.functions ) {

			if ( same_name ( item.name.get ( ), text ) ) {

				result = item.address;

				return true;

			}

		}

		for ( const loader::exported& item : file.exports ) {

			if ( item.address && same_name ( item.name.get ( ), text ) ) {

				result = item.address;

				return true;

			}

		}

		for ( const loader::import& item : file.imports ) {

			if ( !item.name.empty ( ) && same_name ( item.name.get ( ), text ) ) {

				result = item.slot;

				return true;

			}

		}

		for ( const loader::section& item : file.sections ) {

			if ( same_name ( item.name, text ) ) {

				result = item.address;

				return true;

			}

		}

		bool relative = false;

		if ( *text == L'+' ) {

			relative = true;
			text++;

		}

		if ( text [ 0 ] == L'f' && text [ 1 ] == L'n' && text [ 2 ] == L'_' )
			text += 3;
		else if ( text [ 0 ] == L'g' && text [ 1 ] == L'_' )
			text += 2;

		if ( text [ 0 ] == L'0' && ( text [ 1 ] == L'x' || text [ 1 ] == L'X' ) )
			text += 2;

		uint64_t value = 0;
		int32_t digits = 0;

		for ( ; *text; text++ ) {

			wchar_t c = *text;
			uint32_t nibble = 0;

			if ( c >= L'0' && c <= L'9' )
				nibble = static_cast < uint32_t > ( c - L'0' );
			else if ( c >= L'a' && c <= L'f' )
				nibble = static_cast < uint32_t > ( c - L'a' + 10 );
			else if ( c >= L'A' && c <= L'F' )
				nibble = static_cast < uint32_t > ( c - L'A' + 10 );
			else if ( c == L'h' || c == L'H' || c == L'`' || c == L' ' )
				continue;
			else
				return false;

			value = ( value << 4 ) | nibble;
			digits++;

		}

		if ( !digits || digits > 16 )
			return false;

		result = relative ? file.base + value : value;

		if ( !relative && file.type != loader::format::RAW && value < file.base && file.section_at ( file.base + value ) >= 0 )
			result = file.base + value;

		return true;

	}

	void app::string_label ( int32_t index, wchar_t* out, int32_t room ) const {

		const loader::found_string& item = file.strings [ static_cast < size_t > ( index ) ];
		int32_t length = 0;
		bool gap = false;

		out [ length++ ] = L's';
		out [ length++ ] = L'_';

		for ( uint32_t i = 0; i < item.length && length < room - 2 && length < 30; i++ ) {

			wchar_t c = static_cast < wchar_t > ( file.byte ( item.offset + static_cast < uint64_t > ( i ) * ( item.wide ? 2 : 1 ) ) );

			if ( ( c >= L'a' && c <= L'z' ) || ( c >= L'0' && c <= L'9' ) || ( c >= L'A' && c <= L'Z' ) ) {

				if ( gap && length > 2 )
					out [ length++ ] = L'_';

				out [ length++ ] = lower_char ( c );
				gap = false;

			} else
				gap = true;

		}

		if ( length == 2 ) {

			base::writer fallback;

			fallback.add ( L"s_" ).hex ( item.offset );

			for ( int32_t i = 0; i <= fallback.size ( ) && i < room; i++ )
				out [ i ] = fallback.text ( ) [ i ];

			return;

		}

		out [ length ] = 0;

	}

	int32_t app::string_at ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = string_addresses.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = string_addresses [ static_cast < size_t > ( middle ) ];

			if ( value == address )
				return string_order [ static_cast < size_t > ( middle ) ];

			if ( value < address )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return -1;

	}

	const wchar_t* app::user_text ( const base::list < user_name >& list, uint64_t address ) const {

		for ( const user_name& item : list ) {

			if ( item.address == address && !item.text.empty ( ) )
				return item.text.get ( );

		}

		return nullptr;

	}

	bool app::number_text ( uint64_t at, uint64_t raw, uint8_t size, wchar_t* out, int32_t room ) const {

		if ( number_forms.empty ( ) )
			return false;

		const wchar_t* form = user_text ( number_forms, at );

		if ( !form )
			return false;

		int32_t bits = size >= 8 ? 64 : size * 8;
		uint64_t mask = bits >= 64 ? ~0ull : ( 1ull << bits ) - 1;
		int64_t as_signed = bits >= 64 ? static_cast < int64_t > ( raw ) : static_cast < int64_t > ( raw << ( 64 - bits ) ) >> ( 64 - bits );
		base::writer text;

		raw &= mask;

		switch ( form [ 0 ] ) {

			case L'h':
				text.add ( L"0x" ).hex ( raw );
				break;

			case L'd':
				text.number ( as_signed );
				break;

			case L'b': {

				int32_t top = bits - 1;

				while ( top > 0 && !( ( raw >> top ) & 1 ) )
					top--;

				text.add ( L"0b" );

				for ( int32_t i = top; i >= 0; i-- )
					text.add ( ( raw >> i ) & 1 ? L'1' : L'0' );

				break;

			}

			case L'c': {

				int32_t top = bits / 8 - 1;

				while ( top > 0 && !( ( raw >> ( top * 8 ) ) & 0xFF ) )
					top--;

				text.add ( L'\'' );

				for ( int32_t i = top; i >= 0; i-- ) {

					uint8_t value = static_cast < uint8_t > ( raw >> ( i * 8 ) );

					if ( value == L'\'' || value == L'\\' )
						text.add ( L'\\' ).add ( static_cast < wchar_t > ( value ) );
					else if ( value == L'\n' )
						text.add ( L"\\n" );
					else if ( value == L'\r' )
						text.add ( L"\\r" );
					else if ( value == L'\t' )
						text.add ( L"\\t" );
					else if ( value == 0 )
						text.add ( L"\\0" );
					else if ( value >= 0x20 && value < 0x7F )
						text.add ( static_cast < wchar_t > ( value ) );
					else
						text.add ( L"\\x" ).hex ( value, 2 );

				}

				text.add ( L'\'' );

				break;

			}

			case L'f':

				if ( size != 4 && size != 8 )
					return false;

				float_literal ( text, raw, size );

				break;

			default:
				return false;

		}

		int32_t i = 0;

		for ( ; i < text.size ( ) && i < room - 1; i++ )
			out [ i ] = text.text ( ) [ i ];

		out [ i ] = 0;

		return true;

	}

	void app::set_user_text ( base::list < user_name >& list, uint64_t address, const wchar_t* text ) {

		annotation_revision++;

		for ( int32_t i = 0; i < list.count ( ); i++ ) {

			if ( list [ static_cast < size_t > ( i ) ].address == address ) {

				if ( !text || !text [ 0 ] )
					list.remove_at ( static_cast < size_t > ( i ) );
				else
					list [ static_cast < size_t > ( i ) ].text.set ( text );

				names_revision++;

				return;

			}

		}

		if ( !text || !text [ 0 ] )
			return;

		user_name& item = list.emplace ( );

		item.address = address;
		item.text.set ( text );
		names_revision++;

	}

	static void copy_name ( const wchar_t* source, wchar_t* out, int32_t room ) {

		int32_t i = 0;

		for ( ; source [ i ] && i < room - 1; i++ )
			out [ i ] = source [ i ];

		out [ i ] = 0;

	}

	void app::function_name ( int32_t index, wchar_t* out, int32_t room ) const {

		const analysis::function_info& item = db->functions [ static_cast < size_t > ( index ) ];
		const wchar_t* user = user_text ( renamed, item.start );

		if ( user ) {

			copy_name ( user, out, room );

			return;

		}

		const wchar_t* exported = item.name >= 0 ? db->names [ static_cast < size_t > ( item.name ) ].get ( ) : nullptr;
		bool ordinal_only = exported != nullptr;

		for ( int32_t i = 0; ordinal_only && i < 8; i++ )
			ordinal_only = exported [ i ] == L"Ordinal_" [ i ];

		if ( exported && !ordinal_only ) {

			copy_name ( exported, out, room );

			return;

		}

		if ( const wchar_t* known = learned.empty ( ) ? nullptr : learned_name ( item.start ) ) {

			copy_name ( known, out, room );

			return;

		}

		int32_t slot = db->virtual_of ( item.start );

		if ( slot >= 0 ) {

			const analysis::virtual_slot& entry = db->virtuals [ static_cast < size_t > ( slot ) ];
			const analysis::class_info& owner = db->classes [ static_cast < size_t > ( entry.owner ) ];
			base::writer text;

			text.add ( owner.name.get ( ) ).add ( L"::vfunc_" ).number ( entry.slot );

			if ( owner.offset )
				text.add ( L"_" ).hex ( static_cast < uint64_t > ( owner.offset ) );

			copy_name ( text.text ( ), out, room );

			return;

		}

		if ( exported ) {

			copy_name ( exported, out, room );

			return;

		}

		if ( ( item.flags & analysis::THUNK ) && item.import >= 0 ) {

			const loader::import& entry = file.imports [ static_cast < size_t > ( item.import ) ];
			base::writer text;

			if ( entry.by_ordinal || entry.name.empty ( ) )
				text.add ( file.modules [ static_cast < size_t > ( entry.owner ) ].name.get ( ) ).add ( L"#" ).number ( entry.ordinal );
			else
				text.add ( entry.name.get ( ) );

			copy_name ( text.text ( ), out, room );

			return;

		}

		base::writer text;

		text.add ( L"fn_" ).hex ( item.start );
		copy_name ( text.text ( ), out, room );

	}

	bool app::indirect_note ( const x86::instruction& ins, base::writer& out ) const {

		if ( !db || !analysis_applied || !ins.count )
			return false;

		const x86::operand& target = ins.operands [ 0 ];
		wchar_t name [ 200 ];

		auto pointer_at = [ & ] ( uint64_t address, uint64_t& value ) {

			uint64_t offset = 0;

			if ( !file.offset_of ( address, offset ) )
				return false;

			value = file.wide ( ) ? file.read64 ( offset ) : file.read32 ( offset );

			return db->function_starting ( value ) >= 0;

		};

		auto report = [ & ] ( uint64_t value, const wchar_t* how ) {

			out.add ( L"\x2192 " );

			if ( symbol_name ( value, true, name, 200 ) )
				out.add ( name );
			else
				out.hex ( value );

			if ( how )
				out.add ( how );

			return true;

		};

		if ( target.type == x86::kind::MEM ) {

			uint64_t slot = 0;
			uint64_t value = 0;

			for ( const loader::import& item : file.imports ) {

				if ( ins.memory_target ( slot ) && item.slot == slot )
					return false;

			}

			if ( ins.memory_target ( slot ) && pointer_at ( slot, value ) )
				return report ( value, L"  (initial value)" );

			return false;

		}

		if ( target.type != x86::kind::REG )
			return false;

		uint8_t reg = target.reg;
		uint64_t at = ins.address;

		for ( int32_t step = 0; step < 48; step++ ) {

			if ( !at || !db->is_covered ( at - 1 ) )
				return false;

			uint64_t before = db->instruction_at ( at - 1 );
			x86::instruction prior;

			if ( !db->decode ( before, prior ) )
				return false;

			if ( prior.count && prior.operands [ 0 ].type == x86::kind::REG && x86::same_family ( prior.operands [ 0 ].reg, reg ) && ( prior.operands [ 0 ].access & x86::WRITE ) ) {

				const x86::operand& source = prior.operands [ 1 ];
				uint64_t value = 0;

				if ( prior.name == x86::mnemonic::lea && source.rip_relative ( ) )
					return report ( prior.next ( ) + static_cast < uint64_t > ( source.value ), nullptr );

				if ( prior.name == x86::mnemonic::mov && prior.count == 2 && source.type == x86::kind::IMM && db->function_starting ( static_cast < uint64_t > ( source.value ) ) >= 0 )
					return report ( static_cast < uint64_t > ( source.value ), nullptr );

				if ( prior.name == x86::mnemonic::mov && prior.count == 2 && source.type == x86::kind::MEM ) {

					uint64_t slot = 0;

					if ( prior.memory_target ( slot ) )
						return pointer_at ( slot, value ) && report ( value, L"  (initial value)" );

					if ( source.base != x86::NONE && source.index == x86::NONE && source.value == 0 ) {

						uint8_t walker = source.base;
						uint64_t first = 0;
						uint64_t last = 0;
						uint8_t limit_reg = 0;
						uint64_t scan = before;

						for ( int32_t back = 0; back < 64 && db->is_covered ( scan - 1 ); back++ ) {

							scan = db->instruction_at ( scan - 1 );

							x86::instruction look;

							if ( !db->decode ( scan, look ) || look.count < 2 )
								continue;

							if ( look.name == x86::mnemonic::cmp && look.operands [ 0 ].type == x86::kind::REG && look.operands [ 1 ].type == x86::kind::REG && x86::same_family ( look.operands [ 0 ].reg, walker ) )
								limit_reg = look.operands [ 1 ].reg;

							if ( look.name == x86::mnemonic::lea && look.operands [ 1 ].rip_relative ( ) ) {

								uint64_t address = look.next ( ) + static_cast < uint64_t > ( look.operands [ 1 ].value );

								if ( x86::same_family ( look.operands [ 0 ].reg, walker ) && !first )
									first = address;
								else if ( limit_reg && x86::same_family ( look.operands [ 0 ].reg, limit_reg ) && !last )
									last = address;

							}

						}

						if ( first && last > first && last - first <= 0x4000 ) {

							int32_t found = 0;
							base::writer list;
							uint64_t step_size = file.wide ( ) ? 8 : 4;

							for ( uint64_t entry = first; entry < last; entry += step_size ) {

								uint64_t function = 0;

								if ( !pointer_at ( entry, function ) )
									continue;

								if ( found < 3 ) {

									if ( found )
										list.add ( L", " );

									if ( symbol_name ( function, true, name, 200 ) )
										list.add ( name );
									else
										list.hex ( function );

								}

								found++;

							}

							if ( found ) {

								out.add ( L"\x2192 " ).number ( found ).add ( found == 1 ? L" target from a table: " : L" targets from a table: " ).add ( list.text ( ) );

								if ( found > 3 )
									out.add ( L", \x2026" );

								return true;

							}

						}

					}

				}

				return false;

			}

			if ( prior.control == x86::flow::CALL || prior.control == x86::flow::RETURN || prior.control == x86::flow::JUMP )
				return false;

			at = before;

		}

		return false;

	}

	bool app::symbol_name ( uint64_t address, bool code, wchar_t* out, int32_t room ) const {

		const wchar_t* user = user_text ( renamed, address );

		if ( user ) {

			copy_name ( user, out, room );

			return true;

		}

		if ( address == file.base && file.type != loader::format::RAW ) {

			copy_name ( L"__ImageBase", out, room );

			return true;

		}

		if ( db && analysis_applied ) {

			int32_t owner = db->function_starting ( address );

			if ( owner >= 0 ) {

				function_name ( owner, out, room );

				return true;

			}

			if ( db->is_start ( address ) ) {

				int32_t inside = db->function_at ( address );
				base::writer text;

				if ( inside >= 0 )
					text.add ( L".L" ).hex ( address - db->functions [ static_cast < size_t > ( inside ) ].start );
				else
					text.add ( L"loc_" ).hex ( address );

				copy_name ( text.text ( ), out, room );

				return true;

			}

		}

		if ( db && analysis_applied ) {

			int32_t owner = db->class_at_vtable ( address );

			if ( owner >= 0 ) {

				const analysis::class_info& info = db->classes [ static_cast < size_t > ( owner ) ];
				base::writer text;

				text.add ( info.name.get ( ) ).add ( L"::vftable" );

				if ( info.offset )
					text.add ( L"_" ).hex ( static_cast < uint64_t > ( info.offset ) );

				copy_name ( text.text ( ), out, room );

				return true;

			}

		}

		for ( const loader::import& item : file.imports ) {

			if ( item.slot == address ) {

				base::writer text;

				if ( item.by_ordinal || item.name.empty ( ) )
					text.add ( file.modules [ static_cast < size_t > ( item.owner ) ].name.get ( ) ).add ( L"#" ).number ( item.ordinal );
				else
					text.add ( item.name.get ( ) );

				copy_name ( text.text ( ), out, room );

				return true;

			}

		}

		if ( const loader::symbol* label = file.label_at ( address ) ) {

			copy_name ( label->name.get ( ), out, room );

			return true;

		}

		int32_t text_index = string_at ( address );

		if ( text_index >= 0 ) {

			string_label ( text_index, out, room );

			return true;

		}

		for ( const loader::exported& item : file.exports ) {

			if ( item.address == address && !item.name.empty ( ) && item.forward.empty ( ) ) {

				copy_name ( item.name.get ( ), out, room );

				return true;

			}

		}

		int32_t section = file.section_at ( address );

		if ( section > 0 && file.sections [ static_cast < size_t > ( section ) ].type != loader::kind::CODE && !code ) {

			base::writer text;

			text.add ( L"g_" ).hex ( address );
			copy_name ( text.text ( ), out, room );

			return true;

		}

		return false;

	}

}