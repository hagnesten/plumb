#include "decompiler/emit/access/access.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/winstructs/winstructs.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

bool decompiler::emitter::array_element ( int32_t v, uint8_t size, uint8_t bits, bool address ) {

	if ( v < 0 || v >= array_of.count ( ) || array_of [ static_cast < size_t > ( v ) ] < 0 )
		return false;

	int32_t head = array_of [ static_cast < size_t > ( v ) ];
	int64_t k = b.vars [ static_cast < size_t > ( v ) ].offset - b.vars [ static_cast < size_t > ( head ) ].offset;
	int32_t step = array_step [ static_cast < size_t > ( head ) ];
	wchar_t name [ 64 ];

	var_name ( head, name );

	if ( address ) {

		if ( k == 0 ) {

			out.put ( name, syntax::LOCAL );

			return true;

		}

		out.put ( L"&", syntax::OPERATOR );
		out.put ( name, syntax::LOCAL );
		out.put ( L"[" );
		index_number ( k % step == 0 ? k / step : k );
		out.put ( L"]" );

		return true;

	}

	if ( step > 1 && k % step == 0 && size == step ) {

		out.put ( name, syntax::LOCAL );
		out.put ( L"[" );
		index_number ( k / step );
		out.put ( L"]" );

		return true;

	}

	if ( step == 1 && size == 1 ) {

		out.put ( name, syntax::LOCAL );
		out.put ( L"[" );
		index_number ( k );
		out.put ( L"]" );

		return true;

	}

	out.put ( L"*(", syntax::OPERATOR );
	type_name ( size, bits, false, true );

	if ( step == 1 ) {

		out.put ( k ? L" *)&" : L" *)" );
		out.put ( name, syntax::LOCAL );

		if ( k ) {

			out.put ( L"[" );
			index_number ( k );
			out.put ( L"]" );

		}

	} else {

		out.put ( L" *)((char *)" );
		out.put ( name, syntax::LOCAL );
		out.put ( L" + ", syntax::OPERATOR );
		index_number ( k );
		out.put ( L")" );

	}

	return true;

}
bool decompiler::emitter::slot_part ( int32_t e ) const {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::ADD || node.a < 0 || node.b < 0 )
		return false;

	const expr& base = b.exprs [ static_cast < size_t > ( node.a ) ];
	const expr& step = b.exprs [ static_cast < size_t > ( node.b ) ];

	return base.kind == op::ADDRESS && base.a < 0 && base.value >= 0 && base.value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( base.value ) ].kind == var_kind::STACK && step.kind == op::CONST && step.value > 0 && step.value < 16 && !( step.bits & POINTER );

}
int32_t decompiler::emitter::pointer_step ( int32_t e ) const {

	if ( e < 0 )
		return 0;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::VAR || node.value < 0 || node.value >= element.count ( ) )
		return 0;

	uint8_t shape = element [ static_cast < size_t > ( node.value ) ];

	if ( !shape )
		return 0;

	if ( shape == 0x10 )
		return 1;

	if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) )
		return 8;

	return shape & 0x0F;

}
bool decompiler::emitter::void_element_load ( int32_t e ) const {

	if ( e < 0 || b.exprs [ static_cast < size_t > ( e ) ].kind != op::LOAD || b.exprs [ static_cast < size_t > ( e ) ].size != 8 )
		return false;

	int32_t at = b.exprs [ static_cast < size_t > ( e ) ].a;

	if ( at >= 0 && b.exprs [ static_cast < size_t > ( at ) ].kind == op::ADD && b.exprs [ static_cast < size_t > ( at ) ].b >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( at ) ].b ) ].kind == op::CONST ) {

		if ( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( at ) ].b ) ].value % 8 != 0 )
			return false;

		at = b.exprs [ static_cast < size_t > ( at ) ].a;

	}

	if ( at < 0 || b.exprs [ static_cast < size_t > ( at ) ].kind != op::VAR )
		return false;

	int64_t v = b.exprs [ static_cast < size_t > ( at ) ].value;

	return v >= 0 && v < element.count ( ) && ( element [ static_cast < size_t > ( v ) ] & 0x9F ) == 8 && ( element [ static_cast < size_t > ( v ) ] & 0x40 );

}
bool decompiler::emitter::global_index ( int32_t address, int32_t size, int32_t& index, uint64_t& start ) const {

	if ( !global_arrays || address < 0 )
		return false;

	const expr& sum = b.exprs [ static_cast < size_t > ( address ) ];

	if ( sum.kind != op::ADD || sum.a < 0 || sum.b < 0 || sum.size != 8 )
		return false;

	for ( int32_t side = 0; side < 2; side++ ) {

		const expr& base_node = b.exprs [ static_cast < size_t > ( side ? sum.b : sum.a ) ];
		int32_t other = side ? sum.a : sum.b;
		const expr& scaled = b.exprs [ static_cast < size_t > ( other ) ];

		if ( base_node.kind != op::CONST || !( base_node.bits & POINTER ) || b.file->section_at ( static_cast < uint64_t > ( base_node.value ) ) <= 0 )
			continue;

		if ( scaled.kind == op::MUL && scaled.a >= 0 && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == size && size > 1 ) {

			index = scaled.a;
			start = static_cast < uint64_t > ( base_node.value );

			return true;

		}

		if ( scaled.kind == op::SHL && scaled.a >= 0 && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && size > 1 && ( 1ll << b.exprs [ static_cast < size_t > ( scaled.b ) ].value ) == size ) {

			index = scaled.a;
			start = static_cast < uint64_t > ( base_node.value );

			return true;

		}

		if ( size == 1 && !( scaled.bits & POINTER ) && scaled.kind != op::CONST ) {

			index = other;
			start = static_cast < uint64_t > ( base_node.value );

			return true;

		}

	}

	return false;

}
bool decompiler::emitter::split_global ( int32_t address, global_access& out_access ) const {

	if ( address < 0 )
		return false;

	const expr& sum = b.exprs [ static_cast < size_t > ( address ) ];

	if ( sum.kind != op::ADD || sum.a < 0 || sum.b < 0 || sum.size != 8 )
		return false;

	for ( int32_t side = 0; side < 2; side++ ) {

		const expr& base_node = b.exprs [ static_cast < size_t > ( side ? sum.b : sum.a ) ];
		int32_t other = side ? sum.a : sum.b;
		const expr& scaled = b.exprs [ static_cast < size_t > ( other ) ];

		if ( base_node.kind != op::CONST || !( base_node.bits & POINTER ) || b.file->section_at ( static_cast < uint64_t > ( base_node.value ) ) <= 0 )
			continue;

		out_access.start = static_cast < uint64_t > ( base_node.value );

		if ( scaled.kind == op::MUL && scaled.a >= 0 && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value > 0 ) {

			out_access.index = scaled.a;
			out_access.scale = b.exprs [ static_cast < size_t > ( scaled.b ) ].value;

			int32_t plain = bare_index ( scaled.a );

			for ( int32_t k = 0; plain >= 0 && k + 3 < scale_defs.count ( ); k += 4 ) {

				int32_t from = scale_defs [ static_cast < size_t > ( k + 1 ) ];
				int64_t factor = scale_defs [ static_cast < size_t > ( k + 2 ) ];

				if ( ordered_scales && printing_line >= 0 && scale_defs [ static_cast < size_t > ( k + 3 ) ] >= printing_line )
					continue;

				if ( factor > 1 && out_access.scale % factor == 0 && b.same ( from, plain ) ) {

					out_access.index = scale_defs [ static_cast < size_t > ( k ) ];
					out_access.scale /= factor;
					out_access.renamed = true;

					break;

				}

			}

			return true;

		}

		if ( scaled.kind == op::SHL && scaled.a >= 0 && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value < 8 ) {

			out_access.index = scaled.a;
			out_access.scale = 1ll << b.exprs [ static_cast < size_t > ( scaled.b ) ].value;

			return true;

		}

		if ( !( scaled.bits & POINTER ) && scaled.kind != op::CONST ) {

			out_access.index = other;
			out_access.scale = 1;

			return true;

		}

	}

	return false;

}
bool decompiler::emitter::pointer_slot ( uint64_t address ) const {

	uint64_t offset = 0;

	if ( !b.file || !b.file->offset_of ( address, offset ) || offset + 8 > b.file->size )
		return false;

	uint64_t value = 0;

	for ( int32_t i = 7; i >= 0; i-- )
		value = ( value << 8 ) | b.file->byte ( offset + static_cast < uint64_t > ( i ) );

	return value && b.file->section_at ( value ) >= 0;

}
int32_t decompiler::emitter::bare_index ( int32_t e ) const {

	while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
		e = b.exprs [ static_cast < size_t > ( e ) ].a;

	return e;

}
void decompiler::emitter::collect_anchors ( ) {

	anchors_ready = true;
	anchors.clear ( );
	scale_defs.clear ( );
	printing_line = -1;

	{

		base::list < int32_t > defs;

		defs.fill ( b.vars.size ( ), 0 );

		for ( const statement& line : b.statements ) {

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].value < b.vars.count ( ) )
				defs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ]++;

		}

		for ( int32_t s = 0; s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
				continue;

			const expr& target = b.exprs [ static_cast < size_t > ( line.left ) ];
			const expr& source = b.exprs [ static_cast < size_t > ( line.right ) ];

			if ( target.kind != op::VAR || target.value < 0 || target.value >= b.vars.count ( ) || defs [ static_cast < size_t > ( target.value ) ] != 1 )
				continue;

			if ( source.kind != op::MUL || source.a < 0 || source.b < 0 || b.exprs [ static_cast < size_t > ( source.b ) ].kind != op::CONST || b.exprs [ static_cast < size_t > ( source.b ) ].value <= 1 || b.exprs [ static_cast < size_t > ( source.b ) ].value > 0x10000 )
				continue;

			scale_defs.push ( line.left );
			scale_defs.push ( bare_index ( source.a ) );
			scale_defs.push ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( source.b ) ].value ) );
			scale_defs.push ( s );

		}

	}

	auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 96 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
		global_access item;

		if ( node.kind == op::LOAD && !( node.bits & FLOATING ) && node.size <= 8 && split_global ( node.a, item ) ) {

			item.size = node.size;
			anchors.push ( item );

		}

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const block_code& block : b.code ) {

		if ( !block.reachable )
			continue;

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
				continue;

			walk ( walk, line.left, 0 );
			walk ( walk, line.right, 0 );

		}

		walk ( walk, block.condition, 0 );
		walk ( walk, block.selector, 0 );
		walk ( walk, block.returned, 0 );

	}

	for ( int32_t i = 0; i < anchors.count ( ); i++ ) {

		global_access& item = anchors [ static_cast < size_t > ( i ) ];
		uint64_t lowest = item.start;
		uint8_t element = item.size;
		int32_t distinct = 0;

		for ( const global_access& other : anchors ) {

			if ( other.scale != item.scale || !b.same ( bare_index ( other.index ), bare_index ( item.index ) ) )
				continue;

			uint64_t gap = other.start > item.start ? other.start - item.start : item.start - other.start;

			if ( gap >= 0x400 )
				continue;

			if ( other.start < lowest || ( other.start == lowest && other.size < element ) ) {

				lowest = other.start;
				element = other.size;

			}

		}

		for ( const global_access& other : anchors ) {

			if ( other.scale == item.scale && b.same ( bare_index ( other.index ), bare_index ( item.index ) ) && other.start != item.start && ( other.start > item.start ? other.start - item.start : item.start - other.start ) < 0x400 )
				distinct++;

		}

		if ( element && ( item.scale % element != 0 || ( item.start - lowest ) % element != 0 ) ) {

			uint8_t smallest = element;

			for ( const global_access& other : anchors ) {

				if ( other.scale == item.scale && b.same ( bare_index ( other.index ), bare_index ( item.index ) ) && ( other.start > item.start ? other.start - item.start : item.start - other.start ) < 0x400 && other.size < smallest )
					smallest = other.size;

			}

			element = smallest;

		}

		if ( !distinct && pointer_tables && item.size == 8 && item.scale > 8 && item.scale % 8 == 0 && lowest == item.start && pointer_slot ( item.start ) ) {

			item.base = item.start;
			item.element = 8;
			item.grouped = true;

			continue;

		}

		if ( !distinct || element == 0 || item.scale % element != 0 || ( item.start - lowest ) % element != 0 )
			continue;

		item.base = lowest;
		item.element = element;
		item.grouped = true;

	}

}
bool decompiler::emitter::anchored_access ( const expr& item, int32_t parent ) {

	if ( !anchored_globals || !global_arrays || item.kind != op::LOAD || ( item.bits & FLOATING ) )
		return false;

	if ( !anchors_ready )
		collect_anchors ( );

	global_access here;

	if ( !split_global ( item.a, here ) )
		return false;

	const global_access* found = nullptr;

	for ( const global_access& other : anchors ) {

		if ( other.grouped && other.start == here.start && other.scale == here.scale && other.size == item.size && b.same ( bare_index ( other.index ), bare_index ( here.index ) ) )
			found = &other;

	}

	if ( !found || ( found->base == found->start && found->element == item.size && found->scale == item.size && !here.renamed ) )
		return false;

	if ( item.size < found->element && item.size != 1 && item.size != 2 )
		return false;

	int64_t step = found->scale / found->element;
	int64_t extra = static_cast < int64_t > ( ( found->start - found->base ) / found->element );
	bool narrow = item.size < found->element;
	bool wide = item.size > found->element;

	if ( narrow ) {

		out.put ( item.size == 1 ? L"LOBYTE" : L"LOWORD", syntax::FUNCTION );
		out.put ( L"(" );

	} else if ( wide ) {

		if ( parent > 14 )
			out.put ( L"(" );

		out.put ( L"*(" );
		type_name ( item.size, item.bits, false, true );
		out.put ( L" *)&" );

	}

	wchar_t label [ 200 ];

	if ( b.names->name ( found->base, false, label, 200 ) )
		out.put ( label, syntax::GLOBAL, found->base );
	else
		number ( static_cast < int64_t > ( found->base ), 8, true, false );

	out.put ( L"[" );

	int32_t shown_index = here.index;

	if ( pointer_tables && shown_index >= 0 ) {

		const expr& widened = b.exprs [ static_cast < size_t > ( shown_index ) ];

		if ( widened.kind == op::CAST && widened.size == 8 && widened.a >= 0 && b.exprs [ static_cast < size_t > ( widened.a ) ].size == 4 && !( widened.bits & ( FLOATING | POINTER ) ) )
			shown_index = widened.a;

	}

	if ( step != 1 ) {

		out.put ( base::writer ( ).number ( step ).text ( ), syntax::NUMBER );
		out.put ( L" * ", syntax::OPERATOR );
		value ( shown_index, 13 );

	} else
		value ( shown_index, extra ? 11 : 0 );

	if ( extra ) {

		out.put ( extra < 0 ? L" - " : L" + ", syntax::OPERATOR );
		out.put ( base::writer ( ).number ( extra < 0 ? -extra : extra ).text ( ), syntax::NUMBER );

	}

	out.put ( L"]" );

	if ( narrow )
		out.put ( L")" );
	else if ( wide && parent > 14 )
		out.put ( L")" );

	return true;

}
void decompiler::emitter::global_element ( uint64_t start, int32_t index ) {

	wchar_t label [ 200 ];

	if ( b.names->name ( start, false, label, 200 ) )
		out.put ( label, syntax::GLOBAL, start );
	else
		number ( static_cast < int64_t > ( start ), 8, true, false );

	out.put ( L"[" );

	int32_t shown = index;

	while ( shown >= 0 && b.exprs [ static_cast < size_t > ( shown ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( shown ) ].a >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( shown ) ].a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( shown ) ].size > b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( shown ) ].a ) ].size )
		shown = b.exprs [ static_cast < size_t > ( shown ) ].a;

	value ( shown, 0 );
	out.put ( L"]" );

}
bool decompiler::emitter::indexed_element ( const expr& item ) {

	if ( !indexed_arrays || item.a < 0 || ( item.bits & FLOATING ) )
		return false;

	const expr& sum = b.exprs [ static_cast < size_t > ( item.a ) ];

	if ( sum.kind != op::ADD || sum.a < 0 || sum.b < 0 )
		return false;

	for ( int32_t k = 0; k < 2; k++ ) {

		const expr& base_side = b.exprs [ static_cast < size_t > ( k ? sum.b : sum.a ) ];
		int32_t scaled_index = k ? sum.a : sum.b;
		const expr& scaled = b.exprs [ static_cast < size_t > ( scaled_index ) ];

		if ( base_side.kind != op::ADDRESS )
			continue;

		int64_t v = base_side.a < 0 ? base_side.value : ( b.exprs [ static_cast < size_t > ( base_side.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( base_side.a ) ].value : -1 );

		if ( v < 0 || v >= array_of.count ( ) || array_of [ static_cast < size_t > ( v ) ] != v )
			continue;

		int32_t step = array_step [ static_cast < size_t > ( v ) ];

		if ( step != item.size )
			continue;

		int32_t index = -1;

		if ( step == 1 && scaled.kind != op::CONST )
			index = scaled_index;
		else if ( scaled.kind == op::MUL && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == step )
			index = scaled.a;

		if ( index < 0 )
			continue;

		wchar_t name [ 64 ];

		var_name ( static_cast < int32_t > ( v ), name );
		out.put ( name, syntax::LOCAL );
		out.put ( L"[" );
		value ( index, 0 );
		out.put ( L"]" );

		return true;

	}

	return false;

}
void decompiler::emitter::lvalue_or_load ( const expr& item, int32_t parent ) {

	if ( indexed_element ( item ) )
		return;

	if ( anchored_access ( item, parent ) )
		return;

	const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
	wchar_t name [ 64 ];

	{

		int32_t at_index = -1;
		uint64_t start = 0;

		if ( global_index ( item.a, item.size, at_index, start ) ) {

			global_element ( start, at_index );

			return;

		}

	}

	if ( where.kind == op::ADDRESS && where.value >= 0 ) {

		if ( array_element ( static_cast < int32_t > ( where.value ), item.size, item.bits, false ) )
			return;

		var_name ( static_cast < int32_t > ( where.value ), name );
		out.put ( name, syntax::LOCAL );

		return;

	}

	if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && pointer_step ( where.a ) == item.size && item.size > 1 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::MUL && !( item.bits & FLOATING ) == !( element [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value ) ] & 0x80 ) ) {

		const expr& scaled = b.exprs [ static_cast < size_t > ( where.b ) ];

		if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == item.size ) {

			value ( where.a, 15 );
			out.put ( L"[" );
			value ( scaled.a, 0 );
			out.put ( L"]" );

			return;

		}

	}

	if ( byte_indexes && where.kind == op::ADD && where.a >= 0 && where.b >= 0 && item.size == 1 && !( item.bits & FLOATING ) ) {

		for ( int32_t side = 0; side < 2; side++ ) {

			int32_t pointer = side ? where.b : where.a;
			int32_t index = side ? where.a : where.b;
			const expr& offset_node = b.exprs [ static_cast < size_t > ( index ) ];

			if ( b.exprs [ static_cast < size_t > ( pointer ) ].kind != op::VAR || pointer_step ( pointer ) != 1 || offset_node.kind == op::CONST || ( offset_node.bits & POINTER ) || pointer_step ( index ) > 0 )
				continue;

			value ( pointer, 15 );
			out.put ( L"[" );
			value ( index, 0 );
			out.put ( L"]" );

			return;

		}

	}

	if ( slot_part ( item.a ) ) {

		const expr& base = b.exprs [ static_cast < size_t > ( where.a ) ];
		int64_t k = b.exprs [ static_cast < size_t > ( where.b ) ].value;

		var_name ( static_cast < int32_t > ( base.value ), name );

		int32_t whole = base.value >= 0 && base.value < b.vars.count ( ) ? b.vars [ static_cast < size_t > ( base.value ) ].size : 0;

		if ( part_macro_slots && ( item.size == 1 || item.size == 2 || item.size == 4 ) && !( item.bits & FLOATING ) && ( whole == 8 || whole == 16 ) && k > 0 && k % item.size == 0 && k + item.size <= whole ) {

			const wchar_t* unit = item.size == 4 ? L"DWORD" : ( item.size == 2 ? L"WORD" : L"BYTE" );
			base::writer macro;

			if ( item.bits & SIGNED )
				macro.add ( L"S" );

			if ( k + item.size == whole )
				macro.add ( L"HI" ).add ( unit );
			else
				macro.add ( unit ).number ( k / item.size );

			out.put ( macro.text ( ), syntax::FUNCTION );
			out.put ( L"(" );
			out.put ( name, syntax::LOCAL );
			out.put ( L")" );

			return;

		}

		if ( parent > 14 )
			out.put ( L"(" );

		if ( item.size && k % item.size == 0 ) {

			out.put ( L"*((", syntax::OPERATOR );
			type_name ( item.size, item.bits, false, true );
			out.put ( L" *)&" );
			out.put ( name, syntax::LOCAL );
			out.put ( L" + ", syntax::OPERATOR );
			index_number ( k / item.size );
			out.put ( L")" );

		} else {

			out.put ( L"*(", syntax::OPERATOR );
			type_name ( item.size, item.bits, false, true );
			out.put ( L" *)((char *)&" );
			out.put ( name, syntax::LOCAL );
			out.put ( L" + ", syntax::OPERATOR );
			index_number ( k );
			out.put ( L")" );

		}

		if ( parent > 14 )
			out.put ( L")" );

		return;

	}

	if ( !( item.bits & FLOATING ) && char_load ( item, parent ) )
		return;

	{

		int32_t base = item.a;
		int64_t field = 0;

		if ( where.kind == op::ADD && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

			base = where.a;
			field = b.exprs [ static_cast < size_t > ( where.b ) ].value;

		}

		const expr& root = b.exprs [ static_cast < size_t > ( base ) ];

		if ( root.kind == op::INTRINSIC && root.arg_count == 0 && base::same_text ( intrinsic_name ( static_cast < int32_t > ( root.value ) ), L"NtCurrentTeb" ) ) {

			static const wchar_t* const fields [ 14 ] = { L"NtTib.ExceptionList", L"NtTib.StackBase", L"NtTib.StackLimit", L"NtTib.SubSystemTib", L"NtTib.FiberData", L"NtTib.ArbitraryUserPointer", L"NtTib.Self", L"EnvironmentPointer", L"ClientId.UniqueProcess", L"ClientId.UniqueThread", L"ActiveRpcHandle", L"ThreadLocalStoragePointer", L"ProcessEnvironmentBlock", L"LastErrorValue" };
			int64_t slot = field / 8;

			if ( field >= 0 && field % 8 == 0 && slot < 14 && ( item.size == 8 || slot == 13 ) ) {

				out.put ( L"NtCurrentTeb", syntax::FUNCTION );
				out.put ( L"()->" );
				out.put ( fields [ slot ], syntax::GLOBAL );

				return;

			}

		}

	}

	int32_t var = -1;
	int64_t offset = 0;

	if ( where.kind == op::VAR )
		var = static_cast < int32_t > ( where.value );
	else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

		var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );
		offset = b.exprs [ static_cast < size_t > ( where.b ) ].value;

	}

	if ( var >= 0 && var < b.var_structs.count ( ) && !b.var_structs [ static_cast < size_t > ( var ) ].empty ( ) ) {

		wchar_t field [ 64 ];
		uint64_t tag = 0;
		int32_t field_size = 0;

		if ( b.names->struct_field ( b.var_structs [ static_cast < size_t > ( var ) ].get ( ), offset, field, 64, tag, field_size ) ) {

			bool exact = field_size == item.size || field_size == 0;

			if ( !exact ) {

				out.put ( L"*(" );
				type_name ( item.size, item.bits, false, true );
				out.put ( L" *)&" );

			}

			var_name ( var, name );
			out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
			out.put ( L"->", syntax::OPERATOR );
			out.put ( field, syntax::GLOBAL, tag );

			return;

		}

		int32_t slot = 0;
		const builtin_field* known = win_structs ? builtin_field_at ( b.var_structs [ static_cast < size_t > ( var ) ].get ( ), offset, slot ) : nullptr;

		if ( known ) {

			bool exact = known->size == item.size;

			if ( !exact ) {

				out.put ( L"*(" );
				type_name ( item.size, item.bits, false, true );
				out.put ( L" *)&" );

			}

			var_name ( var, name );
			out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
			out.put ( L"->", syntax::OPERATOR );
			out.put ( known->name, syntax::GLOBAL );

			if ( known->count > 1 ) {

				out.put ( L"[" );
				index_number ( slot );
				out.put ( L"]" );

			}

			return;

		}

	}

	if ( var >= 0 && var < element.count ( ) && element [ static_cast < size_t > ( var ) ] && ( element [ static_cast < size_t > ( var ) ] & 0x1F ) == item.size ) {

		var_name ( var, name );

		if ( offset == 0 ) {

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"*", syntax::OPERATOR );

			if ( !put_folded ( var, name ) )
				out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

			if ( parent > 14 )
				out.put ( L")" );

		} else {

			if ( !put_folded ( var, name ) )
				out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

			out.put ( L"[" );
			index_number ( offset / item.size );
			out.put ( L"]" );

		}

		return;

	}

	if ( cast_index_access && var >= 0 && var < element.count ( ) && element [ static_cast < size_t > ( var ) ] && ( element [ static_cast < size_t > ( var ) ] & 0x1F ) != item.size && offset > 0 && item.size > 0 && offset % item.size == 0 && !( item.bits & FLOATING ) && item.size <= 16 ) {

		var_name ( var, name );

		if ( parent > 14 )
			out.put ( L"(" );

		out.put ( L"*((", syntax::OPERATOR );
		type_name ( item.size, item.bits, false, true );
		out.put ( L" *)" );

		if ( !put_folded ( var, name ) )
			out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

		out.put ( L" + ", syntax::OPERATOR );
		index_number ( offset / item.size );
		out.put ( L")" );

		if ( parent > 14 )
			out.put ( L")" );

		return;

	}

	if ( var >= 0 && field_of ( var, offset ) ) {

		wchar_t field [ 32 ] = L"field_";

		var_name ( var, name );
		hex ( static_cast < uint64_t > ( offset ), field + 6 );
		out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
		out.put ( L"->", syntax::OPERATOR );
		out.put ( field, syntax::GLOBAL );

		return;

	}

	if ( parent > 14 )
		out.put ( L"(" );

	out.put ( L"*(", syntax::OPERATOR );
	type_name ( item.size, item.bits, false, true );
	out.put ( L" *)" );
	value ( item.a, 14 );

	if ( parent > 14 )
		out.put ( L")" );

}
void decompiler::emitter::extended_load ( const expr& item, int32_t parent, uint8_t sign ) {

	const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];

	if ( anchored_access ( item, parent ) )
		return;

	{

		int32_t at_index = -1;
		uint64_t start = 0;

		if ( global_index ( item.a, item.size, at_index, start ) ) {

			global_element ( start, at_index );

			return;

		}

	}

	int32_t var = -1;

	if ( where.kind == op::VAR )
		var = static_cast < int32_t > ( where.value );
	else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST )
		var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );

	bool fielded = var >= 0 && ( has_struct ( var ) || ( var < b.var_structs.count ( ) && !b.var_structs [ static_cast < size_t > ( var ) ].empty ( ) ) || ( var < element.count ( ) && ( element [ static_cast < size_t > ( var ) ] & 0x1F ) == item.size ) );

	if ( item.bits & FLOATING || fielded || ( where.kind == op::ADDRESS && where.value >= 0 ) ) {

		lvalue_or_load ( item, parent );

		return;

	}

	if ( parent > 14 )
		out.put ( L"(" );

	out.put ( L"*(", syntax::OPERATOR );
	type_name ( item.size, sign, false );
	out.put ( L" *)" );
	value ( item.a, 14 );

	if ( parent > 14 )
		out.put ( L")" );

}