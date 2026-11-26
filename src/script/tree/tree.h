#pragma once

#include "base/types/types.h"
#include "script/tokens/tokens.h"

namespace script {

	enum class node_kind : uint8_t {

		NUMBER,
		TEXT,
		NOTHING,
		NAME,
		LIST,
		INDEX,
		CALL,
		UNARY,
		BINARY,
		AND,
		OR,
		TERNARY,
		ASSIGN,
		PREFIX,
		POSTFIX,
		BLOCK,
		VAR,
		EXPR,
		IF,
		WHILE,
		DO,
		FOR,
		BREAK,
		CONTINUE,
		RETURN,
		EMPTY,

	};

	struct node {

		node_kind type = node_kind::EMPTY;
		symbol mark = symbol::NONE;
		int32_t a = -1;
		int32_t b = -1;
		int32_t c = -1;
		int32_t d = -1;
		int32_t text = -1;
		int64_t number = 0;
		int32_t first = 0;
		int32_t count = 0;
		int32_t line = 0;
		int32_t column = 0;
		int32_t file = 0;

	};

	struct function_def {

		int32_t name = -1;
		int32_t first_param = 0;
		int32_t params = 0;
		int32_t body = -1;
		int32_t slots = 0;
		int32_t line = 0;
		int32_t file = 0;

	};

	struct scope_entry {

		int32_t name = -1;
		int32_t slot = 0;

	};

}