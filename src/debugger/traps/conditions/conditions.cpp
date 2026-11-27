#include "debugger/traps/conditions/conditions.h"
#include "debugger/core/state/state.h"
#include "base/strings/strings.h"
#include <windows.h>

bool debugger::condition_holds ( uint64_t address, HANDLE thread ) {

	base::text text;

	AcquireSRWLockShared ( &lock );

	for ( const condition_entry& item : conditions ) {

		if ( image_base + item.rva == address )
			text.set ( item.text.get ( ) );

	}

	ReleaseSRWLockShared ( &lock );

	if ( text.empty ( ) )
		return true;

	uint64_t registers [ 18 ] = { };
	bool valid = false;

	get_registers ( thread, registers );

	bool result = test_condition ( text.get ( ), registers, wide_target, valid );

	return !valid || result;

}

void debugger::set_condition ( uint64_t rva, const wchar_t* text ) {

	AcquireSRWLockExclusive ( &lock );

	for ( int32_t i = conditions.count ( ) - 1; i >= 0; i-- ) {

		if ( conditions [ static_cast < size_t > ( i ) ].rva == rva )
			conditions.remove_at ( static_cast < size_t > ( i ) );

	}

	if ( text && text [ 0 ] ) {

		condition_entry& item = conditions.emplace ( );

		item.rva = rva;
		item.text.set ( text );

	}

	ReleaseSRWLockExclusive ( &lock );

}

bool debugger::parse_value ( const wchar_t*& at, uint64_t& value ) {

	bool hex = at [ 0 ] == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' );
	bool any = false;

	if ( hex )
		at += 2;

	value = 0;

	while ( *at ) {

		wchar_t c = *at;
		uint32_t digit = 0;

		if ( c >= L'0' && c <= L'9' )
			digit = static_cast < uint32_t > ( c - L'0' );
		else if ( hex && c >= L'a' && c <= L'f' )
			digit = static_cast < uint32_t > ( c - L'a' + 10 );
		else if ( hex && c >= L'A' && c <= L'F' )
			digit = static_cast < uint32_t > ( c - L'A' + 10 );
		else
			break;

		value = value * ( hex ? 16 : 10 ) + digit;
		any = true;
		at++;

	}

	return any;

}

bool debugger::test_condition ( const wchar_t* text, const uint64_t* registers, bool wide, bool& valid ) {

	const wchar_t* at = text;
	wchar_t name [ 16 ];
	int32_t length = 0;
	uint64_t left = 0;
	uint64_t right = 0;

	valid = false;

	while ( *at == L' ' )
		at++;

	while ( length < 15 && ( ( *at >= L'a' && *at <= L'z' ) || ( *at >= L'A' && *at <= L'Z' ) || ( *at >= L'0' && *at <= L'9' ) ) )
		name [ length++ ] = static_cast < wchar_t > ( *at >= L'a' && *at <= L'z' ? *at - 32 : *at ), at++;

	name [ length ] = 0;

	int32_t found = -1;

	for ( int32_t i = 0; i < 18; i++ ) {

		if ( base::same_text ( name, register_name ( i, wide ) ) )
			found = i;

	}

	if ( found < 0 )
		return false;

	left = registers [ found ];

	while ( *at == L' ' )
		at++;

	wchar_t op [ 3 ] = { };
	int32_t op_length = 0;

	while ( op_length < 2 && ( *at == L'=' || *at == L'!' || *at == L'<' || *at == L'>' ) )
		op [ op_length++ ] = *at++;

	while ( *at == L' ' )
		at++;

	if ( !op_length || !parse_value ( at, right ) )
		return false;

	valid = true;

	if ( base::same_text ( op, L"==" ) || base::same_text ( op, L"=" ) )
		return left == right;

	if ( base::same_text ( op, L"!=" ) )
		return left != right;

	if ( base::same_text ( op, L"<" ) )
		return left < right;

	if ( base::same_text ( op, L">" ) )
		return left > right;

	if ( base::same_text ( op, L"<=" ) )
		return left <= right;

	if ( base::same_text ( op, L">=" ) )
		return left >= right;

	valid = false;

	return false;

}