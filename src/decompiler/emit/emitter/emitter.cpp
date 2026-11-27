#include "decompiler/emit/emitter/emitter.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

bool decompiler::emitter::find_init ( int64_t var, int32_t& found, int32_t& line ) {

	if ( dry )
		return false;

	int32_t expected = out.r->lines.count ( ) - 1;

	for ( int32_t i = recent_line.count ( ) - 1; i >= 0 && expected >= 0 && recent_line.count ( ) - i <= 3; i-- ) {

		if ( recent_line [ static_cast < size_t > ( i ) ] != expected )
			return false;

		const statement& item = b.statements [ static_cast < size_t > ( recent_assign [ static_cast < size_t > ( i ) ] ) ];

		if ( b.exprs [ static_cast < size_t > ( item.left ) ].value == var ) {

			found = recent_assign [ static_cast < size_t > ( i ) ];
			line = expected;

			return true;

		}

		if ( b.references ( item.right, static_cast < int32_t > ( var ), -1 ) )
			return false;

		expected--;

	}

	return false;

}

decompiler::emitter::emitter ( builder& owner ) : b ( owner ) {

	out.spaced = owner.names && owner.names->spaced ( );
	out.names = owner.names;
	next_number = owner.params;
	no_ternary = owner.names && owner.names->expand_ternary ( );
	no_casts = owner.names && owner.names->hide_casts ( );
	indexing = !owner.names || owner.names->index_pointers ( );

}

void decompiler::emitter::hex ( uint64_t value, wchar_t* out_text ) {

	static const wchar_t digits [ ] = L"0123456789ABCDEF";
	wchar_t reverse [ 20 ];
	int32_t n = 0;
	int32_t length = 0;

	do {

		reverse [ n++ ] = digits [ value & 15 ];
		value >>= 4;

	} while ( value );

	while ( n )
		out_text [ length++ ] = reverse [ --n ];

	out_text [ length ] = 0;

}

bool decompiler::emitter::renumber ( ) {

	base::list < int32_t > order;
	base::list < uint64_t > first_def;
	bool changed = false;

	first_def.fill ( b.vars.size ( ), ~0ull );

	for ( const statement& line : b.statements ) {

		if ( line.kind != stmt::ASSIGN || line.left < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || ( !line.address && !line.origin ) )
			continue;

		int64_t v = b.exprs [ static_cast < size_t > ( line.left ) ].value;

		uint64_t at = line.origin ? line.origin : line.address;

		if ( v >= 0 && v < first_def.count ( ) && at < first_def [ static_cast < size_t > ( v ) ] )
			first_def [ static_cast < size_t > ( v ) ] = at;

	}

	for ( int32_t v = 0; v < b.vars.count ( ) && born_numbers; v++ ) {

		if ( first_def [ static_cast < size_t > ( v ) ] == ~0ull && b.vars [ static_cast < size_t > ( v ) ].born )
			first_def [ static_cast < size_t > ( v ) ] = b.vars [ static_cast < size_t > ( v ) ].born;

	}

	for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

		const variable& item = b.vars [ static_cast < size_t > ( v ) ];

		if ( ghost_numbers && item.ghost && item.born && by_address && var_number [ static_cast < size_t > ( v ) ] < 0 ) {

			first_def [ static_cast < size_t > ( v ) ] = item.born;
			order.push ( v );

			continue;

		}

		if ( var_number [ static_cast < size_t > ( v ) ] >= 0 && item.kind != var_kind::PARAMETER )
			order.push ( v );

	}

	base::list < uint64_t > extra;

	auto def_of = [ & ] ( int32_t x ) {

		return x < 0 ? extra [ static_cast < size_t > ( -x - 1 ) ] : first_def [ static_cast < size_t > ( x ) ];

	};

	if ( result_ghosts && by_address && b.returns_value && !b.returns_float && b.result_at && !b.has_prototype ) {

		bool named = result_var >= 0 && result_var < first_def.count ( ) && var_number [ static_cast < size_t > ( result_var ) ] >= 0;

		if ( named ) {

			if ( b.result_at < first_def [ static_cast < size_t > ( result_var ) ] )
				first_def [ static_cast < size_t > ( result_var ) ] = b.result_at;

		} else if ( b.return_points >= 2 ) {

			bool taken = false;

			for ( int32_t v : order )
				taken = taken || ( v >= 0 && first_def [ static_cast < size_t > ( v ) ] == b.result_at && !b.vars [ static_cast < size_t > ( v ) ].ghost );

			if ( !taken ) {

				extra.push ( b.result_at );
				order.push ( -extra.count ( ) );

			}

		}

	}

	base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t x, int32_t y ) {

		bool left_stack = x >= 0 && b.vars [ static_cast < size_t > ( x ) ].kind == var_kind::STACK;
		bool right_stack = y >= 0 && b.vars [ static_cast < size_t > ( y ) ].kind == var_kind::STACK;

		if ( left_stack != right_stack )
			return !left_stack;

		if ( left_stack )
			return b.vars [ static_cast < size_t > ( x ) ].offset < b.vars [ static_cast < size_t > ( y ) ].offset;

		if ( by_address && def_of ( x ) != def_of ( y ) )
			return def_of ( x ) < def_of ( y );

		if ( x < 0 || y < 0 )
			return x < y;

		return var_number [ static_cast < size_t > ( x ) ] < var_number [ static_cast < size_t > ( y ) ];

	} );

	int32_t next = b.params;

	for ( int32_t v : order ) {

		if ( v < 0 || ( b.vars [ static_cast < size_t > ( v ) ].ghost && var_number [ static_cast < size_t > ( v ) ] < 0 ) ) {

			next++;

			continue;

		}

		changed = changed || var_number [ static_cast < size_t > ( v ) ] != next;
		var_number [ static_cast < size_t > ( v ) ] = next++;

	}

	if ( b.names && b.names->number_notes ( ) ) {

		b.number_at.clear ( );
		b.number_at.fill ( b.vars.size ( ), 0 );
		b.ghost_at.clear ( );

		for ( int32_t v : order ) {

			uint64_t at = def_of ( v ) == ~0ull ? 0 : def_of ( v );

			if ( v < 0 || ( b.vars [ static_cast < size_t > ( v ) ].ghost && var_number [ static_cast < size_t > ( v ) ] < 0 ) )
				b.ghost_at.push ( at );
			else
				b.number_at [ static_cast < size_t > ( v ) ] = at;

		}

	}

	next_number = next;

	return changed;

}

void decompiler::emitter::var_name ( int32_t index, wchar_t* text ) {

	default_name ( index, text );

	const variable& item = b.vars [ static_cast < size_t > ( index ) ];
	const wchar_t* user = b.names ? b.names->local_name ( b.function, text ) : nullptr;

	if ( !user && item.kind == var_kind::PARAMETER && b.has_prototype && item.position >= 0 && item.position < b.prototype.count && b.prototype.names [ item.position ] [ 0 ] )
		user = b.prototype.names [ item.position ];

	static const wchar_t* const letters [ 10 ] = { L"i", L"j", L"k", L"m", L"n", L"ii", L"jj", L"kk", L"mm", L"nn" };
	bool lettered = !user && index < loop_letter.count ( ) && loop_letter [ static_cast < size_t > ( index ) ] >= 0;

	if ( !user && index != result_var && ( lettered || ( index < b.call_names.count ( ) && !b.call_names [ static_cast < size_t > ( index ) ].empty ( ) ) ) ) {

		user = lettered ? letters [ loop_letter [ static_cast < size_t > ( index ) ] ] : b.call_names [ static_cast < size_t > ( index ) ].get ( );

		if ( b.out ) {

			bool known = false;

			for ( const name_alias& alias : b.out->aliases )
				known = known || base::same_text ( alias.key, text );

			if ( !known ) {

				name_alias& made = b.out->aliases.emplace ( );

				for ( int32_t k = 0; user [ k ] && k < 63; k++ )
					made.shown [ k ] = user [ k ];

				for ( int32_t k = 0; text [ k ] && k < 63; k++ )
					made.key [ k ] = text [ k ];

			}

		}

	}

	if ( user ) {

		int32_t length = 0;

		for ( ; user [ length ] && length < 60; length++ )
			text [ length ] = user [ length ];

		text [ length ] = 0;

	}

}

void decompiler::emitter::default_name ( int32_t index, wchar_t* text ) {

	const variable& item = b.vars [ static_cast < size_t > ( index ) ];
	int32_t length = 0;

	if ( !dry && index < referenced.count ( ) )
		referenced [ static_cast < size_t > ( index ) ] = 1;

	auto add = [ & ] ( const wchar_t* part ) {

		for ( ; *part && length < 30; part++ )
			text [ length++ ] = *part;

	};

	auto decimal = [ & ] ( int32_t value ) {

		wchar_t reverse [ 12 ];
		int32_t n = 0;

		do {

			reverse [ n++ ] = static_cast < wchar_t > ( L'0' + value % 10 );
			value /= 10;

		} while ( value && n < 11 );

		while ( n && length < 30 )
			text [ length++ ] = reverse [ --n ];

	};

	if ( item.kind == var_kind::PARAMETER ) {

		add ( L"a" );
		decimal ( item.position + 1 );

	} else {

		if ( var_number [ static_cast < size_t > ( index ) ] < 0 )
			var_number [ static_cast < size_t > ( index ) ] = next_number++;

		if ( index == result_var )
			add ( L"result" );
		else if ( index == si128_var )
			add ( L"si128" );
		else {

			add ( L"v" );
			decimal ( var_number [ static_cast < size_t > ( index ) ] );

		}

	}

	text [ length ] = 0;

}

bool decompiler::emitter::has_struct ( int32_t var ) {

	const variable& owner = b.vars [ static_cast < size_t > ( var ) ];

	if ( var < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( var ) ].empty ( ) )
		return false;

	if ( owner.kind != var_kind::PARAMETER || owner.bits & FLOATING || !b.names || !b.names->guess_structs ( ) )
		return false;

	for ( const statement& item : b.statements ) {

		if ( item.kind == stmt::ASSIGN && item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( item.left ) ].value == var )
			return false;

	}

	int32_t distinct = 0;

	for ( const field_use& item : b.fields ) {

		if ( item.var == var )
			distinct++;

	}

	return distinct >= 2;

}

int32_t decompiler::emitter::lane_var ( int32_t e ) const {

	if ( !scalar_lanes || e < 0 )
		return -1;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.size >= 16 || !( node.bits & FLOATING ) )
		return -1;

	int32_t at = e;

	if ( node.kind == op::CAST && node.a >= 0 )
		at = node.a;

	const expr& inner = b.exprs [ static_cast < size_t > ( at ) ];

	if ( inner.kind != op::VAR || inner.value < 0 || inner.value >= vector.count ( ) )
		return -1;

	uint8_t flavor = vector [ static_cast < size_t > ( inner.value ) ];

	if ( !( flavor == 3 && node.size == 4 ) && !( flavor == 4 && node.size == 8 ) )
		return -1;

	return static_cast < int32_t > ( inner.value );

}

bool decompiler::emitter::zero_widened ( int32_t e ) const {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.size == 4 && ( node.kind == op::LOAD || node.kind == op::CALL ) && !( node.bits & FLOATING ) )
		return true;

	if ( node.kind != op::CAST || node.size != 8 || ( node.bits & ( SIGNED | FLOATING | POINTER ) ) || node.a < 0 )
		return false;

	const expr& inner = b.exprs [ static_cast < size_t > ( node.a ) ];

	return inner.size == 4 && ( inner.kind == op::LOAD || inner.kind == op::CALL ) && !( inner.bits & FLOATING );

}

int32_t decompiler::emitter::wide_reads ( int32_t e, int64_t var, int32_t depth ) const {

	if ( e < 0 || depth > 64 )
		return 0;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::VAR )
		return node.value == var && node.size == 8 ? 1 : 0;

	if ( node.kind == op::CAST && node.size <= 4 && node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR )
		return 0;

	if ( node.kind == op::ADDRESS && node.a < 0 && node.value == var )
		return 1;

	int32_t total = wide_reads ( node.a, var, depth + 1 ) + wide_reads ( node.b, var, depth + 1 ) + wide_reads ( node.c, var, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		total += wide_reads ( b.args [ static_cast < size_t > ( node.first_arg + i ) ], var, depth + 1 );

	return total;

}

bool decompiler::emitter::upper_dead ( int32_t at, int64_t var ) const {

	int32_t home = -1;

	for ( int32_t k = 0; k < b.code.count ( ) && home < 0; k++ ) {

		const block_code& block = b.code [ static_cast < size_t > ( k ) ];

		if ( at >= block.first && at < block.first + block.count )
			home = k;

	}

	if ( home < 0 )
		return false;

	base::list < uint8_t > seen;
	base::list < int32_t > work;
	base::list < int32_t > starts;

	seen.fill ( b.code.size ( ), 0 );
	work.push ( home );
	starts.push ( at + 1 );

	while ( work.count ( ) ) {

		int32_t k = work [ static_cast < size_t > ( work.count ( ) - 1 ) ];
		int32_t from = starts [ static_cast < size_t > ( starts.count ( ) - 1 ) ];

		work.pop ( );
		starts.pop ( );

		const block_code& block = b.code [ static_cast < size_t > ( k ) ];
		bool killed = false;

		for ( int32_t s = from; s < block.first + block.count && s < b.statements.count ( ) && !killed; s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind == stmt::ASM )
				return false;

			bool direct = line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

			if ( wide_reads ( line.right, var, 0 ) || ( !direct && wide_reads ( line.left, var, 0 ) ) )
				return false;

			if ( direct && b.exprs [ static_cast < size_t > ( line.left ) ].value == var && b.exprs [ static_cast < size_t > ( line.left ) ].size == 8 )
				killed = true;

		}

		if ( killed )
			continue;

		if ( wide_reads ( block.condition, var, 0 ) || wide_reads ( block.selector, var, 0 ) )
			return false;

		if ( block.end == term::RETURN || block.end == term::TAIL ) {

			if ( wide_reads ( block.returned, var, 0 ) )
				return false;

			continue;

		}

		if ( k < succ.count ( ) ) {

			for ( int32_t next : succ [ static_cast < size_t > ( k ) ] ) {

				if ( next < 0 || next >= b.code.count ( ) || seen [ static_cast < size_t > ( next ) ] )
					continue;

				seen [ static_cast < size_t > ( next ) ] = 1;
				work.push ( next );
				starts.push ( b.code [ static_cast < size_t > ( next ) ].first );

			}

		}

	}

	return true;

}

bool decompiler::emitter::low_write ( int32_t left, int32_t right, int32_t at ) {

	if ( !low_writes || left < 0 || at < 0 || !zero_widened ( right ) )
		return false;

	const expr& target = b.exprs [ static_cast < size_t > ( left ) ];

	if ( target.kind != op::VAR || target.size != 8 || target.value < 0 || target.value >= b.vars.count ( ) || target.value == result_var )
		return false;

	const variable& item = b.vars [ static_cast < size_t > ( target.value ) ];

	if ( item.kind != var_kind::REGISTER || item.size != 8 || ( item.bits & FLOATING ) || item.flags || ( target.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( target.value ) ].empty ( ) ) )
		return false;

	return upper_dead ( at, target.value );

}

const decompiler::field_use* decompiler::emitter::field_of ( int32_t var, int64_t offset ) {

	if ( !has_struct ( var ) )
		return nullptr;

	for ( const field_use& item : b.fields ) {

		if ( item.var == var && item.offset == offset )
			return &item;

	}

	return nullptr;

}

void decompiler::emitter::type_name ( uint8_t size, uint8_t bits, bool pointer, bool memory ) {

	const wchar_t* text = L"__int64";

	if ( size == 16 && vector_store )
		text = L"__m128i";
	else if ( bits & FLOATING )
		text = size == 4 ? L"float" : ( size == 8 ? L"double" : L"__m128" );
	else if ( pointer )
		text = L"_BYTE *";
	else if ( memory && !( bits & SIGNED ) ) {

		switch ( size ) {

			case 1:
				text = L"_BYTE";
				break;

			case 2:
				text = L"_WORD";
				break;

			case 4:
				text = L"_DWORD";
				break;

			case 16:
				text = L"_OWORD";
				break;

			default:
				text = L"_QWORD";
				break;

		}

	} else {

		bool is_unsigned = ( bits & UNSIGNED ) != 0;

		switch ( size ) {

			case 1:
				text = is_unsigned ? L"unsigned __int8" : L"char";
				break;

			case 2:
				text = is_unsigned ? L"unsigned __int16" : L"__int16";
				break;

			case 4:
				text = is_unsigned ? L"unsigned int" : L"int";
				break;

			case 16:
				text = L"__m128i";
				break;

			default:
				text = is_unsigned ? L"unsigned __int64" : L"__int64";
				break;

		}

	}

	out.put ( text, syntax::TYPE );

}

void decompiler::emitter::name_after_type ( ) {

	if ( out.last ( ) != L'*' )
		out.put ( L" " );

}

bool decompiler::emitter::abs_mask ( int32_t e, bool doubles ) const {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::GLOBAL || node.size < 8 )
		return false;

	int32_t section = b.file->section_at ( static_cast < uint64_t > ( node.value ) );
	uint64_t offset = 0;

	if ( section < 0 || section >= b.file->sections.count ( ) || b.file->sections [ static_cast < size_t > ( section ) ].write || !b.file->offset_of ( static_cast < uint64_t > ( node.value ), offset ) || offset + 8 > b.file->size )
		return false;

	return doubles ? b.file->read64 ( offset ) == 0x7FFFFFFFFFFFFFFFull : b.file->read32 ( offset ) == 0x7FFFFFFFu;

}

void decompiler::emitter::vector_load ( int32_t address, uint8_t flavor ) {

	bool aligned = false;

	if ( simd_types && address >= 0 ) {

		const expr& where = b.exprs [ static_cast < size_t > ( address ) ];

		aligned = where.kind == op::GLOBAL || ( where.kind == op::CONST && ( where.bits & POINTER ) );

	}

	if ( flavor == 3 ) {

		out.put ( aligned ? L"_mm_load_ps" : L"_mm_loadu_ps", syntax::FUNCTION );
		out.put ( L"((" );
		out.put ( L"const float", syntax::TYPE );

	} else if ( flavor == 4 ) {

		out.put ( aligned ? L"_mm_load_pd" : L"_mm_loadu_pd", syntax::FUNCTION );
		out.put ( L"((" );
		out.put ( L"const double", syntax::TYPE );

	} else {

		out.put ( aligned ? L"_mm_load_si128" : L"_mm_loadu_si128", syntax::FUNCTION );
		out.put ( L"((" );
		out.put ( L"const __m128i", syntax::TYPE );

	}

	out.put ( L" *)" );
	value ( address, 14 );
	out.put ( L")" );

}

void decompiler::emitter::var_type ( int32_t index ) {

	const variable& item = b.vars [ static_cast < size_t > ( index ) ];

	if ( index == b.va_var ) {

		out.put ( L"va_list", syntax::TYPE );

		return;

	}

	if ( index < vector.count ( ) && vector [ static_cast < size_t > ( index ) ] ) {

		uint8_t flavor = vector [ static_cast < size_t > ( index ) ];

		out.put ( flavor == 3 ? L"__m128" : ( flavor == 4 ? L"__m128d" : ( flavor == 1 && plain_copies ? L"__int128" : L"__m128i" ) ), syntax::TYPE );

		return;

	}

	if ( plain_copies && item.kind == var_kind::STACK && item.size == 16 && !( index < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( index ) ].empty ( ) ) ) {

		out.put ( L"__int128", syntax::TYPE );

		return;

	}

	if ( index < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( index ) ].empty ( ) ) {

		out.put ( b.var_types [ static_cast < size_t > ( index ) ].get ( ), syntax::TYPE );

		return;

	}

	if ( has_struct ( index ) ) {

		wchar_t name [ 64 ];

		var_name ( index, name );
		out.put ( L"struct", syntax::KEYWORD );
		out.put ( L" " );
		out.put ( base::writer ( ).add ( name ).add ( L"_t" ).text ( ), syntax::TYPE );
		out.put ( L" *" );

		return;

	}

	if ( item.flags || ( index == result_var && b.result_width == 2 && item.size == 1 ) || ( index < boolean.count ( ) && boolean [ static_cast < size_t > ( index ) ] ) ) {

		out.put ( L"bool", syntax::TYPE );

		return;

	}

	if ( index < element.count ( ) && element [ static_cast < size_t > ( index ) ] ) {

		uint8_t shape = element [ static_cast < size_t > ( index ) ];

		if ( shape == 0x10 ) {

			out.put ( L"char", syntax::TYPE );
			out.put ( L" *" );

			return;

		}

		if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) ) {

			out.put ( L"void", syntax::TYPE );
			out.put ( L" **" );

			return;

		}

		if ( ( shape & 0x9F ) == 8 && ( shape & 0x20 ) )
			type_name ( 8, SIGNED, false );
		else if ( ( shape & 0x40 ) && !( shape & 0x80 ) )
			type_name ( shape & 0x1F, UNSIGNED, false );
		else
			type_name ( shape & 0x1F, ( shape & 0x80 ) ? FLOATING : 0, false, !( shape & 0x80 ) );
		out.put ( L" *" );

		return;

	}

	type_name ( item.size, item.bits & ~POINTER, false );

}

int32_t decompiler::emitter::precedence ( op kind ) {

	switch ( kind ) {

		case op::TERNARY:
			return 3;

		case op::LOR:
			return 4;

		case op::LAND:
			return 5;

		case op::OR:
			return 6;

		case op::XOR:
			return 7;

		case op::AND:
			return 8;

		case op::EQ:
		case op::NE:
			return 9;

		case op::LT:
		case op::LE:
		case op::GT:
		case op::GE:
			return 10;

		case op::SHL:
		case op::SHR:
			return 11;

		case op::ADD:
		case op::SUB:
			return 12;

		case op::MUL:
		case op::DIV:
		case op::MOD:
			return 13;

		case op::NOT:
		case op::NEG:
		case op::LNOT:
		case op::CAST:
		case op::LOAD:
		case op::ADDRESS:
			return 14;

		default:
			return 16;

	}

}

decompiler::emitter::~emitter ( ) {

	delete shape;

}

bool decompiler::emitter::callee_unsigned ( uint64_t target, int32_t index ) {

	if ( !target || !b.names )
		return false;

	if ( target != shape_target ) {

		shape_target = target;
		shape_ok = false;

		const wchar_t* declared = b.names->prototype ( target );
		wchar_t callee [ 200 ];

		if ( !declared && b.names->name ( target, true, callee, 200 ) )
			declared = api_prototype ( callee );

		if ( declared ) {

			if ( !shape )
				shape = new c_prototype ( );

			base::writer problem;

			shape_ok = parse_prototype ( declared, b.long_mode, *shape, problem );

		}

	}

	return shape_ok && index < shape->count && ( shape->params [ index ].bits & UNSIGNED ) && !shape->params [ index ].pointer;

}

bool decompiler::emitter::no_return ( const expr& call ) {

	wchar_t name [ 200 ];

	if ( call.kind != op::CALL || !call.value || !b.names->name ( static_cast < uint64_t > ( call.value ), true, name, 200 ) )
		return false;

	short_symbol ( name );

	const wchar_t* text = name;

	while ( *text == L'_' )
		text++;

	static const wchar_t* const known [ ] = { L"terminate", L"abort", L"exit", L"ExitProcess", L"ExitThread", L"FatalExit", L"FatalAppExitW", L"FatalAppExitA", L"RaiseFailFastException", L"invalid_parameter_noinfo_noreturn", L"report_gsfailure", L"raise_securityfailure", L"CxxThrowException", L"amsg_exit", L"invoke_watson" };

	for ( const wchar_t* item : known ) {

		if ( base::same_text ( text, item ) )
			return true;

	}

	return false;

}

bool decompiler::emitter::callee_wide ( uint64_t target, int32_t index ) {

	callee_unsigned ( target, index );

	return shape_ok && index < shape->count && ( shape->params [ index ].pointer || shape->params [ index ].size == 8 ) && !( shape->params [ index ].bits & FLOATING );

}

