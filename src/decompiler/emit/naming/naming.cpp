#include "decompiler/emit/naming/naming.h"
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

	wchar_t own [ 200 ];

	if ( !user && known_names && item.kind == var_kind::PARAMETER && !b.has_prototype && b.names && b.names->name ( b.function, true, own, 200 ) )
		user = known_parameter ( own, item.position );

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
void decompiler::emitter::name_after_type ( ) {

	if ( out.last ( ) != L'*' )
		out.put ( L" " );

}
void decompiler::emitter::name_loop_var ( int32_t var ) {

	if ( var < 0 || var >= loop_letter.count ( ) || loop_letter [ static_cast < size_t > ( var ) ] >= 0 || var == result_var )
		return;

	const variable& item = b.vars [ static_cast < size_t > ( var ) ];

	if ( item.kind == var_kind::PARAMETER || next_letter >= 10 )
		return;

	loop_letter [ static_cast < size_t > ( var ) ] = static_cast < int8_t > ( next_letter++ );

}