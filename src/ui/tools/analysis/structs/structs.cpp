#include "ui/tools/analysis/structs/structs.h"
#include "ui/core/app/app.h"
#include "base/strings/strings.h"

int32_t ui::app::struct_named ( const wchar_t* name ) const {

	for ( int32_t i = 0; i < structs.count ( ); i++ ) {

		if ( base::same_text ( structs [ static_cast < size_t > ( i ) ].name.get ( ), name ) )
			return i;

	}

	return -1;

}

int32_t ui::app::struct_size ( int32_t index ) const {

	if ( index < 0 || index >= structs.count ( ) )
		return 0;

	int32_t size = 0;

	for ( const user_field& item : structs [ static_cast < size_t > ( index ) ].fields )
		size = base::max ( size, item.offset + item.size );

	return size;

}

bool ui::app::add_field ( int32_t index, int32_t offset, const wchar_t* name, const wchar_t* type ) {

	if ( index < 0 || index >= structs.count ( ) || offset < 0 || !name || !name [ 0 ] )
		return false;

	decompiler::c_type shape;
	wchar_t clean [ 64 ];
	wchar_t base_name [ 64 ];
	int32_t count = 1;
	int32_t length = 0;

	for ( ; name [ length ] && name [ length ] != L'[' && length < 63; length++ )
		base_name [ length ] = name [ length ];

	base_name [ length ] = 0;

	if ( name [ length ] == L'[' ) {

		count = 0;

		for ( const wchar_t* at = name + length + 1; *at >= L'0' && *at <= L'9'; at++ )
			count = count * 10 + ( *at - L'0' );

		count = base::clamp ( count, 1, 1 << 20 );

	}

	if ( !decompiler::parse_type ( type, file.wide ( ), shape, clean, 64 ) || shape.empty )
		return false;

	base::writer problem;

	if ( !known_type ( shape, problem ) ) {

		log ( log_kind::WARN, problem.text ( ) );

		return false;

	}

	int32_t size = shape.size;
	int32_t nested = shape.pointer ? -1 : struct_named ( clean );

	if ( nested >= 0 )
		size = base::max ( struct_size ( nested ), 1 );

	user_struct& owner = structs [ static_cast < size_t > ( index ) ];

	for ( int32_t i = owner.fields.count ( ) - 1; i >= 0; i-- ) {

		if ( owner.fields [ static_cast < size_t > ( i ) ].offset == offset )
			owner.fields.remove_at ( static_cast < size_t > ( i ) );

	}

	int32_t at = owner.fields.count ( );

	while ( at > 0 && owner.fields [ static_cast < size_t > ( at - 1 ) ].offset > offset )
		at--;

	user_field item;

	item.offset = offset;
	item.size = size * count;
	item.name.set ( name );
	item.type.set ( clean );
	owner.fields.push ( item );

	for ( int32_t i = owner.fields.count ( ) - 1; i > at; i-- ) {

		user_field held = owner.fields [ static_cast < size_t > ( i ) ];

		owner.fields [ static_cast < size_t > ( i ) ] = owner.fields [ static_cast < size_t > ( i - 1 ) ];
		owner.fields [ static_cast < size_t > ( i - 1 ) ] = held;

	}

	names_revision++;

	return true;

}

bool ui::app::known_type ( const decompiler::c_type& shape, base::writer& problem ) const {

	if ( !shape.custom )
		return true;

	if ( struct_named ( shape.custom_name ) >= 0 )
		return true;

	problem.add ( L"Unknown type \"" ).add ( shape.custom_name ).add ( L"\". Use a built-in type (int8_t to uint64_t, char, bool, float, double, size_t, DWORD, HANDLE...) or the name of one of your structs." );

	return false;

}

const wchar_t* ui::app::comment_at ( uint64_t address ) const {

	return comments.empty ( ) ? nullptr : user_text ( comments, address );

}

bool ui::app::code_field ( int32_t& index, int32_t& offset ) const {

	const decompiler::result* code = const_cast < app* > ( this )->pseudocode_for ( now->function );

	if ( !code || now->code_line < 0 || now->code_line >= code->lines.count ( ) )
		return false;

	const decompiler::line& item = code->lines [ static_cast < size_t > ( now->code_line ) ];
	int32_t column = now->code_col - item.indent * metric::code_indent;

	for ( int32_t s = 0; s < item.span_count; s++ ) {

		const decompiler::span& part = code->spans [ static_cast < size_t > ( item.first_span + s ) ];
		int32_t start = part.start - item.text;

		if ( !( part.target >> 63 ) || column < start || column > start + part.length )
			continue;

		index = static_cast < int32_t > ( ( part.target >> 32 ) & 0x7FFFFFFF );
		offset = static_cast < int32_t > ( part.target & 0xFFFFFFFF );

		return index < structs.count ( );

	}

	return false;

}