void decompiler::emitter::number ( int64_t value, uint8_t size, bool hex_form, bool is_unsigned ) {

	wchar_t text [ 80 ];
	int32_t length = 0;
	bool negative = value < 0 && !( size < 8 && value > 0 );
	uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

	bool wrapped = false;

	if ( negative && ( is_unsigned || bare_unsigned || ( magnitude > 0x100000000ull && hex_form ) || ( wide_negatives && size >= 8 && magnitude > 0x80000000ull ) ) ) {

		wrapped = !is_unsigned && !bare_unsigned && size >= 8;
		negative = false;
		magnitude = size >= 8 ? static_cast < uint64_t > ( value ) : static_cast < uint64_t > ( value ) & ( ( 1ull << ( size * 8 ) ) - 1 );

	}

	if ( bare_unsigned && magnitude > 9 )
		hex_form = true;

	bool fits = negative ? magnitude <= 0x80000000ull : magnitude <= ( is_unsigned || bare_unsigned ? 0xFFFFFFFFull : 0x7FFFFFFFull );
	bool unsigned_suffix = ( is_unsigned && !negative && !bare_unsigned ) || ( wrapped && wrapped_unsigned );
	int32_t occurrence = next_occurrence ( value );
	wchar_t form = b.names && !dry ? b.names->number_form ( out.open ? out.current.address : 0, value, occurrence ) : 0;

	hex_form = hex_form || unsigned_suffix || magnitude > 0xFFFFFFFFull;

	if ( power_hex && magnitude >= 0x2000 && ( magnitude & ( magnitude - 1 ) ) == 0 )
		hex_form = true;

	if ( plain_negatives && negative )
		hex_form = false;

	if ( form == L'h' )
		hex_form = true;
	else if ( form == L'd' )
		hex_form = false;
	else if ( form == L'c' && char_literal ( value, text, 40 ) ) {

		put_number ( text, value );

		return;

	} else if ( form == L'b' ) {

		uint64_t bits = negative ? static_cast < uint64_t > ( value ) & ( size >= 8 ? ~0ull : ( ( 1ull << ( size * 8 ) ) - 1 ) ) : magnitude;
		int32_t top = 63;

		while ( top > 0 && !( ( bits >> top ) & 1 ) )
			top--;

		text [ length++ ] = L'0';
		text [ length++ ] = L'b';

		for ( int32_t bit = top; bit >= 0 && length < 78; bit-- )
			text [ length++ ] = ( bits >> bit ) & 1 ? L'1' : L'0';

		text [ length ] = 0;
		put_number ( text, value );

		return;

	}

	if ( negative )
		text [ length++ ] = L'-';

	if ( hex_form && ( magnitude >= 10 || form == L'h' ) ) {

		wchar_t digits [ 20 ];

		hex ( magnitude, digits );
		text [ length++ ] = L'0';
		text [ length++ ] = L'x';

		for ( int32_t i = 0; digits [ i ]; i++ )
			text [ length++ ] = digits [ i ];

	} else {

		wchar_t reverse [ 24 ];
		int32_t n = 0;

		do {

			reverse [ n++ ] = static_cast < wchar_t > ( L'0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		while ( n )
			text [ length++ ] = reverse [ --n ];

	}

	if ( size == 8 && !( operand && fits ) ) {

		if ( unsigned_suffix )
			text [ length++ ] = L'u';

		text [ length++ ] = L'i';
		text [ length++ ] = L'6';
		text [ length++ ] = L'4';

	} else if ( unsigned_suffix )
		text [ length++ ] = L'u';

	text [ length ] = 0;
	put_number ( text, value );

}

void decompiler::emitter::put_number ( const wchar_t* text, int64_t value ) {

	int32_t before = out.r->spans.count ( );

	out.put ( text, syntax::NUMBER );

	if ( out.r->spans.count ( ) > before ) {

		out.r->spans.back ( ).value = value;
		out.r->spans.back ( ).occurrence = last_occurrence;

	}

}

int32_t decompiler::emitter::next_occurrence ( int64_t value ) {

	uint64_t address = out.open ? out.current.address : 0;
	int32_t count = 0;

	if ( dry )
		return 0;

	for ( int32_t i = 0; i < number_addresses.count ( ); i++ ) {

		if ( number_addresses [ static_cast < size_t > ( i ) ] == address && number_values [ static_cast < size_t > ( i ) ] == value )
			count++;

	}

	number_addresses.push ( address );
	number_values.push ( value );
	last_occurrence = count;

	return count;

}

void decompiler::emitter::real ( int64_t bits, uint8_t size ) {

	double value = 0.0;

	if ( size == 4 ) {

		uint32_t raw = static_cast < uint32_t > ( bits );

		value = static_cast < double > ( __builtin_bit_cast ( float, raw ) );

	} else
		value = __builtin_bit_cast ( double, bits );

	base::writer text;

	if ( ida_reals && value == value && value < 1e300 && value > -1e300 ) {

		wchar_t shown [ 64 ];
		int32_t n = 0;

		if ( value < 0 ) {

			shown [ n++ ] = L'-';
			value = -value;

		}

		if ( value == 0.0 ) {

			shown [ n++ ] = L'0';
			shown [ n++ ] = L'.';
			shown [ n++ ] = L'0';

		} else {

			int32_t significant = size == 4 ? 8 : 16;
			int32_t exponent = 0;
			double power = 1.0;

			if ( value >= 1.0 ) {

				while ( value >= power * 10.0 && exponent < 300 ) {

					power *= 10.0;
					exponent++;

				}

			} else {

				while ( value < power && exponent > -300 ) {

					power /= 10.0;
					exponent--;

				}

			}

			double mantissa = value / power;
			double scale = 1.0;

			for ( int32_t i = 1; i < significant; i++ )
				scale *= 10.0;

			uint64_t digits = static_cast < uint64_t > ( mantissa * scale + 0.5 );
			uint64_t limit = static_cast < uint64_t > ( scale * 10.0 );

			if ( digits >= limit ) {

				digits /= 10;
				exponent++;

			}

			wchar_t text_digits [ 24 ];
			int32_t count = significant;

			for ( int32_t i = significant - 1; i >= 0; i-- ) {

				text_digits [ i ] = static_cast < wchar_t > ( L'0' + digits % 10 );
				digits /= 10;

			}

			while ( count > 1 && text_digits [ count - 1 ] == L'0' )
				count--;

			if ( exponent >= significant || exponent < -4 ) {

				shown [ n++ ] = text_digits [ 0 ];
				shown [ n++ ] = L'.';

				if ( count > 1 ) {

					for ( int32_t i = 1; i < count; i++ )
						shown [ n++ ] = text_digits [ i ];

				} else
					shown [ n++ ] = L'0';

				shown [ n++ ] = L'e';

				if ( exponent < 0 ) {

					shown [ n++ ] = L'-';
					exponent = -exponent;

				}

				wchar_t reverse [ 8 ];
				int32_t r = 0;

				do {

					reverse [ r++ ] = static_cast < wchar_t > ( L'0' + exponent % 10 );
					exponent /= 10;

				} while ( exponent && r < 7 );

				while ( r )
					shown [ n++ ] = reverse [ --r ];

			} else if ( exponent >= 0 ) {

				for ( int32_t i = 0; i <= exponent; i++ )
					shown [ n++ ] = i < count ? text_digits [ i ] : L'0';

				shown [ n++ ] = L'.';

				if ( count > exponent + 1 ) {

					for ( int32_t i = exponent + 1; i < count; i++ )
						shown [ n++ ] = text_digits [ i ];

				} else
					shown [ n++ ] = L'0';

			} else {

				shown [ n++ ] = L'0';
				shown [ n++ ] = L'.';

				for ( int32_t i = 0; i < -exponent - 1 && n < 40; i++ )
					shown [ n++ ] = L'0';

				for ( int32_t i = 0; i < count; i++ )
					shown [ n++ ] = text_digits [ i ];

			}

		}

		shown [ n ] = 0;
		out.put ( shown, syntax::NUMBER );

		return;

	}

	if ( value != value )
		text.add ( L"NAN" );
	else if ( value > 1e300 || value < -1e300 )
		text.add ( value < 0 ? L"-INFINITY" : L"INFINITY" );
	else {

		int32_t decimals = 1;

		for ( ; decimals < 9; decimals++ ) {

			double scale = 1.0;

			for ( int32_t i = 0; i < decimals; i++ )
				scale *= 10.0;

			double rounded = static_cast < double > ( static_cast < int64_t > ( value * scale + ( value < 0 ? -0.5 : 0.5 ) ) ) / scale;

			if ( size == 4 ? static_cast < float > ( rounded ) == static_cast < float > ( value ) : rounded == value )
				break;

		}

		if ( value > 1e15 || value < -1e15 )
			text.fixed ( value, 0 );
		else
			text.fixed ( value, decimals );

	}

	if ( size == 4 )
		text.add ( L"f" );

	out.put ( text.text ( ), syntax::NUMBER );

}

bool decompiler::emitter::simple_constant ( int64_t value ) {

	uint64_t magnitude = static_cast < uint64_t > ( value < 0 ? -value : value );

	return magnitude < 0x80000000ull;

}

void decompiler::emitter::pointer_constant ( uint64_t address ) {

	wchar_t text [ 300 ];

	if ( b.names->literal ( address, text, 290 ) ) {

		out.put ( text, syntax::STRING, address );

		return;

	}

	if ( address != b.file->base && b.names->name ( address, true, text, 200 ) ) {

		short_symbol ( text );

		bool code = b.db->function_starting ( address ) >= 0;

		if ( code ) {

			const analysis::function_info& info = b.db->functions [ static_cast < size_t > ( b.db->function_starting ( address ) ) ];

			out.put ( text, ( info.flags & analysis::THUNK ) ? syntax::IMPORT : syntax::FUNCTION, address );

		} else {

			out.put ( L"&" );
			out.put ( text, syntax::GLOBAL, address );

		}

		return;

	}

	if ( address != b.file->base && b.names->name ( address, false, text, 200 ) ) {

		out.put ( L"&" );
		out.put ( text, syntax::GLOBAL, address );

		return;

	}

	number ( static_cast < int64_t > ( address ), 8, true, false );

}

bool decompiler::emitter::put_folded ( int32_t var, const wchar_t* name ) {

	if ( active_var < 0 || var != active_var || var_step ( var ) != active_unit )
		return false;

	if ( active_how <= 2 )
		out.put ( active_how == 1 ? L"++" : L"--", syntax::OPERATOR );

	out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

	if ( active_how >= 3 )
		out.put ( active_how == 3 ? L"++" : L"--", syntax::OPERATOR );

	if ( active_cond && active_slot >= 0 && active_slot < fold_cond_done.count ( ) )
		fold_cond_done [ static_cast < size_t > ( active_slot ) ] = 1;
	else if ( !active_cond && active_slot >= 0 && active_slot < fold_done.count ( ) )
		fold_done [ static_cast < size_t > ( active_slot ) ] = 1;

	active_var = -1;

	return true;

}

int64_t decompiler::emitter::var_step ( int32_t v ) const {

	if ( v < 0 || v >= element.count ( ) || !element [ static_cast < size_t > ( v ) ] )
		return 1;

	uint8_t shape = element [ static_cast < size_t > ( v ) ];

	if ( shape == 0x10 )
		return 1;

	if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) )
		return 8;

	return shape & 0x0F ? shape & 0x0F : 1;

}

bool decompiler::emitter::char_typed_sum ( int32_t e ) const {

	int32_t var = -1;
	int32_t index = -1;
	int64_t offset = 0;

	if ( char_address && char_terms ( e, var, index, offset ) )
		return true;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	return node.kind == op::ADD && ( pointer_step ( node.a ) > 0 || ( node.b >= 0 && pointer_step ( node.b ) > 0 ) );

}

int32_t decompiler::emitter::count_var ( int32_t e, int64_t v, int32_t depth ) const {

	if ( e < 0 || depth > 64 )
		return 0;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
	int32_t n = node.kind == op::VAR && node.value == v ? 1 : 0;

	n += count_var ( node.a, v, depth + 1 ) + count_var ( node.b, v, depth + 1 ) + count_var ( node.c, v, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		n += count_var ( b.args [ static_cast < size_t > ( node.first_arg + i ) ], v, depth + 1 );

	return n;

}

bool decompiler::emitter::count_call ( int32_t e, int32_t depth ) const {

	if ( e < 0 || depth > 64 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind == op::CALL || node.kind == op::INTRINSIC )
		return true;

	return count_call ( node.a, depth + 1 ) || count_call ( node.b, depth + 1 ) || count_call ( node.c, depth + 1 );

}

void decompiler::emitter::plan_increments ( ) {

	fold_at.clear ( );
	fold_how.clear ( );
	fold_cond.clear ( );
	fold_cond_var.clear ( );
	fold_cond_how.clear ( );
	fold_at.fill ( b.statements.size ( ), -1 );
	fold_how.fill ( b.statements.size ( ), 0 );
	fold_inc.clear ( );
	fold_done.clear ( );
	fold_cond_inc.clear ( );
	fold_cond_done.clear ( );
	fold_inc.fill ( b.statements.size ( ), -1 );
	fold_done.fill ( b.statements.size ( ), 0 );
	fold_unit.clear ( );
	fold_cond_unit.clear ( );
	fold_unit.fill ( b.statements.size ( ), 1 );

	if ( !fold_increments )
		return;

	if ( hidden.count ( ) < b.statements.count ( ) )
		hidden.fill ( b.statements.size ( ) - hidden.size ( ), 0 );

	base::list < uint8_t > flag_read;

	flag_read.fill ( b.vars.size ( ), 0 );

	auto mark_reads = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

		if ( e < 0 || depth > 64 )
			return;

		const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

		if ( node.kind == op::VAR && node.value >= 0 && node.value < b.vars.count ( ) )
			flag_read [ static_cast < size_t > ( node.value ) ] = 1;

		self ( self, node.a, depth + 1 );
		self ( self, node.b, depth + 1 );
		self ( self, node.c, depth + 1 );

		for ( int32_t i = 0; i < node.arg_count; i++ )
			self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

	};

	for ( const statement& line : b.statements ) {

		if ( line.kind == stmt::NOP )
			continue;

		mark_reads ( mark_reads, line.right, 0 );

		if ( !( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
			mark_reads ( mark_reads, line.left, 0 );

	}

	for ( const block_code& block : b.code ) {

		mark_reads ( mark_reads, block.condition, 0 );
		mark_reads ( mark_reads, block.selector, 0 );
		mark_reads ( mark_reads, block.returned, 0 );

	}

	for ( int32_t k = 0; k < count; k++ ) {

		const block_code& block = b.code [ static_cast < size_t > ( k ) ];

		if ( !block.reachable )
			continue;

		base::list < int32_t > lines;

		for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind == stmt::NOP )
				continue;

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ].flags && !flag_read [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ] )
				continue;

			lines.push ( s );

		}

		bool latch = false;
		int32_t next_blocks [ 2 ] = { block.taken, block.end == term::BRANCH ? block.other : -1 };

		for ( int32_t n : next_blocks ) {

			if ( n >= 0 && n < count && header [ static_cast < size_t > ( n ) ] && b.graph.blocks [ static_cast < size_t > ( n ) ].start <= b.graph.blocks [ static_cast < size_t > ( k ) ].start )
				latch = true;

		}

		for ( int32_t t = 0; t < lines.count ( ); t++ ) {

			int32_t at = lines [ static_cast < size_t > ( t ) ];
			const statement& line = b.statements [ static_cast < size_t > ( at ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int64_t x = b.exprs [ static_cast < size_t > ( line.left ) ].value;
			const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

			if ( x < 0 || x >= b.vars.count ( ) || b.vars [ static_cast < size_t > ( x ) ].kind != var_kind::REGISTER || ( right.bits & FLOATING ) || right.kind != op::ADD || right.a < 0 || right.b < 0 )
				continue;

			const expr& self_node = b.exprs [ static_cast < size_t > ( right.a ) ];
			const expr& amount = b.exprs [ static_cast < size_t > ( right.b ) ];

			if ( self_node.kind != op::VAR || self_node.value != x || amount.kind != op::CONST )
				continue;

			int64_t unit = amount.value < 0 ? -amount.value : amount.value;

			if ( unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16 )
				continue;

			bool up = amount.value > 0;
			bool last_line = t == lines.count ( ) - 1;

			if ( t > 0 ) {

				int32_t before = lines [ static_cast < size_t > ( t - 1 ) ];
				const statement& prior = b.statements [ static_cast < size_t > ( before ) ];
				bool defines = prior.kind == stmt::ASSIGN && prior.left >= 0 && b.exprs [ static_cast < size_t > ( prior.left ) ].kind == op::VAR;
				int32_t uses = count_var ( prior.right, x, 0 ) + ( defines ? 0 : count_var ( prior.left, x, 0 ) );
				int32_t loaded = prior.right;

				while ( loaded >= 0 && b.exprs [ static_cast < size_t > ( loaded ) ].kind == op::CAST )
					loaded = b.exprs [ static_cast < size_t > ( loaded ) ].a;

				bool memory = !defines || ( loaded >= 0 && b.exprs [ static_cast < size_t > ( loaded ) ].kind == op::LOAD );
				bool calls = prior.right < 0 || count_call ( prior.right, 0 ) || ( !defines && count_call ( prior.left, 0 ) );

				if ( ( !( last_line && latch ) || ( memory && latch_folds ) ) && !calls && ( prior.kind == stmt::ASSIGN || prior.kind == stmt::STORE ) && uses == 1 && fold_at [ static_cast < size_t > ( before ) ] < 0 && !hidden [ static_cast < size_t > ( before ) ] && !( defines && b.exprs [ static_cast < size_t > ( prior.left ) ].value == x ) ) {

					fold_at [ static_cast < size_t > ( before ) ] = static_cast < int32_t > ( x );
					fold_how [ static_cast < size_t > ( before ) ] = up ? 3 : 4;
					fold_inc [ static_cast < size_t > ( before ) ] = at;
					fold_unit [ static_cast < size_t > ( before ) ] = unit;
					hidden [ static_cast < size_t > ( at ) ] = 1;

					continue;

				}

			}

			if ( last_line && !latch && block.end == term::BRANCH && block.condition >= 0 && count_var ( block.condition, x, 0 ) == 1 ) {

				fold_cond.push ( block.condition );
				fold_cond_var.push ( static_cast < int32_t > ( x ) );
				fold_cond_how.push ( up ? 1 : 2 );
				fold_cond_inc.push ( at );
				fold_cond_unit.push ( unit );
				fold_cond_done.push ( 0 );
				hidden [ static_cast < size_t > ( at ) ] = 1;

			}

		}

	}

}

bool decompiler::emitter::unfold_missed ( ) {

	bool missed = false;

	for ( int32_t s = 0; s < fold_at.count ( ); s++ ) {

		if ( fold_at [ static_cast < size_t > ( s ) ] < 0 || fold_done [ static_cast < size_t > ( s ) ] )
			continue;

		hidden [ static_cast < size_t > ( fold_inc [ static_cast < size_t > ( s ) ] ) ] = 0;
		fold_at [ static_cast < size_t > ( s ) ] = -1;
		missed = true;

	}

	for ( int32_t k = 0; k < fold_cond.count ( ); k++ ) {

		if ( fold_cond [ static_cast < size_t > ( k ) ] < 0 || fold_cond_done [ static_cast < size_t > ( k ) ] )
			continue;

		hidden [ static_cast < size_t > ( fold_cond_inc [ static_cast < size_t > ( k ) ] ) ] = 0;
		fold_cond [ static_cast < size_t > ( k ) ] = -1;
		missed = true;

	}

	return missed;

}

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

int32_t decompiler::emitter::bit_operand ( int32_t e ) const {

	while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( e ) ].a >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( e ) ].a ) ].kind == op::AND )
		e = b.exprs [ static_cast < size_t > ( e ) ].a;

	if ( e < 0 )
		return -1;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::AND || node.b < 0 || ( node.bits & FLOATING ) || logical_pair ( node ) )
		return -1;

	const expr& mask = b.exprs [ static_cast < size_t > ( node.b ) ];

	return mask.kind == op::CONST && mask.value != 0 && !( mask.bits & POINTER ) ? e : -1;

}

bool decompiler::emitter::logical_pair ( const expr& item ) const {

	auto logical = [ & ] ( int32_t e ) {

		if ( e < 0 )
			return false;

		op kind = b.exprs [ static_cast < size_t > ( e ) ].kind;

		return ( kind >= op::EQ && kind <= op::LOR ) || kind == op::LNOT;

	};

	return item.size == 1 && !( item.bits & FLOATING ) && logical ( item.a ) && logical ( item.b );

}

void decompiler::emitter::select_arm ( int32_t left, int32_t arm ) {

	const expr& target = b.exprs [ static_cast < size_t > ( left ) ];
	const expr& picked = b.exprs [ static_cast < size_t > ( arm ) ];

	if ( picked.kind == op::CONST && !( picked.bits & POINTER ) && target.size == 8 && b.long_mode )
		number ( picked.value, 8, !simple_constant ( picked.value ), false );
	else
		value ( arm, 2 );

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

void decompiler::emitter::index_number ( int64_t amount ) {

	if ( amount < 0 ) {

		out.put ( L"-", syntax::NUMBER );
		amount = -amount;

	}

	out.put ( base::writer ( ).number ( amount ).text ( ), syntax::NUMBER );

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

bool decompiler::emitter::typed_value ( int32_t e ) const {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( pointer_step ( e ) > 0 )
		return true;

	if ( node.kind == op::ADD && node.b >= 0 && pointer_step ( node.a ) > 1 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::MUL ) {

		const expr& scaled = b.exprs [ static_cast < size_t > ( node.b ) ];

		if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == pointer_step ( node.a ) )
			return true;

	}

	return node.kind == op::ADD && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && pointer_step ( node.a ) > 0 && b.exprs [ static_cast < size_t > ( node.b ) ].value % pointer_step ( node.a ) == 0;

}

void decompiler::emitter::pointer_cast ( int32_t v, bool twice ) {

	uint8_t shape = element [ static_cast < size_t > ( v ) ];

	out.put ( L"(" );

	if ( shape == 0x10 )
		out.put ( L"char", syntax::TYPE );
	else if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) ) {

		out.put ( L"void", syntax::TYPE );
		out.put ( twice ? L" ***)" : L" **)" );

		return;

	} else if ( ( shape & 0x9F ) == 8 && ( shape & 0x20 ) )
		type_name ( 8, SIGNED, false );
	else if ( ( shape & 0x40 ) && !( shape & 0x80 ) )
		type_name ( shape & 0x1F, UNSIGNED, false );
	else
		type_name ( shape & 0x1F, ( shape & 0x80 ) ? FLOATING : 0, false, !( shape & 0x80 ) );

	out.put ( twice ? L" **)" : L" *)" );

}

int32_t decompiler::emitter::char_var ( int32_t e ) const {

	if ( e < 0 )
		return -1;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.kind != op::VAR || node.value < 0 || node.value >= element.count ( ) || element [ static_cast < size_t > ( node.value ) ] != 0x10 )
		return -1;

	return static_cast < int32_t > ( node.value );

}

bool decompiler::emitter::char_terms ( int32_t e, int32_t& var, int32_t& index, int64_t& offset ) const {

	int32_t terms [ 8 ];
	int32_t count = 0;
	int32_t stack [ 8 ];
	int32_t depth = 0;
	int64_t sum = 0;

	stack [ depth++ ] = e;

	while ( depth ) {

		int32_t at = stack [ --depth ];
		const expr& node = b.exprs [ static_cast < size_t > ( at ) ];

		if ( node.kind == op::ADD && node.size == 8 && node.a >= 0 && node.b >= 0 && depth + 2 <= 8 ) {

			stack [ depth++ ] = node.b;
			stack [ depth++ ] = node.a;

		} else if ( node.kind == op::CONST && !( node.bits & POINTER ) )
			sum += node.value;
		else if ( count < 8 )
			terms [ count++ ] = at;
		else
			return false;

	}

	if ( count != 2 )
		return false;

	if ( char_var ( terms [ 0 ] ) >= 0 && char_var ( terms [ 1 ] ) < 0 && pointer_step ( terms [ 1 ] ) == 0 ) {

		var = char_var ( terms [ 0 ] );
		index = terms [ 1 ];

	} else if ( char_var ( terms [ 1 ] ) >= 0 && char_var ( terms [ 0 ] ) < 0 && pointer_step ( terms [ 0 ] ) == 0 ) {

		var = char_var ( terms [ 1 ] );
		index = terms [ 0 ];

	} else
		return false;

	if ( b.exprs [ static_cast < size_t > ( index ) ].bits & POINTER )
		return false;

	offset = sum;

	return true;

}

