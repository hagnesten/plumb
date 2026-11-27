#pragma once

#include "ui/core/app/app.h"
#include "base/json/json.h"
#include "base/list/list.h"

namespace ui {

	struct mcp_tool {

		const char* name;
		const char* description;
		const char* schema;

	};

	extern const mcp_tool mcp_tools [ ];

	struct mcp_bridge {

		const char* name;
		const char* description;
		const char* params;
		const wchar_t* script;

	};

	extern const mcp_bridge mcp_bridges [ ];

	extern const int32_t mcp_bridge_count;

	void write_schema ( const char* params, json::writer& out );

	void bridge_source ( const wchar_t* script, const json::value* args, base::list < wchar_t >& out );

	struct mcp_text {

		base::list < wchar_t > out;

		void add ( const wchar_t* text ) {

			for ( ; *text; text++ )
				out.push ( *text );

		}

		void line ( const wchar_t* text ) {

			add ( text );
			out.push ( L'\n' );

		}

	};

	int64_t integer_arg ( const json::value* args, const char* key, int64_t fallback );

	void text_arg ( const json::value* args, const char* key, wchar_t* out, int32_t room );

}