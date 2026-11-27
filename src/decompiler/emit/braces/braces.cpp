#include "decompiler/emit/braces/braces.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

bool decompiler::line_starts ( const result& r, const line& item, const wchar_t* text ) {

	for ( int32_t n = 0; text [ n ]; n++ ) {

		if ( n >= item.length || r.text [ static_cast < size_t > ( item.text + n ) ] != text [ n ] )
			return false;

	}

	return true;

}

bool decompiler::line_is ( const result& r, const line& item, const wchar_t* text ) {

	int32_t n = 0;

	while ( text [ n ] )
		n++;

	return n == item.length && line_starts ( r, item, text );

}

bool decompiler::control_head ( const result& r, const line& item ) {

	return line_starts ( r, item, L"if ( " ) || line_starts ( r, item, L"else if ( " ) || line_starts ( r, item, L"while ( " ) || line_starts ( r, item, L"for ( " ) || line_is ( r, item, L"else" );

}

bool decompiler::simple_line ( const result& r, const line& item ) {

	static const wchar_t* const compound [ ] = { L"if ( ", L"else", L"while ( ", L"switch ( ", L"for ( " };

	if ( !item.length || r.text [ static_cast < size_t > ( item.text + item.length - 1 ) ] != L';' )
		return false;

	for ( const wchar_t* word : compound ) {

		if ( line_starts ( r, item, word ) )
			return false;

	}

	return true;

}

void decompiler::drop_braces ( result& r ) {

	int32_t total = r.lines.count ( );
	base::list < uint8_t > skip;
	base::list < line > kept;

	skip.fill ( static_cast < size_t > ( total ), 0 );

	for ( int32_t i = 0; i + 2 < total; i++ ) {

		const line& head = r.lines [ static_cast < size_t > ( i ) ];
		const line& open = r.lines [ static_cast < size_t > ( i + 1 ) ];
		const line& close = r.lines [ static_cast < size_t > ( i + 2 ) ];

		if ( line_is ( r, head, L"else" ) && line_is ( r, open, L"{" ) && line_is ( r, close, L"}" ) && open.indent == head.indent && close.indent == head.indent ) {

			skip [ static_cast < size_t > ( i ) ] = 1;
			skip [ static_cast < size_t > ( i + 1 ) ] = 1;
			skip [ static_cast < size_t > ( i + 2 ) ] = 1;

		}

	}

	for ( int32_t i = 0; i + 3 < total; i++ ) {

		if ( skip [ static_cast < size_t > ( i ) ] )
			continue;

		const line& head = r.lines [ static_cast < size_t > ( i ) ];
		const line& open = r.lines [ static_cast < size_t > ( i + 1 ) ];
		const line& only = r.lines [ static_cast < size_t > ( i + 2 ) ];
		const line& close = r.lines [ static_cast < size_t > ( i + 3 ) ];

		if ( !control_head ( r, head ) || !line_is ( r, open, L"{" ) || open.indent != head.indent )
			continue;

		if ( only.indent != head.indent + 1 || !simple_line ( r, only ) || !line_is ( r, close, L"}" ) || close.indent != head.indent )
			continue;

		if ( i > 0 && line_is ( r, head, L"else" ) && line_is ( r, r.lines [ static_cast < size_t > ( i - 1 ) ], L"}" ) && !skip [ static_cast < size_t > ( i - 1 ) ] && r.lines [ static_cast < size_t > ( i - 1 ) ].indent == head.indent )
			continue;

		if ( i + 5 < total && !line_is ( r, head, L"else" ) && line_is ( r, r.lines [ static_cast < size_t > ( i + 4 ) ], L"else" ) && r.lines [ static_cast < size_t > ( i + 4 ) ].indent == head.indent && line_is ( r, r.lines [ static_cast < size_t > ( i + 5 ) ], L"{" ) ) {

			bool single_else = i + 7 < total && line_is ( r, r.lines [ static_cast < size_t > ( i + 7 ) ], L"}" ) && r.lines [ static_cast < size_t > ( i + 7 ) ].indent == head.indent && r.lines [ static_cast < size_t > ( i + 6 ) ].indent == head.indent + 1 && simple_line ( r, r.lines [ static_cast < size_t > ( i + 6 ) ] );

			if ( !single_else )
				continue;

		}

		skip [ static_cast < size_t > ( i + 1 ) ] = 1;
		skip [ static_cast < size_t > ( i + 3 ) ] = 1;

	}

	for ( int32_t i = 0; i < total; i++ ) {

		if ( !skip [ static_cast < size_t > ( i ) ] )
			kept.push ( r.lines [ static_cast < size_t > ( i ) ] );

	}

	r.lines = kept;

	if ( !brace_before_else )
		return;

	total = r.lines.count ( );

	base::list < line > braced;
	int32_t open_text = -1;
	int32_t close_text = -1;

	auto brace_line = [ & ] ( wchar_t symbol, int32_t indent, int32_t& at ) {

		if ( at < 0 ) {

			at = r.text.count ( );
			r.text.push ( symbol );

		}

		line made;

		made.text = at;
		made.length = 1;
		made.first_span = r.spans.count ( );
		made.span_count = 0;
		made.indent = indent;
		made.address = 0;

		return made;

	};

	for ( int32_t i = 0; i < total; i++ ) {

		const line head = r.lines [ static_cast < size_t > ( i ) ];

		braced.push ( head );

		if ( i + 3 >= total || !( line_starts ( r, head, L"if ( " ) || line_starts ( r, head, L"else if ( " ) ) )
			continue;

		const line only = r.lines [ static_cast < size_t > ( i + 1 ) ];
		const line after = r.lines [ static_cast < size_t > ( i + 2 ) ];
		const line next = r.lines [ static_cast < size_t > ( i + 3 ) ];

		if ( only.indent != head.indent + 1 || line_is ( r, only, L"{" ) || after.indent != head.indent )
			continue;

		bool block_else = line_is ( r, after, L"else" ) && line_is ( r, next, L"{" ) && next.indent == head.indent;

		if ( !block_else && !line_starts ( r, after, L"else if ( " ) )
			continue;

		braced.push ( brace_line ( L'{', head.indent, open_text ) );
		braced.push ( only );
		braced.push ( brace_line ( L'}', head.indent, close_text ) );
		i++;

	}

	r.lines = braced;

}