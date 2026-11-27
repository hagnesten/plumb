#include "decompiler/emit/statements/statements.h"
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

	if ( !inline_mode )
		printing_line = index;

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

				if ( and_zero && right.kind == op::AND && amount.kind == op::CONST && amount.value == 0 && !( right.bits & FLOATING ) ) {

					if ( left.kind == op::LOAD )
						lvalue_or_load ( left, 0 );
					else
						value ( item.left, 0 );

					out.put ( L" = ", syntax::OPERATOR );
					number ( 0, left.size, false, false );
					semicolon ( );

					break;

				}

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

			if ( left.kind == op::LOAD && ( left.bits & FLOATING ) && left.size == 16 && ( right.kind == op::CONST || ( plain_stores && plain_copies && right.kind == op::VAR && right.value >= 0 && right.value < vector.count ( ) && vector [ static_cast < size_t > ( right.value ) ] == 1 ) ) ) {

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

	if ( inline_returns && ( target.end == term::RETURN || ( tail_returns && target.end == term::TAIL && target.returned >= 0 ) ) && empty_block ( block ) && ( target.returned < 0 || b.exprs [ static_cast < size_t > ( target.returned ) ].kind == op::CONST || b.exprs [ static_cast < size_t > ( target.returned ) ].kind == op::VAR || ( tail_returns && b.exprs [ static_cast < size_t > ( target.returned ) ].depth <= 6 ) ) ) {

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