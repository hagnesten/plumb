#pragma once

#include "ui/core/app/app.h"
#include "base/writer/writer.h"

namespace ui {

	bool parse_setup ( const wchar_t* text, uint64_t* seed, bool* seeded, base::writer& problem );

}