bool decompiler::emitter::char_load ( const expr& item, int32_t parent ) {

	const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
	wchar_t name [ 64 ];
	int32_t var = char_var ( item.a );
	int32_t index = -1;
	int64_t offset = 0;

	if ( var < 0 && where.kind == op::ADD ) {

		const expr& right = b.exprs [ static_cast < size_t > ( where.b ) ];

		if ( right.kind == op::CONST && !( right.bits & POINTER ) ) {

			offset = right.value;
			var = char_var ( where.a );

			if ( var < 0 && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::ADD ) {

				const expr& inner = b.exprs [ static_cast < size_t > ( where.a ) ];

				var = char_var ( inner.a );
				index = inner.b;

			}

		} else {

			var = char_var ( where.a );
			index = where.b;

		}

	}

	if ( var < 0 && where.kind == op::ADD && char_split && !char_terms ( item.a, var, index, offset ) )
		var = -1;

	if ( var < 0 )
		return false;

	var_name ( var, name );

	auto pointer_type = [ & ] ( ) {

		out.put ( L"(" );
		type_name ( item.size, item.bits, false, true );
		out.put ( L" *)" );

	};

	if ( index >= 0 ) {

		if ( item.size != 1 ) {

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"*", syntax::OPERATOR );
			pointer_type ( );
			out.put ( L"&", syntax::OPERATOR );

		}

		out.put ( name, syntax::LOCAL );
		out.put ( L"[" );
		value ( index, 0 );

		if ( offset ) {

			out.put ( offset < 0 ? L" - " : L" + ", syntax::OPERATOR );
			index_number ( offset < 0 ? -offset : offset );

		}

		out.put ( L"]" );

		if ( item.size != 1 && parent > 14 )
			out.put ( L")" );

		return true;

	}

	if ( item.size == 1 ) {

		if ( offset == 0 ) {

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"*", syntax::OPERATOR );
			out.put ( name, syntax::LOCAL );

			if ( parent > 14 )
				out.put ( L")" );

		} else {

			out.put ( name, syntax::LOCAL );
			out.put ( L"[" );
			index_number ( offset );
			out.put ( L"]" );

		}

		return true;

	}

	if ( offset != 0 && offset % item.size == 0 ) {

		if ( parent > 14 )
			out.put ( L"(" );

		out.put ( L"*(", syntax::OPERATOR );
		pointer_type ( );
		out.put ( name, syntax::LOCAL );
		out.put ( L" + ", syntax::OPERATOR );
		index_number ( offset / item.size );
		out.put ( L")" );

		if ( parent > 14 )
			out.put ( L")" );

		return true;

	}

	return false;

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

			for ( int32_t k = 0; plain >= 0 && k + 2 < scale_defs.count ( ); k += 3 ) {

				int32_t from = scale_defs [ static_cast < size_t > ( k + 1 ) ];
				int64_t factor = scale_defs [ static_cast < size_t > ( k + 2 ) ];

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

int32_t decompiler::emitter::bare_index ( int32_t e ) const {

	while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
		e = b.exprs [ static_cast < size_t > ( e ) ].a;

	return e;

}

void decompiler::emitter::collect_anchors ( ) {

	anchors_ready = true;
	anchors.clear ( );
	scale_defs.clear ( );

	{

		base::list < int32_t > defs;

		defs.fill ( b.vars.size ( ), 0 );

		for ( const statement& line : b.statements ) {

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].value < b.vars.count ( ) )
				defs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ]++;

		}

		for ( const statement& line : b.statements ) {

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

	if ( step != 1 ) {

		out.put ( base::writer ( ).number ( step ).text ( ), syntax::NUMBER );
		out.put ( L" * ", syntax::OPERATOR );
		value ( here.index, 13 );

	} else
		value ( here.index, extra ? 11 : 0 );

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

	if ( slot_part ( item.a ) ) {

		const expr& base = b.exprs [ static_cast < size_t > ( where.a ) ];
		int64_t k = b.exprs [ static_cast < size_t > ( where.b ) ].value;

		var_name ( static_cast < int32_t > ( base.value ), name );

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

void decompiler::emitter::value ( int32_t index, int32_t parent, bool hex_hint ) {

	if ( index < 0 ) {

		out.put ( L"?" );

		return;

	}

	bool saved = in_logic;

	if ( in_logic )
		truth = true;

	const expr& node = b.exprs [ static_cast < size_t > ( index ) ];

	int32_t lane = lane_var ( index );

	if ( lane >= 0 ) {

		wchar_t lane_name [ 80 ];

		var_name ( lane, lane_name );
		out.put ( lane_name, syntax::LOCAL );
		out.put ( node.size == 8 ? L".m128d_f64[0]" : L".m128_f32[0]" );
		in_logic = saved;

		return;

	}

	in_logic = bit_tests && ( node.kind == op::LAND || node.kind == op::LOR || ( ( node.kind == op::AND || node.kind == op::OR ) && logical_pair ( node ) ) );

	uint8_t saved_size = holder_size;
	op saved_kind = holder_kind;

	if ( shift_products && node.kind == op::SHL && node.a >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( node.b ) ].value >= 1 && b.exprs [ static_cast < size_t > ( node.b ) ].value <= 5 && !( node.bits & FLOATING ) ) {

		int32_t shift = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( node.b ) ].value );
		uint8_t size = node.size;
		uint8_t bits = node.bits;
		int32_t operand = node.a;

		index = b.make ( op::MUL, size, operand, b.constant ( 1ll << shift, size ), 0, bits );

	}

	value_body ( index, parent, hex_hint );
	holder_size = saved_size;
	holder_kind = saved_kind;
	in_logic = saved;

}

