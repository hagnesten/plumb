#include "decompiler/emit/calltypes/calltypes.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

bool decompiler::pointee_of ( const wchar_t* type, wchar_t* out, int32_t room ) {

	static const wchar_t* const pairs [ ] [ 2 ] = {
		{ L"LPDWORD", L"DWORD" }, { L"PDWORD", L"DWORD" }, { L"LPWORD", L"WORD" }, { L"PWORD", L"WORD" }, { L"LPLONG", L"LONG" }, { L"PLONG", L"LONG" },
		{ L"PULONG", L"ULONG" }, { L"PUINT", L"UINT" }, { L"LPBOOL", L"BOOL" }, { L"PBOOL", L"BOOL" }, { L"PSIZE_T", L"SIZE_T" }, { L"PULONG_PTR", L"ULONG_PTR" },
		{ L"PDWORD_PTR", L"DWORD_PTR" }, { L"PDWORD64", L"DWORD64" }, { L"LPHANDLE", L"HANDLE" }, { L"PHANDLE", L"HANDLE" }, { L"PHKEY", L"HKEY" },
		{ L"PLARGE_INTEGER", L"LARGE_INTEGER" }, { L"PULARGE_INTEGER", L"ULARGE_INTEGER" }, { L"LPFILETIME", L"FILETIME" }, { L"PFILETIME", L"FILETIME" },
		{ L"PLUID", L"LUID" }, { L"PREGHANDLE", L"REGHANDLE" },
	};

	for ( const auto& pair : pairs ) {

		if ( base::same_text ( pair [ 0 ], type ) ) {

			int32_t k = 0;

			for ( ; pair [ 1 ] [ k ] && k < room - 1; k++ )
				out [ k ] = pair [ 1 ] [ k ];

			out [ k ] = 0;

			return true;

		}

	}

	int32_t length = 0;

	while ( type [ length ] )
		length++;

	while ( length && type [ length - 1 ] == L' ' )
		length--;

	if ( !length || type [ length - 1 ] != L'*' )
		return false;

	length--;

	while ( length && type [ length - 1 ] == L' ' )
		length--;

	if ( !length || ( type [ length - 1 ] == L'*' && !double_pointees ) || length >= room )
		return false;

	int32_t start = 0;

	if ( length > 6 && type [ 0 ] == L'c' && type [ 1 ] == L'o' && type [ 2 ] == L'n' && type [ 3 ] == L's' && type [ 4 ] == L't' && type [ 5 ] == L' ' )
		start = 6;

	int32_t k = 0;

	for ( int32_t i = start; i < length; i++ )
		out [ k++ ] = type [ i ];

	out [ k ] = 0;

	return k > 0 && !base::same_text ( out, L"void" ) && !base::same_text ( out, L"void *" ) && !base::same_text ( out, L"void*" );

}

