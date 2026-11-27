#include "decompiler/lift/signs/signs.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::note_signs ( const builder& b, int32_t e, base::list < uint8_t >& marks, int32_t depth ) {

	if ( e < 0 || depth > 64 )
		return;

	const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
	bool ordered = node.kind == op::LT || node.kind == op::LE || node.kind == op::GT || node.kind == op::GE;

	if ( sign_ops && ( node.kind == op::SHR || node.kind == op::DIV || node.kind == op::MOD ) && ( node.bits & ( SIGNED | UNSIGNED ) ) && !( node.bits & FLOATING ) && node.a >= 0 ) {

		const expr& operand = b.exprs [ static_cast < size_t > ( node.a ) ];

		if ( operand.kind == op::VAR && operand.value >= 0 && operand.value < marks.count ( ) && b.vars [ static_cast < size_t > ( operand.value ) ].size == operand.size && operand.size == 8 )
			marks [ static_cast < size_t > ( operand.value ) ] |= ( node.bits & UNSIGNED ) ? 1 : 2;

	}

	if ( ordered && ( node.bits & ( SIGNED | UNSIGNED ) ) ) {

		const int32_t sides [ 2 ] = { node.a, node.b };

		for ( int32_t side : sides ) {

			if ( side < 0 )
				continue;

			const expr& operand = b.exprs [ static_cast < size_t > ( side ) ];

			if ( operand.kind != op::VAR || operand.value < 0 || operand.value >= marks.count ( ) )
				continue;

			if ( b.vars [ static_cast < size_t > ( operand.value ) ].size != operand.size )
				continue;

			marks [ static_cast < size_t > ( operand.value ) ] |= ( node.bits & UNSIGNED ) ? 1 : 2;

		}

	}

	note_signs ( b, node.a, marks, depth + 1 );
	note_signs ( b, node.b, marks, depth + 1 );
	note_signs ( b, node.c, marks, depth + 1 );

	for ( int32_t i = 0; i < node.arg_count; i++ )
		note_signs ( b, b.args [ static_cast < size_t > ( node.first_arg + i ) ], marks, depth + 1 );

}
void decompiler::builder::infer_signs ( ) {

	base::list < uint8_t > marks;

	marks.fill ( vars.size ( ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		note_signs ( *this, line.left, marks, 0 );
		note_signs ( *this, line.right, marks, 0 );

	}

	for ( const block_code& item : code ) {

		if ( !item.reachable )
			continue;

		note_signs ( *this, item.condition, marks, 0 );
		note_signs ( *this, item.returned, marks, 0 );

		if ( widened_returns && long_mode && returns_value && !returns_float && !has_prototype && result_width == 0 && result_text.empty ( ) && item.returned >= 0 ) {

			int32_t back = item.returned;

			while ( back >= 0 && exprs [ static_cast < size_t > ( back ) ].kind == op::CAST )
				back = exprs [ static_cast < size_t > ( back ) ].a;

			if ( back >= 0 && exprs [ static_cast < size_t > ( back ) ].kind == op::VAR ) {

				int64_t v = exprs [ static_cast < size_t > ( back ) ].value;

				if ( v >= 0 && v < marks.count ( ) && vars [ static_cast < size_t > ( v ) ].size == 4 && vars [ static_cast < size_t > ( v ) ].kind == var_kind::REGISTER )
					marks [ static_cast < size_t > ( v ) ] |= 1;

			}

		}

	}

	for ( int32_t round = 0; sign_copies && round < 6; round++ ) {

		bool changed = false;

		for ( const statement& line : statements ) {

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
				continue;

			const expr& left = exprs [ static_cast < size_t > ( line.left ) ];
			const expr* right_node = &exprs [ static_cast < size_t > ( line.right ) ];
			bool masked = false;

			while ( masked_casts && right_node->kind == op::CAST && !( right_node->bits & SIGNED ) && right_node->a >= 0 )
				right_node = &exprs [ static_cast < size_t > ( right_node->a ) ];

			if ( masked_signs && ( right_node->kind == op::AND || ( right_node->kind == op::SHR && !( right_node->bits & SIGNED ) ) ) && right_node->a >= 0 && right_node->b >= 0 && exprs [ static_cast < size_t > ( right_node->b ) ].kind == op::CONST ) {

				const expr* inner = &exprs [ static_cast < size_t > ( right_node->a ) ];

				while ( masked_casts && inner->kind == op::CAST && !( inner->bits & SIGNED ) && inner->a >= 0 )
					inner = &exprs [ static_cast < size_t > ( inner->a ) ];

				if ( inner->kind == op::VAR ) {

					right_node = inner;
					masked = true;

				}

			}

			const expr& right = *right_node;

			if ( left.kind != op::VAR || right.kind != op::VAR || left.size != 8 || ( right.size != 8 && !( masked && masked_casts ) ) || left.value < 0 || right.value < 0 || left.value >= marks.count ( ) || right.value >= marks.count ( ) )
				continue;

			uint8_t from = marks [ static_cast < size_t > ( right.value ) ];

			if ( masked_signs && !from && ( vars [ static_cast < size_t > ( right.value ) ].bits & UNSIGNED ) && !( vars [ static_cast < size_t > ( right.value ) ].bits & FLOATING ) )
				from = 1;
			uint8_t& to = marks [ static_cast < size_t > ( left.value ) ];

			if ( from == 1 && to == 0 ) {

				to = 1;
				changed = true;

			}

		}

		if ( !changed )
			break;

	}

	for ( int32_t v = 0; v < vars.count ( ); v++ ) {

		variable& item = vars [ static_cast < size_t > ( v ) ];

		if ( marks [ static_cast < size_t > ( v ) ] == 1 && !( item.bits & FLOATING ) && ( sign_pointers || !( item.bits & POINTER ) ) )
			item.bits = static_cast < uint8_t > ( ( item.bits & ~SIGNED ) | UNSIGNED );

	}

}