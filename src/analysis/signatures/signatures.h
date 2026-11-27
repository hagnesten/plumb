#pragma once

#include "analysis/database/database.h"
#include "analysis/runtime_names/runtime_names.h"

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

	void add_byte ( pattern& out, uint8_t value, bool fixed );

	struct scan_task {

		const uint8_t* data;
		uint64_t length;
		uint64_t offset;

	};

	struct scan_job {

		const pattern* find;
		base::list < scan_task > tasks;
		volatile long next;
		volatile long found;
		int32_t limit;
		int32_t anchor;

	};

	struct scan_worker {

		scan_job* job;
		base::list < uint64_t > hits;

	};

	unsigned long __stdcall scan_entry ( void* parameter );

	int32_t scan_offsets ( const loader::binary& file, const pattern& find, bool code_only, base::list < uint64_t >& hits, int32_t limit );

	void wildcard ( pattern& out, size_t from, int32_t at, int32_t size );

	bool grow_unique ( const database& db, uint64_t start, uint64_t limit, int32_t max_bytes, pattern& out, int32_t& matches );

}