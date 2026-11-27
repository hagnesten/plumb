#include "ui/listing/listing/listing.h"
#include "ui/core/app/app.h"
#include "ui/style/icons/icons.h"
#include "ui/style/format/format.h"
#include "base/files/files.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "decompiler/apis/apis.h"
#include "decompiler/prototypes/prototypes.h"
#include "decompiler/lift/yields/yields.h"
#include <windows.h>

void ui::sort_marks ( base::list < mark >& items ) {

	base::sort ( items.data ( ), items.count ( ), [ ] ( const mark& a, const mark& b ) {

		return a.address < b.address || ( a.address == b.address && a.kind < b.kind );

	} );

}

bool ui::padding_value ( uint8_t value ) {

	return value == 0xCC || value == 0x90 || value == 0x00;

}

void ui::app::push_item ( line_kind kind, int32_t section, uint64_t address, uint64_t size, uint32_t rows, uint8_t stride, int32_t ref, uint64_t& total ) {

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

void ui::app::build_code_items ( int32_t section, uint64_t& rows ) {

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

void ui::app::build_listing ( ) {

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

int32_t ui::app::item_at ( uint64_t row ) const {

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

uint64_t ui::app::address_at_row ( uint64_t row ) const {

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

bool ui::content_kind ( line_kind kind ) {

	return kind != line_kind::BANNER && kind != line_kind::BLANK && kind != line_kind::FUNCTION && kind != line_kind::END && kind != line_kind::CHUNK && kind != line_kind::LABEL;

}

uint64_t ui::app::row_of ( uint64_t address ) const {

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

void ui::app::set_cursor_row ( uint64_t row, bool center ) {

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

void ui::app::go ( uint64_t address, bool remember ) {

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

void ui::app::go_offset ( uint64_t offset ) {

	now->hex_cursor = offset;
	now->hex_anchor = offset;
	now->hex_scroll.target = static_cast < float > ( offset / 16 ) - 6.0f;
	pending = base::max ( pending, 2 );

}

bool ui::app::parse_address ( const wchar_t* text, uint64_t& result ) const {

	while ( *text == L' ' )
		text++;

	if ( !*text )
		return false;

	for ( const user_name& item : renamed ) {

		if ( base::compare_folded ( item.text.get ( ), text ) == 0 ) {

			result = item.address;

			return true;

		}

	}

	if ( db && analysis_applied ) {

		wchar_t name [ 200 ];

		for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

			function_name ( i, name, 200 );

			if ( base::compare_folded ( name, text ) == 0 ) {

				result = db->functions [ static_cast < size_t > ( i ) ].start;

				return true;

			}

		}

	}

	for ( const loader::symbol& item : file.functions ) {

		if ( base::compare_folded ( item.name.get ( ), text ) == 0 ) {

			result = item.address;

			return true;

		}

	}

	for ( const loader::exported& item : file.exports ) {

		if ( item.address && base::compare_folded ( item.name.get ( ), text ) == 0 ) {

			result = item.address;

			return true;

		}

	}

	for ( const loader::import& item : file.imports ) {

		if ( !item.name.empty ( ) && base::compare_folded ( item.name.get ( ), text ) == 0 ) {

			result = item.slot;

			return true;

		}

	}

	for ( const loader::section& item : file.sections ) {

		if ( base::compare_folded ( item.name, text ) == 0 ) {

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

void ui::app::string_label ( int32_t index, wchar_t* out, int32_t room ) const {

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

			out [ length++ ] = base::fold_case ( c );
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

int32_t ui::app::string_at ( uint64_t address ) const {

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

const wchar_t* ui::app::user_text ( const base::list < user_name >& list, uint64_t address ) const {

	for ( const user_name& item : list ) {

		if ( item.address == address && !item.text.empty ( ) )
			return item.text.get ( );

	}

	return nullptr;

}

bool ui::app::number_text ( uint64_t at, uint64_t raw, uint8_t size, wchar_t* out, int32_t room ) const {

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

void ui::app::set_user_text ( base::list < user_name >& list, uint64_t address, const wchar_t* text ) {

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

void ui::app::function_name ( int32_t index, wchar_t* out, int32_t room ) const {

	const analysis::function_info& item = db->functions [ static_cast < size_t > ( index ) ];
	const wchar_t* user = user_text ( renamed, item.start );

	if ( user ) {

		base::copy_text ( user, out, room );

		return;

	}

	const wchar_t* exported = item.name >= 0 ? db->names [ static_cast < size_t > ( item.name ) ].get ( ) : nullptr;
	bool ordinal_only = exported != nullptr;

	for ( int32_t i = 0; ordinal_only && i < 8; i++ )
		ordinal_only = exported [ i ] == L"Ordinal_" [ i ];

	if ( exported && !ordinal_only ) {

		base::copy_text ( exported, out, room );

		return;

	}

	if ( const wchar_t* known = learned.empty ( ) ? nullptr : learned_name ( item.start ) ) {

		base::copy_text ( known, out, room );

		return;

	}

	int32_t slot = name_virtuals ? db->virtual_of ( item.start ) : -1;

	if ( slot >= 0 ) {

		const analysis::virtual_slot& entry = db->virtuals [ static_cast < size_t > ( slot ) ];
		const analysis::class_info& owner = db->classes [ static_cast < size_t > ( entry.owner ) ];
		base::writer text;

		text.add ( owner.name.get ( ) ).add ( L"::vfunc_" ).number ( entry.slot );

		if ( owner.offset )
			text.add ( L"_" ).hex ( static_cast < uint64_t > ( owner.offset ) );

		base::copy_text ( text.text ( ), out, room );

		return;

	}

	if ( exported ) {

		base::copy_text ( exported, out, room );

		return;

	}

	if ( ( item.flags & analysis::THUNK ) && item.import >= 0 ) {

		const loader::import& entry = file.imports [ static_cast < size_t > ( item.import ) ];
		base::writer text;

		if ( entry.by_ordinal || entry.name.empty ( ) )
			text.add ( file.modules [ static_cast < size_t > ( entry.owner ) ].name.get ( ) ).add ( L"#" ).number ( entry.ordinal );
		else
			text.add ( entry.name.get ( ) );

		base::copy_text ( text.text ( ), out, room );

		return;

	}

	if ( item.start == file.entry && file.entry ) {

		base::copy_text ( file.dll ? L"DllEntryPoint" : L"start", out, room );

		return;

	}

	if ( callback_name ( item.start, out, room ) )
		return;

	base::writer text;

	text.add ( L"fn_" ).hex ( item.start );
	base::copy_text ( text.text ( ), out, room );

}

const ui::user_name* ui::app::named_at ( const base::list < user_name >& items, uint64_t address ) {

	int32_t low = 0;
	int32_t high = items.count ( ) - 1;

	while ( low <= high ) {

		int32_t middle = ( low + high ) / 2;
		const user_name& item = items [ static_cast < size_t > ( middle ) ];

		if ( item.address == address )
			return &item;

		if ( item.address < address )
			low = middle + 1;
		else
			high = middle - 1;

	}

	return nullptr;

}

void ui::app::build_callback_names ( ) {

	callback_roles.clear ( );
	callback_names.clear ( );

	if ( !db || file.arch != loader::machine::X64 )
		return;

	static const int32_t passed [ 4 ] = { 1, 2, 8, 9 };
	static const wchar_t* const endings [ ] = { L"ROUTINE", L"PROC", L"FILTER", L"CALLBACK", L"HANDLER", L"_proc_type", L"FUNC" };
	base::list < uint64_t > sites;
	base::list < user_name > found;

	for ( int32_t m = 0; m < file.imports.count ( ); m++ ) {

		const loader::import& item = file.imports [ static_cast < size_t > ( m ) ];

		if ( item.name.empty ( ) )
			continue;

		const wchar_t* text = decompiler::api_prototype ( item.name.get ( ) );
		decompiler::c_prototype shape;
		base::writer problem;

		if ( !text || !decompiler::parse_prototype ( text, true, shape, problem ) )
			continue;

		base::list < uint64_t > targets;

		targets.push ( item.slot );

		for ( const analysis::function_info& function : db->functions ) {

			if ( ( function.flags & analysis::THUNK ) && function.import == m )
				targets.push ( function.start );

		}

		base::list < uint64_t > calls;

		for ( uint64_t slot : targets )
			import_call_sites ( slot, calls );

		for ( uint64_t at : calls ) {

			for ( int32_t k = 0; k < 4 && k < shape.count; k++ ) {

				const wchar_t* type = shape.types [ k ];
				const wchar_t* name = shape.names [ k ];
				int32_t type_length = base::text_length ( type );
				bool routine = false;

				for ( const wchar_t* ending : endings ) {

					int32_t n = base::text_length ( ending );

					if ( type_length >= n && base::same_text ( type + type_length - n, ending ) )
						routine = true;

				}

				if ( !routine || !name [ 0 ] )
					continue;

				uint64_t routine_at = traced_address ( at, passed [ k ] );

				if ( !routine_at || db->function_starting ( routine_at ) < 0 )
					continue;

				while ( *name == L'_' )
					name++;

				if ( name [ 0 ] == L'l' && name [ 1 ] == L'p' && name [ 2 ] == L'f' && name [ 3 ] == L'n' && name [ 4 ] >= L'A' && name [ 4 ] <= L'Z' )
					name += 4;
				else if ( name [ 0 ] == L'l' && name [ 1 ] == L'p' && name [ 2 ] >= L'A' && name [ 2 ] <= L'Z' )
					name += 2;
				else if ( name [ 0 ] == L'p' && name [ 1 ] == L'f' && name [ 2 ] == L'n' && name [ 3 ] >= L'A' && name [ 3 ] <= L'Z' )
					name += 3;

				user_name entry;

				entry.address = routine_at;
				entry.text.set ( name );
				found.push ( static_cast < user_name&& > ( entry ) );
				sites.push ( at * 4 + static_cast < uint64_t > ( k ) );

			}

		}

	}

	base::list < int32_t > order;

	for ( int32_t i = 0; i < sites.count ( ); i++ )
		order.push ( i );

	base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t a, int32_t b ) { return sites [ static_cast < size_t > ( a ) ] < sites [ static_cast < size_t > ( b ) ]; } );

	for ( int32_t index : order ) {

		const user_name& candidate = found [ static_cast < size_t > ( index ) ];
		bool role_seen = false;
		bool name_seen = false;

		for ( const user_name& item : callback_roles )
			role_seen = role_seen || item.address == candidate.address;

		if ( role_seen )
			continue;

		for ( const user_name& item : callback_names )
			name_seen = name_seen || base::same_text ( item.text.get ( ), candidate.text.get ( ) );

		user_name role;

		role.address = candidate.address;
		role.text.set ( candidate.text.get ( ) );
		callback_roles.push ( static_cast < user_name&& > ( role ) );

		if ( name_seen )
			continue;

		user_name named;

		named.address = candidate.address;
		named.text.set ( candidate.text.get ( ) );
		callback_names.push ( static_cast < user_name&& > ( named ) );

	}

	base::sort ( callback_roles.data ( ), callback_roles.count ( ), [ ] ( const user_name& a, const user_name& b ) { return a.address < b.address; } );
	base::sort ( callback_names.data ( ), callback_names.count ( ), [ ] ( const user_name& a, const user_name& b ) { return a.address < b.address; } );

}

const wchar_t* ui::app::callback_role ( uint64_t function ) const {

	const user_name* item = named_at ( callback_roles, function );

	return item ? item->text.get ( ) : nullptr;

}

bool ui::app::callback_name ( uint64_t start, wchar_t* out, int32_t room ) const {

	const user_name* item = named_at ( callback_names, start );

	if ( !item )
		return false;

	base::copy_text ( item->text.get ( ), out, room );

	return true;

}

bool ui::app::indirect_note ( const x86::instruction& ins, base::writer& out ) const {

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

bool ui::app::symbol_name ( uint64_t address, bool code, wchar_t* out, int32_t room ) const {

	const wchar_t* user = user_text ( renamed, address );

	if ( user ) {

		base::copy_text ( user, out, room );

		return true;

	}

	if ( address == file.base && file.type != loader::format::RAW ) {

		base::copy_text ( L"__ImageBase", out, room );

		return true;

	}

	if ( db && analysis_applied ) {

		int32_t owner = db->function_starting ( address );

		if ( owner >= 0 ) {

			function_name ( owner, out, room );

			return true;

		}

		if ( db->is_start ( address ) ) {

			x86::instruction ins;
			base::writer text;

			text.add ( db->decode ( address, ins ) && ins.name == x86::mnemonic::ret ? L"locret_" : L"loc_" ).hex ( address );

			base::copy_text ( text.text ( ), out, room );

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

			base::copy_text ( text.text ( ), out, room );

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

			base::copy_text ( text.text ( ), out, room );

			return true;

		}

	}

	if ( const loader::symbol* label = file.label_at ( address ) ) {

		base::copy_text ( label->name.get ( ), out, room );

		return true;

	}

	int32_t text_index = string_at ( address );

	if ( text_index >= 0 ) {

		string_label ( text_index, out, room );

		return true;

	}

	for ( const loader::exported& item : file.exports ) {

		if ( item.address == address && !item.name.empty ( ) && item.forward.empty ( ) ) {

			base::copy_text ( item.name.get ( ), out, room );

			return true;

		}

	}

	int32_t section = file.section_at ( address );

	if ( section > 0 && file.sections [ static_cast < size_t > ( section ) ].type != loader::kind::CODE && !code ) {

		if ( api_data_name ( address, out, room ) )
			return true;

		base::writer text;

		text.add ( data_prefix ( address ) ).hex ( address );
		base::copy_text ( text.text ( ), out, room );

		return true;

	}

	return false;

}

uint64_t ui::app::traced_address ( uint64_t call, int32_t family ) const {

	int32_t function = db->function_at ( call );
	uint64_t floor = function >= 0 ? db->functions [ static_cast < size_t > ( function ) ].start : call;
	uint64_t at = call;

	for ( int32_t step = 0; step < 300 && at > floor; step++ ) {

		uint64_t before = db->instruction_at ( at - 1 );

		if ( before >= at )
			return 0;

		at = before;

		if ( !db->is_covered ( at ) )
			continue;

		x86::instruction ins;

		if ( !db->decode ( at, ins ) )
			return 0;

		bool volatile_family = family <= 2 || ( family >= 8 && family <= 11 );

		if ( ins.control == x86::flow::CALL && volatile_family )
			return 0;

		if ( ins.control == x86::flow::RETURN )
			return 0;

		if ( !ins.count || ins.operands [ 0 ].type != x86::kind::REG || x86::register_family ( ins.operands [ 0 ].reg ) != family )
			continue;

		x86::mnemonic name = ins.name;

		if ( name == x86::mnemonic::cmp || name == x86::mnemonic::test || name == x86::mnemonic::push || name == x86::mnemonic::bt )
			continue;

		if ( name == x86::mnemonic::lea && ins.count >= 2 && ins.operands [ 1 ].type == x86::kind::MEM && ins.operands [ 1 ].rip_relative ( ) && x86::register_size ( ins.operands [ 0 ].reg ) == 8 )
			return ins.next ( ) + static_cast < uint64_t > ( ins.operands [ 1 ].value );

		if ( name == x86::mnemonic::mov && ins.count >= 2 && ins.operands [ 1 ].type == x86::kind::REG && x86::register_size ( ins.operands [ 0 ].reg ) == 8 && x86::register_size ( ins.operands [ 1 ].reg ) == 8 ) {

			family = x86::register_family ( ins.operands [ 1 ].reg );

			continue;

		}

		return 0;

	}

	return 0;

}

void ui::app::import_call_sites ( uint64_t slot, base::list < uint64_t >& out ) const {

	int32_t first = 0;
	int32_t count = 0;

	db->refs_to ( slot, first, count );

	for ( int32_t i = 0; i < count; i++ ) {

		uint64_t at = file.base + db->refs [ static_cast < size_t > ( first + i ) ].source;
		x86::instruction ins;

		if ( !db->decode ( at, ins ) )
			continue;

		if ( ins.control == x86::flow::CALL ) {

			if ( decompiler::call_target ( ins ) == slot )
				out.push ( at );

			continue;

		}

		if ( ins.name != x86::mnemonic::mov || ins.count < 2 || ins.operands [ 0 ].type != x86::kind::REG || ins.operands [ 1 ].type != x86::kind::MEM || !ins.operands [ 1 ].rip_relative ( ) || x86::register_size ( ins.operands [ 0 ].reg ) != 8 )
			continue;

		int32_t family = x86::register_family ( ins.operands [ 0 ].reg );
		bool keeps = family == 3 || family == 5 || family == 6 || family == 7 || family >= 12;
		int32_t function = db->function_at ( at );
		uint64_t end = function >= 0 ? db->functions [ static_cast < size_t > ( function ) ].end : at + 0x400;
		uint64_t cursor = ins.next ( );

		for ( int32_t step = 0; step < 400 && cursor < end; step++ ) {

			x86::instruction next;

			if ( !db->decode ( cursor, next ) )
				break;

			if ( next.control == x86::flow::CALL && next.count && next.operands [ 0 ].type == x86::kind::REG && x86::register_family ( next.operands [ 0 ].reg ) == family )
				out.push ( cursor );
			else if ( next.control == x86::flow::CALL && !keeps )
				break;
			else if ( next.count && next.operands [ 0 ].type == x86::kind::REG && x86::register_family ( next.operands [ 0 ].reg ) == family && next.name != x86::mnemonic::cmp && next.name != x86::mnemonic::test && next.name != x86::mnemonic::push )
				break;

			cursor = next.next ( );

		}

	}

}

void ui::app::build_api_names ( ) {

	api_names.clear ( );

	if ( !db || file.arch != loader::machine::X64 )
		return;

	static const int32_t passed [ 4 ] = { 1, 2, 8, 9 };
	static const wchar_t* const named_types [ ] = { L"LPCWSTR", L"LPWSTR", L"LPCSTR", L"LPSTR", L"PCWSTR", L"PWSTR", L"PCSTR", L"PSTR", L"LPCWCH", L"LPWCH", L"LPCCH", L"LPCH", L"LPDWORD", L"PDWORD", L"PLARGE_INTEGER", L"LPCONTEXT", L"PCONTEXT", L"LPSTARTUPINFOW", L"LPSTARTUPINFOA", L"LPSYSTEM_INFO" };
	base::list < user_name > sites;

	for ( const loader::import& item : file.imports ) {

		if ( item.name.empty ( ) )
			continue;

		const wchar_t* text = decompiler::api_prototype ( item.name.get ( ) );
		decompiler::c_prototype shape;
		base::writer problem;

		if ( !text || !decompiler::parse_prototype ( text, true, shape, problem ) )
			continue;

		base::list < uint64_t > calls;

		import_call_sites ( item.slot, calls );

		for ( uint64_t at : calls ) {

			for ( int32_t k = 0; k < 4 && k < shape.count; k++ ) {

				bool fits = false;

				for ( const wchar_t* type : named_types )
					fits = fits || base::same_text ( shape.types [ k ], type );

				const wchar_t* name = shape.names [ k ];

				if ( !fits || !name [ 0 ] )
					continue;

				uint64_t global = traced_address ( at, passed [ k ] );
				int32_t section = global ? file.section_at ( global ) : -1;

				if ( section <= 0 || file.sections [ static_cast < size_t > ( section ) ].type == loader::kind::CODE )
					continue;

				if ( name [ 0 ] == L'l' && name [ 1 ] == L'p' && name [ 2 ] >= L'A' && name [ 2 ] <= L'Z' )
					name += 2;
				else if ( name [ 0 ] == L'p' && name [ 1 ] >= L'A' && name [ 1 ] <= L'Z' )
					name += 1;

				user_name site;

				site.address = at * 4 + static_cast < uint64_t > ( k );
				sites.push ( static_cast < user_name&& > ( site ) );

				user_name named;

				named.address = global;
				named.text.set ( name );
				api_names.push ( static_cast < user_name&& > ( named ) );

			}

		}

	}

	base::list < int32_t > order;

	for ( int32_t i = 0; i < sites.count ( ); i++ )
		order.push ( i );

	base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t a, int32_t b ) { return sites [ static_cast < size_t > ( a ) ].address < sites [ static_cast < size_t > ( b ) ].address; } );

	base::list < user_name > kept;

	for ( int32_t index : order ) {

		const user_name& candidate = api_names [ static_cast < size_t > ( index ) ];
		bool seen = false;

		for ( const user_name& item : kept )
			seen = seen || item.address == candidate.address;

		if ( seen )
			continue;

		user_name named;

		named.address = candidate.address;
		named.text.set ( candidate.text.get ( ) );
		kept.push ( static_cast < user_name&& > ( named ) );

	}

	base::sort ( kept.data ( ), kept.count ( ), [ ] ( const user_name& a, const user_name& b ) { return a.address < b.address; } );
	api_names = static_cast < base::list < user_name >&& > ( kept );

}

bool ui::app::api_data_name ( uint64_t address, wchar_t* out, int32_t room ) const {

	const user_name* item = named_at ( api_names, address );

	if ( !item )
		return false;

	base::copy_text ( item->text.get ( ), out, room );

	return true;

}

const wchar_t* ui::app::data_prefix ( uint64_t address ) const {

	uint64_t offset = 0;

	if ( file.arch == loader::machine::X64 && ( address & 7 ) == 0 && file.offset_of ( address, offset ) && offset + 8 <= file.size ) {

		uint64_t value = file.read64 ( offset );

		if ( value && file.section_at ( value ) > 0 )
			return L"off_";

	}

	if ( !db || !analysis_applied )
		return L"unk_";

	int32_t first = 0;
	int32_t count = 0;

	db->refs_to ( address, first, count );

	for ( int32_t i = 0; i < count; i++ ) {

		const analysis::xref& ref = db->refs [ static_cast < size_t > ( first + i ) ];

		if ( ref.type != analysis::ref::READ && ref.type != analysis::ref::WRITE )
			continue;

		x86::instruction ins;

		if ( !db->decode ( file.base + ref.source, ins ) )
			continue;

		uint8_t size = 0;
		bool vector = false;

		for ( int32_t k = 0; k < ins.count; k++ ) {

			const x86::operand& item = ins.operands [ k ];

			if ( item.type == x86::kind::MEM )
				size = item.size;
			else if ( item.type == x86::kind::REG && item.reg >= x86::XMM0 && item.reg < x86::YMM0 )
				vector = true;

		}

		switch ( size ) {

			case 1:
				return L"byte_";

			case 2:
				return L"word_";

			case 4:
				return vector ? L"flt_" : L"dword_";

			case 8:
				return vector && ins.name != x86::mnemonic::movq ? L"dbl_" : L"qword_";

			case 16:
				return L"xmmword_";

			default:
				break;

		}

	}

	for ( int32_t i = 0; i < count; i++ ) {

		const analysis::xref& ref = db->refs [ static_cast < size_t > ( first + i ) ];

		if ( ref.type != analysis::ref::ADDRESS )
			continue;

		x86::instruction ins;
		uint64_t at = file.base + ref.source;

		if ( !db->decode ( at, ins ) || ins.name != x86::mnemonic::lea || ins.count < 2 || ins.operands [ 0 ].type != x86::kind::REG )
			continue;

		int32_t family = x86::register_family ( ins.operands [ 0 ].reg );

		at = ins.next ( );

		for ( int32_t step = 0; step < 8; step++ ) {

			x86::instruction next;

			if ( !db->decode ( at, next ) )
				break;

			if ( next.control == x86::flow::CALL ) {

				static const int32_t passed [ 4 ] = { 1, 2, 8, 9 };
				int32_t slot_index = -1;

				for ( int32_t k = 0; k < 4; k++ ) {

					if ( passed [ k ] == family )
						slot_index = k;

				}

				uint64_t target = decompiler::call_target ( next );
				const wchar_t* api = nullptr;

				for ( const loader::import& item : file.imports ) {

					if ( item.slot == target && !item.name.empty ( ) )
						api = item.name.get ( );

				}

				const wchar_t* text = api && slot_index >= 0 ? decompiler::api_prototype ( api ) : nullptr;
				decompiler::c_prototype shape;
				base::writer problem;

				if ( text && decompiler::parse_prototype ( text, true, shape, problem ) && slot_index < shape.count ) {

					const wchar_t* type = shape.types [ slot_index ];
					bool starred = false;

					for ( const wchar_t* c = type; *c; c++ )
						starred = starred || *c == L'*';

					bool pointer_name = !starred && ( type [ 0 ] == L'P' || ( type [ 0 ] == L'L' && type [ 1 ] == L'P' ) || ( type [ 0 ] == L'R' && type [ 1 ] == L'E' && type [ 2 ] == L'F' ) ) && !decompiler::void_pointer_text ( type );
					static const wchar_t* const not_structs [ ] = { L"LPSTR", L"LPCSTR", L"LPWSTR", L"LPCWSTR", L"PSTR", L"PCSTR", L"PWSTR", L"PCWSTR", L"LPCVOID", L"LPBYTE", L"PBYTE", L"LPCH", L"LPWCH", L"LPCCH", L"LPCWCH", L"PCNZWCH", L"LPHANDLE", L"PHANDLE",
						L"LPDWORD", L"PDWORD", L"LPWORD", L"PWORD", L"LPLONG", L"PLONG", L"PULONG", L"PUINT", L"LPBOOL", L"PBOOL", L"PSIZE_T", L"PULONG_PTR", L"PDWORD_PTR", L"PDWORD64", L"PHKEY", L"PLARGE_INTEGER", L"PULARGE_INTEGER", L"LPINT", L"PINT", L"PUCHAR", L"PULONG64", L"PLONG64", L"PSID", L"PACL", L"PSECURITY_DESCRIPTOR", L"LPUNKNOWN", L"PTP_WORK", L"PSLIST_ENTRY" };

					for ( const wchar_t* name : not_structs )
						pointer_name = pointer_name && !base::same_text ( type, name );

					if ( pointer_name )
						return L"stru_";

				}

				break;

			}

			if ( next.control != x86::flow::NONE )
				break;

			uint8_t size = 0;
			bool vector = false;
			bool written = false;

			for ( int32_t k = 0; k < next.count; k++ ) {

				const x86::operand& item = next.operands [ k ];

				if ( item.type == x86::kind::MEM && ( ( item.base && x86::register_family ( item.base ) == family ) || ( item.index && x86::register_family ( item.index ) == family ) ) )
					size = item.size;
				else if ( item.type == x86::kind::REG && item.reg >= x86::XMM0 && item.reg < x86::YMM0 )
					vector = true;
				else if ( k == 0 && item.type == x86::kind::REG && x86::register_family ( item.reg ) == family )
					written = true;

			}

			if ( next.name != x86::mnemonic::lea ) {

				switch ( size ) {

					case 1:
						return L"byte_";

					case 2:
						return L"word_";

					case 4:
						return vector ? L"flt_" : L"dword_";

					case 8:
						return vector && next.name != x86::mnemonic::movq ? L"dbl_" : L"qword_";

					case 16:
						return L"xmmword_";

					default:
						break;

				}

			}

			if ( written )
				break;

			at = next.next ( );

		}

	}

	return L"unk_";

}