void decompiler::emitter::value_body ( int32_t index, int32_t parent, bool hex_hint ) {

	for ( int32_t h = 0; h < hoisted.count ( ); h++ ) {

		if ( hoisted [ static_cast < size_t > ( h ) ] == index ) {

			out.put ( base::writer ( ).add ( L"t" ).number ( hoisted_temp [ static_cast < size_t > ( h ) ] ).text ( ), syntax::LOCAL );

			return;

		}

	}

	const expr item = b.exprs [ static_cast < size_t > ( index ) ];
	bool truthy = truth;
	uint8_t outer_size = holder_size;
	op outer_kind = holder_kind;

	holder_size = item.size;
	holder_kind = item.kind;
	truth = false;

	if ( bit_tests ) {

		int32_t tested_bits = -1;
		bool equal = false;

		if ( truthy && bit_operand ( index ) >= 0 )
			tested_bits = bit_operand ( index );
		else if ( item.kind == op::LNOT && bit_operand ( item.a ) >= 0 ) {

			tested_bits = bit_operand ( item.a );
			equal = true;

		} else if ( ( item.kind == op::EQ || item.kind == op::NE ) && item.b >= 0 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( item.b ) ].value == 0 && !( b.exprs [ static_cast < size_t > ( item.b ) ].bits & POINTER ) && bit_operand ( item.a ) >= 0 ) {

			tested_bits = bit_operand ( item.a );
			equal = item.kind == op::EQ;

		}

		if ( tested_bits >= 0 ) {

			bool wrap = parent > precedence ( op::NE );

			if ( wrap )
				out.put ( L"(" );

			const expr& masked = b.exprs [ static_cast < size_t > ( tested_bits ) ];
			int32_t subject = masked.a;
			int64_t mask = b.exprs [ static_cast < size_t > ( masked.b ) ].value;

			while ( subject >= 0 && b.exprs [ static_cast < size_t > ( subject ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( subject ) ].a >= 0 && b.exprs [ static_cast < size_t > ( subject ) ].size < b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( subject ) ].a ) ].size && b.exprs [ static_cast < size_t > ( subject ) ].size < 8 && mask > 0 && static_cast < uint64_t > ( mask ) < ( 1ull << ( b.exprs [ static_cast < size_t > ( subject ) ].size * 8 - 1 ) ) && !( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( subject ) ].a ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( subject ) ].a ) ].kind == op::VAR )
				subject = b.exprs [ static_cast < size_t > ( subject ) ].a;

			if ( subject != masked.a ) {

				out.put ( L"(" );
				value ( subject, precedence ( op::AND ) );
				out.put ( L" & ", syntax::OPERATOR );
				value ( masked.b, precedence ( op::AND ) + 1 );
				out.put ( L")" );

			} else
				value ( tested_bits, precedence ( op::NE ) + 1 );

			out.put ( equal ? L" == " : L" != ", syntax::OPERATOR );
			out.put ( L"0", syntax::NUMBER );

			if ( wrap )
				out.put ( L")" );

			return;

		}

	}

	if ( ( item.kind == op::EQ || item.kind == op::NE ) && item.b >= 0 && !( item.bits & FLOATING ) ) {

		const expr& zero = b.exprs [ static_cast < size_t > ( item.b ) ];
		const expr& tested = b.exprs [ static_cast < size_t > ( item.a ) ];

		int32_t bare = item.a;

		while ( bare >= 0 && b.exprs [ static_cast < size_t > ( bare ) ].kind == op::CAST )
			bare = b.exprs [ static_cast < size_t > ( bare ) ].a;

		bool assigning = bare >= 0 && b.exprs [ static_cast < size_t > ( bare ) ].kind == op::SET;

		if ( zero.kind == op::CONST && zero.value == 0 && !( zero.bits & POINTER ) && !( tested.bits & FLOATING ) && !explicit_zero && !assigning ) {

			if ( item.kind == op::NE ) {

				value ( item.a, parent );

				return;

			}

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"!", syntax::OPERATOR );
			value ( item.a, 14 );

			if ( parent > 14 )
				out.put ( L")" );

			return;

		}

	}

	if ( global_arrays && item.kind == op::ADD && item.a >= 0 && item.b >= 0 ) {

		int32_t scaled = b.exprs [ static_cast < size_t > ( item.a ) ].kind == op::MUL ? item.a : ( b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::MUL ? item.b : -1 );
		int64_t step = scaled >= 0 && b.exprs [ static_cast < size_t > ( scaled ) ].b >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( scaled ) ].b ) ].kind == op::CONST ? b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( scaled ) ].b ) ].value : 0;
		int32_t at_index = -1;
		uint64_t start = 0;

		if ( ( step == 2 || step == 4 || step == 8 ) && global_index ( index, static_cast < int32_t > ( step ), at_index, start ) ) {

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"&", syntax::OPERATOR );
			global_element ( start, at_index );

			if ( parent > 14 )
				out.put ( L")" );

			return;

		}

	}

	if ( void_arithmetic && item.kind == op::ADD && item.a >= 0 && item.b >= 0 && item.size == 8 ) {

		int32_t terms [ 8 ];
		int32_t count_terms = 0;
		int32_t stack [ 8 ];
		int32_t depth = 0;
		int64_t sum = 0;
		bool ok = true;

		stack [ depth++ ] = index;

		while ( depth && ok ) {

			int32_t at = stack [ --depth ];
			const expr& node = b.exprs [ static_cast < size_t > ( at ) ];

			if ( node.kind == op::ADD && node.size == 8 && node.a >= 0 && node.b >= 0 && depth + 2 <= 8 ) {

				stack [ depth++ ] = node.b;
				stack [ depth++ ] = node.a;

			} else if ( node.kind == op::CONST && !( node.bits & POINTER ) )
				sum += node.value;
			else if ( count_terms < 8 )
				terms [ count_terms++ ] = at;
			else
				ok = false;

		}

		if ( ok && count_terms == 2 && void_element_load ( terms [ 0 ] ) != void_element_load ( terms [ 1 ] ) && !( b.exprs [ static_cast < size_t > ( void_element_load ( terms [ 0 ] ) ? terms [ 1 ] : terms [ 0 ] ) ].bits & POINTER ) && pointer_step ( void_element_load ( terms [ 0 ] ) ? terms [ 1 ] : terms [ 0 ] ) == 0 ) {

			int32_t element_load = void_element_load ( terms [ 0 ] ) ? terms [ 0 ] : terms [ 1 ];
			int32_t offset_term = element_load == terms [ 0 ] ? terms [ 1 ] : terms [ 0 ];

			if ( parent > 12 )
				out.put ( L"(" );

			out.put ( L"(" );
			out.put ( L"char", syntax::TYPE );
			out.put ( L" *)" );
			value ( element_load, 14 );
			out.put ( L" + ", syntax::OPERATOR );
			value ( offset_term, 13 );

			if ( sum ) {

				out.put ( sum < 0 ? L" - " : L" + ", syntax::OPERATOR );
				index_number ( sum < 0 ? -sum : sum );

			}

			if ( parent > 12 )
				out.put ( L")" );

			return;

		}

	}

	if ( char_address && item.kind == op::ADD && item.a >= 0 && item.b >= 0 ) {

		int32_t base_var = -1;
		int32_t at_index = -1;
		int64_t at_offset = 0;

		if ( char_terms ( index, base_var, at_index, at_offset ) ) {

			wchar_t base_name [ 64 ];

			var_name ( base_var, base_name );

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"&", syntax::OPERATOR );
			out.put ( base_name, b.vars [ static_cast < size_t > ( base_var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
			out.put ( L"[" );
			value ( at_index, at_offset ? 12 : 0 );

			if ( at_offset ) {

				out.put ( at_offset < 0 ? L" - " : L" + ", syntax::OPERATOR );
				index_number ( at_offset < 0 ? -at_offset : at_offset );

			}

			out.put ( L"]" );

			if ( parent > 14 )
				out.put ( L")" );

			return;

		}

	}

	int32_t mine = precedence ( item.kind );
	bool wrap = mine < parent;
	bool outer = operand;
	wchar_t name [ 300 ];

	if ( wrap )
		out.put ( L"(" );

	switch ( item.kind ) {

		case op::CONST:

			if ( item.bits & POINTER )
				pointer_constant ( static_cast < uint64_t > ( item.value ) );
			else
				number ( item.value, item.size, hex_hint || !simple_constant ( item.value ), false );

			break;

		case op::REAL:
			real ( item.value, item.size );
			break;

		case op::VAR: {

			int32_t var = static_cast < int32_t > ( item.value );

			if ( array_element ( var, b.vars [ static_cast < size_t > ( var ) ].size, b.vars [ static_cast < size_t > ( var ) ].bits, false ) )
				break;

			if ( active_var >= 0 && var == active_var && var_step ( var ) == active_unit ) {

				var_name ( var, name );

				if ( active_how <= 2 )
					out.put ( active_how == 1 ? L"++" : L"--", syntax::OPERATOR );

				out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

				if ( active_how >= 3 )
					out.put ( active_how == 3 ? L"++" : L"--", syntax::OPERATOR );

				if ( active_cond && active_slot >= 0 && active_slot < fold_cond_done.count ( ) )
					fold_cond_done [ static_cast < size_t > ( active_slot ) ] = 1;
				else if ( !active_cond && active_slot >= 0 && active_slot < fold_done.count ( ) )
					fold_done [ static_cast < size_t > ( active_slot ) ] = 1;

				active_var = -1;

				break;

			}

			var_name ( var, name );
			out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

			break;

		}

		case op::GLOBAL:

			if ( literal_floats && ( item.bits & FLOATING ) && ( item.size == 4 || item.size == 8 ) ) {

				int32_t section = b.file->section_at ( static_cast < uint64_t > ( item.value ) );
				uint64_t offset = 0;

				if ( section >= 0 && section < b.file->sections.count ( ) && !b.file->sections [ static_cast < size_t > ( section ) ].write && !b.file->sections [ static_cast < size_t > ( section ) ].execute && b.file->offset_of ( static_cast < uint64_t > ( item.value ), offset ) && offset + item.size <= b.file->size ) {

					int64_t bits = item.size == 4 ? static_cast < int64_t > ( b.file->read32 ( offset ) ) : static_cast < int64_t > ( b.file->read64 ( offset ) );

					real ( bits, item.size );

					break;

				}

			}

			if ( b.file->section_at ( static_cast < uint64_t > ( item.value ) ) <= 0 && static_cast < uint64_t > ( item.value ) >= b.file->base ) {

				out.put ( L"MEMORY[" );
				out.put ( base::writer ( ).add ( L"0x" ).hex ( static_cast < uint64_t > ( item.value ) ).text ( ), syntax::NUMBER );
				out.put ( L"]" );

			} else if ( b.names->name ( static_cast < uint64_t > ( item.value ), false, name, 200 ) ) {

				if ( simd_types && item.size >= 8 && name [ 0 ] == L's' && name [ 1 ] == L'_' )
					out.put ( base::writer ( ).add ( L"g_" ).hex ( static_cast < uint64_t > ( item.value ) ).text ( ), syntax::GLOBAL, static_cast < uint64_t > ( item.value ) );
				else
					out.put ( name, syntax::GLOBAL, static_cast < uint64_t > ( item.value ) );

			} else
				number ( item.value, 8, true, false );

			break;

		case op::TEXT:
			pointer_constant ( static_cast < uint64_t > ( item.value ) );
			break;

		case op::ADDRESS:

			if ( item.a < 0 && item.value >= 0 && array_element ( static_cast < int32_t > ( item.value ), 0, 0, true ) )
				break;

			if ( item.a >= 0 && b.exprs [ static_cast < size_t > ( item.a ) ].kind == op::VAR && array_element ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.a ) ].value ), 0, 0, true ) )
				break;

			if ( item.value >= 0 && item.a < 0 ) {

				var_name ( static_cast < int32_t > ( item.value ), name );

				if ( item.value != b.va_var )
					out.put ( L"&", syntax::OPERATOR );

				out.put ( name, syntax::LOCAL );

			} else {

				out.put ( L"&", syntax::OPERATOR );
				value ( item.a, 14 );

			}

			break;

		case op::LOAD:
			lvalue_or_load ( item, parent );
			break;

		case op::CALL: {

			uint64_t target = static_cast < uint64_t > ( item.value );

			if ( target && b.names->name ( target, true, name, 200 ) ) {

				int32_t callee = b.db->function_starting ( target );
				bool imported = callee < 0 || ( b.db->functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK );

				short_symbol ( name );

				bool trimmed = imported && name [ 0 ] == L'_' && ( ( name [ 1 ] == L'_' && name [ 2 ] >= L'a' && name [ 2 ] <= L'z' ) || ( trim_crt && name [ 1 ] >= L'a' && name [ 1 ] <= L'z' ) );

				out.put ( trimmed ? name + 1 : name, imported ? syntax::IMPORT : syntax::FUNCTION, target );

			} else if ( target )
				number ( static_cast < int64_t > ( target ), 8, true, false );
			else {

				const expr& callee_node = b.exprs [ static_cast < size_t > ( item.a ) ];

				if ( callee_node.kind == op::LOAD && callee_node.size == 8 && callee_node.a >= 0 && b.long_mode ) {

					out.put ( L"(*(" );
					out.put ( call_unused ? L"void" : L"__int64", syntax::TYPE );
					out.put ( L" (__fastcall **)(" );

					for ( int32_t i = 0; i < item.arg_count; i++ ) {

						const expr& passed = b.exprs [ static_cast < size_t > ( b.args [ static_cast < size_t > ( item.first_arg + i ) ] ) ];

						if ( i )
							out.put ( L", " );

						if ( passed.bits & FLOATING )
							out.put ( passed.size == 4 ? L"float" : L"double", syntax::TYPE );
						else
							out.put ( passed.size == 1 ? L"char" : ( passed.size == 2 ? L"__int16" : ( passed.size == 4 ? L"int" : L"__int64" ) ), syntax::TYPE );

					}

					out.put ( L"))" );

					const expr& slot = b.exprs [ static_cast < size_t > ( callee_node.a ) ];

					if ( slot.kind == op::ADD && slot.b >= 0 && b.exprs [ static_cast < size_t > ( slot.b ) ].kind == op::CONST ) {

						out.put ( L"(" );
						value ( slot.a, 12 );
						out.put ( L" + ", syntax::OPERATOR );
						number ( b.exprs [ static_cast < size_t > ( slot.b ) ].value, 8, false, false );
						out.put ( L")" );

					} else
						value ( callee_node.a, 14 );

					out.put ( L")" );

				} else {

					out.put ( L"(" );
					value ( item.a, 14 );
					out.put ( L")" );

				}

				call_unused = false;

			}

			out.put ( L"(" );
			operand = false;

			for ( int32_t i = 0; i < item.arg_count; i++ ) {

				int32_t argument = b.args [ static_cast < size_t > ( item.first_arg + i ) ];
				const expr& passed = b.exprs [ static_cast < size_t > ( argument ) ];

				if ( i )
					out.put ( L", " );

				if ( !no_casts && passed.kind == op::CONST && b.long_mode && passed.value > 0 && b.db->function_starting ( static_cast < uint64_t > ( passed.value ) ) >= 0 ) {

					callee_unsigned ( target, i );
					out.put ( L"(" );

					if ( target && shape_ok && i < shape->count && shape->types [ i ] [ 0 ] )
						out.put ( shape->types [ i ], syntax::TYPE );
					else
						type_name ( 8, 0, false );

					out.put ( L")" );
					pointer_constant ( static_cast < uint64_t > ( passed.value ) );

					continue;

				}

				if ( !no_casts && target && b.long_mode && passed.kind == op::CAST && passed.size == 8 && passed.a >= 0 && b.exprs [ static_cast < size_t > ( passed.a ) ].size == 16 && i < 4 && callee_parameter_type ( *b.db, *b.file, b.names, target, i, param_type, 64 ) ) {

					out.put ( L"(" );
					out.put ( param_type, syntax::TYPE );
					out.put ( L")" );
					value ( passed.a, 14 );

					continue;

				}

				bool unsigned_slot = passed.kind == op::CONST && passed.value < -9 && passed.size <= 4 && callee_unsigned ( target, i );

				bare_unsigned = unsigned_slot;

				if ( inferred_unsigned && passed.kind == op::CONST && !( passed.bits & POINTER ) && passed.value > 0 && target && b.long_mode && i < 4 && ( callee_unsigned ( target, i ), !shape_ok ) && callee_parameter_type ( *b.db, *b.file, b.names, target, i, param_type, 64 ) && base::same_text ( param_type, L"unsigned __int64" ) ) {

					number ( passed.value, 8, passed.value > 9, true );
					bare_unsigned = false;

					continue;

				}

				if ( passed.kind == op::CONST && ( !( passed.bits & POINTER ) || passed.value < 0x10000 ) && passed.value > 0 && callee_unsigned ( target, i ) )
					number ( passed.value, callee_wide ( target, i ) && b.long_mode ? 8 : 4, passed.value > 9, true );
				else if ( passed.kind == op::CONST && !( passed.bits & POINTER ) && b.long_mode && callee_wide ( target, i ) )
					number ( passed.value, 8, !simple_constant ( passed.value ), false );
				else if ( wide_internal_args && passed.kind == op::CONST && !( passed.bits & POINTER ) && b.long_mode && target && !shape_ok && i < 4 && b.db->function_starting ( target ) >= 0 && callee_argument_width ( *b.db, target, i, 0 ) == 8 )
					number ( passed.value, 8, !simple_constant ( passed.value ), false );
				else if ( b.long_mode && !no_casts && target && passed.kind != op::CONST && passed.size == 4 && !( passed.bits & FLOATING ) && ( callee_unsigned ( target, i ), !shape_ok ) && !( i < 4 && callee_argument_width ( *b.db, target, i, 0 ) == 4 ) && !( passed.kind == op::VAR && passed.value >= 0 && passed.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( passed.value ) ].bits & UNSIGNED ) && b.vars [ static_cast < size_t > ( passed.value ) ].size == 4 ) ) {

					out.put ( L"(unsigned int)" );

					if ( call_untruncate && passed.kind == op::CAST && passed.a >= 0 && b.exprs [ static_cast < size_t > ( passed.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( passed.a ) ].size == 8 )
						value ( passed.a, 14 );
					else
						value ( argument, 14 );

				} else if ( b.long_mode && !no_casts && target && passed.size == 8 && !( passed.bits & ( FLOATING | POINTER ) ) && ( passed.kind == op::LOAD || passed.kind == op::GLOBAL || passed.kind == op::ADD ) && !( passed.kind == op::ADD && char_typed_sum ( argument ) ) && ( callee_unsigned ( target, i ), !shape_ok ) && !void_element_load ( argument ) && callee_parameter_type ( *b.db, *b.file, b.names, target, i, param_type, 64 ) ) {

					if ( passed.kind == op::LOAD ) {

						out.put ( L"*(" );
						out.put ( param_type, syntax::TYPE );
						out.put ( param_type [ 0 ] && param_type [ base::text_length ( param_type ) - 1 ] == L'*' ? L"*)" : L" *)" );
						value ( passed.a, 14 );

					} else {

						out.put ( L"(" );
						out.put ( param_type, syntax::TYPE );
						out.put ( L")" );
						value ( argument, 14 );

					}

				} else if ( b.long_mode && !no_casts && target && passed.kind == op::VAR && passed.value >= 0 && passed.value < b.vars.count ( ) && passed.size == 8 && !( passed.bits & FLOATING ) && b.vars [ static_cast < size_t > ( passed.value ) ].kind != var_kind::PARAMETER && ( passed.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( passed.value ) ].empty ( ) ) && ( passed.value >= element.count ( ) || !element [ static_cast < size_t > ( passed.value ) ] ) && ( callee_unsigned ( target, i ), shape_ok ) && i < shape->count && shape->params [ i ].pointer && shape->types [ i ] [ 0 ] ) {

					out.put ( L"(" );
					out.put ( shape->types [ i ], syntax::TYPE );
					out.put ( L")" );
					value ( argument, 14 );

				} else if ( b.long_mode && !no_casts && target && passed.kind == op::VAR && passed.value >= 0 && passed.value < element.count ( ) && element [ static_cast < size_t > ( passed.value ) ] && ( callee_unsigned ( target, i ), !shape_ok ) && !( i < 4 && b.db && ( callee_pointer_shape ( *b.db, b.names, target, i, 0 ) & 0x9F ) == ( element [ static_cast < size_t > ( passed.value ) ] & 0x9F ) ) ) {

					out.put ( L"(__int64)" );
					value ( argument, 14 );

				} else {

					bool narrow_arg = call_untruncate && passed.size == 4 && !( passed.bits & FLOATING );
					bool held = untruncate;

					untruncate = untruncate || narrow_arg;
					value ( argument, 2 );
					untruncate = held;

				}

				bare_unsigned = false;

			}

			out.put ( L")" );

			break;

		}

		case op::INTRINSIC: {

			const wchar_t* called = intrinsic_name ( static_cast < int32_t > ( item.value ) );

			if ( abs_masks && item.arg_count == 2 && ( base::same_text ( called, L"_mm_and_pd" ) || base::same_text ( called, L"_mm_and_ps" ) ) ) {

				bool doubles = called [ 9 ] == L'd';
				int32_t first = b.args [ static_cast < size_t > ( item.first_arg ) ];
				int32_t second = b.args [ static_cast < size_t > ( item.first_arg + 1 ) ];
				int32_t subject = abs_mask ( first, doubles ) ? second : ( abs_mask ( second, doubles ) ? first : -1 );

				if ( subject >= 0 ) {

					out.put ( L"fabs", syntax::FUNCTION );
					out.put ( L"(" );
					value ( subject, 0 );
					out.put ( L")" );

					break;

				}

			}
			bool wide = item.size == 8 && called [ 0 ] == L'_' && called [ 1 ] == L'I' && called [ 2 ] == L'n' && called [ 3 ] == L't';

			out.put ( wide ? base::writer ( ).add ( called ).add ( L"64" ).text ( ) : called, syntax::FUNCTION );
			out.put ( L"(" );
			operand = false;

			int32_t whole = -1;

			if ( item.arg_count == 3 && base::same_text ( called, L"memset" ) ) {

				const expr& where = b.exprs [ static_cast < size_t > ( b.args [ static_cast < size_t > ( item.first_arg ) ] ) ];
				const expr& amount = b.exprs [ static_cast < size_t > ( b.args [ static_cast < size_t > ( item.first_arg + 2 ) ] ) ];
				int64_t v = where.kind == op::ADDRESS ? ( where.a < 0 ? where.value : ( b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( where.a ) ].value : -1 ) ) : -1;

				if ( v >= 0 && v < array_of.count ( ) && array_of [ static_cast < size_t > ( v ) ] == v && amount.kind == op::CONST && amount.value == static_cast < int64_t > ( array_step [ static_cast < size_t > ( v ) ] ) * array_count [ static_cast < size_t > ( v ) ] )
					whole = static_cast < int32_t > ( v );

			}

			for ( int32_t i = 0; i < item.arg_count; i++ ) {

				if ( i )
					out.put ( L", " );

				if ( i == 2 && whole >= 0 ) {

					wchar_t array_name [ 64 ];

					var_name ( whole, array_name );
					out.put ( L"sizeof", syntax::KEYWORD );
					out.put ( L"(" );
					out.put ( array_name, syntax::LOCAL );
					out.put ( L")" );

					continue;

				}

				int32_t argument = b.args [ static_cast < size_t > ( item.first_arg + i ) ];
				int32_t flavor = simd_types ? packed_flavor ( called, false ) : 0;
				const wchar_t* pointer_cast = nullptr;

				if ( simd_types && i == 0 && ( base::same_text ( called, L"_mm_storel_ps" ) || base::same_text ( called, L"_mm_storeh_ps" ) || base::same_text ( called, L"_mm_storel_pd" ) || base::same_text ( called, L"_mm_storeh_pd" ) ) )
					pointer_cast = L"double *";
				else if ( simd_types && i == 1 && ( base::same_text ( called, L"_mm_loadl_pi" ) || base::same_text ( called, L"_mm_loadh_pi" ) ) )
					pointer_cast = L"const __m64 *";
				else if ( simd_types && i == 1 && ( base::same_text ( called, L"_mm_loadl_pd" ) || base::same_text ( called, L"_mm_loadh_pd" ) ) )
					pointer_cast = L"const double *";

				if ( pointer_cast ) {

					out.put ( L"(" );
					out.put ( pointer_cast, syntax::TYPE );
					out.put ( L")" );
					value ( argument, 14 );

					continue;

				}

				if ( simd_types && i == 1 && b.exprs [ static_cast < size_t > ( argument ) ].kind == op::CONST && called [ 0 ] == L'_' && called [ 4 ] == L's' && ( called [ 5 ] == L'r' || called [ 5 ] == L'l' ) && called [ 7 ] == L'i' && called [ 8 ] == L'_' && called [ 9 ] == L'e' && called [ 10 ] == L'p' ) {

					out.put ( base::writer ( ).number ( b.exprs [ static_cast < size_t > ( argument ) ].value ).add ( L"u" ).text ( ), syntax::NUMBER );

					continue;

				}

				if ( flavor == 2 && b.exprs [ static_cast < size_t > ( argument ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( argument ) ].size == 16 ) {

					vector_load ( b.exprs [ static_cast < size_t > ( argument ) ].a, static_cast < uint8_t > ( flavor ) );

					continue;

				}

				if ( scalar_lanes && ( flavor == 3 || flavor == 4 ) ) {

					const expr& passed = b.exprs [ static_cast < size_t > ( argument ) ];
					const wchar_t* packed = flavor == 3 ? L"__m128" : L"__m128d";

					if ( passed.kind == op::VAR && passed.size < 16 && ( passed.bits & FLOATING ) && passed.value >= 0 && passed.value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( passed.value ) ].kind == var_kind::PARAMETER && !( passed.value < vector.count ( ) && vector [ static_cast < size_t > ( passed.value ) ] ) ) {

						out.put ( L"*(" );
						out.put ( packed, syntax::TYPE );
						out.put ( L" *)&" );
						value ( argument, 14 );

						continue;

					}

					if ( passed.kind == op::LOAD && ( passed.size == 4 || passed.size == 8 ) && ( passed.bits & FLOATING ) && passed.a >= 0 ) {

						out.put ( L"(" );
						out.put ( packed, syntax::TYPE );
						out.put ( L")*(" );
						out.put ( passed.size == 4 ? L"unsigned int" : L"unsigned __int64", syntax::TYPE );
						out.put ( L" *)" );
						value ( passed.a, 14 );

						continue;

					}

				}

				value ( argument, 2 );

			}

			out.put ( L")" );

			break;

		}

		case op::CAST: {

			const expr& inner = b.exprs [ static_cast < size_t > ( item.a ) ];
			bool float_change = ( inner.bits & FLOATING ) != ( item.bits & FLOATING );

			if ( float_views && inner.kind == op::VAR && ( inner.bits & FLOATING ) && inner.size == 8 && item.size == 4 && !( item.bits & ( FLOATING | SIGNED | UNSIGNED | POINTER ) ) && !no_casts ) {

				if ( outer_kind == op::SHL || outer_kind == op::SHR || outer_kind == op::OR || outer_kind == op::AND || outer_kind == op::XOR ) {

					out.put ( L"COERCE_UNSIGNED_INT", syntax::FUNCTION );
					out.put ( L"(" );
					value ( item.a, 0 );
					out.put ( L")" );

					break;

				}

				if ( parent > 14 )
					out.put ( L"(" );

				out.put ( L"(float)", syntax::TYPE );
				value ( item.a, 14 );

				if ( parent > 14 )
					out.put ( L")" );

				break;

			}

			if ( float_zero && ( item.bits & FLOATING ) && !( item.bits & POINTER ) && ( item.size == 4 || item.size == 8 ) && inner.kind == op::CONST && inner.value == 0 ) {

				out.put ( L"0.0", syntax::NUMBER );

				break;

			}

			if ( abs_masks && ( item.bits & FLOATING ) && inner.kind == op::INTRINSIC && inner.arg_count == 2 && inner.size == 16 ) {

				const wchar_t* inner_name = intrinsic_name ( static_cast < int32_t > ( inner.value ) );

				if ( ( base::same_text ( inner_name, L"_mm_and_pd" ) && item.size == 8 ) || ( base::same_text ( inner_name, L"_mm_and_ps" ) && item.size == 4 ) ) {

					bool doubles = item.size == 8;

					if ( abs_mask ( b.args [ static_cast < size_t > ( inner.first_arg ) ], doubles ) || abs_mask ( b.args [ static_cast < size_t > ( inner.first_arg + 1 ) ], doubles ) ) {

						value ( item.a, parent );

						break;

					}

				}

			}

			if ( bare_tests && !float_change && !( item.bits & FLOATING ) && ( outer_kind == op::ADD || outer_kind == op::SUB ) && ( inner.kind == op::LNOT || ( inner.kind >= op::EQ && inner.kind <= op::GE ) ) ) {

				out.put ( L"(" );

				if ( inner.kind == op::LNOT && inner.a >= 0 ) {

					value ( inner.a, 9 );
					out.put ( L" == ", syntax::OPERATOR );
					out.put ( L"0", syntax::NUMBER );

				} else {

					bool held_zero = explicit_zero;

					explicit_zero = true;
					value ( item.a, 0 );
					explicit_zero = held_zero;

				}

				out.put ( L")" );

				break;

			}

			if ( zero_ranges && !float_change && inner.kind == op::VAR && inner.size > item.size && !( item.bits & ( SIGNED | FLOATING ) ) && inner.value >= 0 && inner.value < zero_known.count ( ) && zero_known [ static_cast < size_t > ( inner.value ) ] && zero_known [ static_cast < size_t > ( inner.value ) ] <= item.size ) {

				value ( item.a, parent );

				break;

			}

			if ( untruncate && !float_change && inner.kind == op::VAR && inner.size > item.size && item.size == 4 ) {

				value ( item.a, parent );

				break;

			}

			bool retyped =inner.kind == op::VAR && inner.value >= 0 && inner.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( inner.value ) ].empty ( ) && b.vars [ static_cast < size_t > ( inner.value ) ].size == item.size;
			bool shown = ( float_change || inner.size != item.size ) && !no_casts && !retyped;

			if ( ( item.bits & POINTER ) && ( item.bits & FLOATING ) ) {

				out.put ( inner.size == 8 ? L"COERCE_DOUBLE" : L"COERCE_FLOAT", syntax::FUNCTION );
				out.put ( L"(" );
				value ( item.a, 0 );
				out.put ( L")" );

				break;

			}

			if ( ( item.bits & POINTER ) && !float_change ) {

				value ( item.a, parent );

				break;

			}

			if ( inner.kind == op::LOAD && item.size > inner.size && !float_change && !( item.bits & FLOATING ) ) {

				extended_load ( inner, parent, item.bits & SIGNED ? SIGNED : UNSIGNED );

				break;

			}

			if ( collapse_casts && !no_casts && !float_change && item.size == 8 && inner.kind == op::CAST && inner.size == 4 && inner.a >= 0 && b.exprs [ static_cast < size_t > ( inner.a ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( inner.a ) ].bits & FLOATING ) && !( inner.bits & FLOATING ) ) {

				if ( parent > 14 )
					out.put ( L"(" );

				out.put ( ( item.bits & SIGNED ) ? L"(int)" : L"(unsigned int)", syntax::TYPE );
				value ( inner.a, 14 );

				if ( parent > 14 )
					out.put ( L")" );

				break;

			}

			if ( shown && widened_halves && !float_change && item.size == 4 && inner.size == 8 && outer_size == 8 && !( item.bits & ( SIGNED | FLOATING ) ) && !( inner.bits & FLOATING ) && ( outer_kind == op::MUL || outer_kind == op::ADD || outer_kind == op::SUB || outer_kind == op::SHL || outer_kind == op::AND || outer_kind == op::OR ) ) {

				if ( parent > 14 )
					out.put ( L"(" );

				out.put ( L"(unsigned int)", syntax::TYPE );
				value ( item.a, 14 );

				if ( parent > 14 )
					out.put ( L")" );

				break;

			}

			if ( shown ) {

				bool narrow = !float_change && item.size < inner.size && !( item.bits & ( SIGNED | UNSIGNED ) );

				out.put ( L"(" );
				type_name ( item.size, item.bits, false, narrow );
				out.put ( L")" );

			}

			value ( item.a, shown ? 14 : parent );

			break;

		}

		case op::NOT:
			out.put ( L"~", syntax::OPERATOR );
			value ( item.a, 14, true );
			break;

		case op::NEG:
			out.put ( L"-", syntax::OPERATOR );
			value ( item.a, 14 );
			break;

		case op::LNOT:
			out.put ( L"!", syntax::OPERATOR );
			value ( item.a, 14 );
			break;

		case op::SET:

			out.put ( L"(" );
			value ( item.a, 0 );
			out.put ( L" = ", syntax::OPERATOR );

			{

				const expr& stored = b.exprs [ static_cast < size_t > ( item.b ) ];
				const expr& target_var = b.exprs [ static_cast < size_t > ( item.a ) ];

				if ( stored.kind == op::CONST && !( stored.bits & POINTER ) && target_var.size == 8 && b.long_mode )
					number ( stored.value, 8, !simple_constant ( stored.value ), false );
				else
					value ( item.b, 2 );

			}

			if ( item.value == 1 && item.c >= 0 ) {

				out.put ( L", " );
				value ( item.c, 2 );

			}

			out.put ( L")" );
			break;

		case op::TERNARY: {

			auto arm = [ & ] ( int32_t e, int32_t level ) {

				const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

				bare_unsigned = node.kind == op::CONST && node.size == 4 && node.value < -9;
				value ( e, level );
				bare_unsigned = false;

			};

			const expr& test = b.exprs [ static_cast < size_t > ( item.a ) ];
			bool zero_test = ( test.kind == op::EQ || test.kind == op::NE ) && test.b >= 0 && b.exprs [ static_cast < size_t > ( test.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( test.b ) ].value == 0 && !( test.bits & FLOATING );
			bool swap = zero_test && test.kind == op::EQ;

			if ( zero_test )
				value ( test.a, 4 );
			else {

				explicit_zero = test.kind == op::EQ || test.kind == op::NE;
				value ( item.a, 4 );
				explicit_zero = false;

			}

			out.put ( L" ? ", syntax::OPERATOR );
			arm ( swap ? item.c : item.b, 4 );
			out.put ( L" : ", syntax::OPERATOR );
			arm ( swap ? item.b : item.c, 3 );
			break;

		}

		default: {

			if ( slot_part ( index ) ) {

				wchar_t slot_name [ 64 ];

				var_name ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.a ) ].value ), slot_name );
				out.put ( L"(char *)&" );
				out.put ( slot_name, syntax::LOCAL );
				out.put ( L" + ", syntax::OPERATOR );
				index_number ( b.exprs [ static_cast < size_t > ( item.b ) ].value );

				break;

			}

			if ( item.kind == op::ADD && item.a >= 0 && item.b >= 0 && pointer_step ( item.a ) > 1 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::MUL ) {

				const expr& scaled = b.exprs [ static_cast < size_t > ( item.b ) ];

				if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == pointer_step ( item.a ) ) {

					out.put ( L"&", syntax::OPERATOR );
					value ( item.a, 15 );
					out.put ( L"[" );
					value ( scaled.a, 0 );
					out.put ( L"]" );

					break;

				}

			}

			if ( item.kind == op::ADD && item.b >= 0 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::CONST && !( b.exprs [ static_cast < size_t > ( item.b ) ].bits & POINTER ) ) {

				int32_t step = pointer_step ( item.a );
				int64_t amount = b.exprs [ static_cast < size_t > ( item.b ) ].value;

				if ( step > 0 && amount % step == 0 ) {

					value ( item.a, mine );
					out.put ( amount < 0 ? L" - " : L" + ", syntax::OPERATOR );
					out.put ( base::writer ( ).number ( static_cast < int64_t > ( amount < 0 ? -amount : amount ) / step ).text ( ), syntax::NUMBER );

					break;

				}

			}

			const wchar_t* symbol = L"?";
			bool bitwise = false;

			switch ( item.kind ) {

				case op::ADD:
					symbol = L" + ";
					break;

				case op::SUB:
					symbol = L" - ";
					break;

				case op::MUL:
					symbol = L" * ";
					break;

				case op::DIV:
					symbol = L" / ";
					bitwise = hex_divisors && ( item.bits & UNSIGNED );
					break;

				case op::MOD:
					symbol = L" % ";
					bitwise = hex_divisors && ( item.bits & UNSIGNED );
					break;

				case op::AND:
					symbol = logical_pair ( item ) ? L" && " : L" & ";
					bitwise = !logical_pair ( item );
					break;

				case op::OR:
					symbol = logical_pair ( item ) ? L" || " : L" | ";
					bitwise = !logical_pair ( item );
					break;

				case op::XOR:
					symbol = L" ^ ";
					bitwise = true;
					break;

				case op::SHL:
					symbol = L" << ";
					break;

				case op::SHR:
					symbol = L" >> ";
					break;

				case op::EQ:
					symbol = L" == ";
					break;

				case op::NE:
					symbol = L" != ";
					break;

				case op::LT:
					symbol = L" < ";
					break;

				case op::LE:
					symbol = L" <= ";
					break;

				case op::GT:
					symbol = L" > ";
					break;

				case op::GE:
					symbol = L" >= ";
					break;

				case op::LAND:
					symbol = L" && ";
					break;

				case op::LOR:
					symbol = L" || ";
					break;

				default:
					break;

			}

			const expr& right = b.exprs [ static_cast < size_t > ( item.b ) ];

			operand = true;

			if ( mask_forms && item.kind == op::AND && right.kind == op::CONST && !( right.bits & POINTER ) && !( item.bits & FLOATING ) && item.a >= 0 ) {

				const expr& masked = b.exprs [ static_cast < size_t > ( item.a ) ];
				int32_t subject = item.a;

				if ( masked.kind == op::CAST && masked.a >= 0 && masked.size < b.exprs [ static_cast < size_t > ( masked.a ) ].size && masked.size <= 4 && right.value > 0 && right.value < ( 1ll << ( masked.size * 8 - 1 ) ) && b.exprs [ static_cast < size_t > ( masked.a ) ].kind == op::VAR && !( b.exprs [ static_cast < size_t > ( masked.a ) ].bits & FLOATING ) )
					subject = masked.a;

				if ( subject != item.a || ( right.value < 0 && item.size == 8 ) ) {

					value ( subject, mine, true );
					out.put ( symbol, syntax::OPERATOR );

					bool held = operand;

					operand = false;

					if ( right.value < 0 && item.size == 8 )
						number ( right.value, 8, true, true );
					else
						value ( item.b, mine + 1, true );

					operand = held;

					break;

				}

			}

			if ( base_first && item.kind == op::ADD && item.a >= 0 && !( item.bits & FLOATING ) && right.kind != op::CONST && right.kind != op::MUL && right.kind != op::SHL ) {

				const expr& scaled = b.exprs [ static_cast < size_t > ( item.a ) ];

				if ( ( scaled.kind == op::MUL || scaled.kind == op::SHL ) && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && ( right.kind == op::LOAD || right.kind == op::VAR || right.kind == op::GLOBAL || right.kind == op::CAST || ( right.bits & POINTER ) ) ) {

					value ( item.b, mine, bitwise );
					out.put ( symbol, syntax::OPERATOR );
					value ( item.a, mine + 1, bitwise );

					break;

				}

			}

			if ( item.kind == op::ADD && right.kind == op::CONST && right.value < 0 && !( right.bits & POINTER ) ) {

				value ( item.a, mine );
				out.put ( L" - ", syntax::OPERATOR );
				number ( -right.value, right.size, !simple_constant ( -right.value ), false );

				break;

			}

			if ( item.kind == op::ADD && right.kind != op::CONST && item.a >= 0 && !( item.bits & FLOATING ) ) {

				const expr& inner = b.exprs [ static_cast < size_t > ( item.a ) ];

				if ( inner.kind == op::ADD && inner.b >= 0 && inner.size == item.size && !( inner.bits & FLOATING ) ) {

					const expr& offset = b.exprs [ static_cast < size_t > ( inner.b ) ];

					if ( offset.kind == op::CONST && !( offset.bits & POINTER ) ) {

						value ( inner.a, mine, bitwise );
						out.put ( L" + ", syntax::OPERATOR );
						value ( item.b, mine + 1, bitwise );
						out.put ( offset.value < 0 ? L" - " : L" + ", syntax::OPERATOR );
						number ( offset.value < 0 ? -offset.value : offset.value, offset.size, !simple_constant ( offset.value ), false );

						break;

					}

				}

			}

			if ( item.kind == op::MUL && right.kind == op::CONST && !( right.bits & POINTER ) && !( item.bits & FLOATING ) ) {

				bool held = operand;
				bool widened = item.size == 8 && item.a >= 0 && b.exprs [ static_cast < size_t > ( item.a ) ].size < 8;
				const expr& factor = b.exprs [ static_cast < size_t > ( item.a ) ];
				bool extended = sext_products && item.size == 8 && factor.kind == op::CAST && factor.size == 8 && ( factor.bits & SIGNED ) && factor.a >= 0 && b.exprs [ static_cast < size_t > ( factor.a ) ].size == 4 && !( b.exprs [ static_cast < size_t > ( factor.a ) ].bits & FLOATING );

				operand = !( widened || extended ) && held;
				number ( right.value, item.size == 8 ? 8 : right.size, !simple_constant ( right.value ), false );
				operand = held;
				out.put ( symbol, syntax::OPERATOR );

				if ( extended ) {

					const expr& inner = b.exprs [ static_cast < size_t > ( factor.a ) ];

					if ( inner.kind == op::CAST && inner.a >= 0 && b.exprs [ static_cast < size_t > ( inner.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( inner.a ) ].size > 4 ) {

						out.put ( L"(int)", syntax::TYPE );
						value ( inner.a, 14, bitwise );

					} else if ( inner.kind == op::LOAD )
						extended_load ( inner, mine + 1, SIGNED );
					else
						value ( factor.a, mine + 1, bitwise );

					break;

				}

				value ( item.a, mine + 1, bitwise );

				break;

			}

			if ( item.kind == op::ADD && right.kind == op::CONST && !( right.bits & POINTER ) && right.value > 0 && item.size == 8 && !( item.bits & FLOATING ) && item.a >= 0 && b.exprs [ static_cast < size_t > ( item.a ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( item.a ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( item.a ) ].bits & FLOATING ) ) {

				value ( item.a, mine, bitwise );
				out.put ( symbol, syntax::OPERATOR );

				bool held = operand;

				operand = false;
				number ( right.value, 8, !simple_constant ( right.value ), false );
				operand = held;

				break;

			}

			bool compare = item.kind >= op::EQ && item.kind <= op::GE;
			bool unsigned_compare = compare && ( item.bits & UNSIGNED ) && item.kind != op::EQ && item.kind != op::NE;
			bool float_compare = compare && ( item.bits & FLOATING );
			const expr& lhs = b.exprs [ static_cast < size_t > ( item.a ) ];
			bool lhs_unsigned = lhs.kind == op::LOAD || ( lhs.bits & UNSIGNED ) || ( lhs.kind == op::VAR && lhs.value >= 0 && lhs.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( lhs.value ) ].bits & UNSIGNED ) ) || lhs.kind == op::CAST;
			bool cast_lhs = unsigned_compare && !lhs_unsigned && !no_casts && right.kind == op::CONST && lhs.size <= 8;

			bool signed_compare = compare && ( item.bits & SIGNED ) && item.kind != op::EQ && item.kind != op::NE && !( item.bits & FLOATING );

			bool negative_bound = false;
			bool void_equal = void_equals && flow_casts && !no_casts && ( item.kind == op::EQ || item.kind == op::NE ) && void_element_load ( item.b ) && lhs.kind == op::VAR && pointer_step ( item.a ) == 0 && lhs.value >= 0 && lhs.value < b.vars.count ( ) && !( lhs.bits & FLOATING ) && !( lhs.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( lhs.value ) ].empty ( ) );

			if ( void_equal ) {

				out.put ( L"(void *)", syntax::TYPE );
				value ( item.a, 14, bitwise );

			} else if ( cast_lhs ) {

				out.put ( lhs.size == 1 ? L"(unsigned __int8)" : ( lhs.size == 2 ? L"(unsigned __int16)" : ( lhs.size == 4 ? L"(unsigned int)" : L"(unsigned __int64)" ) ), syntax::TYPE );
				value ( item.a, 14, bitwise );
				unsigned_compare = false;
				negative_bound = unsigned_bounds && right.value < 0;

			} else if ( unsigned_narrow && unsigned_compare && !no_casts && lhs.kind == op::CAST && lhs.a >= 0 && lhs.size < b.exprs [ static_cast < size_t > ( lhs.a ) ].size && lhs.size <= 4 && !( lhs.bits & FLOATING ) && !( b.exprs [ static_cast < size_t > ( lhs.a ) ].bits & FLOATING ) ) {

				out.put ( lhs.size == 1 ? L"(unsigned __int8)" : ( lhs.size == 2 ? L"(unsigned __int16)" : L"(unsigned int)" ), syntax::TYPE );
				value ( lhs.a, 14, bitwise );

			} else if ( signed_compare && !no_casts && lhs.kind == op::CAST && lhs.a >= 0 && lhs.size < b.exprs [ static_cast < size_t > ( lhs.a ) ].size && lhs.size <= 4 && !( lhs.bits & FLOATING ) && b.exprs [ static_cast < size_t > ( lhs.a ) ].kind == op::VAR ) {

				out.put ( lhs.size == 1 ? L"(char)" : ( lhs.size == 2 ? L"(__int16)" : L"(int)" ), syntax::TYPE );
				value ( lhs.a, 14, bitwise );

			} else
				value ( item.a, item.kind == op::SHL || item.kind == op::SHR ? mine + 1 : mine, bitwise );

			bool hex_bound = false;

			if ( unsigned_compare && lhs.kind == op::VAR && lhs.value >= 0 && lhs.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( lhs.value ) ].bits & UNSIGNED ) ) {

				unsigned_compare = false;
				hex_bound = true;

			}

			out.put ( symbol, syntax::OPERATOR );

			if ( right.kind == op::CONST && !( right.bits & POINTER ) && compare ) {

				bool held = operand;

				if ( lhs.kind == op::SET && lhs.size == 8 && b.long_mode )
					operand = false;

				if ( float_compare && right.value == 0 )
					real ( 0, right.size );
				else if ( negative_bound )
					number ( right.value, lhs.size, true, true );
				else
					number ( right.value, lhs.kind == op::SET ? lhs.size : right.size, !simple_constant ( right.value ) || ( ( unsigned_compare || hex_bound ) && right.value > 9 ), unsigned_compare || ( hex_bound && right.value < 0 ) );

				operand = held;

			} else if ( compare && !void_equal && flow_casts && void_element_load ( item.b ) && lhs.kind == op::VAR && pointer_step ( item.a ) == 0 && lhs.value >= 0 && lhs.value < b.vars.count ( ) ) {

				out.put ( ( b.vars [ static_cast < size_t > ( lhs.value ) ].bits & UNSIGNED ) ? L"(unsigned __int64)" : L"(__int64)", syntax::TYPE );
				value ( item.b, 14 );

			} else if ( ( item.kind == op::LAND || item.kind == op::LOR ) && item.b >= 0 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == item.kind )
				value ( item.b, mine, bitwise );
			else
				value ( item.b, mine + 1, bitwise );

			break;

		}

	}

	if ( wrap )
		out.put ( L")" );

	operand = outer;

}

