#pragma once

#include "base/types/types.h"

namespace script {

	enum class token_kind : uint8_t {

		END,
		NAME,
		NUMBER,
		TEXT,
		SYMBOL,

	};

	enum class symbol : uint8_t {

		NONE,
		OPEN_PAREN,
		CLOSE_PAREN,
		OPEN_BRACE,
		CLOSE_BRACE,
		OPEN_BRACKET,
		CLOSE_BRACKET,
		COMMA,
		SEMICOLON,
		QUESTION,
		COLON,
		PLUS,
		MINUS,
		STAR,
		SLASH,
		PERCENT,
		AMPERSAND,
		PIPE,
		CARET,
		TILDE,
		BANG,
		LESS,
		GREATER,
		LESS_EQUAL,
		GREATER_EQUAL,
		EQUAL_EQUAL,
		BANG_EQUAL,
		AND_AND,
		PIPE_PIPE,
		SHIFT_LEFT,
		SHIFT_RIGHT,
		ASSIGN,
		PLUS_ASSIGN,
		MINUS_ASSIGN,
		STAR_ASSIGN,
		SLASH_ASSIGN,
		PERCENT_ASSIGN,
		AND_ASSIGN,
		OR_ASSIGN,
		XOR_ASSIGN,
		SHIFT_LEFT_ASSIGN,
		SHIFT_RIGHT_ASSIGN,
		PLUS_PLUS,
		MINUS_MINUS,

	};

	struct symbol_spelling {

		const wchar_t* text;
		symbol mark;

	};

	struct token {

		token_kind type = token_kind::END;
		symbol mark = symbol::NONE;
		int32_t text = -1;
		int64_t number = 0;
		int32_t line = 0;
		int32_t column = 0;
		int32_t file = 0;

	};

}