bool ui::split_declaration ( const wchar_t* text, wchar_t* type, wchar_t* name ) {

	int32_t length = 0;

	while ( text [ length ] )
		length++;

	while ( length && text [ length - 1 ] == L' ' )
		length--;

	int32_t end = length;
	int32_t start = end;

	if ( start && text [ start - 1 ] == L']' ) {

		while ( start && text [ start - 1 ] != L'[' )
			start--;

		if ( start )
			start--;

	}

	while ( start && text [ start - 1 ] != L' ' && text [ start - 1 ] != L'*' )
		start--;

	if ( start == 0 )
		return false;

	int32_t k = 0;

	for ( int32_t i = start; i < end && k < 62; i++ )
		name [ k++ ] = text [ i ];

	name [ k ] = 0;
	k = 0;

	for ( int32_t i = 0; i < start && k < 62; i++ )
		type [ k++ ] = text [ i ];

	while ( k && type [ k - 1 ] == L' ' )
		k--;

	type [ k ] = 0;

	return k > 0 && name [ 0 ];

}

void ui::app::open_struct_prompt ( int32_t mode, int32_t index, int32_t offset, const wchar_t* fill ) {

	open_code_prompt ( mode == 4 ? command::RENAME : command::SET_TYPE, mode, L"", fill );
	prompt_struct = index;
	prompt_offset = offset;
	prompt_from_var = false;

}

