#pragma once

#include "analysis/database/database.h"

namespace analysis {

	struct emulated_string {

		uint64_t address = 0;
		uint64_t writer = 0;
		base::text text;
		bool wide = false;

	};

	struct emulated_call {

		uint64_t site = 0;
		uint64_t target = 0;
		uint64_t arguments [ 4 ] = { };

	};

	struct emulation {

		uint64_t registers [ 16 ] = { };
		uint64_t steps = 0;
		uint64_t stopped_at = 0;
		bool returned = false;
		base::text reason;
		base::list < emulated_string > strings;
		base::list < emulated_call > calls;

	};

	bool emulate ( const database& db, uint64_t start, const uint64_t* seed, const bool* seeded, uint64_t step_limit, emulation& out );

	using x86::mnemonic;

	struct memory_page {

		uint64_t index = 0;
		uint8_t* data = nullptr;
		uint8_t* written = nullptr;
		uint64_t* writers = nullptr;

	};

	struct machine {

		const database* db = nullptr;
		const loader::binary* file = nullptr;
		bool wide = true;
		uint64_t r [ 16 ] = { };
		uint64_t rip = 0;
		uint64_t current = 0;
		bool cf = false;
		bool zf = false;
		bool sf = false;
		bool of = false;
		bool pf = false;
		bool df = false;
		bool fault = false;
		base::text reason;
		base::list < memory_page > pages;

		~machine ( );

		void stop ( const wchar_t* why );

		memory_page* page ( uint64_t index );

		uint8_t read8 ( uint64_t address );

		void write8 ( uint64_t address, uint8_t value );

		uint64_t read ( uint64_t address, int32_t size );

		void write ( uint64_t address, int32_t size, uint64_t value );

		static uint64_t mask ( int32_t size );

		static uint64_t sign ( int32_t size );

		static int64_t extend ( uint64_t value, int32_t size );

		uint64_t reg ( uint8_t id );

		void set_reg ( uint8_t id, uint64_t value );

		uint64_t effective ( const x86::operand& item, const x86::instruction& ins );

		uint64_t get ( const x86::operand& item, const x86::instruction& ins );

		void put ( const x86::operand& item, const x86::instruction& ins, uint64_t value );

		void result_flags ( uint64_t value, int32_t size );

		bool condition ( uint8_t code ) const;

		int32_t width ( ) const;

		void push ( uint64_t value );

		uint64_t pop ( );

		bool import_slot ( uint64_t address ) const;

	};

	int32_t operand_size ( const x86::instruction& ins, int32_t index );

	bool in_range ( mnemonic name, mnemonic first, mnemonic last );

	void scan_strings ( machine& m, emulation& out );

}