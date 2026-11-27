#include "decompiler/emit/sink/sink.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

wchar_t decompiler::sink::last ( int32_t back ) const {

	return r->text.count ( ) - back >= current.text ? r->text [ static_cast < size_t > ( r->text.count ( ) - back ) ] : 0;

}

void decompiler::sink::begin ( uint64_t address ) {

	terminal = -1;
	current = line ( );
	current.text = r->text.count ( );
	current.first_span = r->spans.count ( );
	current.indent = indent;
	current.address = address;
	open = true;

}

void decompiler::sink::put ( const wchar_t* text, syntax kind, uint64_t target ) {

	if ( !open )
		begin ( 0 );

	bool structural = kind == syntax::PLAIN || kind == syntax::OPERATOR;

	if ( spaced && !structural && kind != syntax::COMMENT && *text && last ( ) == L')' )
		r->text.push ( L' ' );

	int32_t start = r->text.count ( );

	for ( ; *text; text++ ) {

		wchar_t c = *text;

		if ( spaced && structural && ( c == L'(' || c == L'[' ) ) {

			wchar_t before = last ( );

			if ( before && before != L' ' && before != L'(' && before != L'[' && before != L'!' && before != L'~' && before != L'*' && before != L'&' && before != L'-' )
				r->text.push ( L' ' );

			r->text.push ( c );
			r->text.push ( L' ' );

			continue;

		}

		if ( spaced && structural && ( c == L')' || c == L']' ) ) {

			if ( last ( ) != L' ' )
				r->text.push ( L' ' );

			r->text.push ( c );

			continue;

		}

		r->text.push ( c );

	}

	if ( kind != syntax::PLAIN && r->text.count ( ) > start ) {

		span& item = r->spans.emplace ( );

		item.start = start;
		item.length = r->text.count ( ) - start;
		item.kind = kind;
		item.target = target;

	}

}

void decompiler::sink::wrap ( ) {

	int32_t start = current.text;
	int32_t length = r->text.count ( ) - start;

	if ( spaced || length < 4 )
		return;

	const wchar_t* text = r->text.data ( ) + start;
	int32_t width = current.indent * 2 + length;

	for ( int32_t i = 0; i + 2 < length; i++ ) {

		bool edge = i == 0 || !( ( text [ i - 1 ] >= L'a' && text [ i - 1 ] <= L'z' ) || ( text [ i - 1 ] >= L'A' && text [ i - 1 ] <= L'Z' ) || ( text [ i - 1 ] >= L'0' && text [ i - 1 ] <= L'9' ) || text [ i - 1 ] == L'_' );

		if ( !edge )
			continue;

		if ( text [ i ] == L'g' && text [ i + 1 ] == L'_' )
			width += 4;
		else if ( text [ i ] == L'f' && text [ i + 1 ] == L'n' && text [ i + 2 ] == L'_' )
			width += 1;

	}

	if ( width < ( header ? 121 : 120 ) )
		return;

	int32_t close = header ? length - 1 : length - 2;

	if ( text [ close ] != L')' || ( !header && text [ length - 1 ] != L';' ) )
		return;

	int32_t open_at = -1;
	int32_t level = 0;
	bool quoted = false;

	for ( int32_t i = close; i >= 0; i-- ) {

		wchar_t c = text [ i ];

		if ( c == L'"' && ( i == 0 || text [ i - 1 ] != L'\\' ) )
			quoted = !quoted;

		if ( quoted )
			continue;

		if ( c == L')' )
			level++;
		else if ( c == L'(' && --level == 0 ) {

			open_at = i;

			break;

		}

	}

	if ( open_at <= 0 )
		return;

	wchar_t before = text [ open_at - 1 ];

	if ( !( ( before >= L'a' && before <= L'z' ) || ( before >= L'A' && before <= L'Z' ) || ( before >= L'0' && before <= L'9' ) || before == L'_' ) )
		return;

	int32_t name_at = open_at - 1;

	while ( name_at > 0 ) {

		wchar_t c = text [ name_at - 1 ];

		if ( ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L'_' || c == L':' )
			name_at--;
		else
			break;

	}

	if ( name_at > 0 && text [ name_at - 1 ] != L' ' )
		return;

	if ( name_at >= 2 && text [ name_at - 2 ] == L'=' && name_at >= 3 && text [ name_at - 3 ] == L'=' )
		return;

	base::list < int32_t > cuts;

	level = 0;
	quoted = false;

	for ( int32_t i = open_at + 1; i < close; i++ ) {

		wchar_t c = text [ i ];

		if ( c == L'"' && text [ i - 1 ] != L'\\' )
			quoted = !quoted;

		if ( quoted )
			continue;

		if ( c == L'(' || c == L'[' )
			level++;
		else if ( c == L')' || c == L']' )
			level--;
		else if ( c == L',' && level == 0 && text [ i + 1 ] == L' ' )
			cuts.push ( i );

	}

	if ( cuts.count ( ) == 0 && open_at + 1 >= close )
		return;

	int32_t column = header ? 8 : name_at + 2;
	base::list < wchar_t > copy;

	for ( int32_t i = 0; i < length; i++ )
		copy.push ( text [ i ] );

	base::list < int32_t > moved;

	moved.fill ( static_cast < size_t > ( length + 1 ), 0 );

	while ( r->text.count ( ) > start )
		r->text.pop ( );

	base::list < int32_t > breaks;
	int32_t cut = 0;

	for ( int32_t i = 0; i < length; i++ ) {

		moved [ static_cast < size_t > ( i ) ] = r->text.count ( ) - start;
		r->text.push ( copy [ static_cast < size_t > ( i ) ] );

		bool split = i == open_at || ( cut < cuts.count ( ) && i == cuts [ static_cast < size_t > ( cut ) ] );

		if ( !split )
			continue;

		if ( i != open_at ) {

			cut++;
			i++;
			moved [ static_cast < size_t > ( i ) ] = r->text.count ( ) - start;

		}

		breaks.push ( r->text.count ( ) - start );

		for ( int32_t k = 0; k < column; k++ )
			r->text.push ( L' ' );

	}

	moved [ static_cast < size_t > ( length ) ] = r->text.count ( ) - start;

	for ( int32_t s = current.first_span; s < r->spans.count ( ); s++ ) {

		span& item = r->spans [ static_cast < size_t > ( s ) ];
		int32_t from = item.start - start;
		int32_t to = from + item.length;

		if ( from < 0 || to > length || to <= from )
			continue;

		item.start = start + moved [ static_cast < size_t > ( from ) ];
		item.length = moved [ static_cast < size_t > ( to - 1 ) ] + 1 - moved [ static_cast < size_t > ( from ) ];

	}

	int32_t from = 0;
	int32_t span_at = current.first_span;

	for ( int32_t edge : breaks ) {

		line piece = current;

		piece.text = start + from;
		piece.length = edge - from;
		piece.first_span = span_at;

		while ( span_at < r->spans.count ( ) && r->spans [ static_cast < size_t > ( span_at ) ].start < start + edge )
			span_at++;

		piece.span_count = span_at - piece.first_span;
		r->lines.push ( piece );
		from = edge;

	}

	current.text = start + from;
	current.first_span = span_at;

}

