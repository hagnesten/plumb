#include "decompiler/emit/spacing/spacing.h"
#include "base/strings/strings.h"

decompiler::line_shape decompiler::shape_of ( const result& code, const line& item, int32_t& column ) {

	const wchar_t* text = code.text.data ( ) + item.text;
	int32_t length = item.length;

	column = item.indent * 2;

	while ( length > 0 && *text == L' ' ) {

		text++;
		length--;
		column++;

	}

	if ( length <= 0 )
		return line_shape::BLANK;

	if ( length == 1 && *text == L'{' )
		return line_shape::OPEN;

	if ( *text == L'}' )
		return line_shape::CLOSE;

	if ( base::starts_with ( text, length, L"&& " ) || base::starts_with ( text, length, L"|| " ) )
		return line_shape::CONTINUATION;

	if ( base::starts_with ( text, length, L"//" ) )
		return line_shape::STATEMENT;

	if ( base::starts_with ( text, length, L"else" ) && ( length == 4 || text [ 4 ] == L' ' ) )
		return line_shape::ELSE;

	if ( base::starts_with ( text, length, L"case " ) || base::starts_with ( text, length, L"default:" ) )
		return line_shape::CASE;

	if ( base::starts_with ( text, length, L"return" ) && ( length == 6 || text [ 6 ] == L' ' || text [ 6 ] == L';' ) )
		return line_shape::RETURN;

	if ( base::starts_with ( text, length, L"while (" ) || base::starts_with ( text, length, L"for (" ) )
		return text [ length - 1 ] == L';' ? line_shape::LOOP_TAIL : line_shape::CONTROL;

	if ( base::starts_with ( text, length, L"if (" ) || base::starts_with ( text, length, L"switch (" ) || ( length == 2 && text [ 0 ] == L'd' && text [ 1 ] == L'o' ) )
		return line_shape::CONTROL;

	if ( length > 1 && text [ length - 1 ] == L':' ) {

		for ( int32_t i = 0; i < length - 1; i++ ) {

			wchar_t c = base::fold_case ( text [ i ] );

			if ( !( c == L'_' || ( c >= L'a' && c <= L'z' ) || ( c >= L'0' && c <= L'9' ) ) )
				return line_shape::STATEMENT;

		}

		return line_shape::LABEL;

	}

	return line_shape::STATEMENT;

}

bool decompiler::wants_gap ( line_shape shape, int32_t column, line_shape before, int32_t before_column ) {

	if ( shape == line_shape::CLOSE || shape == line_shape::ELSE || shape == line_shape::CONTINUATION )
		return false;

	if ( before == line_shape::OPEN || before == line_shape::LABEL || before == line_shape::CASE )
		return false;

	if ( shape == line_shape::LABEL || shape == line_shape::CASE )
		return true;

	if ( shape == line_shape::LOOP_TAIL && before == line_shape::CLOSE && column == before_column )
		return false;

	if ( column != before_column )
		return column < before_column;

	if ( before == line_shape::CLOSE || before == line_shape::LOOP_TAIL )
		return true;

	return shape == line_shape::CONTROL || shape == line_shape::LOOP_TAIL || shape == line_shape::RETURN;

}

void decompiler::space_lines ( result& code ) {

	base::list < line > spaced;
	line_shape before = line_shape::OPEN;
	int32_t before_column = 0;
	bool gap = true;

	for ( const line& item : code.lines ) {

		int32_t column = 0;
		line_shape shape = shape_of ( code, item, column );

		if ( shape == line_shape::BLANK ) {

			spaced.push ( item );
			gap = true;

			continue;

		}

		if ( !gap && wants_gap ( shape, column, before, before_column ) ) {

			line blank;

			blank.text = item.text;
			blank.first_span = item.first_span;
			spaced.push ( blank );

		}

		spaced.push ( item );
		gap = false;

		if ( shape != line_shape::CONTINUATION ) {

			before = shape;
			before_column = column;

		}

	}

	code.lines = spaced;

}