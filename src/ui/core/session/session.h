#pragma once

#include "ui/core/app/app.h"

namespace ui {

	unsigned long __stdcall analysis_entry ( void* parameter );

	struct code_names : decompiler::namer {

		app* owner = nullptr;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override;

		bool literal ( uint64_t address, wchar_t* out, int32_t room ) override;

		const wchar_t* local_name ( uint64_t function, const wchar_t* key ) override;

		const wchar_t* local_type ( uint64_t function, const wchar_t* key ) override;

		const wchar_t* prototype ( uint64_t function ) override;

		bool spaced ( ) override;

		bool expand_ternary ( ) override;

		bool hide_casts ( ) override;

		bool collapse_locals ( ) override;

		bool guess_structs ( ) override;

		bool index_pointers ( ) override;

		wchar_t number_form ( uint64_t address, int64_t value, int32_t occurrence ) override;

		bool struct_field ( const wchar_t* type, int64_t offset, wchar_t* out, int32_t room, uint64_t& tag, int32_t& size ) override;

		const wchar_t* comment ( uint64_t address ) override;

	};

}