int32_t decompiler::sink::estimated_width ( const wchar_t* text, int32_t length ) const {

	int32_t width = current.indent * 2 + length;

	for ( int32_t i = 0; i + 2 < length; i++ ) {

		bool edge = i == 0 || !( ( text [ i - 1 ] >= L'a' && text [ i - 1 ] <= L'z' ) || ( text [ i - 1 ] >= L'A' && text [ i - 1 ] <= L'Z' ) || ( text [ i - 1 ] >= L'0' && text [ i - 1 ] <= L'9' ) || text [ i - 1 ] == L'_' );

		if ( !edge )
			continue;

		if ( text [ i ] == L'g' && text [ i + 1 ] == L'_' )
			width += 4;
		else if ( text [ i ] == L'f' && text [ i + 1 ] == L'n' && text [ i + 2 ] == L'_' )
			width += 1;

	}

	return width;

}

bool decompiler::sink::starts ( const wchar_t* text, int32_t length, const wchar_t* head ) {

	int32_t i = 0;

	for ( ; head [ i ]; i++ ) {

		if ( i >= length || text [ i ] != head [ i ] )
			return false;

	}

	return true;

}

void decompiler::sink::wrap_logic ( ) {

	int32_t start = current.text;
	int32_t length = r->text.count ( ) - start;

	if ( spaced || length < 8 || !logic_wrap )
		return;

	const wchar_t* text = r->text.data ( ) + start;
	int32_t open_at = -1;

	if ( starts ( text, length, L"if ( " ) )
		open_at = 3;
	else if ( starts ( text, length, L"else if ( " ) )
		open_at = 8;
	else if ( starts ( text, length, L"while ( " ) )
		open_at = 6;

	if ( open_at < 0 || text [ length - 1 ] != L')' || text [ length - 2 ] != L' ' )
		return;

	if ( estimated_width ( text, length ) < 123 )
		return;

	int32_t close = length - 2;
	base::list < int32_t > cuts;
	int32_t level = 0;
	bool quoted = false;

	for ( int32_t i = open_at + 2; i + 3 < close; i++ ) {

		wchar_t c = text [ i ];

		if ( ( c == L'"' || c == L'\'' ) && text [ i - 1 ] != L'\\' )
			quoted = !quoted;

		if ( quoted )
			continue;

		if ( c == L'(' || c == L'[' )
			level++;
		else if ( c == L')' || c == L']' )
			level--;
		else if ( level == 0 && c == L' ' && ( ( text [ i + 1 ] == L'|' && text [ i + 2 ] == L'|' ) || ( text [ i + 1 ] == L'&' && text [ i + 2 ] == L'&' ) ) && text [ i + 3 ] == L' ' )
			cuts.push ( i );

	}

	if ( cuts.empty ( ) )
		return;

	int32_t column = open_at - 1;
	base::list < wchar_t > copy;

	for ( int32_t i = 0; i < length; i++ )
		copy.push ( text [ i ] );

	base::list < int32_t > moved;

	moved.fill ( static_cast < size_t > ( length + 1 ), 0 );

	while ( r->text.count ( ) > start )
		r->text.pop ( );

	base::list < int32_t > breaks;
	int32_t cut = 0;

	for ( int32_t i = 0; i < length; i++ ) {

		if ( cut < cuts.count ( ) && i == cuts [ static_cast < size_t > ( cut ) ] ) {

			cut++;
			moved [ static_cast < size_t > ( i ) ] = r->text.count ( ) - start;
			breaks.push ( r->text.count ( ) - start );

			for ( int32_t k = 0; k < column; k++ )
				r->text.push ( L' ' );

			continue;

		}

		moved [ static_cast < size_t > ( i ) ] = r->text.count ( ) - start;
		r->text.push ( copy [ static_cast < size_t > ( i ) ] );

	}

	moved [ static_cast < size_t > ( length ) ] = r->text.count ( ) - start;

	for ( int32_t s = current.first_span; s < r->spans.count ( ); s++ ) {

		span& item = r->spans [ static_cast < size_t > ( s ) ];
		int32_t from = item.start - start;
		int32_t to = from + item.length;

		if ( from < 0 || to > length || to <= from )
			continue;

		item.start = start + moved [ static_cast < size_t > ( from ) ];
		item.length = moved [ static_cast < size_t > ( to - 1 ) ] + 1 - moved [ static_cast < size_t > ( from ) ];

	}

	int32_t from = 0;
	int32_t span_at = current.first_span;

	for ( int32_t edge : breaks ) {

		line piece = current;

		piece.text = start + from;
		piece.length = edge - from;
		piece.first_span = span_at;

		while ( span_at < r->spans.count ( ) && r->spans [ static_cast < size_t > ( span_at ) ].start < start + edge )
			span_at++;

		piece.span_count = span_at - piece.first_span;
		r->lines.push ( piece );
		from = edge;

	}

	current.text = start + from;
	current.first_span = span_at;

}