bool ui::app::apply_struct_prompt ( const wchar_t* text ) {

	wchar_t type [ 64 ];
	wchar_t name [ 64 ];

	switch ( prompt_mode ) {

		case 4:
		case 5: {

			if ( prompt_struct < 0 || prompt_struct >= structs.count ( ) )
				return false;

			user_struct& owner = structs [ static_cast < size_t > ( prompt_struct ) ];

			for ( user_field& item : owner.fields ) {

				if ( item.offset != prompt_offset )
					continue;

				if ( prompt_mode == 4 ) {

					if ( !base::is_identifier ( text ) ) {

						log ( log_kind::WARN, L"Field names are letters, digits and underscores, and can't start with a digit." );

						return false;

					}

					item.name.set ( text );
					names_revision++;

				} else {

					base::text keep;

					keep.set ( item.name.get ( ) );

					if ( !add_field ( prompt_struct, prompt_offset, keep.get ( ), text ) )
						return false;

				}

				log ( log_kind::INFO, base::writer ( ).add ( owner.name.get ( ) ).add ( L" updated." ).text ( ) );

				return true;

			}

			return false;

		}

		case 6: {

			if ( !base::is_identifier ( text ) ) {

				log ( log_kind::WARN, L"Struct names are letters, digits and underscores, and can't start with a digit." );

				return false;

			}

			if ( struct_named ( text ) >= 0 && !prompt_from_var ) {

				log ( log_kind::WARN, base::writer ( ).add ( L"There's already a struct called " ).add ( text ).add ( L"." ).text ( ) );

				return false;

			}

			int32_t index = struct_named ( text );

			if ( index < 0 ) {

				user_struct& made = structs.emplace ( );

				made.name.set ( text );
				index = structs.count ( ) - 1;

			}

			int32_t added = 0;

			if ( prompt_from_var ) {

				const decompiler::result* code = pseudocode_for ( prompt_function );

				if ( code ) {

					base::list < decompiler::field_hint > hints;

					for ( const decompiler::field_hint& hint : code->hints ) {

						if ( base::same_text ( hint.var, prompt_shown.get ( ) ) )
							hints.push ( hint );

					}

					for ( const decompiler::field_hint& hint : hints ) {

						const wchar_t* field_type = L"int64_t";
						base::writer field_name;

						if ( hint.bits & 4 )
							field_type = hint.size == 4 ? L"float" : L"double";
						else if ( hint.size == 1 )
							field_type = L"uint8_t";
						else if ( hint.size == 2 )
							field_type = L"int16_t";
						else if ( hint.size == 4 )
							field_type = L"int32_t";

						field_name.add ( L"field_" ).hex ( static_cast < uint64_t > ( hint.offset ) );

						if ( add_field ( index, static_cast < int32_t > ( hint.offset ), field_name.text ( ), field_type ) )
							added++;

					}

				}

				set_local ( prompt_function, prompt_key.get ( ), nullptr, base::writer ( ).add ( text ).add ( L"*" ).text ( ) );

			}

			expanded_struct = index;
			names_revision++;
			refresh_filter ( panel::STRUCTS );
			log ( log_kind::GOOD, base::writer ( ).add ( L"Created struct " ).add ( text ).add ( prompt_from_var ? L" with " : L"" ).add ( prompt_from_var ? base::writer ( ).number ( added ).text ( ) : L"" ).add ( prompt_from_var ? L" fields from how the variable is used." : L"." ).text ( ) );

			return true;

		}

		case 7:
		case 8: {

			if ( prompt_struct < 0 || prompt_struct >= structs.count ( ) )
				return false;

			user_struct& owner = structs [ static_cast < size_t > ( prompt_struct ) ];

			if ( prompt_mode == 7 && !text [ 0 ] ) {

				for ( int32_t i = 0; i < owner.fields.count ( ); i++ ) {

					if ( owner.fields [ static_cast < size_t > ( i ) ].offset == prompt_offset ) {

						owner.fields.remove_at ( static_cast < size_t > ( i ) );
						names_revision++;
						refresh_filter ( panel::STRUCTS );

						return true;

					}

				}

				return false;

			}

			const wchar_t* body = text;
			int32_t offset = prompt_offset;

			while ( *body == L' ' )
				body++;

			if ( body [ 0 ] == L'+' ) {

				body++;

				if ( body [ 0 ] == L'0' && ( body [ 1 ] == L'x' || body [ 1 ] == L'X' ) )
					body += 2;

				offset = 0;

				while ( ( *body >= L'0' && *body <= L'9' ) || ( *body >= L'a' && *body <= L'f' ) || ( *body >= L'A' && *body <= L'F' ) ) {

					wchar_t c = *body++;

					offset = offset * 16 + ( c >= L'a' ? c - L'a' + 10 : ( c >= L'A' ? c - L'A' + 10 : c - L'0' ) );

				}

				while ( *body == L' ' )
					body++;

			}

			if ( !split_declaration ( body, type, name ) ) {

				log ( log_kind::WARN, L"Write the field as a type then a name, like  int32_t health  or  char name[32]." );

				return false;

			}

			if ( prompt_mode == 8 && offset == prompt_offset ) {

				decompiler::c_type shape;
				wchar_t clean [ 64 ];

				if ( decompiler::parse_type ( type, file.wide ( ), shape, clean, 64 ) && !shape.empty ) {

					int32_t nested = shape.pointer ? -1 : struct_named ( clean );
					int32_t align = 1;
					int32_t size = nested >= 0 ? 8 : base::max ( static_cast < int32_t > ( shape.size ), 1 );

					while ( align < size && align < 8 )
						align *= 2;

					offset = ( offset + align - 1 ) & ~( align - 1 );

				}

			}

			if ( prompt_mode == 7 && offset != prompt_offset ) {

				for ( int32_t i = 0; i < owner.fields.count ( ); i++ ) {

					if ( owner.fields [ static_cast < size_t > ( i ) ].offset == prompt_offset )
						owner.fields.remove_at ( static_cast < size_t > ( i ) );

				}

			}

			if ( !add_field ( prompt_struct, offset, name, type ) )
				return false;

			refresh_filter ( panel::STRUCTS );

			return true;

		}

		case 9: {

			if ( prompt_struct < 0 || prompt_struct >= structs.count ( ) )
				return false;

			if ( !text [ 0 ] ) {

				log ( log_kind::INFO, base::writer ( ).add ( L"Deleted struct " ).add ( structs [ static_cast < size_t > ( prompt_struct ) ].name.get ( ) ).text ( ) );
				structs.remove_at ( static_cast < size_t > ( prompt_struct ) );
				expanded_struct = -1;

			} else if ( base::is_identifier ( text ) )
				structs [ static_cast < size_t > ( prompt_struct ) ].name.set ( text );
			else
				return false;

			names_revision++;
			refresh_filter ( panel::STRUCTS );

			return true;

		}

		default:
			return false;

	}

}