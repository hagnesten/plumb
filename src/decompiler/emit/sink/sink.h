#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"

namespace decompiler {

	struct sink {

		result* r = nullptr;
		bool open = false;
		int32_t terminal = -1;
		bool spaced = false;
		bool header = false;
		namer* names = nullptr;
		uint64_t commented = 0;
		int32_t indent = 0;
		line current;

		wchar_t last ( int32_t back = 1 ) const;

		void begin ( uint64_t address );

		void put ( const wchar_t* text, syntax kind = syntax::PLAIN, uint64_t target = 0 );

		void wrap ( );

		int32_t estimated_width ( const wchar_t* text, int32_t length ) const;

		static bool starts ( const wchar_t* text, int32_t length, const wchar_t* head );

		void wrap_logic ( );

		void finish ( );

	};

	void short_symbol ( wchar_t* name );

}