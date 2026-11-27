#pragma once

#include "base/list/list.h"
#include "base/text/text.h"
#include "base/writer/writer.h"
#include "script/values/values.h"
#include "script/tokens/tokens.h"
#include "script/tree/tree.h"
#include "script/host/host.h"
#include "script/jit/jit.h"

namespace script {

	struct problem {

		bool set = false;
		int32_t line = 0;
		int32_t column = 0;
		wchar_t file [ 260 ] = { };
		wchar_t message [ 300 ] = { };

	};
	class engine {

		friend struct jit_bridge;
		friend struct compiler;

	public:

		engine ( ) = default;

		engine ( const engine& ) = delete;

		engine& operator= ( const engine& ) = delete;

		~engine ( );

		bool load ( const wchar_t* source, const wchar_t* path, host& with );

		bool run ( host& with );

		value text ( const wchar_t* chars, int32_t length = -1 );

		value join_texts ( value a, value b );

		void flatten ( int32_t ref ) const;

		bool is_rope ( int32_t ref ) const;

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
		bool jit_enabled = true;
		bool jit_used = false;

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

		mutable base::list < base::list < wchar_t > > texts;
		mutable base::list < rope_node > ropes;
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
		void* jit_memory = nullptr;
		size_t jit_size = 0;
		uint64_t jit_top = 0;
		base::list < uint64_t > jit_entries;

		bool compile ( );

		void release_jit ( );

	};
	const wchar_t* kind_name ( kind type );

	symbol compound_operator ( symbol mark );

	void append ( base::list < wchar_t >& out, const wchar_t* text, int32_t length = -1 );

	void append_unsigned ( base::list < wchar_t >& out, uint64_t value, int32_t radix, bool upper, int32_t width, wchar_t pad );

	void append_signed ( base::list < wchar_t >& out, int64_t value );

	void describe_into ( const engine& owner, value item, base::list < wchar_t >& out, bool quoted, int32_t depth );

	bool fast_arithmetic ( symbol mark, int64_t x, int64_t y, int64_t& out );

}