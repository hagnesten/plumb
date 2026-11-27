#include "decompiler/emit/expressions/expressions.h"
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

bool decompiler::emitter::part_macro ( int32_t e ) {

	if ( e < 0 )
		return false;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

	if ( node.bits & ( FLOATING | POINTER ) )
		return false;

	int32_t shifted = e;
	uint8_t width = node.size;

	if ( node.kind == op::CAST ) {

		if ( node.a < 0 )
			return false;

		shifted = node.a;

	}

	const expr& shift = b.exprs [ static_cast < size_t > ( shifted ) ];

	if ( shift.kind != op::SHR || ( shift.bits & ( SIGNED | FLOATING ) ) || shift.a < 0 || shift.b < 0 || b.exprs [ static_cast < size_t > ( shift.b ) ].kind != op::CONST )
		return false;

	const expr& source = b.exprs [ static_cast < size_t > ( shift.a ) ];

	if ( source.kind != op::VAR || ( source.bits & FLOATING ) )
		return false;

	int64_t amount = b.exprs [ static_cast < size_t > ( shift.b ) ].value;
	int32_t bits = source.size * 8;
	const wchar_t* macro = nullptr;

	if ( node.kind != op::CAST ) {

		if ( source.size == 8 && amount == 32 )
			macro = L"HIDWORD";

	} else if ( width == 4 && source.size == 8 && amount == 32 )
		macro = L"HIDWORD";
	else if ( width == 2 && amount == bits - 16 && source.size >= 4 )
		macro = L"HIWORD";
	else if ( width == 1 && amount == bits - 8 && source.size >= 2 )
		macro = L"HIBYTE";
	else if ( width == 1 ) {

		switch ( amount ) {

			case 8: macro = L"BYTE1"; break;
			case 16: macro = L"BYTE2"; break;
			case 24: macro = L"BYTE3"; break;
			case 32: macro = L"BYTE4"; break;
			default: break;

		}

	}

	if ( !macro )
		return false;

	out.put ( macro, syntax::FUNCTION );
	out.put ( L"(" );
	value ( shift.a, 0 );
	out.put ( L")" );

	return true;

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
void decompiler::emitter::value ( int32_t index, int32_t parent, bool hex_hint ) {

	if ( index < 0 ) {

		out.put ( L"?" );

		return;

	}

	bool saved = in_logic;

	if ( in_logic )
		truth = true;

	const expr& node = b.exprs [ static_cast < size_t > ( index ) ];

	if ( part_macros && part_macro ( index ) ) {

		in_logic = saved;

		return;

	}

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
					out.put ( base::writer ( ).add ( item.size == 16 ? L"xmmword_" : ( item.size == 8 ? ( ( item.bits & FLOATING ) ? L"dbl_" : L"qword_" ) : L"unk_" ) ).hex ( static_cast < uint64_t > ( item.value ) ).text ( ), syntax::GLOBAL, static_cast < uint64_t > ( item.value ) );
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
				bool global_slot = callee_node.kind == op::LOAD && callee_node.size == 8 && callee_node.a >= 0 && b.exprs [ static_cast < size_t > ( callee_node.a ) ].kind == op::CONST && ( b.exprs [ static_cast < size_t > ( callee_node.a ) ].bits & POINTER );

				if ( bare_callees && ( callee_node.kind == op::VAR || callee_node.kind == op::GLOBAL || global_slot ) ) {

					value ( item.a, 15 );

				} else if ( callee_node.kind == op::LOAD && callee_node.size == 8 && callee_node.a >= 0 && b.long_mode ) {

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

				} else if ( b.long_mode && !no_casts && target && passed.kind == op::VAR && passed.value >= 0 && passed.value < element.count ( ) && element [ static_cast < size_t > ( passed.value ) ] && ( callee_unsigned ( target, i ), !shape_ok ) && !( i < 4 && b.db && ( callee_pointer_shape ( *b.db, b.names, target, i, 0 ) & 0x9F ) == ( element [ static_cast < size_t > ( passed.value ) ] & 0x9F ) ) && !( pointer_arg_casts && derived_same ( target, i, static_cast < int32_t > ( passed.value ) ) ) ) {

					out.put ( L"(__int64)" );
					value ( argument, 14 );

				} else if ( pointer_arg_casts && b.long_mode && !no_casts && target && pointer_arg_cast ( target, i, argument, param_type, 64 ) ) {

					out.put ( L"(" );
					out.put ( param_type, syntax::TYPE );
					out.put ( L")" );
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

			if ( simd_converts && !( item.bits & FLOATING ) && item.size == 8 && inner.size == 16 && ( inner.kind == op::INTRINSIC || inner.kind == op::VAR ) ) {

				value ( item.a, 15 );
				out.put ( L".m128i_u64[0]" );

				break;

			}

			if ( simd_converts && !( item.bits & FLOATING ) && ( item.size == 4 || item.size == 8 ) && inner.size == 16 && ( inner.kind == op::INTRINSIC || inner.kind == op::VAR ) ) {

				out.put ( item.size == 8 ? L"_mm_cvtsi128_si64" : L"_mm_cvtsi128_si32", syntax::FUNCTION );
				out.put ( L"(" );
				value ( item.a, 0 );
				out.put ( L")" );

				break;

			}

			if ( simd_converts && ( item.bits & POINTER ) && ( item.bits & FLOATING ) && outer_kind == op::INTRINSIC && !( inner.bits & FLOATING ) && ( inner.size == 4 || inner.size == 8 ) ) {

				out.put ( inner.size == 8 ? L"_mm_cvtsi64_si128" : L"_mm_cvtsi32_si128", syntax::FUNCTION );
				out.put ( L"(" );
				value ( item.a, 0 );
				out.put ( L")" );

				break;

			}

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

			int32_t truncated = item.a;

			while ( round_trips && truncated >= 0 && b.exprs [ static_cast < size_t > ( truncated ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( truncated ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( truncated ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( truncated ) ].a >= 0 && !( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( truncated ) ].a ) ].bits & FLOATING ) )
				truncated = b.exprs [ static_cast < size_t > ( truncated ) ].a;

			if ( shown && round_trips && ( item.bits & FLOATING ) && truncated >= 0 && b.exprs [ static_cast < size_t > ( truncated ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( truncated ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( truncated ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( truncated ) ].a >= 0 && ( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( truncated ) ].a ) ].bits & FLOATING ) ) {

				if ( parent > 14 )
					out.put ( L"(" );

				out.put ( L"(" );
				type_name ( item.size, item.bits, false );
				out.put ( L")" );
				out.put ( L"(int)", syntax::TYPE );
				value ( b.exprs [ static_cast < size_t > ( truncated ) ].a, 14 );

				if ( parent > 14 )
					out.put ( L")" );

				break;

			}

			bool odd_source = inner.kind == op::SHR || inner.kind == op::OR || inner.kind == op::AND || ( inner.kind == op::VAR && inner.value >= 0 && inner.value < b.vars.count ( ) && ( ( b.vars [ static_cast < size_t > ( inner.value ) ].bits & UNSIGNED ) || inner.size == 8 ) && ( inner.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( inner.value ) ].empty ( ) ) );

			if ( shown && int_sources && ( item.bits & FLOATING ) && ( item.size == 4 || item.size == 8 ) && !( inner.bits & FLOATING ) && odd_source && !( inner.kind == op::VAR && inner.value >= 0 && inner.value < element.count ( ) && element [ static_cast < size_t > ( inner.value ) ] ) ) {

				if ( parent > 14 )
					out.put ( L"(" );

				out.put ( L"(" );
				type_name ( item.size, item.bits, false );
				out.put ( L")" );
				out.put ( L"(int)", syntax::TYPE );
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

			if ( void_compares && item.kind == op::MUL && item.size == 8 && !( item.bits & FLOATING ) && item.a >= 0 ) {

				const expr& left_side = b.exprs [ static_cast < size_t > ( item.a ) ];
				int32_t scaled = left_side.kind == op::CONST ? item.b : ( right.kind == op::CONST ? item.a : -1 );
				const expr& factor = left_side.kind == op::CONST ? left_side : right;
				const expr* held_var = scaled >= 0 ? &b.exprs [ static_cast < size_t > ( scaled ) ] : nullptr;

				if ( held_var && held_var->kind == op::VAR && held_var->value >= 0 && held_var->value < element.count ( ) && element [ static_cast < size_t > ( held_var->value ) ] == 0x10 && b.vars [ static_cast < size_t > ( held_var->value ) ].kind == var_kind::PARAMETER ) {

					operand = false;
					number ( factor.value, 8, !simple_constant ( factor.value ), false );
					operand = true;
					out.put ( L" * ", syntax::OPERATOR );
					out.put ( L"(_QWORD)", syntax::TYPE );
					value ( scaled, 14 );

					break;

				}

			}

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
			bool offset_unsigned = false;

			if ( cast_hex && !lhs_unsigned && ( lhs.kind == op::ADD || lhs.kind == op::SUB ) && lhs.a >= 0 && lhs.b >= 0 && b.exprs [ static_cast < size_t > ( lhs.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( lhs.a ) ].kind == op::VAR ) {

				int64_t inner = b.exprs [ static_cast < size_t > ( lhs.a ) ].value;

				if ( inner >= 0 && inner < b.vars.count ( ) && b.vars [ static_cast < size_t > ( inner ) ].size == lhs.size && ( b.vars [ static_cast < size_t > ( inner ) ].bits & UNSIGNED ) )
					lhs_unsigned = offset_unsigned = true;

			}
			bool cast_lhs = unsigned_compare && !lhs_unsigned && !no_casts && right.kind == op::CONST && lhs.size <= 8;

		if ( unsigned_sum_compares && unsigned_compare && !lhs_unsigned && !no_casts && !cast_lhs && lhs.size == 8 && !( lhs.bits & FLOATING ) && ( lhs.kind == op::ADD || lhs.kind == op::SUB ) && right.kind == op::LOAD )
			cast_lhs = true;

		if ( unsigned_sum_compares && unsigned_compare && offset_unsigned && !no_casts && lhs.size == 8 && right.kind == op::CONST && b.exprs [ static_cast < size_t > ( lhs.a ) ].value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( lhs.a ) ].value ) ].empty ( ) ) {

			cast_lhs = true;
			offset_unsigned = false;

		}

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

				if ( cast_bounds && lhs.size == 4 )
					unsigned_compare = false;

			} else if ( signed_loads && signed_compare && lhs.kind == op::LOAD && !( lhs.bits & ( FLOATING | SIGNED ) ) && ( lhs.size == 1 || lhs.size == 2 || lhs.size == 4 || lhs.size == 8 ) && right.kind == op::CONST ) {

				expr shown_load = lhs;

				shown_load.bits = static_cast < uint8_t > ( shown_load.bits | SIGNED );
				lvalue_or_load ( shown_load, mine );

			} else if ( signed_compare && !no_casts && lhs.kind == op::CAST && lhs.a >= 0 && lhs.size < b.exprs [ static_cast < size_t > ( lhs.a ) ].size && lhs.size <= 4 && !( lhs.bits & FLOATING ) && b.exprs [ static_cast < size_t > ( lhs.a ) ].kind == op::VAR ) {

				out.put ( lhs.size == 1 ? L"(char)" : ( lhs.size == 2 ? L"(__int16)" : L"(int)" ), syntax::TYPE );
				value ( lhs.a, 14, bitwise );

			} else
				value ( item.a, item.kind == op::SHL || item.kind == op::SHR ? mine + 1 : mine, bitwise );

			bool hex_bound = cast_hex && ( cast_lhs || offset_unsigned ) && compare && item.kind != op::EQ && item.kind != op::NE;

			if ( hex_bound )
				unsigned_compare = false;

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

			} else if ( signed_rights && signed_compare && !no_casts && right.kind == op::CAST && right.a >= 0 && right.size < b.exprs [ static_cast < size_t > ( right.a ) ].size && right.size <= 4 && !( right.bits & FLOATING ) && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR ) {

				out.put ( right.size == 1 ? L"(char)" : ( right.size == 2 ? L"(__int16)" : L"(int)" ), syntax::TYPE );
				value ( right.a, 14 );

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
int32_t decompiler::emitter::strip ( int32_t e ) const {

	while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
		e = b.exprs [ static_cast < size_t > ( e ) ].a;

	return e;

}