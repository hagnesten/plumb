#pragma once

#include "ui/app.hh"
#include "script/script.hh"
#include "x86/x86.hh"

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

	bool load_text_file ( const wchar_t* path, base::list < wchar_t >& out );

}
