#pragma once

#include "loader/binary.hh"

namespace loader {

	bool load_symbols ( binary& file, base::writer& report );

	bool read_pdb ( binary& file, const wchar_t* path, base::writer& report );

	void undecorate ( const char* name, bool x86, wchar_t* out, int32_t room );

}