void decompiler::sink::finish ( ) {

	if ( !open )
		return;

	wrap_logic ( );
	wrap ( );

	if ( names && current.address && current.address != commented ) {

		const wchar_t* note = names->comment ( current.address );

		commented = current.address;

		if ( note ) {

			bool keep = spaced;

			spaced = false;
			put ( L"  // ", syntax::COMMENT );
			put ( note, syntax::COMMENT );
			spaced = keep;

		}

	}

	current.length = r->text.count ( ) - current.text;
	current.span_count = r->spans.count ( ) - current.first_span;
	r->lines.push ( current );
	open = false;

}

void decompiler::short_symbol ( wchar_t* name ) {

	if ( name [ 0 ] != L'?' )
		return;

	const wchar_t* at = name + 1;
	int32_t special = 0;

	if ( at [ 0 ] == L'?' ) {

		if ( at [ 1 ] == L'0' )
			special = 1;
		else if ( at [ 1 ] == L'1' )
			special = 2;
		else
			return;

		at += 2;

	}

	const wchar_t* parts [ 8 ];
	int32_t sizes [ 8 ];
	int32_t count = 0;

	while ( *at && *at != L'@' && count < 8 ) {

		const wchar_t* start = at;

		while ( *at && *at != L'@' ) {

			if ( !( ( *at >= L'a' && *at <= L'z' ) || ( *at >= L'A' && *at <= L'Z' ) || ( *at >= L'0' && *at <= L'9' ) || *at == L'_' ) )
				return;

			at++;

		}

		parts [ count ] = start;
		sizes [ count ] = static_cast < int32_t > ( at - start );
		count++;

		if ( *at == L'@' )
			at++;

	}

	if ( *at != L'@' || count == 0 )
		return;

	wchar_t result [ 200 ];
	int32_t length = 0;

	auto add = [ & ] ( const wchar_t* text, int32_t size ) {

		for ( int32_t i = 0; i < size && length < 190; i++ )
			result [ length++ ] = text [ i ];

	};

	for ( int32_t i = count - 1; i >= 1; i-- ) {

		add ( parts [ i ], sizes [ i ] );
		add ( L"::", 2 );

	}

	if ( special == 2 )
		add ( L"~", 1 );

	if ( special && count >= 2 )
		add ( parts [ 1 ], sizes [ 1 ] );
	else
		add ( parts [ 0 ], sizes [ 0 ] );

	if ( special && count == 1 ) {

		add ( L"::", 2 );

		if ( special == 2 )
			add ( L"~", 1 );

		add ( parts [ 0 ], sizes [ 0 ] );

	}

	result [ length ] = 0;

	for ( int32_t i = 0; i <= length; i++ )
		name [ i ] = result [ i ];

}