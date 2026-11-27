#include "decompiler/lift/sink_tests/sink_tests.h"
#include "decompiler/lift/expressions/expressions.h"
#include "decompiler/lift/availability/availability.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/yields/yields.h"
#include "decompiler/apis/apis.h"
#include "base/strings/strings.h"

void decompiler::builder::sink_into_tests ( ) {

	base::list < int32_t > uses;

	uses.fill ( vars.size ( ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::NOP || line.kind == stmt::ASM )
			continue;

		bool defines = line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR;

		if ( !defines )
			count_vars ( *this, line.left, uses, 0 );

		count_vars ( *this, line.right, uses, 0 );

	}

	for ( const block_code& item : code ) {

		count_vars ( *this, item.condition, uses, 0 );
		count_vars ( *this, item.selector, uses, 0 );
		count_vars ( *this, item.returned, uses, 0 );

	}

	base::list < int32_t > writes;

	writes.fill ( vars.size ( ), 0 );

	for ( const statement& line : statements ) {

		if ( line.kind == stmt::ASSIGN && line.left >= 0 && exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

			int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

			if ( v >= 0 && v < writes.count ( ) )
				writes [ static_cast < size_t > ( v ) ]++;

		}

	}

	for ( block_code& item : code ) {

		bool test = item.end == term::BRANCH && item.condition >= 0;
		bool back = item.end == term::RETURN && item.returned >= 0;

		if ( !test && !back )
			continue;

		int32_t last = -1;

		for ( int32_t s = item.first; s < item.first + item.count && s < statements.count ( ); s++ ) {

			if ( statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
				last = s;

		}

		if ( last < 0 )
			continue;

		int32_t& wanted = test ? item.condition : item.returned;
		base::list < int64_t > passed;

		for ( int32_t s = last; s >= item.first && sink_past_moves; s-- ) {

			const statement& other = statements [ static_cast < size_t > ( s ) ];

			if ( other.kind == stmt::NOP )
				continue;

			if ( other.kind != stmt::ASSIGN || other.left < 0 || other.right < 0 || exprs [ static_cast < size_t > ( other.left ) ].kind != op::VAR ) {

				last = s;

				break;

			}

			int64_t held = exprs [ static_cast < size_t > ( other.left ) ].value;

			if ( held >= 0 && held < vars.count ( ) && uses [ static_cast < size_t > ( held ) ] == 1 && writes [ static_cast < size_t > ( held ) ] == 1 && mentions ( *this, wanted, held, 0 ) ) {

				last = s;

				break;

			}

			if ( !register_only ( *this, other.right, 0 ) ) {

				last = s;

				break;

			}

			passed.push ( held );
			last = s - 1;

		}

		if ( last < item.first )
			continue;

		statement& line = statements [ static_cast < size_t > ( last ) ];

		if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
			continue;

		bool blocked = false;

		for ( int64_t held : passed )
			blocked = blocked || mentions ( *this, line.right, held, 0 );

		if ( blocked )
			continue;

		int64_t v = exprs [ static_cast < size_t > ( line.left ) ].value;

		if ( v < 0 || v >= vars.count ( ) || uses [ static_cast < size_t > ( v ) ] != 1 || writes [ static_cast < size_t > ( v ) ] != 1 )
			continue;

		const variable& owner = vars [ static_cast < size_t > ( v ) ];

		if ( owner.kind != var_kind::REGISTER || owner.cookie || owner.saved || ( owner.bits & FLOATING ) )
			continue;

		if ( back && exprs [ static_cast < size_t > ( line.right ) ].kind != op::CALL )
			continue;

		int32_t& slot = test ? item.condition : item.returned;
		int32_t made = substitute ( *this, slot, v, line.right, 0 );

		if ( made == slot )
			continue;

		slot = made;
		line.kind = stmt::NOP;

		if ( ghost_sinks && !owner.flags && exprs [ static_cast < size_t > ( line.right ) ].kind == op::CALL ) {

			vars [ static_cast < size_t > ( v ) ].ghost = true;
			vars [ static_cast < size_t > ( v ) ].born = line.origin ? line.origin : line.address;

		}

	}

}