#pragma once

#include "base/types/types.h"

namespace decompiler {

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

		virtual const wchar_t* role ( uint64_t ) {

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

}