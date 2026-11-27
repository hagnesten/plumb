#include "decompiler/emit/vartypes/vartypes.h"
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
bool decompiler::emitter::char_typed_sum ( int32_t e ) const {

	int32_t var = -1;
	int32_t index = -1;
	int64_t offset = 0;

	if ( char_address && char_terms ( e, var, index, offset ) )
		return true;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	return node.kind == op::ADD && ( pointer_step ( node.a ) > 0 || ( node.b >= 0 && pointer_step ( node.b ) > 0 ) );

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