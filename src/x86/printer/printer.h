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

}