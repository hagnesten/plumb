#pragma once

#include "analysis/database.hh"

namespace analysis {

	enum class pattern_style : int32_t {

		IDA,
		X64DBG,
		CODE,
		ARRAY,
		COUNT,

	};

	struct pattern {

		base::list < uint8_t > bytes;
		base::list < uint8_t > mask;

	};

	struct signature {

		pattern bytes;
		uint64_t start = 0;
		uint64_t target = 0;
		int32_t matches = 0;
		int32_t target_offset = 0;
		bool unique = false;
		bool via_reference = false;
		uint64_t reference = 0;

	};

	bool parse_pattern ( const wchar_t* text, pattern& out );

	int32_t scan ( const loader::binary& file, const pattern& find, bool code_only, base::list < uint64_t >& hits, int32_t limit );

	bool make_signature ( const database& db, uint64_t address, signature& out );

	void format_pattern ( const pattern& value, pattern_style style, base::list < wchar_t >& out );

	bool masked_instruction ( const database& db, const x86::instruction& ins, pattern& out );

	bool function_key ( const database& db, int32_t index, uint64_t& key, bool strict = true );

}