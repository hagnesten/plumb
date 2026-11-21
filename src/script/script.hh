#pragma once

#include "base/base.hh"

namespace script {

	enum class kind : uint8_t {

		NONE,
		NUMBER,
		TEXT,
		LIST,

	};

	struct value {

		kind type = kind::NONE;
		int64_t number = 0;
		int32_t ref = -1;

	};

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

	struct problem {

		bool set = false;
		int32_t line = 0;
		int32_t column = 0;
		wchar_t file [ 260 ] = { };
		wchar_t message [ 300 ] = { };

	};

	class engine;

	struct host {

		virtual bool call ( engine& owner, const wchar_t* name, const value* args, int32_t count, value& result ) = 0;

		virtual void print ( const wchar_t* text ) = 0;

		virtual bool read_file ( const wchar_t* path, const wchar_t* from, base::list < wchar_t >& source, base::writer& resolved ) = 0;

	};

	class engine {

	public:

		bool load ( const wchar_t* source, const wchar_t* path, host& with );

		bool run ( host& with );

		value text ( const wchar_t* chars, int32_t length = -1 );

		value list ( );

		void push ( value list_value, value item );

		const wchar_t* text_of ( value item ) const;

		int32_t text_length ( value item ) const;

		int32_t list_count ( value item ) const;

		value list_item ( value item, int32_t index ) const;

		bool fail ( const wchar_t* message );

		void describe ( value item, base::writer& out );

		problem error;
		int64_t step_limit = 200000000;
		int64_t steps = 0;
		bool stopped = false;

	private:

		bool lex ( const wchar_t* source, int32_t file, int32_t depth );

		int32_t intern ( const wchar_t* chars, int32_t length );

		int32_t store ( const wchar_t* chars, int32_t length );

		bool lex_error ( int32_t line, int32_t column, int32_t file, const wchar_t* message );

		bool parse_error ( const wchar_t* message );

		const token& peek ( int32_t ahead = 0 ) const;

		bool is ( symbol mark ) const;

		bool is_word ( const wchar_t* word ) const;

		bool accept ( symbol mark );

		bool expect ( symbol mark, const wchar_t* what );

		int32_t make ( node_kind type );

		bool parse_program ( );

		bool parse_function ( );

		int32_t parse_statement ( );

		int32_t parse_block ( );

		int32_t parse_var ( );

		int32_t parse_expression ( );

		int32_t parse_assignment ( );

		int32_t parse_ternary ( );

		int32_t parse_binary ( int32_t level );

		int32_t parse_unary ( );

		int32_t parse_postfix ( );

		int32_t parse_primary ( );

		bool resolve ( );

		bool resolve_node ( int32_t index, base::list < scope_entry >& scope, int32_t& next, int32_t& most, bool top );

		bool runtime_error ( const node& at, const wchar_t* message );

		enum class flow : uint8_t {

			NEXT,
			BREAK,
			CONTINUE,
			RETURN,

		};

		flow execute ( int32_t index );

		value evaluate ( int32_t index );

		bool assign ( const node& target, value item );

		value evaluate_call ( const node& at );

		value call_function ( const node& at, const value* args, int32_t count );

		value call_user ( const node& at, int32_t index, const value* args, int32_t count );

		bool builtin ( const node& at, const wchar_t* name, const value* args, int32_t count, value& result );

		bool truthy ( value item ) const;

		bool equal ( value a, value b ) const;

		int32_t compare_text ( value a, value b ) const;

		value binary_op ( const node& at, symbol mark, value a, value b );

		bool format ( const node& at, const value* args, int32_t count, base::list < wchar_t >& out );

		bool overran ( const node& at );

		bool tick ( const node& at ) {

			return ++steps <= step_limit || overran ( at );

		}

		base::list < base::list < wchar_t > > texts;
		base::list < base::list < value > > lists;
		base::list < int32_t > interned;
		base::list < token > tokens;
		base::list < node > nodes;
		base::list < int32_t > items;
		base::list < function_def > functions;
		base::list < int32_t > params;
		base::list < int32_t > top_level;
		base::list < base::text > files;
		base::list < int32_t > global_names;
		base::list < value > globals;
		base::list < value > stack;
		int32_t top_slots = 0;
		int32_t frame_base = 0;
		int32_t loops = 0;
		int32_t depth = 0;
		int32_t position = 0;
		int64_t text_chars = 0;
		value returned;
		host* current = nullptr;
		bool in_function = false;

	};

	const wchar_t* kind_name ( kind type );

}