void decompiler::infer_call_types ( builder& b, base::list < base::text >& out ) {

	out.clear ( );
	out.resize ( b.vars.size ( ) );

	if ( !b.names )
		return;

	uint64_t cached_target = 0;
	bool cached_ok = false;
	bool cached_derived = false;
	c_prototype* shape = new c_prototype ( );

	auto load = [ & ] ( uint64_t target ) {

		if ( target == cached_target )
			return cached_ok;

		cached_target = target;
		cached_ok = false;
		cached_derived = false;

		const wchar_t* declared = target ? b.names->prototype ( target ) : nullptr;
		base::writer problem;
		wchar_t callee [ 200 ];

		if ( !declared && api_types && target && b.names->name ( target, true, callee, 200 ) )
			declared = api_prototype ( callee );

		if ( !declared && derived_types && target ) {

			declared = b.names->derived ( target );
			cached_derived = declared != nullptr;

		}

		if ( declared && parse_prototype ( declared, b.long_mode, *shape, problem ) )
			cached_ok = true;

		return cached_ok;

	};

	auto offer = [ & ] ( int32_t v, const wchar_t* type, bool slot = false ) {

		if ( v < 0 || v >= b.vars.count ( ) || !type || !type [ 0 ] || !out [ static_cast < size_t > ( v ) ].empty ( ) )
			return;

		c_type parsed;
		wchar_t clean [ 64 ];

		if ( !parse_type ( type, b.long_mode, parsed, clean, 64 ) || parsed.empty || !parsed.size )
			return;

		bool smaller_slot = slot && ida_typedefs && b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::STACK && parsed.size < b.vars [ static_cast < size_t > ( v ) ].size && parsed.size >= 4;

		if ( parsed.size != b.vars [ static_cast < size_t > ( v ) ].size && !smaller_slot )
			return;

		out [ static_cast < size_t > ( v ) ].set ( clean );

	};

	for ( const statement& item : b.statements ) {

		if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 )
			continue;

		const expr& left = b.exprs [ static_cast < size_t > ( item.left ) ];
		const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

		if ( left.kind != op::VAR || right.kind != op::CALL || !load ( static_cast < uint64_t > ( right.value ) ) || cached_derived )
			continue;

		if ( !shape->result_type.empty )
			offer ( static_cast < int32_t > ( left.value ), shape->result );

	}

	for ( const expr& item : b.exprs ) {

		if ( item.kind != op::CALL || !item.value || !load ( static_cast < uint64_t > ( item.value ) ) )
			continue;

		for ( int32_t i = 0; i < item.arg_count && i < shape->count; i++ ) {

			int32_t index = b.args [ static_cast < size_t > ( item.first_arg + i ) ];

			while ( api_types && index >= 0 && b.exprs [ static_cast < size_t > ( index ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( index ) ].a >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( index ) ].a ) ].kind == op::ADDRESS )
				index = b.exprs [ static_cast < size_t > ( index ) ].a;

			if ( index < 0 )
				continue;

			const expr& passed = b.exprs [ static_cast < size_t > ( index ) ];

			if ( cached_derived && ( passed.kind != op::VAR || b.vars [ static_cast < size_t > ( passed.value ) ].kind != var_kind::PARAMETER || !( base::same_text ( shape->types [ i ], L"unsigned __int64" ) || base::same_text ( shape->types [ i ], L"unsigned int" ) || base::same_text ( shape->types [ i ], L"unsigned __int16" ) || base::same_text ( shape->types [ i ], L"unsigned __int8" ) ) ) )
				continue;

			if ( passed.kind == op::VAR ) {

				bool constants_only = b.vars [ static_cast < size_t > ( passed.value ) ].kind != var_kind::PARAMETER;
				bool written = false;

				for ( const statement& line : b.statements ) {

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( line.left ) ].value != passed.value )
						continue;

					written = true;

					if ( b.exprs [ static_cast < size_t > ( line.right ) ].kind != op::CONST )
						constants_only = false;

				}

				if ( !( constants_only && written ) )
					offer ( static_cast < int32_t > ( passed.value ), shape->types [ i ] );

			}
			else if ( passed.kind == op::ADDRESS && ( passed.a < 0 || ( api_types && b.exprs [ static_cast < size_t > ( passed.a ) ].kind == op::VAR ) ) ) {

				wchar_t inner [ 64 ];
				int64_t target_var = passed.a < 0 ? passed.value : b.exprs [ static_cast < size_t > ( passed.a ) ].value;

				if ( pointee_of ( shape->types [ i ], inner, 64 ) )
					offer ( static_cast < int32_t > ( target_var ), inner, true );

			}

		}

	}

	delete shape;

	if ( intrinsic_types ) {

		for ( const expr& item : b.exprs ) {

			if ( item.kind != op::INTRINSIC || item.arg_count != 3 )
				continue;

			const wchar_t* called = intrinsic_name ( static_cast < int32_t > ( item.value ) );
			bool fill = base::same_text ( called, L"memset" );

			if ( !fill && !base::same_text ( called, L"qmemcpy" ) )
				continue;

			const wchar_t* types [ 3 ] = { L"void *", fill ? L"char" : L"const void *", L"unsigned __int64" };

			for ( int32_t i = 0; i < 3; i++ ) {

				int32_t index = b.args [ static_cast < size_t > ( item.first_arg + i ) ];

				while ( index >= 0 && b.exprs [ static_cast < size_t > ( index ) ].kind == op::CAST )
					index = b.exprs [ static_cast < size_t > ( index ) ].a;

				if ( index < 0 || b.exprs [ static_cast < size_t > ( index ) ].kind != op::VAR )
					continue;

				int32_t v = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( index ) ].value );

				if ( v < 0 || v >= b.vars.count ( ) || b.vars [ static_cast < size_t > ( v ) ].kind != var_kind::PARAMETER || !out [ static_cast < size_t > ( v ) ].empty ( ) )
					continue;

				if ( i == 1 && fill && b.vars [ static_cast < size_t > ( v ) ].size >= 1 ) {

					out [ static_cast < size_t > ( v ) ].set ( L"char" );

					continue;

				}

				offer ( v, types [ i ] );

			}

		}

	}

	for ( int32_t round = 0; round < 4; round++ ) {

		bool moved = false;

		for ( const statement& item : b.statements ) {

			if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 )
				continue;

			const expr& left = b.exprs [ static_cast < size_t > ( item.left ) ];
			const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

			if ( left.kind != op::VAR || right.kind != op::VAR )
				continue;

			int32_t into = static_cast < int32_t > ( left.value );
			int32_t from = static_cast < int32_t > ( right.value );

			if ( into < 0 || from < 0 || into >= out.count ( ) || from >= out.count ( ) )
				continue;

			if ( out [ static_cast < size_t > ( into ) ].empty ( ) && !out [ static_cast < size_t > ( from ) ].empty ( ) && b.vars [ static_cast < size_t > ( into ) ].size == b.vars [ static_cast < size_t > ( from ) ].size ) {

				out [ static_cast < size_t > ( into ) ].set ( out [ static_cast < size_t > ( from ) ].get ( ) );
				moved = true;

			} else if ( out [ static_cast < size_t > ( from ) ].empty ( ) && !out [ static_cast < size_t > ( into ) ].empty ( ) && b.vars [ static_cast < size_t > ( into ) ].size == b.vars [ static_cast < size_t > ( from ) ].size ) {

				out [ static_cast < size_t > ( from ) ].set ( out [ static_cast < size_t > ( into ) ].get ( ) );
				moved = true;

			}

		}

		if ( !moved )
			break;

	}

}