bool decompiler::emitter::float_target ( int32_t left ) {

	const expr& item = b.exprs [ static_cast < size_t > ( left ) ];

	if ( item.bits & FLOATING )
		return true;

	if ( item.kind == op::VAR )
		return ( b.vars [ static_cast < size_t > ( item.value ) ].bits & FLOATING ) != 0;

	if ( item.kind == op::GLOBAL ) {

		for ( uint64_t address : b.float_globals ) {

			if ( address == static_cast < uint64_t > ( item.value ) )
				return true;

		}

	}

	if ( item.kind == op::LOAD ) {

		const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
		int32_t var = -1;
		int64_t offset = 0;

		if ( where.kind == op::VAR )
			var = static_cast < int32_t > ( where.value );
		else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

			var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );
			offset = b.exprs [ static_cast < size_t > ( where.b ) ].value;

		}

		for ( const field_use& field : b.fields ) {

			if ( field.var == var && field.offset == offset && ( field.bits & FLOATING ) )
				return true;

		}

	}

	return false;

}

void decompiler::emitter::collect_ternaries ( int32_t index, base::list < int32_t >& found, bool top ) {

	if ( index < 0 || found.count ( ) >= 8 )
		return;

	const expr item = b.exprs [ static_cast < size_t > ( index ) ];

	for ( int32_t done : hoisted ) {

		if ( done == index )
			return;

	}

	switch ( item.kind ) {

		case op::TERNARY:

			if ( !top )
				found.push ( index );

			return;

		case op::LAND:
		case op::LOR:
			collect_ternaries ( item.a, found, false );
			return;

		case op::CALL:
		case op::INTRINSIC:

			if ( item.kind == op::CALL && !item.value )
				collect_ternaries ( item.a, found, false );

			for ( int32_t i = 0; i < item.arg_count; i++ )
				collect_ternaries ( b.args [ static_cast < size_t > ( item.first_arg + i ) ], found, false );

			return;

		case op::CONST:
		case op::REAL:
		case op::VAR:
		case op::GLOBAL:
		case op::TEXT:
			return;

		default:
			collect_ternaries ( item.a, found, false );
			collect_ternaries ( item.b, found, false );
			return;

	}

}

bool decompiler::emitter::hoist ( int32_t index, uint64_t address ) {

	base::list < int32_t > found;

	collect_ternaries ( index, found, false );

	for ( int32_t chosen : found ) {

		const expr item = b.exprs [ static_cast < size_t > ( chosen ) ];
		int32_t number = next_temp++;

		hoist ( item.a, address );
		out.begin ( address );
		type_name ( item.size, item.bits, false );
		name_after_type ( );
		out.put ( base::writer ( ).add ( L"t" ).number ( number ).text ( ), syntax::LOCAL );
		out.put ( L";" );
		out.finish ( );
		out.begin ( address );
		branch_open ( item.a, address );
		branch_assign ( -1, item.b, address, number );
		branch_else ( address );
		branch_assign ( -1, item.c, address, number );
		branch_close ( );
		out.finish ( );
		hoisted.push ( chosen );
		hoisted_temp.push ( number );

	}

	return !found.empty ( );

}

void decompiler::emitter::assign_target ( int32_t left ) {

	if ( b.exprs [ static_cast < size_t > ( left ) ].kind == op::LOAD )
		lvalue_or_load ( b.exprs [ static_cast < size_t > ( left ) ], 0 );
	else
		value ( left, 0 );

}

void decompiler::emitter::branch_open ( int32_t test, uint64_t address ) {

	out.put ( L"if", syntax::CONTROL );
	paren_open ( );
	value ( test, 0 );
	paren_close ( );
	out.finish ( );
	out.indent++;
	out.begin ( address );

}

void decompiler::emitter::branch_else ( uint64_t address ) {

	out.finish ( );
	out.indent--;
	out.begin ( 0 );
	out.put ( L"else", syntax::CONTROL );
	out.finish ( );
	out.indent++;
	out.begin ( address );

}

void decompiler::emitter::branch_close ( ) {

	out.finish ( );
	out.indent--;

}

bool decompiler::emitter::split_ternary ( int32_t chosen, int32_t& test, int32_t& yes, int32_t& no ) {

	const expr& pick = b.exprs [ static_cast < size_t > ( chosen ) ];

	if ( pick.kind == op::TERNARY ) {

		test = pick.a;
		yes = pick.b;
		no = pick.c;

		return true;

	}

	if ( pick.kind != op::CAST || pick.a < 0 || b.exprs [ static_cast < size_t > ( pick.a ) ].kind != op::TERNARY )
		return false;

	uint8_t size = pick.size;
	uint8_t bits = pick.bits;
	const expr& inner = b.exprs [ static_cast < size_t > ( pick.a ) ];
	int32_t inner_yes = inner.b;
	int32_t inner_no = inner.c;

	test = inner.a;
	yes = b.cast ( inner_yes, size, bits );
	no = b.cast ( inner_no, size, bits );

	return true;

}

void decompiler::emitter::branch_assign ( int32_t left, int32_t chosen, uint64_t address, int32_t temp ) {

	int32_t test = -1;
	int32_t yes = -1;
	int32_t no = -1;

	if ( split_ternary ( chosen, test, yes, no ) ) {

		if ( hoist ( test, address ) )
			out.begin ( address );

		branch_open ( test, address );
		branch_assign ( left, yes, address, temp );
		branch_else ( address );
		branch_assign ( left, no, address, temp );
		branch_close ( );

		return;

	}

	bool lifted = hoist ( chosen, address );

	if ( temp <= 0 )
		lifted = hoist ( left, address ) || lifted;

	if ( lifted )
		out.begin ( address );

	const expr& pick = b.exprs [ static_cast < size_t > ( chosen ) ];
	int32_t size = temp > 0 ? 0 : b.exprs [ static_cast < size_t > ( left ) ].size;
	bool float_constant = temp <= 0 && pick.kind == op::CONST && !( pick.bits & POINTER ) && ( size == 4 || size == 8 ) && float_target ( left );
	int64_t constant = pick.value;

	if ( temp > 0 )
		out.put ( base::writer ( ).add ( L"t" ).number ( temp ).text ( ), syntax::LOCAL );
	else
		assign_target ( left );

	out.put ( L" = ", syntax::OPERATOR );

	if ( float_constant )
		real ( constant, size == 4 ? 4 : 8 );
	else
		value ( chosen, 2 );

	out.put ( L";" );

}

void decompiler::emitter::branch_return ( int32_t chosen, uint64_t address ) {

	int32_t test = -1;
	int32_t yes = -1;
	int32_t no = -1;

	if ( split_ternary ( chosen, test, yes, no ) ) {

		if ( hoist ( test, address ) )
			out.begin ( address );

		branch_open ( test, address );
		branch_return ( yes, address );
		branch_else ( address );
		branch_return ( no, address );
		branch_close ( );

		return;

	}

	if ( hoist ( chosen, address ) )
		out.begin ( address );

	out.put ( L"return", syntax::CONTROL );
	out.put ( L" " );
	value ( chosen, 0 );
	out.put ( L";" );

}

void decompiler::emitter::semicolon ( ) {

	if ( !inline_mode )
		out.put ( L";" );

}

int32_t decompiler::emitter::statement_index ( const statement& item ) const {

	return static_cast < int32_t > ( &item - b.statements.data ( ) );

}

void decompiler::emitter::inline_statement ( int32_t index ) {

	bool keep_ternary = no_ternary;

	inline_mode = true;
	no_ternary = false;
	statement_line ( b.statements [ static_cast < size_t > ( index ) ] );
	inline_mode = false;
	no_ternary = keep_ternary;

}

void decompiler::emitter::statement_line ( const statement& item ) {

	if ( item.kind == stmt::NOP )
		return;

	int32_t index = statement_index ( item );

	if ( !inline_mode && index >= 0 && index < hidden.count ( ) && hidden [ static_cast < size_t > ( index ) ] )
		return;

	if ( dry )
		return;

	untruncate = store_untruncate && ( item.kind == stmt::STORE || item.kind == stmt::ASSIGN ) && item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( item.left ) ].size == 4 && !( b.exprs [ static_cast < size_t > ( item.left ) ].bits & FLOATING );
	active_var = -1;

	if ( !inline_mode && index >= 0 && index < fold_at.count ( ) && fold_at [ static_cast < size_t > ( index ) ] >= 0 ) {

		active_var = fold_at [ static_cast < size_t > ( index ) ];
		active_how = fold_how [ static_cast < size_t > ( index ) ];
		active_slot = index;
		active_cond = false;
		active_unit = fold_unit [ static_cast < size_t > ( index ) ];

	}

	if ( !inline_mode ) {

		hoisted.clear ( );
		hoisted_temp.clear ( );
		out.begin ( item.address );

		if ( no_ternary && item.kind == stmt::EXPR && hoist ( item.right, item.address ) )
			out.begin ( item.address );

	}

	switch ( item.kind ) {

		case stmt::ASSIGN:
		case stmt::STORE: {

			const expr left = b.exprs [ static_cast < size_t > ( item.left ) ];
			const expr right = b.exprs [ static_cast < size_t > ( item.right ) ];

			if ( raw_float_copies && literal_floats && left.kind == op::LOAD && right.kind == op::GLOBAL && ( right.bits & FLOATING ) && ( right.size == 4 || right.size == 8 ) && left.size == right.size ) {

				int32_t section = b.file->section_at ( static_cast < uint64_t > ( right.value ) );
				uint64_t offset = 0;

				if ( section >= 0 && section < b.file->sections.count ( ) && !b.file->sections [ static_cast < size_t > ( section ) ].write && !b.file->sections [ static_cast < size_t > ( section ) ].execute && b.file->offset_of ( static_cast < uint64_t > ( right.value ), offset ) && offset + right.size <= b.file->size ) {

					int64_t raw = right.size == 4 ? static_cast < int64_t > ( b.file->read32 ( offset ) ) : static_cast < int64_t > ( b.file->read64 ( offset ) );
					expr plain_left = left;

					plain_left.bits = static_cast < uint8_t > ( plain_left.bits & ~FLOATING );
					lvalue_or_load ( plain_left, 0 );
					out.put ( L" = ", syntax::OPERATOR );
					number ( raw, right.size, !simple_constant ( raw ), false );
					semicolon ( );

					break;

				}

			}

			if ( right.kind == op::ADD && right.a >= 0 && left.kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST && !( right.bits & FLOATING ) ) {

				int64_t step_value = b.exprs [ static_cast < size_t > ( right.b ) ].value;
				int32_t scale = pointer_step ( item.left );

				if ( scale > 1 && step_value % scale == 0 ) {

					step_value /= scale;

					if ( step_value != 1 && step_value != -1 ) {

						value ( item.left, 0 );
						out.put ( step_value < 0 ? L" -= " : L" += ", syntax::OPERATOR );
						out.put ( base::writer ( ).number ( step_value < 0 ? -step_value : step_value ).text ( ), syntax::NUMBER );
						semicolon ( );

						break;

					}

				}

				if ( step_value == 1 || step_value == -1 ) {

					out.put ( step_value == 1 ? L"++" : L"--", syntax::OPERATOR );
					value ( item.left, 0 );

				} else {

					value ( item.left, 0 );
					out.put ( step_value < 0 ? L" -= " : L" += ", syntax::OPERATOR );
					number ( step_value < 0 ? -step_value : step_value, right.size, !simple_constant ( step_value ), false );

				}

				semicolon ( );

				break;

			}

			if ( widen_assign && item.kind == stmt::ASSIGN && left.kind == op::VAR && left.size == 8 && left.value >= 0 && left.value < b.vars.count ( ) && !( b.vars [ static_cast < size_t > ( left.value ) ].bits & ( FLOATING | POINTER ) ) && pointer_step ( item.left ) == 0 && right.size == 4 && !( right.bits & FLOATING ) && ( right.kind == op::ADD || right.kind == op::SUB || right.kind == op::MUL || right.kind == op::SHL || right.kind == op::XOR || right.kind == op::OR ) && ( left.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( left.value ) ].empty ( ) ) ) {

				value ( item.left, 0 );
				out.put ( L" = ", syntax::OPERATOR );
				out.put ( L"(unsigned int)", syntax::TYPE );
				out.put ( L"(" );
				untruncate = true;
				value ( item.right, 0 );
				untruncate = false;
				out.put ( L")" );
				semicolon ( );

				break;

			}

			if ( widened_halves && left.size == 8 && !( left.bits & FLOATING ) && right.kind == op::CAST && right.size == 4 && !( right.bits & ( SIGNED | FLOATING | POINTER ) ) && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( right.a ) ].bits & FLOATING ) && ( left.kind == op::LOAD || ( left.kind == op::VAR && left.value >= 0 && left.value < b.vars.count ( ) && !( b.vars [ static_cast < size_t > ( left.value ) ].bits & ( FLOATING | POINTER ) ) && ( left.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( left.value ) ].empty ( ) ) ) ) ) {

				if ( left.kind == op::LOAD )
					lvalue_or_load ( left, 0 );
				else
					value ( item.left, 0 );

				out.put ( L" = ", syntax::OPERATOR );
				out.put ( L"(unsigned int)", syntax::TYPE );
				value ( right.a, 14 );
				semicolon ( );

				break;

			}

			if ( simd_types && left.kind == op::VAR && right.kind == op::GLOBAL && right.size == 16 && left.value >= 0 && left.value < vector.count ( ) && vector [ static_cast < size_t > ( left.value ) ] == 2 ) {

				value ( item.left, 0 );
				out.put ( L" = ", syntax::OPERATOR );
				out.put ( L"_mm_load_si128", syntax::FUNCTION );
				out.put ( L"((" );
				out.put ( L"const __m128i", syntax::TYPE );
				out.put ( L" *)&" );
				value ( item.right, 15 );
				out.put ( L")" );
				semicolon ( );

				break;

			}

			if ( left.kind == op::VAR && right.kind == op::LOAD && right.size == 16 && left.value >= 0 && left.value < vector.count ( ) && ( ( vector [ static_cast < size_t > ( left.value ) ] == 1 && !plain_copies ) || vector [ static_cast < size_t > ( left.value ) ] == 2 ) ) {

				value ( item.left, 0 );
				out.put ( L" = ", syntax::OPERATOR );
				vector_load ( right.a, vector [ static_cast < size_t > ( left.value ) ] );
				semicolon ( );

				break;

			}

			if ( left.kind == op::LOAD && left.size == 16 && right.kind == op::VAR && right.value >= 0 && right.value < vector.count ( ) && vector [ static_cast < size_t > ( right.value ) ] && !( plain_copies && vector [ static_cast < size_t > ( right.value ) ] == 1 ) ) {

				vector_store = true;
				value ( item.left, 0 );
				vector_store = false;
				out.put ( L" = ", syntax::OPERATOR );
				value ( item.right, 2 );
				semicolon ( );

				break;

			}

			if ( left.kind == op::LOAD && right.kind == op::LOAD && left.size == right.size && ( left.bits & FLOATING ) && ( right.bits & FLOATING ) && ( left.size == 4 || left.size == 8 ) ) {

				expr plain_left = left;
				expr plain_right = right;

				plain_left.bits = static_cast < uint8_t > ( plain_left.bits & ~FLOATING );
				plain_right.bits = static_cast < uint8_t > ( plain_right.bits & ~FLOATING );
				lvalue_or_load ( plain_left, 0 );
				out.put ( L" = ", syntax::OPERATOR );
				lvalue_or_load ( plain_right, 2 );
				semicolon ( );

				break;

			}

			bool self_var = left.kind == op::VAR && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.a ) ].size == left.size;
			bool self_load = left.kind == op::LOAD && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( right.a ) ].size == left.size && b.same ( right.a, item.left );

			if ( ( self_var || self_load ) && right.b >= 0 && right.kind != op::CAST ) {

				const wchar_t* compound = nullptr;

				switch ( right.kind ) {

					case op::SUB: compound = L" -= "; break;
					case op::MUL: compound = L" *= "; break;
					case op::DIV: compound = L" /= "; break;
					case op::MOD: compound = L" %= "; break;
					case op::AND: compound = L" &= "; break;
					case op::OR: compound = L" |= "; break;
					case op::XOR: compound = L" ^= "; break;
					case op::SHL: compound = L" <<= "; break;
					case op::SHR: compound = L" >>= "; break;
					case op::ADD: compound = ( right.bits & FLOATING ) || b.exprs [ static_cast < size_t > ( right.b ) ].kind != op::CONST || self_load ? L" += " : nullptr; break;
					default: break;

				}

				const expr& amount = b.exprs [ static_cast < size_t > ( right.b ) ];

				if ( compound && self_load && right.kind == op::ADD && amount.kind == op::CONST && !( right.bits & FLOATING ) && ( amount.value == 1 || amount.value == -1 ) ) {

					out.put ( amount.value == 1 ? L"++" : L"--", syntax::OPERATOR );
					lvalue_or_load ( left, 14 );
					semicolon ( );

					break;

				}

				if ( compound && self_load && right.kind == op::ADD && amount.kind == op::CONST && amount.value < 0 && !( right.bits & FLOATING ) ) {

					lvalue_or_load ( left, 0 );
					out.put ( L" -= ", syntax::OPERATOR );
					number ( -amount.value, left.size, !simple_constant ( -amount.value ), false );
					semicolon ( );

					break;

				}

				if ( compound ) {

					if ( left.kind == op::LOAD )
						lvalue_or_load ( left, 0 );
					else
						value ( item.left, 0 );

					out.put ( compound, syntax::OPERATOR );

					int32_t operand_value = right.b;

					if ( implicit_narrow && left.size <= 4 && !( right.bits & FLOATING ) && right.kind != op::SHL && right.kind != op::SHR ) {

						while ( operand_value >= 0 && b.exprs [ static_cast < size_t > ( operand_value ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( operand_value ) ].a >= 0 && b.exprs [ static_cast < size_t > ( operand_value ) ].size == left.size && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( operand_value ) ].a ) ].size > left.size && !( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( operand_value ) ].a ) ].bits & FLOATING ) )
							operand_value = b.exprs [ static_cast < size_t > ( operand_value ) ].a;

					}

					value ( operand_value, 2 );
					semicolon ( );

					break;

				}

			}

			if ( !inline_mode && left.kind == op::VAR && right.kind == op::TERNARY && right.a >= 0 && right.b >= 0 && right.c >= 0 ) {

				const expr& yes_arm = b.exprs [ static_cast < size_t > ( strip ( right.b ) ) ];
				const expr& no_arm = b.exprs [ static_cast < size_t > ( strip ( right.c ) ) ];
				bool keep_no = no_arm.kind == op::VAR && no_arm.value == left.value;
				bool keep_yes = yes_arm.kind == op::VAR && yes_arm.value == left.value;

				if ( split_selects && !keep_no && !keep_yes && !b.same ( right.b, right.c ) ) {

					value ( item.left, 0 );
					out.put ( L" = ", syntax::OPERATOR );
					select_arm ( item.left, right.c );
					semicolon ( );
					out.finish ( );
					out.begin ( item.address );
					out.put ( L"if", syntax::CONTROL );
					paren_open ( );
					value ( right.a, 0 );
					paren_close ( );
					out.finish ( );
					out.indent++;
					out.begin ( item.address );
					out.indent--;
					value ( item.left, 0 );
					out.put ( L" = ", syntax::OPERATOR );
					select_arm ( item.left, right.b );
					semicolon ( );

					break;

				}

				if ( keep_no != keep_yes ) {

					out.put ( L"if", syntax::CONTROL );
					paren_open ( );
					value ( keep_no ? right.a : b.negate ( right.a ), 0 );
					paren_close ( );
					out.finish ( );
					out.indent++;
					out.begin ( item.address );
					out.indent--;
					value ( item.left, 0 );
					out.put ( L" = ", syntax::OPERATOR );

					int32_t chosen = keep_no ? right.b : right.c;
					const expr& picked = b.exprs [ static_cast < size_t > ( chosen ) ];

					if ( picked.kind == op::CONST && !( picked.bits & POINTER ) && left.size == 8 && b.long_mode )
						number ( picked.value, 8, !simple_constant ( picked.value ), false );
					else
						value ( chosen, 2 );

					semicolon ( );

					break;

				}

			}

			if ( left.kind == op::VAR && right.kind == op::CAST && ( right.bits & POINTER ) && ( right.bits & FLOATING ) && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].size <= 8 ) {

				uint8_t width = b.exprs [ static_cast < size_t > ( right.a ) ].size;

				out.put ( width == 8 ? L"*(_QWORD *)&" : L"LODWORD(", width == 8 ? syntax::OPERATOR : syntax::FUNCTION );
				value ( item.left, 0 );

				if ( width != 8 )
					out.put ( L")" );

				out.put ( L" = ", syntax::OPERATOR );
				value ( right.a, 2 );
				semicolon ( );

				break;

			}

			if ( scalar_lanes && left.kind == op::VAR && right.size < 16 && ( right.bits & FLOATING ) && right.kind != op::LOAD && left.value >= 0 && left.value < vector.count ( ) && ( ( vector [ static_cast < size_t > ( left.value ) ] == 3 && right.size == 4 ) || ( vector [ static_cast < size_t > ( left.value ) ] == 4 && right.size == 8 ) ) ) {

				wchar_t lane_name [ 80 ];

				var_name ( static_cast < int32_t > ( left.value ), lane_name );
				out.put ( lane_name, syntax::LOCAL );
				out.put ( right.size == 8 ? L".m128d_f64[0]" : L".m128_f32[0]" );
				out.put ( L" = ", syntax::OPERATOR );
				value ( item.right, 2 );
				semicolon ( );

				break;

			}

			if ( low_write ( item.left, item.right,b.statements.count ( ) && &item >= &b.statements [ 0 ] && &item < &b.statements [ 0 ] + b.statements.count ( ) ? static_cast < int32_t > ( &item - &b.statements [ 0 ] ) : -1 ) ) {

				out.put ( L"LODWORD(", syntax::FUNCTION );
				value ( item.left, 0 );
				out.put ( L")" );
				out.put ( L" = ", syntax::OPERATOR );
				value ( right.kind == op::CAST ? right.a : item.right, 2 );
				semicolon ( );

				break;

			}

			int32_t test = -1;
			int32_t yes = -1;
			int32_t no = -1;

			if ( no_ternary && split_ternary ( item.right, test, yes, no ) ) {

				if ( hoist ( item.left, item.address ) )
					out.begin ( item.address );

				branch_assign ( item.left, item.right, item.address );

				break;

			}

			if ( no_ternary ) {

				bool lifted = hoist ( item.left, item.address );

				lifted = hoist ( item.right, item.address ) || lifted;

				if ( lifted )
					out.begin ( item.address );

			}

			if ( left.kind == op::LOAD && ( left.bits & FLOATING ) && right.kind == op::CONST && left.size == 16 ) {

				expr plain_left = left;

				plain_left.bits = static_cast < uint8_t > ( plain_left.bits & ~FLOATING );
				lvalue_or_load ( plain_left, 0 );

			} else if ( left.kind == op::LOAD )
				lvalue_or_load ( left, 0 );
			else
				value ( item.left, 0 );

			out.put ( L" = ", syntax::OPERATOR );

			if ( right.kind == op::CONST && !( right.bits & POINTER ) && ( left.size == 4 || left.size == 8 ) && ( left.kind == op::LOAD && integer_stores ? ( left.bits & FLOATING ) != 0 : float_target ( item.left ) ) )
				real ( right.value, left.size == 4 ? 4 : 8 );
			else if ( right.kind == op::LOAD && right.size < left.size && !( left.bits & FLOATING ) && !float_target ( item.left ) )
				extended_load ( right, 2, UNSIGNED );
			else if ( right.kind == op::CAST && right.a >= 0 && right.size == left.size && !( right.bits & ( FLOATING | POINTER ) ) && b.exprs [ static_cast < size_t > ( right.a ) ].size > right.size && !( b.exprs [ static_cast < size_t > ( right.a ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR )
				value ( right.a, 2 );
			else if ( right.kind == op::CONST && !( right.bits & POINTER ) && ( left.size == 8 || left.size == 16 ) && b.long_mode && !float_target ( item.left ) && !( left.kind == op::VAR && left.value == result_var && b.result_width ) ) {

				int64_t shown_value = right.size == 4 && right.value < 0 && left.kind == op::VAR ? static_cast < int64_t > ( static_cast < uint32_t > ( right.value ) ) : right.value;

				number ( shown_value, 8, !simple_constant ( shown_value ), false );

			}
			else if ( flow_casts && left.kind == op::VAR && pointer_step ( item.left ) > 0 && !typed_value ( item.right ) && right.kind != op::CAST && ( right.kind != op::CALL || ( char_calls && element [ static_cast < size_t > ( left.value ) ] == 0x10 && right.value ) ) ) {

				if ( right.kind == op::LOAD && right.size == 8 && !void_element_load ( item.right ) && element [ static_cast < size_t > ( left.value ) ] != 0x10 && !( ( element [ static_cast < size_t > ( left.value ) ] & 0x9F ) == 8 && ( element [ static_cast < size_t > ( left.value ) ] & 0x40 ) ) ) {

					out.put ( L"*", syntax::OPERATOR );
					pointer_cast ( static_cast < int32_t > ( left.value ), true );
					value ( right.a, 14 );

				} else {

					pointer_cast ( static_cast < int32_t > ( left.value ) );
					value ( item.right, 14 );

				}

			} else if ( void_stores && flow_casts && !no_casts && left.kind == op::LOAD && void_element_load ( item.left ) && right.kind == op::VAR && right.size == 8 && pointer_step ( item.right ) == 0 && !( right.bits & ( FLOATING | POINTER ) ) && right.value >= 0 && right.value < b.vars.count ( ) && !( b.vars [ static_cast < size_t > ( right.value ) ].bits & ( FLOATING | POINTER ) ) && !( right.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( right.value ) ].empty ( ) ) ) {

				out.put ( L"(void *)", syntax::TYPE );
				value ( item.right, 14 );

			} else if ( flow_casts && left.kind == op::VAR && left.size == 8 && pointer_step ( item.left ) == 0 && void_element_load ( item.right ) && !( left.bits & FLOATING ) && !( left.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( left.value ) ].empty ( ) ) ) {

				bool unsigned_target = left.value >= 0 && left.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( left.value ) ].bits & UNSIGNED );

				out.put ( unsigned_target ? L"(unsigned __int64)" : L"(__int64)", syntax::TYPE );
				value ( item.right, 14 );

			} else if ( flow_casts && ( left.kind == op::VAR || left.kind == op::GLOBAL ) && left.size == 8 && pointer_step ( item.left ) == 0 && typed_value ( item.right ) && !( left.bits & FLOATING ) ) {

				bool unsigned_target = left.kind == op::VAR && left.value >= 0 && left.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( left.value ) ].bits & UNSIGNED );

				out.put ( unsigned_target ? L"(unsigned __int64)" : L"(__int64)", syntax::TYPE );
				value ( item.right, 14 );

			} else if ( implicit_narrow && left.kind == op::VAR && left.size <= 4 && !( left.bits & FLOATING ) && ( right.kind == op::AND || right.kind == op::OR || right.kind == op::XOR || right.kind == op::ADD || right.kind == op::SUB ) && !( right.bits & FLOATING ) && right.size == left.size ) {

				auto loose = [ & ] ( int32_t e ) {

					if ( e < 0 )
						return e;

					const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

					if ( node.kind == op::CAST && node.a >= 0 && node.size < b.exprs [ static_cast < size_t > ( node.a ) ].size && !( node.bits & ( FLOATING | POINTER ) ) && !( b.exprs [ static_cast < size_t > ( node.a ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR )
						return node.a;

					return e;

				};

				expr copy = right;

				copy.a = loose ( right.a );
				copy.b = loose ( right.b );

				if ( copy.a != right.a || copy.b != right.b ) {

					b.exprs.push ( copy );
					value ( b.exprs.count ( ) - 1, 2 );

				} else
					value ( item.right, 2 );

			} else
				value ( item.right, 2 );

			semicolon ( );

			break;

		}

		case stmt::EXPR:
			call_unused = item.right >= 0 && b.exprs [ static_cast < size_t > ( item.right ) ].kind == op::CALL;
			value ( item.right, 0 );
			call_unused = false;
			semicolon ( );

			if ( item.right >= 0 && no_return ( b.exprs [ static_cast < size_t > ( item.right ) ] ) ) {

				if ( !inline_mode )
					out.finish ( );

				ended = true;

				return;

			}

			break;

		case stmt::ASM:
			out.put ( L"__asm", syntax::KEYWORD );
			out.put ( L" { " );
			out.put ( b.asm_text [ static_cast < size_t > ( item.left ) ].get ( ), syntax::COMMENT );
			out.put ( L" }" );
			break;

		default:
			break;

	}

	if ( inline_mode )
		return;

	out.finish ( );
	hoisted.clear ( );
	hoisted_temp.clear ( );

	const expr& target = b.exprs [ static_cast < size_t > ( item.left >= 0 ? item.left : 0 ) ];

	if ( item.kind == stmt::ASSIGN && item.left >= 0 && target.kind == op::VAR ) {

		last_assign = index;
		last_assign_line = out.r->lines.count ( ) - 1;
		recent_assign.push ( index );
		recent_line.push ( last_assign_line );

	}

}

