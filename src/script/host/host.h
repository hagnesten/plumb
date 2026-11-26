#pragma once

#include "base/list/list.h"
#include "base/writer/writer.h"
#include "script/values/values.h"

namespace script {

	class engine;

	struct host {

		virtual bool call ( engine& owner, const wchar_t* name, const value* args, int32_t count, value& result ) = 0;

		virtual void print ( const wchar_t* text ) = 0;

		virtual bool read_file ( const wchar_t* path, const wchar_t* from, base::list < wchar_t >& source, base::writer& resolved ) = 0;

	};

}