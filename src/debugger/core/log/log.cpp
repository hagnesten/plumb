#include "debugger/core/log/log.h"
#include "debugger/core/state/state.h"
#include "debugger/target/memory/memory.h"
#include <windows.h>

void debugger::log_text ( const wchar_t* text ) {

	AcquireSRWLockExclusive ( &lock );

	if ( messages.count ( ) < 500 )
		messages.push ( base::text ( text ) );

	ReleaseSRWLockExclusive ( &lock );

}

void debugger::append_number ( wchar_t* note, int32_t& at, uint64_t value, bool hex ) {

	static const wchar_t digits [ ] = L"0123456789ABCDEF";
	wchar_t reverse [ 24 ];
	int32_t n = 0;
	uint64_t radix = hex ? 16 : 10;

	do {

		reverse [ n++ ] = digits [ value % radix ];
		value /= radix;

	} while ( value && n < 23 );

	while ( n && at < 190 )
		note [ at++ ] = reverse [ --n ];

	note [ at ] = 0;

}

void debugger::append_text ( wchar_t* note, int32_t& at, const wchar_t* text ) {

	for ( ; *text && at < 190; text++ )
		note [ at++ ] = *text;

	note [ at ] = 0;

}

void debugger::read_debug_string ( const OUTPUT_DEBUG_STRING_INFO& info ) {

	uint32_t length = base::min ( static_cast < uint32_t > ( info.nDebugStringLength ), 400u );
	wchar_t text [ 420 ] = L"debug: ";
	int32_t at = 7;

	if ( info.fUnicode ) {

		wchar_t buffer [ 401 ] = { };

		read_memory ( reinterpret_cast < uint64_t > ( info.lpDebugStringData ), buffer, length * 2 );

		for ( uint32_t i = 0; i < length && buffer [ i ]; i++ )
			text [ at++ ] = buffer [ i ] == L'\n' || buffer [ i ] == L'\r' ? L' ' : buffer [ i ];

	} else {

		char buffer [ 401 ] = { };

		read_memory ( reinterpret_cast < uint64_t > ( info.lpDebugStringData ), buffer, length );

		for ( uint32_t i = 0; i < length && buffer [ i ]; i++ )
			text [ at++ ] = buffer [ i ] == '\n' || buffer [ i ] == '\r' ? L' ' : static_cast < wchar_t > ( static_cast < uint8_t > ( buffer [ i ] ) );

	}

	while ( at > 7 && text [ at - 1 ] == L' ' )
		at--;

	text [ at ] = 0;

	if ( at > 7 )
		log_text ( text );

}

bool debugger::take_log ( wchar_t* out, int32_t room ) {

	AcquireSRWLockExclusive ( &lock );

	bool found = !messages.empty ( );

	if ( found ) {

		const wchar_t* text = messages [ 0 ].get ( );
		int32_t i = 0;

		for ( ; text [ i ] && i < room - 1; i++ )
			out [ i ] = text [ i ];

		out [ i ] = 0;
		messages.remove_at ( 0 );

	}

	ReleaseSRWLockExclusive ( &lock );

	return found;

}