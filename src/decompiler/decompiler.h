#pragma once

#include "base/list/list.h"
#include "base/writer/writer.h"
#include "loader/binary/binary.h"
#include "analysis/database/database.h"

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

	struct c_type {

		uint8_t size = 0;
		uint8_t bits = 0;
		bool pointer = false;
		bool empty = false;
		bool custom = false;
		int32_t stars = 0;
		wchar_t pointee [ 48 ] = { };
		wchar_t custom_name [ 48 ] = { };

	};

	struct c_prototype {

		wchar_t result [ 64 ] = { };
		wchar_t name [ 64 ] = { };
		c_type result_type;
		wchar_t types [ 16 ] [ 64 ] = { };
		wchar_t names [ 16 ] [ 64 ] = { };
		c_type params [ 16 ];
		int32_t count = 0;
		bool variadic = false;

	};

	bool parse_type ( const wchar_t* text, bool long_mode, c_type& out, wchar_t* clean, int32_t room );

	bool char_literal ( int64_t value, wchar_t* out, int32_t room );

	bool parse_prototype ( const wchar_t* text, bool long_mode, c_prototype& out, base::writer& problem );

	const wchar_t* api_prototype ( const wchar_t* name );

	bool string_literal ( const loader::binary& file, uint64_t address, wchar_t* out, int32_t room );

	struct namer {

		virtual bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) = 0;

		virtual bool literal ( uint64_t address, wchar_t* out, int32_t room ) = 0;

		virtual const wchar_t* local_name ( uint64_t, const wchar_t* ) {

			return nullptr;

		}

		virtual const wchar_t* local_type ( uint64_t, const wchar_t* ) {

			return nullptr;

		}

		virtual const wchar_t* prototype ( uint64_t ) {

			return nullptr;

		}

		virtual const wchar_t* derived ( uint64_t ) {

			return nullptr;

		}

		virtual bool spaced ( ) {

			return false;

		}

		virtual bool number_notes ( ) {

			return false;

		}

		virtual bool expand_ternary ( ) {

			return false;

		}

		virtual bool hide_casts ( ) {

			return false;

		}

		virtual bool collapse_locals ( ) {

			return false;

		}

		virtual bool guess_structs ( ) {

			return false;

		}

		virtual bool index_pointers ( ) {

			return true;

		}

		virtual wchar_t number_form ( uint64_t, int64_t, int32_t ) {

			return 0;

		}

		virtual bool struct_field ( const wchar_t*, int64_t, wchar_t*, int32_t, uint64_t&, int32_t& ) {

			return false;

		}

		virtual const wchar_t* comment ( uint64_t ) {

			return nullptr;

		}

	};

	class cache {

	public:

		cache ( ) = default;

		cache ( const cache& ) = delete;

		cache& operator= ( const cache& ) = delete;

		~cache ( );

		result* find ( uint64_t function, uint32_t revision );

		result* make ( uint64_t function );

		void clear ( );

	private:

		base::list < result* > entries;

	};

	bool decompile ( const analysis::database& db, uint64_t function, namer& names, result& out );

}