void decompiler::emitter::emit_line ( uint64_t address, const wchar_t* text, syntax kind ) {

	if ( dry )
		return;

	out.begin ( address );
	out.put ( text, kind );
	out.finish ( );

}

void decompiler::emitter::label ( int32_t block ) {

	if ( dry || !wanted_label [ static_cast < size_t > ( block ) ] || labeled [ static_cast < size_t > ( block ) ] )
		return;

	labeled [ static_cast < size_t > ( block ) ] = 1;

	int32_t saved = out.indent;

	out.indent = 0;
	out.begin ( b.graph.blocks [ static_cast < size_t > ( block ) ].start );
	out.put ( base::writer ( ).add ( L"LABEL_" ).number ( block + 1 ).text ( ), syntax::LABEL );
	out.put ( L":" );
	out.finish ( );
	out.indent = saved;

}

void decompiler::emitter::go_to ( int32_t block, uint64_t address ) {

	if ( inline_return ( block, address ) )
		return;

	wanted_label [ static_cast < size_t > ( block ) ] = 1;
	gotos++;
	ended = true;

	if ( dry || dead_exit ( ) )
		return;

	out.begin ( address );
	out.put ( L"goto", syntax::CONTROL );
	out.put ( L" " );
	out.put ( base::writer ( ).add ( L"LABEL_" ).number ( block + 1 ).text ( ), syntax::LABEL );
	out.put ( L";" );
	out.finish ( );
	out.terminal = out.indent;

}

bool decompiler::emitter::inline_return ( int32_t block, uint64_t address ) {

	const block_code& target = b.code [ static_cast < size_t > ( block ) ];

	if ( inline_returns && target.end == term::RETURN && empty_block ( block ) && ( target.returned < 0 || b.exprs [ static_cast < size_t > ( target.returned ) ].kind == op::CONST || b.exprs [ static_cast < size_t > ( target.returned ) ].kind == op::VAR ) ) {

		return_line ( target );

		return true;

	}

	const block_code* ending = &target;

	if ( target.end == term::FALL && target.taken >= 0 && b.code [ static_cast < size_t > ( target.taken ) ].end == term::RETURN && empty_block ( target.taken ) )
		ending = &b.code [ static_cast < size_t > ( target.taken ) ];

	if ( inline_returns && ending->end == term::RETURN && ending->returned >= 0 && b.returns_value && b.exprs [ static_cast < size_t > ( ending->returned ) ].kind == op::VAR ) {

		int32_t only = -1;
		int32_t live = 0;

		for ( int32_t s = target.first; s < target.first + target.count; s++ ) {

			if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP ) {

				only = s;
				live++;

			}

		}

		if ( live == 1 ) {

			const statement& line = b.statements [ static_cast < size_t > ( only ) ];

			if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == b.exprs [ static_cast < size_t > ( ending->returned ) ].value ) {

				const expr& source = b.exprs [ static_cast < size_t > ( line.right ) ];

				if ( source.kind == op::VAR || source.kind == op::CONST ) {

					ended = true;

					if ( dry || dead_exit ( ) )
						return true;

					out.begin ( address );
					out.put ( L"return", syntax::CONTROL );
					out.put ( L" " );

					if ( source.kind == op::CONST && b.long_mode && !b.result_width )
						number ( source.value, 8, !simple_constant ( source.value ), false );
					else
						value ( line.right, 0 );

					out.put ( L";" );
					out.finish ( );
					out.terminal = out.indent;

					return true;

				}

			}

		}

	}

	return false;

}

void decompiler::emitter::body ( int32_t block ) {

	ended = false;
	label ( block );
	emitted [ static_cast < size_t > ( block ) ] = 1;

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	for ( int32_t i = 0; i < item.count; i++ )
		statement_line ( b.statements [ static_cast < size_t > ( item.first + i ) ] );

}

bool decompiler::emitter::empty_block ( int32_t block ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	for ( int32_t i = 0; i < item.count; i++ ) {

		if ( b.statements [ static_cast < size_t > ( item.first + i ) ].kind != stmt::NOP )
			return false;

	}

	return true;

}

void decompiler::emitter::open_brace ( uint64_t address, const wchar_t* keyword, int32_t condition, bool negated ) {

	if ( dry ) {

		out.indent++;

		return;

	}

	int32_t shown = condition >= 0 && negated ? b.negate ( condition ) : condition;

	untruncate = false;
	hoisted.clear ( );
	hoisted_temp.clear ( );

	if ( no_ternary && shown >= 0 && keyword [ 0 ] == L'i' && keyword [ 1 ] == L'f' && !keyword [ 2 ] )
		hoist ( shown, address );

	out.begin ( address );
	out.put ( keyword, syntax::CONTROL );

	if ( condition >= 0 ) {

		const expr& test = b.exprs [ static_cast < size_t > ( condition ) ];

		paren_open ( );

		if ( test.kind == op::CONST && !negated )
			out.put ( test.value ? L"1" : L"0", syntax::NUMBER );
		else {

			active_var = -1;

			if ( keyword [ 0 ] == L'i' || keyword [ 0 ] == L'e' ) {

				for ( int32_t k = 0; k < fold_cond.count ( ); k++ ) {

					if ( fold_cond [ static_cast < size_t > ( k ) ] == condition ) {

						active_var = fold_cond_var [ static_cast < size_t > ( k ) ];
						active_how = fold_cond_how [ static_cast < size_t > ( k ) ];
						active_slot = k;
						active_cond = true;
						active_unit = fold_cond_unit [ static_cast < size_t > ( k ) ];

					}

				}

			}

			truth = bit_tests;
			value ( shown, 0 );
			active_var = -1;

		}

		paren_close ( );

	}

	out.finish ( );
	brace ( L"{" );
	out.indent++;
	hoisted.clear ( );
	hoisted_temp.clear ( );

}

void decompiler::emitter::close_brace ( ) {

	ended = false;
	out.indent--;

	if ( dry )
		return;

	brace ( L"}" );

}

void decompiler::emitter::brace ( const wchar_t* text ) {

	if ( dry )
		return;

	out.begin ( 0 );
	out.put ( text );
	out.finish ( );

}

void decompiler::emitter::paren_open ( ) {

	bool keep = out.spaced;

	out.spaced = false;
	out.put ( L" ( " );
	out.spaced = keep;

}

void decompiler::emitter::paren_close ( ) {

	bool keep = out.spaced;

	out.spaced = false;
	out.put ( L" )" );
	out.spaced = keep;

}

void decompiler::emitter::else_line ( ) {

	if ( dry )
		return;

	out.begin ( 0 );
	out.put ( L"else", syntax::CONTROL );
	out.finish ( );

}

void decompiler::emitter::while_tail ( uint64_t address, int32_t condition ) {

	if ( dry )
		return;

	out.begin ( address );
	out.put ( L"while", syntax::CONTROL );
	paren_open ( );
	untruncate = false;
	truth = bit_tests;
	value ( condition, 0 );
	paren_close ( );
	out.put ( L";" );
	out.finish ( );

}

bool decompiler::emitter::dead_exit ( ) const {

	return out.terminal >= 0 && out.terminal == out.indent;

}

void decompiler::emitter::return_line ( const block_code& item ) {

	ended = true;

	if ( dry || dead_exit ( ) )
		return;

	int32_t test = -1;
	int32_t yes = -1;
	int32_t no = -1;
	bool shown = item.returned >= 0 && ( b.returns_value || item.end == term::TAIL );

	untruncate = false;

	hoisted.clear ( );
	hoisted_temp.clear ( );
	out.begin ( item.last );

	if ( no_ternary && shown && b.returns_value && split_ternary ( item.returned, test, yes, no ) ) {

		branch_return ( item.returned, item.last );
		out.finish ( );
		out.terminal = out.indent;
		hoisted.clear ( );
		hoisted_temp.clear ( );

		return;

	}

	if ( no_ternary && shown && hoist ( item.returned, item.last ) )
		out.begin ( item.last );

	if ( item.end == term::TAIL && !b.returns_value && item.returned >= 0 ) {

		value ( item.returned, 0 );
		out.put ( L";" );
		out.finish ( );
		hoisted.clear ( );
		hoisted_temp.clear ( );

		return;

	}

	out.put ( L"return", syntax::CONTROL );

	if ( shown ) {

		const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];

		out.put ( L" " );

		if ( back.kind == op::CONST && b.returns_float && !b.has_prototype && ( back.value == 0 || back.size == 4 || back.size == 8 ) )
			real ( back.value, b.return_size == 4 ? 4 : 8 );
		else if ( back.kind == op::CONST && !( back.bits & POINTER ) && !b.has_prototype && !b.returns_float && b.long_mode ) {

			bool narrow = b.result_width == 1 || b.result_width == 2 || ( !b.result_text.empty ( ) && b.return_size <= 4 );

			if ( narrow && !b.result_width && !b.result_text.empty ( ) ) {

				c_type declared;
				wchar_t clean [ 64 ];

				if ( parse_type ( b.result_text.get ( ), b.long_mode, declared, clean, 64 ) && declared.size == 8 )
					narrow = false;

			}

			int64_t shown_value = back.value;

			if ( b.result_width == 1 || b.result_width == 2 )
				shown_value = static_cast < int8_t > ( back.value );
			else if ( !narrow && back.size == 4 && back.value < 0 )
				shown_value = static_cast < int64_t > ( static_cast < uint32_t > ( back.value ) );

			number ( shown_value, narrow ? 4 : 8, !simple_constant ( shown_value ), false );

		} else {

			const expr& top = b.exprs [ static_cast < size_t > ( item.returned ) ];
			bool keep = explicit_zero;
			int32_t inner = top.kind == op::CAST ? top.a : -1;
			bool widened = return_casts && b.long_mode && !b.has_prototype && !b.returns_float && b.result_width == 0 && b.result_text.empty ( ) && top.kind == op::CAST && top.size == 8 && inner >= 0 && b.exprs [ static_cast < size_t > ( inner ) ].size == 4 && !( top.bits & ( SIGNED | FLOATING | POINTER ) ) && !( b.exprs [ static_cast < size_t > ( inner ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( inner ) ].kind != op::CONST && !( b.exprs [ static_cast < size_t > ( inner ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( inner ) ].value >= 0 && b.exprs [ static_cast < size_t > ( inner ) ].value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( inner ) ].value ) ].bits & UNSIGNED ) );

			explicit_zero = ( b.result_width == 1 || b.result_width == 2 ) && ( top.kind == op::EQ || top.kind == op::NE );

			if ( !widened && return_casts && b.long_mode && !b.has_prototype && !b.returns_float && b.result_width == 0 && b.result_text.empty ( ) && top.size == 4 && ( top.kind == op::ADD || top.kind == op::SUB || top.kind == op::MUL || top.kind == op::SHL || top.kind == op::AND || top.kind == op::OR || top.kind == op::XOR || ( return_var_casts && top.kind == op::VAR && top.value >= 0 && top.value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( top.value ) ].size == 4 && !( b.vars [ static_cast < size_t > ( top.value ) ].bits & ( UNSIGNED | FLOATING ) ) && !( top.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( top.value ) ].empty ( ) ) ) ) && !( top.bits & ( FLOATING | POINTER ) ) ) {

				widened = true;
				inner = item.returned;

			}

			if ( widened ) {

				out.put ( L"(unsigned int)", syntax::TYPE );
				value ( inner, 14 );

			} else
				value ( item.returned, 0 );

			explicit_zero = keep;

		}

	}

	out.put ( L";" );
	out.finish ( );
	out.terminal = out.indent;
	hoisted.clear ( );
	hoisted_temp.clear ( );

}

void decompiler::emitter::jump_line ( const wchar_t* keyword, uint64_t address ) {

	ended = true;

	if ( dry || dead_exit ( ) )
		return;

	out.begin ( address );
	out.put ( keyword, syntax::CONTROL );
	out.put ( L";" );
	out.finish ( );
	out.terminal = out.indent;

}

bool decompiler::emitter::in_loop ( int32_t loop, int32_t block ) {

	return loop >= 0 && loop_body [ static_cast < size_t > ( loop ) ] [ static_cast < size_t > ( block ) ];

}

bool decompiler::emitter::special ( int32_t target, int32_t stop, const frame& context, uint64_t address ) {

	if ( target == context.loop && context.loop >= 0 ) {

		if ( stop != target )
			jump_line ( L"continue", address );

		return true;

	}

	if ( target == context.exit && context.exit >= 0 ) {

		if ( stop != target )
			jump_line ( L"break", address );

		return true;

	}

	if ( target == context.latch && context.latch >= 0 ) {

		if ( stop != target )
			jump_line ( L"continue", address );

		return true;

	}

	if ( target == context.outer && context.outer >= 0 ) {

		if ( stop != target )
			go_to ( target, address );

		return true;

	}

	return false;

}

void decompiler::emitter::region ( int32_t block, int32_t stop, frame context ) {

	if ( ++depth > 400 ) {

		depth--;

		return;

	}

	while ( block >= 0 && block != stop ) {

		if ( emitted [ static_cast < size_t > ( block ) ] ) {

			if ( block == context.latch && context.latch >= 0 )
				jump_line ( L"continue", 0 );
			else
				go_to ( block, 0 );

			break;

		}

		if ( header [ static_cast < size_t > ( block ) ] && context.loop != block ) {

			block = loop ( block, context );

			continue;

		}

		body ( block );

		const block_code& item = b.code [ static_cast < size_t > ( block ) ];

		switch ( item.end ) {

			case term::RETURN:
			case term::TAIL:
				return_line ( item );
				block = -1;
				break;

			case term::STOP:
				block = -1;
				break;

			case term::FALL:

				if ( special ( item.taken, stop, context, item.last ) )
					block = -1;
				else if ( fall_returns && item.taken >= 0 && item.taken != stop && pred [ static_cast < size_t > ( item.taken ) ].count ( ) > 1 && b.code [ static_cast < size_t > ( item.taken ) ].end != term::RETURN && inline_return ( item.taken, item.last ) )
					block = -1;
				else
					block = item.taken;

				break;

			case term::BRANCH:
				block = branch ( block, stop, context, false );
				break;

			case term::SWITCH:
				block = choice ( block, stop, context );
				break;

			default:
				block = -1;
				break;

		}

	}

	depth--;

}

int32_t decompiler::emitter::follow_of ( int32_t block, int32_t stop, const frame& context ) {

	int32_t follow = ipdom [ static_cast < size_t > ( block ) ];

	if ( follow == count )
		follow = -1;

	if ( context.loop >= 0 && follow >= 0 && !in_loop ( context.loop, follow ) )
		follow = exits_inline && stop == context.loop ? context.loop : context.exit;

	if ( follow < 0 )
		follow = stop;

	return follow;

}

bool decompiler::emitter::silent_return ( int32_t block ) {

	if ( block < 0 || block >= count )
		return false;

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	return item.end == term::RETURN && !b.returns_value && empty_block ( block ) && pred [ static_cast < size_t > ( block ) ].count ( ) == 1 && !emitted [ static_cast < size_t > ( block ) ];

}

decompiler::op decompiler::emitter::swapped ( op kind ) {

	switch ( kind ) {

		case op::LT: return op::GT;
		case op::GT: return op::LT;
		case op::LE: return op::GE;
		case op::GE: return op::LE;
		default: return kind;

	}

}

bool decompiler::emitter::equivalent ( int32_t guard, int32_t test, int32_t var, int32_t seed, int32_t depth ) {

	if ( guard < 0 || test < 0 || depth > 24 )
		return guard == test;

	while ( b.exprs [ static_cast < size_t > ( guard ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( test ) ].kind != op::CAST )
		guard = b.exprs [ static_cast < size_t > ( guard ) ].a;

	const expr& g = b.exprs [ static_cast < size_t > ( guard ) ];
	const expr& t = b.exprs [ static_cast < size_t > ( test ) ];

	if ( var >= 0 && t.kind == op::VAR && t.value == var ) {

		if ( b.same ( guard, seed ) )
			return true;

		const expr& s = b.exprs [ static_cast < size_t > ( seed ) ];

		return g.kind == op::CONST && s.kind == op::CONST && g.value == s.value;

	}

	if ( b.same ( guard, test ) )
		return true;

	bool compare = t.kind == op::LT || t.kind == op::LE || t.kind == op::GT || t.kind == op::GE || t.kind == op::EQ || t.kind == op::NE;

	if ( g.kind == t.kind && g.value == t.value && g.arg_count == 0 && t.arg_count == 0 && g.kind != op::CALL && g.kind != op::INTRINSIC && g.kind != op::CONST && g.kind != op::VAR ) {

		if ( equivalent ( g.a, t.a, var, seed, depth + 1 ) && equivalent ( g.b, t.b, var, seed, depth + 1 ) && ( g.c < 0 && t.c < 0 ) )
			return true;

	}

	if ( compare && g.kind == swapped ( t.kind ) && ( g.bits & ( SIGNED | UNSIGNED ) ) == ( t.bits & ( SIGNED | UNSIGNED ) ) )
		return equivalent ( g.a, t.b, var, seed, depth + 1 ) && equivalent ( g.b, t.a, var, seed, depth + 1 );

	return false;

}

int32_t decompiler::emitter::sinkable_step ( int32_t latch, int32_t test ) {

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
	int32_t last = -1;

	for ( int32_t s = tail.first; s < tail.first + tail.count; s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
			last = s;

	}

	for ( int32_t s = tail.first; s < tail.first + tail.count; s++ ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || s == last )
			continue;

		const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];
		int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value );

		if ( !( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == var && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST ) )
			continue;

		if ( !b.references ( test, var, -1 ) )
			continue;

		bool used = false;
		bool only_steps = true;

		for ( int32_t k = s + 1; k < tail.first + tail.count && !used; k++ ) {

			const statement& later = b.statements [ static_cast < size_t > ( k ) ];

			if ( later.kind == stmt::NOP || later.kind == stmt::ASM ) {

				used = used || later.kind == stmt::ASM;

				continue;

			}

			used = b.references ( later.right, var, -1 ) || ( later.left >= 0 && b.references ( later.left, var, -1 ) );

			bool step = later.kind == stmt::ASSIGN && later.left >= 0 && later.right >= 0 && b.exprs [ static_cast < size_t > ( later.left ) ].kind == op::VAR;

			if ( step ) {

				const expr& next = b.exprs [ static_cast < size_t > ( later.right ) ];

				step = ( next.kind == op::ADD || next.kind == op::SUB ) && next.a >= 0 && b.exprs [ static_cast < size_t > ( next.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( next.a ) ].value == b.exprs [ static_cast < size_t > ( later.left ) ].value && next.b >= 0 && b.exprs [ static_cast < size_t > ( next.b ) ].kind == op::CONST;

			}

			only_steps = only_steps && step;

		}

		if ( keep_step_order && only_steps )
			continue;

		if ( !used )
			return s;

	}

	return -1;

}

int32_t decompiler::emitter::single_assign ( int32_t block, int32_t follow ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];

	if ( item.end != term::FALL || item.taken != follow || pred [ static_cast < size_t > ( block ) ].count ( ) != 1 || wanted_label [ static_cast < size_t > ( block ) ] || header [ static_cast < size_t > ( block ) ] || emitted [ static_cast < size_t > ( block ) ] )
		return -1;

	int32_t found = -1;

	for ( int32_t s = item.first; s < item.first + item.count; s++ ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP )
			continue;

		if ( found >= 0 || line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || ( s < hidden.count ( ) && hidden [ static_cast < size_t > ( s ) ] ) )
			return -1;

		found = s;

	}

	return found;

}

int32_t decompiler::emitter::counted_step ( int32_t latch, int32_t test ) {

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

	for ( int32_t s = tail.first + tail.count - 1; s >= tail.first; s-- ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP )
			continue;

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
			return -1;

		const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];
		int64_t var = b.exprs [ static_cast < size_t > ( line.left ) ].value;

		if ( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == var && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST && b.references ( test, static_cast < int32_t > ( var ), -1 ) )
			return s;

		return -1;

	}

	return -1;

}

int32_t decompiler::emitter::strip ( int32_t e ) const {

	while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
		e = b.exprs [ static_cast < size_t > ( e ) ].a;

	return e;

}

bool decompiler::emitter::guarded_entry ( int32_t start, int64_t init, op kind, int32_t bound ) {

	if ( !guarded_loops || start < 0 )
		return false;

	int32_t pre = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

		if ( loop_body [ static_cast < size_t > ( start ) ] [ static_cast < size_t > ( p ) ] )
			continue;

		pre = pre < 0 ? p : -2;

	}

	if ( pre < 0 )
		return false;

	int32_t gate = pre;
	int32_t into = start;

	if ( b.code [ static_cast < size_t > ( pre ) ].end != term::BRANCH ) {

		if ( pred [ static_cast < size_t > ( pre ) ].count ( ) != 1 )
			return false;

		gate = pred [ static_cast < size_t > ( pre ) ] [ 0 ];
		into = pre;

	}

	const block_code& g = b.code [ static_cast < size_t > ( gate ) ];

	if ( g.end != term::BRANCH || g.condition < 0 || ( g.taken != into && g.other != into ) || g.taken == g.other )
		return false;

	int32_t condition = g.taken == into ? g.condition : b.negate ( g.condition );
	const expr& c = b.exprs [ static_cast < size_t > ( condition ) ];

	if ( c.a < 0 || c.b < 0 )
		return false;

	int32_t target = strip ( bound );

	if ( target < 0 || b.exprs [ static_cast < size_t > ( target ) ].kind != op::VAR )
		return false;

	for ( int32_t s = b.code [ static_cast < size_t > ( pre ) ].first; s < b.code [ static_cast < size_t > ( pre ) ].first + b.code [ static_cast < size_t > ( pre ) ].count && pre != gate; s++ ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && target >= 0 && b.exprs [ static_cast < size_t > ( target ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == b.exprs [ static_cast < size_t > ( target ) ].value && b.exprs [ static_cast < size_t > ( strip ( line.right ) ) ].kind == op::VAR )
			target = strip ( line.right );

	}

	int32_t left = strip ( c.a );
	int32_t right = strip ( c.b );
	op relation = c.kind;
	int64_t k = 0;

	if ( right >= 0 && b.exprs [ static_cast < size_t > ( right ) ].kind == op::CONST && left >= 0 && ( b.same ( left, target ) || b.same ( left, strip ( bound ) ) ) )
		k = b.exprs [ static_cast < size_t > ( right ) ].value;
	else if ( left >= 0 && b.exprs [ static_cast < size_t > ( left ) ].kind == op::CONST && right >= 0 && ( b.same ( right, target ) || b.same ( right, strip ( bound ) ) ) ) {

		k = b.exprs [ static_cast < size_t > ( left ) ].value;

		switch ( relation ) {

			case op::LT: relation = op::GT; break;
			case op::LE: relation = op::GE; break;
			case op::GT: relation = op::LT; break;
			case op::GE: relation = op::LE; break;
			default: break;

		}

	} else
		return false;

	bool above = ( relation == op::GT && k >= init ) || ( relation == op::GE && k > init );

	switch ( kind ) {

		case op::NE:
			return above || ( relation == op::NE && k == init ) || ( relation == op::LT && k <= init );

		case op::LT:
			return above;

		case op::LE:
			return above || ( relation == op::GE && k >= init );

		default:
			return false;

	}

}

