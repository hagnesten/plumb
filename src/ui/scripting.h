#pragma once

#include "ui/app.h"
#include "script/script.h"
#include "x86/x86.h"

namespace ui {

	struct script_bridge : script::host {

		app* owner = nullptr;

		bool call ( script::engine& engine_ref, const wchar_t* name, const script::value* args, int32_t count, script::value& result ) override;

		void print ( const wchar_t* text ) override;

		bool read_file ( const wchar_t* path, const wchar_t* from, base::list < wchar_t >& source, base::writer& resolved ) override;

	};

	struct script_symbols : x86::symbolizer {

		const app* owner = nullptr;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override;

	};

	struct script_context {

		app* owner = nullptr;
		script::engine* engine = nullptr;
		const wchar_t* name = L"";
		const script::value* args = nullptr;
		int32_t count = 0;
		script::value* result = nullptr;

		bool is ( const wchar_t* word ) const;

		bool takes ( int32_t low, int32_t high );

		bool fail ( const wchar_t* message );

		bool address ( int32_t index, uint64_t& out );

		bool number ( int32_t index, int64_t& out );

		const wchar_t* text ( int32_t index );

		bool has ( int32_t index ) const;

		bool need_file ( );

		bool need_analysis ( );

		bool function_start ( int32_t index, uint64_t& out );

		bool read ( uint64_t where, int32_t size, uint64_t& out ) const;

		bool decode ( uint64_t where, x86::instruction& out ) const;

		bool flag ( int32_t index, bool& out );

		void give_number ( int64_t value );

		void give_text ( const wchar_t* value, int32_t length = -1 );

		void give_list ( script::value list );

		script::value list ( );

		void push_number ( script::value list, int64_t value );

		void push_text ( script::value list, const wchar_t* value, int32_t length = -1 );

		void push_none ( script::value list );

		void push_list ( script::value list, script::value item );

	};

	bool load_text_file ( const wchar_t* path, base::list < wchar_t >& out );

}
