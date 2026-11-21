#include "decompiler/internal.hh"

namespace decompiler {

	struct named_type {

		const wchar_t* name;
		uint8_t size;
		uint8_t bits;

	};

	static const named_type known_types [ ] = {
		{ L"int8_t", 1, SIGNED }, { L"uint8_t", 1, UNSIGNED }, { L"int16_t", 2, SIGNED }, { L"uint16_t", 2, UNSIGNED },
		{ L"int32_t", 4, SIGNED }, { L"uint32_t", 4, UNSIGNED }, { L"int64_t", 8, SIGNED }, { L"uint64_t", 8, UNSIGNED },
		{ L"__int8", 1, SIGNED }, { L"__int16", 2, SIGNED }, { L"__int32", 4, SIGNED }, { L"__int64", 8, SIGNED },
		{ L"BYTE", 1, UNSIGNED }, { L"UCHAR", 1, UNSIGNED }, { L"CHAR", 1, SIGNED }, { L"BOOLEAN", 1, UNSIGNED },
		{ L"WORD", 2, UNSIGNED }, { L"USHORT", 2, UNSIGNED }, { L"SHORT", 2, SIGNED }, { L"WCHAR", 2, UNSIGNED },
		{ L"wchar_t", 2, UNSIGNED }, { L"char16_t", 2, UNSIGNED }, { L"DWORD", 4, UNSIGNED }, { L"ULONG", 4, UNSIGNED },
		{ L"UINT", 4, UNSIGNED }, { L"LONG", 4, SIGNED }, { L"INT", 4, SIGNED }, { L"BOOL", 4, SIGNED },
		{ L"NTSTATUS", 4, SIGNED }, { L"HRESULT", 4, SIGNED }, { L"char32_t", 4, UNSIGNED }, { L"QWORD", 8, UNSIGNED },
		{ L"ULONGLONG", 8, UNSIGNED }, { L"LONGLONG", 8, SIGNED }, { L"DWORD64", 8, UNSIGNED }, { L"ULONG64", 8, UNSIGNED },
		{ L"LONG64", 8, SIGNED }, { L"bool", 1, UNSIGNED }, { L"_Bool", 1, UNSIGNED }, { L"float", 4, FLOATING },
		{ L"FLOAT", 4, FLOATING }, { L"double", 8, FLOATING }, { L"_BYTE", 1, UNSIGNED }, { L"_WORD", 2, UNSIGNED },
		{ L"_DWORD", 4, UNSIGNED }, { L"_QWORD", 8, UNSIGNED }, { L"_BOOL1", 1, UNSIGNED }, { L"_BOOL4", 4, UNSIGNED },
		{ L"_BOOL8", 8, UNSIGNED }
	};

	static const wchar_t* pointer_types [ ] = {
		L"size_t", L"ssize_t", L"uintptr_t", L"intptr_t", L"ptrdiff_t", L"SIZE_T", L"ULONG_PTR", L"LONG_PTR", L"UINT_PTR", L"DWORD_PTR", L"INT_PTR"
	};

	static const wchar_t* handle_types [ ] = {
		L"HANDLE", L"HMODULE", L"HINSTANCE", L"HWND", L"HKEY", L"LPVOID", L"PVOID", L"LPCVOID", L"LPCSTR", L"LPSTR", L"LPCWSTR", L"LPWSTR", L"PCSTR", L"PSTR", L"PCWSTR", L"PWSTR", L"LPBYTE", L"PBYTE", L"FARPROC"
	};

	static const wchar_t* ignored_words [ ] = {
		L"const", L"volatile", L"struct", L"class", L"enum", L"union", L"__ptr64", L"__unaligned", L"__restrict", L"restrict"
	};

	static const wchar_t* calling_words [ ] = {
		L"__fastcall", L"__stdcall", L"__cdecl", L"__thiscall", L"__vectorcall", L"__usercall", L"WINAPI", L"CALLBACK", L"APIENTRY", L"NTAPI"
	};

	struct token_list {

		wchar_t text [ 24 ] [ 64 ];
		int32_t count = 0;

	};