bool decompiler::emitter::entry_true ( int32_t test, int32_t step, int32_t start ) {

	const statement& grow = b.statements [ static_cast < size_t > ( step ) ];
	int32_t found = -1;
	int32_t line = -1;

	if ( !find_init ( b.exprs [ static_cast < size_t > ( grow.left ) ].value, found, line ) )
		return false;

	const statement& seed = b.statements [ static_cast < size_t > ( found ) ];

	if ( seed.left < 0 || seed.right < 0 || b.exprs [ static_cast < size_t > ( seed.left ) ].value != b.exprs [ static_cast < size_t > ( grow.left ) ].value || b.exprs [ static_cast < size_t > ( seed.right ) ].kind != op::CONST )
		return false;

	const expr& t = b.exprs [ static_cast < size_t > ( test ) ];

	if ( t.a < 0 || t.b < 0 )
		return false;

	int32_t subject = strip ( t.a );
	int32_t other_side = t.b;
	op test_kind = t.kind;

	if ( b.exprs [ static_cast < size_t > ( subject ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( subject ) ].value != b.exprs [ static_cast < size_t > ( seed.left ) ].value ) {

		int32_t flipped = strip ( t.b );

		if ( flipped < 0 || b.exprs [ static_cast < size_t > ( flipped ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( flipped ) ].value != b.exprs [ static_cast < size_t > ( seed.left ) ].value )
			return false;

		subject = flipped;
		other_side = t.a;

		switch ( test_kind ) {

			case op::LT: test_kind = op::GT; break;
			case op::LE: test_kind = op::GE; break;
			case op::GT: test_kind = op::LT; break;
			case op::GE: test_kind = op::LE; break;
			default: break;

		}

	}

	if ( b.exprs [ static_cast < size_t > ( other_side ) ].kind != op::CONST )
		return guarded_entry ( start, b.exprs [ static_cast < size_t > ( seed.right ) ].value, test_kind, other_side );

	if ( other_side != t.b )
		return false;

	int64_t x = b.exprs [ static_cast < size_t > ( seed.right ) ].value;
	int64_t y = b.exprs [ static_cast < size_t > ( t.b ) ].value;
	bool is_unsigned = ( t.bits & UNSIGNED ) != 0;
	uint64_t ux = static_cast < uint64_t > ( x );
	uint64_t uy = static_cast < uint64_t > ( y );

	switch ( t.kind ) {

		case op::EQ: return x == y;
		case op::NE: return x != y;
		case op::LT: return is_unsigned ? ux < uy : x < y;
		case op::LE: return is_unsigned ? ux <= uy : x <= y;
		case op::GT: return is_unsigned ? ux > uy : x > y;
		case op::GE: return is_unsigned ? ux >= uy : x >= y;
		default: return false;

	}

}

int32_t decompiler::emitter::rotate ( int32_t block, int32_t entry, int32_t leave, int32_t guard_test, const frame& context ) {

	int32_t pre = -1;
	int32_t head = entry;

	if ( !header [ static_cast < size_t > ( head ) ] ) {

		const block_code& passage = b.code [ static_cast < size_t > ( entry ) ];

		if ( passage.end != term::FALL || pred [ static_cast < size_t > ( entry ) ].count ( ) != 1 || passage.taken < 0 || !header [ static_cast < size_t > ( passage.taken ) ] )
			return -2;

		pre = entry;
		head = passage.taken;

	}

	if ( emitted [ static_cast < size_t > ( head ) ] )
		return -2;

	const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( head ) ];
	int32_t latch = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( head ) ] ) {

		if ( !members.empty ( ) && members [ static_cast < size_t > ( p ) ] )
			latch = latch < 0 ? p : -2;
		else if ( p != ( pre >= 0 ? pre : block ) )
			return -2;

	}

	if ( latch < 0 )
		return -2;

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

	if ( tail.end != term::BRANCH || tail.condition < 0 )
		return -2;

	bool back_taken = tail.taken == head;

	if ( ( back_taken ? tail.other : tail.taken ) != leave || ( back_taken ? tail.taken : tail.other ) != head )
		return -2;

	int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
	bool matched = equivalent ( guard_test, test, -1, -1, 0 );
	int32_t seed_statement = -1;

	const int32_t sources [ 2 ] = { pre, block };

	for ( int32_t source : sources ) {

		if ( matched || source < 0 )
			continue;

		const block_code& owner = b.code [ static_cast < size_t > ( source ) ];

		for ( int32_t s = owner.first + owner.count - 1; s >= owner.first && !matched; s-- ) {

			const statement& line = b.statements [ static_cast < size_t > ( s ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
				continue;

			int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value );

			if ( equivalent ( guard_test, test, var, line.right, 0 ) ) {

				matched = true;
				seed_statement = s;

			}

		}

	}

	if ( !matched )
		return -2;

	( void ) seed_statement;

	if ( pre >= 0 )
		body ( pre );

	int32_t step = -1;

	for ( int32_t s = tail.first + tail.count - 1; s >= tail.first; s-- ) {

		const statement& line = b.statements [ static_cast < size_t > ( s ) ];

		if ( line.kind == stmt::NOP )
			continue;

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

			const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];

			if ( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == b.exprs [ static_cast < size_t > ( line.left ) ].value && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST && b.references ( test, static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ), -1 ) )
				step = s;

		}

		break;

	}

	frame inner;

	inner.loop = head;
	inner.exit = leave;

	label ( head );

	if ( step >= 0 && open_for ( tail.last, test, false, step, false ) )
		hidden [ static_cast < size_t > ( step ) ] = 1;
	else
		open_brace ( tail.last, L"while", test, false );

	emitted [ static_cast < size_t > ( latch ) ] = 1;

	if ( latch != head ) {

		region ( head, latch, inner );

		if ( !empty_block ( latch ) )
			body ( latch );

	} else
		body ( head );

	emitted [ static_cast < size_t > ( latch ) ] = 1;
	close_brace ( );
	ended = false;

	( void ) context;

	return leave;

}

bool decompiler::emitter::deferred ( int32_t target, int32_t from ) {

	if ( target < 0 || target >= count || emitted [ static_cast < size_t > ( target ) ] || pred [ static_cast < size_t > ( target ) ].count ( ) < 2 || header [ static_cast < size_t > ( target ) ] )
		return false;

	uint64_t start = b.graph.blocks [ static_cast < size_t > ( target ) ].start;

	for ( int32_t p : pred [ static_cast < size_t > ( target ) ] ) {

		if ( p != from && !emitted [ static_cast < size_t > ( p ) ] && ( b.graph.blocks [ static_cast < size_t > ( p ) ].end == start || ( defer_later && b.graph.blocks [ static_cast < size_t > ( p ) ].start < start ) ) && b.code [ static_cast < size_t > ( p ) ].reachable && !dominates ( target, p ) )
			return true;

	}

	return false;

}

bool decompiler::emitter::branch_fed ( int32_t block ) const {

	for ( int32_t p : pred [ static_cast < size_t > ( block ) ] ) {

		if ( b.code [ static_cast < size_t > ( p ) ].end == term::BRANCH )
			return true;

	}

	return false;

}

bool decompiler::emitter::negative_test ( int32_t condition ) const {

	const expr& test = b.exprs [ static_cast < size_t > ( condition ) ];

	if ( test.kind == op::LNOT )
		return true;

	if ( ( test.kind != op::EQ && test.kind != op::NE ) || test.b < 0 || ( test.bits & FLOATING ) )
		return false;

	const expr& right = b.exprs [ static_cast < size_t > ( test.b ) ];
	bool zero = right.kind == op::CONST && right.value == 0;

	if ( !fall_then_signs )
		return test.kind == op::EQ;

	return test.kind == op::EQ ? zero : ( right.kind == op::CONST && !zero );

}

int32_t decompiler::emitter::branch ( int32_t block, int32_t stop, const frame& context, bool chained ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];
	int32_t taken = item.taken;
	int32_t other = item.other;
	int32_t follow = follow_of ( block, stop, context );
	bool taken_jump = taken == context.loop || taken == context.exit;
	bool other_jump = other == context.loop || other == context.exit;

	if ( shared_returns && follow < 0 && context.loop < 0 && taken >= 0 && other >= 0 && taken < count && other < count ) {

		int32_t last = -1;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( b.code [ static_cast < size_t > ( i ) ].reachable && ( last < 0 || b.graph.blocks [ static_cast < size_t > ( i ) ].start > b.graph.blocks [ static_cast < size_t > ( last ) ].start ) )
				last = i;

		}

		auto same_value = [ & ] ( int32_t x ) {

			const block_code& tail = b.code [ static_cast < size_t > ( x ) ];

			if ( tail.returned < 0 )
				return true;

			const expr& back = b.exprs [ static_cast < size_t > ( tail.returned ) ];

			if ( back.kind == op::CONST )
				return true;

			if ( back.kind != op::VAR )
				return false;

			for ( int32_t p : pred [ static_cast < size_t > ( x ) ] ) {

				const block_code& from = b.code [ static_cast < size_t > ( p ) ];

				for ( int32_t s = from.first; s < from.first + from.count && s < b.statements.count ( ); s++ ) {

					const statement& line = b.statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == back.value )
						return false;

				}

			}

			return true;

		};

		auto falls_in = [ & ] ( int32_t x ) {

			for ( int32_t p : pred [ static_cast < size_t > ( x ) ] ) {

				if ( b.graph.blocks [ static_cast < size_t > ( p ) ].end == b.graph.blocks [ static_cast < size_t > ( x ) ].start && b.code [ static_cast < size_t > ( p ) ].end != term::BRANCH )
					return true;

			}

			return false;

		};

		auto sets_return = [ & ] ( int32_t x, int32_t end_block ) {

			const block_code& tail = b.code [ static_cast < size_t > ( end_block ) ];

			if ( tail.returned < 0 )
				return true;

			const expr& back = b.exprs [ static_cast < size_t > ( tail.returned ) ];

			if ( back.kind == op::CONST )
				return true;

			if ( back.kind != op::VAR )
				return false;

			const block_code& mine = b.code [ static_cast < size_t > ( x ) ];

			for ( int32_t s = mine.first; s < mine.first + mine.count && s < b.statements.count ( ); s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == back.value && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.right ) ].kind == op::CONST )
					return true;

			}

			return false;

		};

		auto shared = [ & ] ( int32_t x ) {

			if ( emitted [ static_cast < size_t > ( x ) ] || pred [ static_cast < size_t > ( x ) ].count ( ) < 2 || !small_block ( x ) || !falls_in ( x ) || header [ static_cast < size_t > ( x ) ] )
				return false;

			if ( exit_block ( x ) )
				return same_value ( x ) && ( x == last || sets_return ( x, x ) );

			const block_code& step = b.code [ static_cast < size_t > ( x ) ];

			if ( step.end != term::FALL || step.taken < 0 || step.taken >= count || !exit_block ( step.taken ) || !empty_block ( step.taken ) )
				return false;

			return sets_return ( x, step.taken );

		};

		if ( shared ( taken ) && !exit_block ( other ) )
			follow = taken;
		else if ( shared ( other ) && !exit_block ( taken ) )
			follow = other;

	}

	if ( context.loop >= 0 && stop == context.loop && ( taken == context.loop ) != ( other == context.loop ) ) {

		int32_t away = taken == context.loop ? other : taken;
		const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( context.loop ) ];
		bool outside = away == context.exit || emitted [ static_cast < size_t > ( away ) ] || ( !members.empty ( ) && !members [ static_cast < size_t > ( away ) ] );

		if ( outside ) {

			open_brace ( item.last, chained ? L"else if" : L"if", item.condition, taken == context.loop );

			if ( !special ( away, -2, context, item.last ) ) {

				if ( emitted [ static_cast < size_t > ( away ) ] )
					go_to ( away, item.last );
				else
					region ( away, context.loop, context );

			}

			close_brace ( );

			return -1;

		}

	}

	if ( taken_jump && !other_jump ) {

		open_brace ( item.last, chained ? L"else if" : L"if", item.condition, false );
		special ( taken, -2, context, item.last );
		close_brace ( );

		return other;

	}

	if ( other_jump && !taken_jump ) {

		open_brace ( item.last, chained ? L"else if" : L"if", item.condition, true );
		special ( other, -2, context, item.last );
		close_brace ( );

		return taken;

	}

	bool negated = false;
	int32_t then_part = taken;
	int32_t else_part = other;

	auto leads = [ & ] ( int32_t x, int32_t target ) {

		for ( int32_t hops = 0; hops < 8 && x >= 0 && x < count; hops++ ) {

			if ( x == target )
				return true;

			const block_code& step = b.code [ static_cast < size_t > ( x ) ];

			if ( !empty_block ( x ) || step.end != term::FALL || header [ static_cast < size_t > ( x ) ] || wanted_label [ static_cast < size_t > ( x ) ] )
				return false;

			x = step.taken;

		}

		return x == target;

	};

	if ( empty_then && then_part != follow && else_part != follow && stop >= 0 && ( leads ( then_part, stop ) || ( follow >= 0 && leads ( then_part, follow ) ) ) && !leads ( else_part, stop ) ) {

		then_part = other;
		else_part = taken;
		negated = true;

		if ( follow < 0 || leads ( else_part, stop ) )
			follow = stop;

		if ( leads ( else_part, follow ) )
			else_part = follow;

	} else if ( then_part == follow ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( fall_then && follow >= 0 && else_part != follow && other >= 0 && taken >= 0 && item.condition >= 0 && negative_test ( item.condition ) && ( !fall_then_simple || ( b.code [ static_cast < size_t > ( other ) ].end == term::FALL && b.code [ static_cast < size_t > ( other ) ].taken == follow && b.code [ static_cast < size_t > ( taken ) ].end == term::FALL && b.code [ static_cast < size_t > ( taken ) ].taken == follow ) ) ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( fall_returns_first && follow < 0 && other >= 0 && taken >= 0 && item.condition >= 0 && b.exprs [ static_cast < size_t > ( item.condition ) ].kind == op::LOR && else_returns && !chained && empty_block ( taken ) && empty_block ( other ) && !branch_fed ( block ) && b.code [ static_cast < size_t > ( other ) ].end == term::RETURN && b.code [ static_cast < size_t > ( taken ) ].end == term::RETURN && pred [ static_cast < size_t > ( other ) ].count ( ) == 1 && pred [ static_cast < size_t > ( taken ) ].count ( ) == 1 ) {

		then_part = other;
		else_part = taken;
		negated = true;

	} else if ( fall_returns_first && follow < 0 && other >= 0 && taken >= 0 && item.condition >= 0 && negative_test ( item.condition ) && ( b.exprs [ static_cast < size_t > ( item.condition ) ].kind == op::NE || ( b.code [ static_cast < size_t > ( taken ) ].returned >= 0 && b.exprs [ static_cast < size_t > ( b.code [ static_cast < size_t > ( taken ) ].returned ) ].kind != op::CONST ) || ( else_returns && !chained && empty_block ( taken ) && empty_block ( other ) && !branch_fed ( block ) ) ) && b.code [ static_cast < size_t > ( other ) ].end == term::RETURN && b.code [ static_cast < size_t > ( taken ) ].end == term::RETURN && pred [ static_cast < size_t > ( other ) ].count ( ) == 1 && pred [ static_cast < size_t > ( taken ) ].count ( ) == 1 ) {

		then_part = other;
		else_part = taken;
		negated = true;

	}

	if ( diamond_ternary && !no_ternary && follow >= 0 && then_part >= 0 && else_part >= 0 && then_part != follow && else_part != follow ) {

		int32_t then_line = single_assign ( then_part, follow );
		int32_t else_line = single_assign ( else_part, follow );

		if ( then_line >= 0 && else_line >= 0 ) {

			const statement& yes = b.statements [ static_cast < size_t > ( then_line ) ];
			const statement& no = b.statements [ static_cast < size_t > ( else_line ) ];
			const expr& yes_left = b.exprs [ static_cast < size_t > ( yes.left ) ];
			const expr& no_left = b.exprs [ static_cast < size_t > ( no.left ) ];

			if ( yes_left.kind == op::VAR && no_left.kind == op::VAR && yes_left.value == no_left.value ) {

				int32_t test = negated ? b.negate ( item.condition ) : item.condition;
				int32_t select = b.make ( op::TERNARY, yes_left.size, test, yes.right );

				b.exprs [ static_cast < size_t > ( select ) ].c = no.right;
				b.deepen ( select );
				label ( then_part );
				label ( else_part );
				emitted [ static_cast < size_t > ( then_part ) ] = 1;
				emitted [ static_cast < size_t > ( else_part ) ] = 1;

				if ( !dry ) {

					out.begin ( yes.address );
					value ( yes.left, 0 );
					out.put ( L" = ", syntax::OPERATOR );
					value ( select, 2 );
					out.put ( L";" );
					out.finish ( );

				}

				ended = false;

				return follow;

			}

		}

	}

	if ( follow < 0 && then_part >= 0 && else_part >= 0 && !chained ) {

		for ( int32_t side = 0; side < 2; side++ ) {

			int32_t bare = side == 0 ? else_part : then_part;
			int32_t work = side == 0 ? then_part : else_part;
			const block_code& empty_ret = b.code [ static_cast < size_t > ( bare ) ];
			const block_code& full_ret = b.code [ static_cast < size_t > ( work ) ];

			if ( empty_ret.end != term::RETURN || full_ret.end != term::RETURN || !empty_block ( bare ) || empty_block ( work ) || emitted [ static_cast < size_t > ( work ) ] || emitted [ static_cast < size_t > ( bare ) ] || pred [ static_cast < size_t > ( work ) ].count ( ) != 1 )
				continue;

			bool alike = ( empty_ret.returned < 0 && full_ret.returned < 0 ) || ( empty_ret.returned >= 0 && full_ret.returned >= 0 && b.same ( empty_ret.returned, full_ret.returned ) );

			if ( !alike )
				continue;

			bool toward = side == 0 ? negated : !negated;

			open_brace ( item.last, L"if", item.condition, toward );
			body ( work );
			close_brace ( );

			return bare;

		}

	}

	if ( rotate_guards && !chained && then_part >= 0 && else_part >= 0 && then_part != follow && else_part != context.loop && else_part != context.exit ) {

		int32_t after = rotate ( block, then_part, else_part, negated ? b.negate ( item.condition ) : item.condition, context );

		if ( after != -2 )
			return after;

	}

	bool skip_else = false;

	if ( early_returns && follow < 0 && !chained && then_part >= 0 && else_part >= 0 && then_part != else_part && b.code [ static_cast < size_t > ( else_part ) ].end == term::RETURN && empty_block ( else_part ) && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && b.code [ static_cast < size_t > ( then_part ) ].end != term::RETURN && b.code [ static_cast < size_t > ( then_part ) ].end != term::STOP && !exit_block ( then_part ) && !( b.code [ static_cast < size_t > ( else_part ) ].returned >= 0 && b.exprs [ static_cast < size_t > ( b.code [ static_cast < size_t > ( else_part ) ].returned ) ].kind == op::VAR ) && !emitted [ static_cast < size_t > ( else_part ) ] && then_part != context.loop && then_part != context.exit && else_part != context.loop && else_part != context.exit ) {

		int32_t held = then_part;

		then_part = else_part;
		else_part = held;
		negated = !negated;

	}

	if ( follow < 0 && context.loop < 0 && silent_return ( then_part ) && !silent_return ( else_part ) && else_part >= 0 ) {

		int32_t held = then_part;

		then_part = else_part;
		else_part = held;
		negated = !negated;

	}

	if ( else_part != follow && follow < 0 && context.loop < 0 && silent_return ( else_part ) ) {

		skip_else = true;
		emitted [ static_cast < size_t > ( else_part ) ] = 1;

	}

	open_brace ( item.last, chained ? L"else if" : L"if", item.condition, negated );

	ended = false;

	if ( !special ( then_part, -2, context, item.last ) ) {

		if ( defer_joins && deferred ( then_part, block ) )
			go_to ( then_part, item.last );
		else
			region ( then_part, follow, context );

	}

	bool then_ends = ended;

	out.indent--;

	if ( skip_else ) {

		brace ( L"}" );
		ended = false;

	} else if ( then_ends && else_part != follow && else_part >= 0 && !( chain_rest && chained && chain_falls ) && !( else_returns && !chained && follow < 0 && then_part >= 0 && b.code [ static_cast < size_t > ( then_part ) ].end == term::RETURN && b.code [ static_cast < size_t > ( else_part ) ].end == term::RETURN && pred [ static_cast < size_t > ( then_part ) ].count ( ) == 1 && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && !emitted [ static_cast < size_t > ( else_part ) ] && empty_block ( then_part ) && empty_block ( else_part ) && !branch_fed ( block ) ) ) {

		brace ( L"}" );
		ended = false;

		if ( !special ( else_part, -2, context, item.last ) )
			region ( else_part, follow, context );

	} else if ( else_part != follow && else_part >= 0 ) {

		bool chain = !emitted [ static_cast < size_t > ( else_part ) ] && b.code [ static_cast < size_t > ( else_part ) ].end == term::BRANCH && empty_block ( else_part ) && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && !header [ static_cast < size_t > ( else_part ) ] && follow_of ( else_part, stop, context ) == follow && !wanted_label [ static_cast < size_t > ( else_part ) ];

		if ( chain ) {

			brace ( L"}" );
			body ( else_part );

			bool saved_falls = chain_falls;

			chain_falls = ( chained && chain_falls ) || !then_ends;

			int32_t next = branch ( else_part, stop, context, true );
			bool chain_ends = ended;

			chain_falls = saved_falls;

			if ( chain_rest && next >= 0 && next < count && next != follow && !emitted [ static_cast < size_t > ( next ) ] ) {

				else_line ( );
				brace ( L"{" );
				out.indent++;
				ended = false;

				if ( !special ( next, -2, context, item.last ) )
					region ( next, follow, context );

				chain_ends = ended;
				close_brace ( );

			}

			ended = then_ends && chain_ends;

		} else {

			brace ( L"}" );
			else_line ( );
			brace ( L"{" );
			out.indent++;
			ended = false;

			if ( !special ( else_part, -2, context, item.last ) )
				region ( else_part, follow, context );

			bool else_ends = ended;

			close_brace ( );
			ended = then_ends && else_ends;

		}

	} else {

		brace ( L"}" );
		ended = false;

	}

	return follow;

}

int32_t decompiler::emitter::loop_step ( int32_t start, const base::list < uint8_t >& members, int32_t condition ) {

	int32_t latch = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

		if ( members [ static_cast < size_t > ( p ) ] )
			latch = latch < 0 ? p : -2;

	}

	if ( latch < 0 || latch == start )
		return -1;

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

	if ( tail.end != term::FALL || tail.taken != start )
		return -1;

	int32_t last = -1;

	for ( int32_t s = tail.first; s < tail.first + tail.count && s < b.statements.count ( ); s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
			last = s;

	}

	if ( last < 0 )
		return -1;

	const statement& line = b.statements [ static_cast < size_t > ( last ) ];

	if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
		return -1;

	const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];
	const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

	step_needs_init = false;

	if ( left.kind != op::VAR || ( right.bits & FLOATING ) )
		return -1;

	bool counted = right.kind == op::ADD && right.a >= 0 && right.b >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST;

	if ( !counted ) {

		if ( condition < 0 || !b.references ( line.right, static_cast < int32_t > ( left.value ), -1 ) )
			return -1;

		step_needs_init = true;

	}

	if ( condition >= 0 && !b.references ( condition, static_cast < int32_t > ( left.value ), -1 ) )
		return -1;

	return last;

}

void decompiler::emitter::name_loop_var ( int32_t var ) {

	if ( var < 0 || var >= loop_letter.count ( ) || loop_letter [ static_cast < size_t > ( var ) ] >= 0 || var == result_var )
		return;

	const variable& item = b.vars [ static_cast < size_t > ( var ) ];

	if ( item.kind == var_kind::PARAMETER || next_letter >= 10 )
		return;

	loop_letter [ static_cast < size_t > ( var ) ] = static_cast < int8_t > ( next_letter++ );

}

