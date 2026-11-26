#pragma once

#include "base/types/types.h"
#include "x86/instruction/instruction.h"

namespace x86 {

	enum class token : uint8_t {

		TEXT,
		MNEMONIC,
		PREFIX,
		REGISTER,
		NUMBER,
		ADDRESS,
		SIZE,
		PUNCT,
		SYMBOL,

	};

	struct symbolizer {

		virtual bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) = 0;

		virtual bool number ( uint64_t, uint64_t, uint8_t, wchar_t*, int32_t ) {

			return false;

		}

	};

	struct printer {

		virtual void put ( const wchar_t* text, int32_t length, token type, uint64_t value ) = 0;

	};

	enum format_flags : uint32_t {

		ALWAYS_SIZE = 1,
		HEX_SUFFIX = 2,
		PLAIN_RIP = 4,
		SIGNED_IMM = 8,

	};

	void format ( const instruction& item, printer& out, symbolizer* names, uint32_t flags );

	int32_t format_text ( const instruction& item, wchar_t* out, int32_t room, symbolizer* names, uint32_t flags );

	int32_t mnemonic_text ( const instruction& item, wchar_t* out, int32_t room );

	const wchar_t* size_word ( uint8_t size );

	extern const wchar_t* const predicates [ ];

	struct line {

		printer& out;

		void put ( const wchar_t* text, token type, uint64_t value = 0 ) {

			int32_t length = 0;

			while ( text [ length ] )
				length++;

			out.put ( text, length, type, value );

		}

		void put ( const char* text, token type ) {

			wchar_t wide [ 48 ];
			int32_t length = 0;

			while ( text [ length ] && length < 47 ) {

				wide [ length ] = static_cast < wchar_t > ( text [ length ] );
				length++;

			}

			wide [ length ] = 0;
			out.put ( wide, length, type, 0 );

		}

	};

	int32_t hex_text ( uint64_t value, wchar_t* out, bool suffix );

	bool signed_display ( mnemonic name );

	bool legacy_simd ( mnemonic name );

	bool string_op ( mnemonic name );

	bool compare_string ( mnemonic name );

	int32_t predicate_form ( const instruction& item );

	void put_mnemonic ( const instruction& item, line& out, int32_t predicate );

	void put_number ( line& out, uint64_t value, uint32_t flags, token type = token::NUMBER );

	bool put_symbol ( line& out, symbolizer* names, uint64_t address, bool code );

	void put_memory ( const instruction& item, const operand& value, line& out, symbolizer* names, uint32_t flags, bool show_size );

	struct buffer_printer : printer {

		wchar_t* out;
		int32_t room;
		int32_t length;

		void put ( const wchar_t* text, int32_t count, token type, uint64_t value ) override {

			for ( int32_t i = 0; i < count && length < room - 1; i++ )
				out [ length++ ] = text [ i ];

			out [ length ] = 0;

		}

	};

}