const wchar_t* decompiler::returned_pointer ( const builder& b ) {

	const wchar_t* found = nullptr;

	for ( const block_code& item : b.code ) {

		if ( !item.reachable || item.end != term::RETURN )
			continue;

		if ( item.returned < 0 )
			return nullptr;

		const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];

		if ( back.kind != op::VAR || back.value < 0 || back.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( back.value ) ].empty ( ) )
			return nullptr;

		const wchar_t* text = b.var_types [ static_cast < size_t > ( back.value ) ].get ( );
		bool star = false;

		for ( const wchar_t* p = text; *p; p++ )
			star = star || *p == L'*';

		if ( !star || ( found && !base::same_text ( found, text ) ) )
			return nullptr;

		found = text;

	}

	return found;

}

bool decompiler::unsigned_result ( const builder& b ) {

	int32_t seen = 0;

	for ( const block_code& item : b.code ) {

		if ( !item.reachable || item.end != term::RETURN || item.returned < 0 )
			continue;

		const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];
		bool wide_constant = back.kind == op::CONST && back.size == 8 && back.value < 0 && back.value < -0x100000000ll;
		bool marked = back.kind != op::CONST && ( back.bits & UNSIGNED ) && !( back.bits & SIGNED ) && back.size == 8;

		if ( back.kind == op::VAR && back.value >= 0 && back.value < b.vars.count ( ) )
			marked = ( b.vars [ static_cast < size_t > ( back.value ) ].bits & UNSIGNED ) && b.vars [ static_cast < size_t > ( back.value ) ].size == 8;

		if ( back.kind == op::CONST && !wide_constant && back.value >= 0 && back.value < 0x100000000ll )
			continue;

		if ( !marked && !wide_constant )
			return false;

		seen++;

	}

	return seen > 0;

}