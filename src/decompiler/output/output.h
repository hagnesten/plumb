#pragma once

#include "base/list/list.h"
#include "base/text/text.h"
#include "base/writer/writer.h"

namespace decompiler {

	enum class syntax : uint8_t {

		PLAIN,
		KEYWORD,
		CONTROL,
		TYPE,
		FUNCTION,
		LOCAL,
		PARAMETER,
		GLOBAL,
		NUMBER,
		STRING,
		COMMENT,
		OPERATOR,
		LABEL,
		IMPORT,

	};

	struct span {

		int32_t start = 0;
		int32_t length = 0;
		syntax kind = syntax::PLAIN;
		uint64_t target = 0;
		int64_t value = 0;
		int32_t occurrence = 0;

	};

	struct line {

		int32_t text = 0;
		int32_t length = 0;
		int32_t first_span = 0;
		int32_t span_count = 0;
		int32_t indent = 0;
		uint64_t address = 0;

	};

	struct field_hint {

		wchar_t var [ 64 ] = { };
		int64_t offset = 0;
		uint8_t size = 0;
		uint8_t bits = 0;

	};

	struct name_alias {

		wchar_t shown [ 64 ] = { };
		wchar_t key [ 64 ] = { };

	};

	struct result {

		uint64_t function = 0;
		base::list < field_hint > hints;
		base::list < name_alias > aliases;
		base::list < wchar_t > text;
		base::list < span > spans;
		base::list < line > lines;
		int32_t instructions = 0;
		int32_t blocks = 0;
		int32_t gotos = 0;
		int32_t locals = 0;
		double seconds = 0.0;
		uint32_t revision = 0;
		bool ok = false;

	};

}