bool decompiler::emitter::usable_step ( int32_t step ) const {

	if ( step < 0 )
		return false;

	for ( int32_t s = 0; s < fold_at.count ( ) && s < fold_inc.count ( ); s++ ) {

		if ( fold_at [ static_cast < size_t > ( s ) ] >= 0 && fold_inc [ static_cast < size_t > ( s ) ] == step )
			return false;

	}

	for ( int32_t k = 0; k < fold_cond.count ( ) && k < fold_cond_inc.count ( ); k++ ) {

		if ( fold_cond [ static_cast < size_t > ( k ) ] >= 0 && fold_cond_inc [ static_cast < size_t > ( k ) ] == step )
			return false;

	}

	return true;

}

int32_t decompiler::emitter::step_carrier ( int32_t step, int32_t latch ) const {

	if ( !step_carriers || step < 0 || latch < 0 || latch >= count )
		return -1;

	int32_t carrier = -1;

	for ( int32_t s = 0; s < fold_at.count ( ) && s < fold_inc.count ( ); s++ ) {

		if ( fold_at [ static_cast < size_t > ( s ) ] >= 0 && fold_inc [ static_cast < size_t > ( s ) ] == step )
			carrier = s;

	}

	if ( carrier < 0 || b.statements [ static_cast < size_t > ( carrier ) ].kind != stmt::STORE )
		return -1;

	const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
	int32_t last = -1;

	for ( int32_t s = tail.first; s < tail.first + tail.count && s < b.statements.count ( ); s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP && s != step )
			last = s;

	}

	return last == carrier ? carrier : -1;

}

bool decompiler::emitter::open_for ( uint64_t address, int32_t condition, bool negated, int32_t step, bool need_init, int32_t carrier ) {

	int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( b.statements [ static_cast < size_t > ( step ) ].left ) ].value );
	int32_t init = -1;
	int32_t init_line = -1;

	if ( !find_init ( var, init, init_line ) )
		init = -1;

	if ( need_init && ( dry || init < 0 ) )
		return false;

	name_loop_var ( var );

	if ( dry ) {

		out.indent++;

		return true;

	}

	if ( init >= 0 ) {

		if ( init_line == out.r->lines.count ( ) - 1 ) {

			const line held = out.r->lines.back ( );

			out.r->lines.pop ( );
			out.r->text.resize ( static_cast < size_t > ( held.text ) );
			out.r->spans.resize ( static_cast < size_t > ( held.first_span ) );

		} else
			out.r->lines.remove_at ( static_cast < size_t > ( init_line ) );

		last_assign = -1;
		last_assign_line = -1;
		recent_assign.clear ( );
		recent_line.clear ( );

	}

	hoisted.clear ( );
	hoisted_temp.clear ( );
	out.begin ( address );
	out.put ( L"for", syntax::CONTROL );
	paren_open ( );

	if ( init >= 0 )
		inline_statement ( init );

	out.put ( condition >= 0 ? L"; " : L"; ;" );

	if ( condition >= 0 ) {

		int32_t shown = negated ? b.negate ( condition ) : condition;
		const expr test = b.exprs [ static_cast < size_t > ( shown ) ];
		bool relation = test.kind >= op::EQ && test.kind <= op::GE;

		if ( relation && test.a >= 0 && test.b >= 0 && strip ( test.b ) >= 0 && b.exprs [ static_cast < size_t > ( strip ( test.b ) ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( strip ( test.b ) ) ].value == var && !( strip ( test.a ) >= 0 && b.exprs [ static_cast < size_t > ( strip ( test.a ) ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( strip ( test.a ) ) ].value == var ) ) {

			op turned = test.kind;

			switch ( test.kind ) {

				case op::LT: turned = op::GT; break;
				case op::LE: turned = op::GE; break;
				case op::GT: turned = op::LT; break;
				case op::GE: turned = op::LE; break;
				default: break;

			}

			shown = b.make ( turned, 1, test.b, test.a, 0, test.bits );

		}

		value ( shown, 0 );
		out.put ( L";" );

	}

	out.put ( L" " );
	inline_statement ( carrier >= 0 ? carrier : step );
	paren_close ( );
	out.finish ( );
	brace ( L"{" );
	out.indent++;

	return true;

}

int32_t decompiler::emitter::loop ( int32_t start, const frame& outer ) {

	const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( start ) ];
	int32_t exit = -1;

	for ( int32_t v = 0; v < count && exit < 0; v++ ) {

		if ( !members [ static_cast < size_t > ( v ) ] )
			continue;

		for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

			if ( !members [ static_cast < size_t > ( s ) ] ) {

				exit = s;

				break;

			}

		}

	}

	int32_t pd = ipdom [ static_cast < size_t > ( start ) ];

	if ( pd >= 0 && pd < count && !members [ static_cast < size_t > ( pd ) ] )
		exit = pd;

	if ( forced_exit >= 0 ) {

		exit = forced_exit;
		forced_exit = -1;

	}

	if ( return_exits && exit >= 0 && exit < count && exit_block ( exit ) && small_block ( exit ) ) {

		int32_t other = -1;

		for ( int32_t v = 0; v < count && other != -2; v++ ) {

			if ( !members [ static_cast < size_t > ( v ) ] )
				continue;

			for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

				if ( members [ static_cast < size_t > ( s ) ] || s == exit || exit_block ( s ) )
					continue;

				if ( other == -1 )
					other = s;
				else if ( other != s )
					other = -2;

			}

		}

		if ( other >= 0 )
			exit = other;

	}

	frame inner;

	inner.loop = start;
	inner.exit = exit;
	inner.outer = forced_outer;
	forced_outer = -1;

	const block_code& item = b.code [ static_cast < size_t > ( start ) ];

	if ( item.end == term::BRANCH && ( item.taken == start || item.other == start ) ) {

		bool back_taken = item.taken == start;
		int32_t leaving = back_taken ? item.other : item.taken;
		int32_t test = back_taken ? item.condition : b.negate ( item.condition );
		int32_t step = counted_step ( start, test );
		int32_t carrier = usable_step ( step ) ? -1 : step_carrier ( step, start );

		if ( ( usable_step ( step ) || carrier >= 0 ) && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true, carrier ) ) {

			hidden [ static_cast < size_t > ( step ) ] = 1;

			if ( carrier >= 0 ) {

				hidden [ static_cast < size_t > ( carrier ) ] = 1;
				carried.push ( carrier );

			}

			body ( start );
			close_brace ( );

			return leaving;

		}

		int32_t moved = sinkable_step ( start, test );

		open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

		if ( moved >= 0 )
			hidden [ static_cast < size_t > ( moved ) ] = 1;

		body ( start );

		if ( moved >= 0 ) {

			hidden [ static_cast < size_t > ( moved ) ] = 0;
			statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

		}

		close_brace ( );
		while_tail ( item.last, test );

		return leaving;

	}

	int32_t latch = -1;

	for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

		if ( members [ static_cast < size_t > ( p ) ] )
			latch = latch < 0 ? p : -2;

	}

	if ( latch >= 0 && latch != start && b.code [ static_cast < size_t > ( latch ) ].end == term::BRANCH ) {

		const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
		bool back_taken = tail.taken == start;
		int32_t leaving = back_taken ? tail.other : tail.taken;

		if ( leaving == exit ) {

			int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
			int32_t step = counted_step ( latch, test );
			int32_t carrier = usable_step ( step ) ? -1 : step_carrier ( step, latch );

			if ( ( usable_step ( step ) || carrier >= 0 ) && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true, carrier ) ) {

				hidden [ static_cast < size_t > ( step ) ] = 1;

				if ( carrier >= 0 ) {

					hidden [ static_cast < size_t > ( carrier ) ] = 1;
					carried.push ( carrier );

				}

				emitted [ static_cast < size_t > ( latch ) ] = 1;

				if ( empty_block ( latch ) && latch_continues )
					inner.latch = latch;

				region ( start, latch, inner );

				if ( !empty_block ( latch ) )
					body ( latch );

				emitted [ static_cast < size_t > ( latch ) ] = 1;
				close_brace ( );

				return exit;

			}

			bool plain = empty_block ( latch );

			open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

			emitted [ static_cast < size_t > ( latch ) ] = 1;

			if ( plain && latch_continues )
				inner.latch = latch;

			region ( start, latch, inner );

			if ( !plain ) {

				int32_t moved = sinkable_step ( latch, test );

				if ( moved >= 0 )
					hidden [ static_cast < size_t > ( moved ) ] = 1;

				body ( latch );

				if ( moved >= 0 ) {

					hidden [ static_cast < size_t > ( moved ) ] = 0;
					statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

				}

			}

			emitted [ static_cast < size_t > ( latch ) ] = 1;
			close_brace ( );
			while_tail ( tail.last, test );

			return exit;

		}

	}

	if ( nested_loops && latch == -2 && nest_depth < 3 ) {

		int32_t outer_latch = -1;

		for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

			if ( members [ static_cast < size_t > ( p ) ] && p != start && ( outer_latch < 0 || b.graph.blocks [ static_cast < size_t > ( p ) ].start > b.graph.blocks [ static_cast < size_t > ( outer_latch ) ].start ) )
				outer_latch = p;

		}

		const block_code& tail = b.code [ static_cast < size_t > ( outer_latch >= 0 ? outer_latch : start ) ];
		bool usable = outer_latch >= 0 && tail.end == term::BRANCH && ( tail.taken == start ) != ( tail.other == start ) && ( tail.taken == start ? tail.other : tail.taken ) == exit;

		base::list < uint8_t > inner_set;

		if ( usable ) {

			inner_set.fill ( static_cast < size_t > ( count ), 0 );

			base::list < int32_t > work;

			inner_set [ static_cast < size_t > ( start ) ] = 1;

			for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

				if ( members [ static_cast < size_t > ( p ) ] && p != outer_latch && !inner_set [ static_cast < size_t > ( p ) ] ) {

					inner_set [ static_cast < size_t > ( p ) ] = 1;
					work.push ( p );

				}

			}

			while ( !work.empty ( ) ) {

				int32_t v = work.back ( );

				work.pop ( );

				for ( int32_t p : pred [ static_cast < size_t > ( v ) ] ) {

					if ( members [ static_cast < size_t > ( p ) ] && !inner_set [ static_cast < size_t > ( p ) ] ) {

						inner_set [ static_cast < size_t > ( p ) ] = 1;
						work.push ( p );

					}

				}

			}

			if ( inner_set [ static_cast < size_t > ( outer_latch ) ] )
				usable = false;

		}

		int32_t resume = -1;

		if ( usable ) {

			for ( int32_t v = 0; v < count && resume != -2; v++ ) {

				if ( !inner_set [ static_cast < size_t > ( v ) ] )
					continue;

				for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

					if ( inner_set [ static_cast < size_t > ( s ) ] )
						continue;

					if ( !members [ static_cast < size_t > ( s ) ] ) {

						if ( !exit_block ( s ) && s != exit )
							resume = -2;

						continue;

					}

					if ( resume == -1 )
						resume = s;
					else if ( resume != s )
						resume = -2;

				}

			}

			usable = resume >= 0;

		}

		if ( usable ) {

			bool back_taken = tail.taken == start;
			int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
			base::list < uint8_t > saved = loop_body [ static_cast < size_t > ( start ) ];

			int32_t step = counted_step ( outer_latch, test );
			bool counted = usable_step ( step ) && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true );

			if ( counted )
				hidden [ static_cast < size_t > ( step ) ] = 1;
			else
				open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

			loop_body [ static_cast < size_t > ( start ) ] = inner_set;
			forced_exit = resume;
			forced_outer = exit;
			nest_depth++;

			int32_t after = loop ( start, inner );

			nest_depth--;
			forced_exit = -1;
			loop_body [ static_cast < size_t > ( start ) ] = saved;
			emitted [ static_cast < size_t > ( outer_latch ) ] = 1;

			if ( after >= 0 && after != outer_latch && !emitted [ static_cast < size_t > ( after ) ] )
				region ( after, outer_latch, inner );

			int32_t moved = counted ? -1 : sinkable_step ( outer_latch, test );

			if ( moved >= 0 )
				hidden [ static_cast < size_t > ( moved ) ] = 1;

			if ( !empty_block ( outer_latch ) )
				body ( outer_latch );

			if ( moved >= 0 ) {

				hidden [ static_cast < size_t > ( moved ) ] = 0;
				statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

			}

			close_brace ( );

			if ( !counted )
				while_tail ( tail.last, test );

			return exit;

		}

	}

	if ( item.end == term::BRANCH && empty_block ( start ) && ( ( item.taken == exit && members [ static_cast < size_t > ( item.other ) ] ) || ( item.other == exit && members [ static_cast < size_t > ( item.taken ) ] ) ) ) {

		bool stay_taken = item.other == exit;
		int32_t step = loop_step ( start, members, item.condition );

		label ( start );
		emitted [ static_cast < size_t > ( start ) ] = 1;

		if ( usable_step ( step ) && open_for ( item.last, item.condition, !stay_taken, step, step_needs_init ) )
			hidden [ static_cast < size_t > ( step ) ] = 1;
		else
			open_brace ( item.last, L"while", item.condition, !stay_taken );

		region ( stay_taken ? item.taken : item.other, start, inner );
		close_brace ( );

		return exit;

	}

	int32_t forever_step = loop_step ( start, members, -1 );

	if ( forever_step >= 0 && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, -1, false, forever_step, true ) )
		hidden [ static_cast < size_t > ( forever_step ) ] = 1;
	else
		open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"while", b.constant ( 1, 1 ), false );

	frame body_frame = inner;

	emitted [ static_cast < size_t > ( start ) ] = 0;
	region_header ( start, body_frame );
	close_brace ( );

	( void ) outer;

	return exit;

}

void decompiler::emitter::region_header ( int32_t start, frame context ) {

	body ( start );

	const block_code& item = b.code [ static_cast < size_t > ( start ) ];
	int32_t next = -1;

	switch ( item.end ) {

		case term::RETURN:
		case term::TAIL:
			return_line ( item );
			break;

		case term::FALL:

			if ( !special ( item.taken, start, context, item.last ) )
				next = item.taken;

			break;

		case term::BRANCH:
			next = branch ( start, start, context, false );
			break;

		case term::SWITCH:
			next = choice ( start, start, context );
			break;

		default:
			break;

	}

	if ( next >= 0 && next != start )
		region ( next, start, context );

}

int32_t decompiler::emitter::choice ( int32_t block, int32_t stop, const frame& context ) {

	const block_code& item = b.code [ static_cast < size_t > ( block ) ];
	const analysis::block& node = b.graph.blocks [ static_cast < size_t > ( block ) ];
	int32_t follow = follow_of ( block, stop, context );

	if ( !dry ) {

		out.begin ( item.last );
		out.put ( L"switch", syntax::CONTROL );
		paren_open ( );
		value ( item.selector, 0 );
		paren_close ( );
		out.finish ( );

	}

	brace ( L"{" );
	out.indent++;

	base::list < int32_t > targets;

	for ( int32_t e = 0; e < node.out_count; e++ ) {

		const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];
		int32_t to = link.to;
		bool seen = false;

		if ( link.kind == analysis::edge_kind::JUMP && to == follow )
			continue;

		for ( int32_t t : targets ) {

			if ( t == to )
				seen = true;

		}

		if ( !seen )
			targets.push ( to );

	}

	frame inner = context;

	inner.exit = follow;

	int32_t default_to = -1;

	for ( int32_t e = 0; e < node.out_count; e++ ) {

		const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

		if ( link.kind == analysis::edge_kind::JUMP )
			default_to = link.to;

	}

	for ( int32_t target : targets ) {

		if ( default_cases && target == default_to ) {

			bool labeled = false;

			for ( int32_t e = 0; e < node.out_count; e++ ) {

				const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

				if ( link.to == target && link.kind == analysis::edge_kind::JUMP )
					labeled = true;

			}

			if ( !labeled || target == follow )
				continue;

		}

		if ( !dry ) {

			for ( int32_t e = 0; e < node.out_count; e++ ) {

				const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

				if ( link.to != target )
					continue;

				if ( default_cases && link.kind != analysis::edge_kind::JUMP && target == default_to )
					continue;

				out.begin ( b.graph.blocks [ static_cast < size_t > ( target ) ].start );

				if ( link.kind == analysis::edge_kind::JUMP )
					out.put ( L"default", syntax::CONTROL );
				else {

					out.put ( L"case", syntax::CONTROL );
					out.put ( L" " );
					number ( link.value, 4, false, false );

				}

				out.put ( L":" );
				out.finish ( );

			}

		}

		out.indent++;

		if ( target != follow && !emitted [ static_cast < size_t > ( target ) ] )
			region ( target, follow, inner );
		else if ( target != follow )
			go_to ( target, 0 );

		if ( out.terminal < 0 )
			jump_line ( L"break", 0 );

		out.indent--;

	}

	close_brace ( );

	return follow;

}

void decompiler::emitter::run ( ) {

	for ( int32_t c : carried ) {

		if ( c >= 0 && c < hidden.count ( ) )
			hidden [ static_cast < size_t > ( c ) ] = 0;

	}

	carried.clear ( );
	out.terminal = -1;
	emitted.fill ( static_cast < size_t > ( count ), 0 );
	labeled.fill ( static_cast < size_t > ( count ), 0 );

	if ( !dry )
		referenced.fill ( b.vars.size ( ), 0 );

	number_addresses.clear ( );
	number_values.clear ( );
	gotos = 0;
	depth = 0;
	next_temp = 1;

	frame top;

	region ( 0, -1, top );

	for ( int32_t i = 0; i < count; i++ ) {

		if ( !emitted [ static_cast < size_t > ( i ) ] && b.code [ static_cast < size_t > ( i ) ].reachable ) {

			wanted_label [ static_cast < size_t > ( i ) ] = 1;
			region ( i, -1, top );

		}

	}

}

void decompiler::emitter::analyze ( ) {

	count = b.graph.blocks.count ( );
	succ.resize ( static_cast < size_t > ( count ) );
	pred.resize ( static_cast < size_t > ( count ) );

	for ( const analysis::edge& link : b.graph.edges ) {

		bool seen = false;

		for ( int32_t s : succ [ static_cast < size_t > ( link.from ) ] ) {

			if ( s == link.to )
				seen = true;

		}

		if ( seen )
			continue;

		succ [ static_cast < size_t > ( link.from ) ].push ( link.to );
		pred [ static_cast < size_t > ( link.to ) ].push ( link.from );

	}

	base::list < int32_t > order;

	postorder ( count, 0, false, order );

	base::list < int32_t > rpo;

	for ( int32_t i = order.count ( ) - 1; i >= 0; i-- )
		rpo.push ( order [ static_cast < size_t > ( i ) ] );

	rpo_index.fill ( static_cast < size_t > ( count + 1 ), -1 );

	for ( int32_t i = 0; i < rpo.count ( ); i++ )
		rpo_index [ static_cast < size_t > ( rpo [ static_cast < size_t > ( i ) ] ) ] = i;

	dominate ( rpo, false, idom );

	base::list < int32_t > reverse_order;

	postorder ( count + 1, count, true, reverse_order );

	base::list < int32_t > reverse_rpo;

	for ( int32_t i = reverse_order.count ( ) - 1; i >= 0; i-- )
		reverse_rpo.push ( reverse_order [ static_cast < size_t > ( i ) ] );

	rpo_index.fill ( static_cast < size_t > ( count + 1 ), -1 );

	for ( int32_t i = 0; i < reverse_rpo.count ( ); i++ )
		rpo_index [ static_cast < size_t > ( reverse_rpo [ static_cast < size_t > ( i ) ] ) ] = i;

	dominate ( reverse_rpo, true, ipdom );

	header.fill ( static_cast < size_t > ( count ), 0 );
	loop_body.resize ( static_cast < size_t > ( count ) );

	for ( int32_t u = 0; u < count; u++ ) {

		for ( int32_t h : succ [ static_cast < size_t > ( u ) ] ) {

			if ( !dominates ( h, u ) )
				continue;

			header [ static_cast < size_t > ( h ) ] = 1;

			base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( h ) ];

			if ( members.empty ( ) )
				members.fill ( static_cast < size_t > ( count ), 0 );

			members [ static_cast < size_t > ( h ) ] = 1;

			base::list < int32_t > work;

			if ( !members [ static_cast < size_t > ( u ) ] ) {

				members [ static_cast < size_t > ( u ) ] = 1;
				work.push ( u );

			}

			while ( !work.empty ( ) ) {

				int32_t v = work.back ( );

				work.pop ( );

				for ( int32_t p : pred [ static_cast < size_t > ( v ) ] ) {

					if ( !members [ static_cast < size_t > ( p ) ] ) {

						members [ static_cast < size_t > ( p ) ] = 1;
						work.push ( p );

					}

				}

			}

		}

	}

	wanted_label.fill ( static_cast < size_t > ( count ), 0 );
	hidden.fill ( b.statements.size ( ), 0 );
	loop_letter.fill ( b.vars.size ( ), -1 );
	var_number.fill ( b.vars.size ( ), -1 );
	referenced.fill ( b.vars.size ( ), 0 );

}

bool decompiler::emitter::dominates ( int32_t a, int32_t v ) {

	int32_t steps = 0;

	while ( v >= 0 && steps++ < 100000 ) {

		if ( v == a )
			return true;

		int32_t up = idom [ static_cast < size_t > ( v ) ];

		if ( up == v )
			return false;

		v = up;

	}

	return false;

}

void decompiler::emitter::postorder ( int32_t total, int32_t root, bool reverse, base::list < int32_t >& order ) {

	base::list < uint8_t > state;
	base::list < int32_t > stack;
	base::list < int32_t > cursor;

	state.fill ( static_cast < size_t > ( total ), 0 );
	cursor.fill ( static_cast < size_t > ( total ), 0 );
	stack.push ( root );
	state [ static_cast < size_t > ( root ) ] = 1;

	while ( !stack.empty ( ) ) {

		int32_t v = stack.back ( );
		base::list < int32_t > next;

		neighbors ( v, reverse, next );

		int32_t& at = cursor [ static_cast < size_t > ( v ) ];

		if ( at < next.count ( ) ) {

			int32_t to = next [ static_cast < size_t > ( at++ ) ];

			if ( !state [ static_cast < size_t > ( to ) ] ) {

				state [ static_cast < size_t > ( to ) ] = 1;
				stack.push ( to );

			}

			continue;

		}

		order.push ( v );
		stack.pop ( );

	}

}

bool decompiler::emitter::small_block ( int32_t v ) {

	const block_code& item = b.code [ static_cast < size_t > ( v ) ];
	int32_t live = 0;

	for ( int32_t s = item.first; s < item.first + item.count && s < b.statements.count ( ); s++ ) {

		if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
			live++;

	}

	return live <= 1;

}

bool decompiler::emitter::exit_block ( int32_t v ) {

	term end = b.code [ static_cast < size_t > ( v ) ].end;

	return succ [ static_cast < size_t > ( v ) ].empty ( ) || end == term::RETURN || end == term::TAIL || end == term::STOP;

}

void decompiler::emitter::neighbors ( int32_t v, bool reverse, base::list < int32_t >& out_list ) {

	out_list.clear ( );

	if ( !reverse ) {

		if ( v < count )
			out_list = succ [ static_cast < size_t > ( v ) ];

		return;

	}

	if ( v == count ) {

		for ( int32_t i = 0; i < count; i++ ) {

			if ( exit_block ( i ) )
				out_list.push ( i );

		}

		return;

	}

	out_list = pred [ static_cast < size_t > ( v ) ];

}

void decompiler::emitter::incoming ( int32_t v, bool reverse, base::list < int32_t >& out_list ) {

	out_list.clear ( );

	if ( !reverse ) {

		out_list = pred [ static_cast < size_t > ( v ) ];

		return;

	}

	if ( v == count )
		return;

	out_list = succ [ static_cast < size_t > ( v ) ];

	if ( exit_block ( v ) )
		out_list.push ( count );

}

void decompiler::emitter::dominate ( const base::list < int32_t >& rpo, bool reverse, base::list < int32_t >& result_list ) {

	int32_t total = count + 1;
	int32_t root = rpo.empty ( ) ? 0 : rpo [ 0 ];

	result_list.fill ( static_cast < size_t > ( total ), -1 );
	result_list [ static_cast < size_t > ( root ) ] = root;

	bool changed = true;
	base::list < int32_t > preds;

	while ( changed ) {

		changed = false;

		for ( int32_t i = 1; i < rpo.count ( ); i++ ) {

			int32_t v = rpo [ static_cast < size_t > ( i ) ];
			int32_t chosen = -1;

			incoming ( v, reverse, preds );

			for ( int32_t p : preds ) {

				if ( result_list [ static_cast < size_t > ( p ) ] < 0 )
					continue;

				if ( chosen < 0 )
					chosen = p;
				else {

					int32_t x = p;
					int32_t y = chosen;

					while ( x != y ) {

						while ( rpo_index [ static_cast < size_t > ( x ) ] > rpo_index [ static_cast < size_t > ( y ) ] )
							x = result_list [ static_cast < size_t > ( x ) ];

						while ( rpo_index [ static_cast < size_t > ( y ) ] > rpo_index [ static_cast < size_t > ( x ) ] )
							y = result_list [ static_cast < size_t > ( y ) ];

					}

					chosen = x;

				}

			}

			if ( chosen >= 0 && result_list [ static_cast < size_t > ( v ) ] != chosen ) {

				result_list [ static_cast < size_t > ( v ) ] = chosen;
				changed = true;

			}

		}

	}

}