	static bool same ( const wchar_t* a, const wchar_t* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	static bool identifier_char ( wchar_t c ) {

		return ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L'_' || c == L':' || c == L'$' || c == L'@';

	}

	static void split ( const wchar_t* text, int32_t length, token_list& out ) {

		out.count = 0;

		for ( int32_t i = 0; i < length && out.count < 24; ) {

			wchar_t c = text [ i ];

			if ( c == L' ' || c == L'\t' ) {

				i++;

				continue;

			}

			int32_t size = 0;

			if ( identifier_char ( c ) ) {

				while ( i < length && identifier_char ( text [ i ] ) && size < 63 )
					out.text [ out.count ] [ size++ ] = text [ i++ ];

			} else if ( c == L'.' && i + 2 < length && text [ i + 1 ] == L'.' && text [ i + 2 ] == L'.' ) {

				for ( int32_t k = 0; k < 3; k++ )
					out.text [ out.count ] [ size++ ] = text [ i++ ];

			} else
				out.text [ out.count ] [ size++ ] = text [ i++ ];

			out.text [ out.count ] [ size ] = 0;
			out.count++;

		}

	}

	static bool in_list ( const wchar_t* word, const wchar_t* const* list, int32_t count ) {

		for ( int32_t i = 0; i < count; i++ ) {

			if ( same ( word, list [ i ] ) )
				return true;

		}

		return false;

	}

	static bool is_type_word ( const wchar_t* word ) {

		static const wchar_t* words [ ] = { L"void", L"char", L"short", L"int", L"long", L"unsigned", L"signed", L"float", L"double" };

		if ( in_list ( word, words, 9 ) || in_list ( word, pointer_types, 11 ) || in_list ( word, handle_types, 19 ) || in_list ( word, ignored_words, 10 ) )
			return true;

		for ( const named_type& item : known_types ) {

			if ( same ( word, item.name ) )
				return true;

		}

		return false;

	}

	static bool parse_tokens ( const token_list& tokens, int32_t from, int32_t to, bool long_mode, c_type& out, wchar_t* text, int32_t room ) {

		out = c_type ( );

		uint8_t pointer_size = long_mode ? 8 : 4;
		int32_t stars = 0;
		int32_t longs = 0;
		bool is_unsigned = false;
		bool is_signed = false;
		bool is_short = false;
		bool is_char = false;
		bool is_int = false;
		bool is_void = false;
		bool is_float = false;
		bool is_double = false;
		const named_type* named = nullptr;
		bool sized_pointer = false;
		bool handle = false;
		bool custom = false;
		const wchar_t* custom_word = nullptr;
		int32_t length = 0;

		for ( int32_t i = from; i < to; i++ ) {

			const wchar_t* word = tokens.text [ i ];

			if ( word [ 0 ] == L'*' || word [ 0 ] == L'&' ) {

				stars++;

				if ( length && text [ length - 1 ] != L'*' && length < room - 3 )
					text [ length++ ] = L' ';

				if ( length < room - 2 )
					text [ length++ ] = L'*';

				continue;

			}

			if ( !identifier_char ( word [ 0 ] ) )
				return false;

			if ( in_list ( word, calling_words, 10 ) )
				continue;

			if ( length && length < room - 2 )
				text [ length++ ] = L' ';

			for ( const wchar_t* at = word; *at && length < room - 2; at++ )
				text [ length++ ] = *at;

			if ( in_list ( word, ignored_words, 10 ) )
				continue;

			if ( same ( word, L"unsigned" ) )
				is_unsigned = true;
			else if ( same ( word, L"signed" ) )
				is_signed = true;
			else if ( same ( word, L"long" ) )
				longs++;
			else if ( same ( word, L"short" ) )
				is_short = true;
			else if ( same ( word, L"char" ) )
				is_char = true;
			else if ( same ( word, L"int" ) )
				is_int = true;
			else if ( same ( word, L"void" ) )
				is_void = true;
			else if ( same ( word, L"float" ) )
				is_float = true;
			else if ( same ( word, L"double" ) )
				is_double = true;
			else if ( in_list ( word, pointer_types, 11 ) )
				sized_pointer = true;
			else if ( in_list ( word, handle_types, 19 ) )
				handle = true;
			else {

				for ( const named_type& item : known_types ) {

					if ( same ( word, item.name ) )
						named = &item;

				}

				if ( !named ) {

					custom = true;
					custom_word = word;

				}

			}

		}

		while ( length && text [ length - 1 ] == L' ' )
			length--;

		text [ length ] = 0;

		if ( !length )
			return false;

		out.stars = stars;

		if ( custom_word ) {

			int32_t k = 0;

			for ( ; custom_word [ k ] && k < 47; k++ )
				out.custom_name [ k ] = custom_word [ k ];

			out.custom_name [ k ] = 0;

		}

		if ( stars || handle ) {

			out.size = pointer_size;
			out.bits = POINTER | UNSIGNED;
			out.pointer = true;

			if ( custom_word && stars == 1 ) {

				int32_t k = 0;

				for ( ; custom_word [ k ] && k < 47; k++ )
					out.pointee [ k ] = custom_word [ k ];

				out.pointee [ k ] = 0;

			}

			return true;

		}

		if ( custom ) {

			out.size = pointer_size;
			out.bits = 0;
			out.custom = true;

			return true;

		}

		if ( is_void ) {

			out.empty = true;

			return true;

		}

		if ( named ) {

			out.size = named->size;
			out.bits = is_unsigned && ( named->bits & SIGNED ) ? UNSIGNED : named->bits;

		} else if ( sized_pointer ) {

			out.size = pointer_size;
			out.bits = UNSIGNED;

		} else if ( is_float ) {

			out.size = 4;
			out.bits = FLOATING;

		} else if ( is_double ) {

			out.size = 8;
			out.bits = FLOATING;

		} else if ( is_char ) {

			out.size = 1;
			out.bits = is_unsigned ? UNSIGNED : SIGNED;

		} else if ( is_short ) {

			out.size = 2;
			out.bits = is_unsigned ? UNSIGNED : SIGNED;

		} else if ( longs >= 2 ) {

			out.size = 8;
			out.bits = is_unsigned ? UNSIGNED : SIGNED;

		} else if ( longs == 1 || is_int || is_unsigned || is_signed ) {

			out.size = 4;
			out.bits = is_unsigned ? UNSIGNED : SIGNED;

		} else
			return false;

		return true;

	}

	bool parse_type ( const wchar_t* text, bool long_mode, c_type& out, wchar_t* clean, int32_t room ) {

		token_list tokens;
		int32_t length = 0;

		while ( text [ length ] )
			length++;

		split ( text, length, tokens );

		return tokens.count && parse_tokens ( tokens, 0, tokens.count, long_mode, out, clean, room );

	}

	bool parse_prototype ( const wchar_t* text, bool long_mode, c_prototype& out, base::writer& problem ) {

		out = c_prototype ( );

		int32_t length = 0;
		int32_t open = -1;
		int32_t close = -1;

		while ( text [ length ] ) {

			if ( text [ length ] == L'(' && open < 0 )
				open = length;

			if ( text [ length ] == L')' )
				close = length;

			length++;

		}

		if ( open < 0 || close < open ) {

			problem.add ( L"A prototype needs a parameter list, like  int64_t name(int32_t count, char* text)" );

			return false;

		}

		token_list head;

		split ( text, open, head );

		while ( head.count && in_list ( head.text [ head.count - 1 ], calling_words, 10 ) )
			head.count--;

		int32_t type_end = head.count;

		if ( head.count >= 2 && identifier_char ( head.text [ head.count - 1 ] [ 0 ] ) && !is_type_word ( head.text [ head.count - 1 ] ) ) {

			type_end = head.count - 1;

			int32_t k = 0;

			for ( const wchar_t* at = head.text [ head.count - 1 ]; *at && k < 63; at++ )
				out.name [ k++ ] = *at;

			out.name [ k ] = 0;

		}

		if ( !parse_tokens ( head, 0, type_end, long_mode, out.result_type, out.result, 64 ) ) {

			problem.add ( L"Couldn't read the return type. Try int64_t, void, char* or a typedef like DWORD." );

			return false;

		}

		int32_t at = open + 1;

		while ( at < close ) {

			int32_t end = at;
			int32_t depth = 0;

			while ( end < close && ( text [ end ] != L',' || depth > 0 ) ) {

				if ( text [ end ] == L'(' )
					depth++;
				else if ( text [ end ] == L')' )
					depth--;

				end++;

			}

			token_list piece;

			split ( text + at, end - at, piece );

			if ( piece.count == 1 && same ( piece.text [ 0 ], L"..." ) )
				out.variadic = true;
			else if ( piece.count == 1 && same ( piece.text [ 0 ], L"void" ) && out.count == 0 && end >= close ) {

			} else if ( piece.count ) {

				if ( out.count >= 16 ) {

					problem.add ( L"Plumb handles up to 16 parameters." );

					return false;

				}

				int32_t type_stop = piece.count;
				int32_t slot = out.count;

				if ( piece.count >= 2 && identifier_char ( piece.text [ piece.count - 1 ] [ 0 ] ) && !is_type_word ( piece.text [ piece.count - 1 ] ) ) {

					type_stop = piece.count - 1;

					int32_t k = 0;

					for ( const wchar_t* word = piece.text [ piece.count - 1 ]; *word && k < 63; word++ )
						out.names [ slot ] [ k++ ] = *word;

					out.names [ slot ] [ k ] = 0;

				}

				if ( !parse_tokens ( piece, 0, type_stop, long_mode, out.params [ slot ], out.types [ slot ], 64 ) || out.params [ slot ].empty ) {

					problem.add ( L"Couldn't read parameter " ).number ( slot + 1 ).add ( L"." );

					return false;

				}

				out.count++;

			}

			at = end + 1;

		}

		return true;

	}

}