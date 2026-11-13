#pragma once

#include "base/base.hh"

namespace loader {

	enum class format : int32_t {

		NONE,
		RAW,
		PE32,
		PE64,

	};

	enum class machine : int32_t {

		UNKNOWN,
		X86,
		X64,
		ARM,
		ARM64,

	};

	enum class kind : int32_t {

		HEADER,
		CODE,
		DATA,
		CONSTANT,
		EMPTY,

	};

	enum class symbol_kind : int32_t {

		ENTRY,
		EXPORT,
		TLS,
		DEBUG,

	};

	struct section {

		wchar_t name [ 12 ] = { };
		uint64_t address = 0;
		uint64_t size = 0;
		uint64_t offset = 0;
		uint64_t raw = 0;
		uint32_t flags = 0;
		kind type = kind::DATA;
		bool read = false;
		bool write = false;
		bool execute = false;

	};

	struct module {

		base::text name;
		int32_t first = 0;
		int32_t count = 0;

	};

	struct import {

		int32_t owner = 0;
		base::text name;
		uint32_t ordinal = 0;
		bool by_ordinal = false;
		uint64_t slot = 0;

	};

	struct exported {

		base::text name;
		uint32_t ordinal = 0;
		uint64_t address = 0;
		base::text forward;

	};

	struct found_string {

		uint64_t offset = 0;
		uint32_t length = 0;
		bool wide = false;

	};

	struct symbol {

		base::text name;
		uint64_t address = 0;
		symbol_kind type = symbol_kind::EXPORT;

	};

	struct directory {

		uint32_t address = 0;
		uint32_t size = 0;

	};

	class binary {

	public:

		bool open ( const wchar_t* file_path, base::writer& problem );

		void close ( );

		bool loaded ( ) const;

		bool offset_of ( uint64_t address, uint64_t& result ) const;

		bool address_of ( uint64_t offset, uint64_t& result ) const;

		int32_t section_at ( uint64_t address ) const;

		uint64_t low ( ) const;

		uint64_t high ( ) const;

		bool wide ( ) const;

		const wchar_t* machine_name ( ) const;

		const wchar_t* format_name ( ) const;

		uint8_t byte ( uint64_t offset ) const;

		uint16_t read16 ( uint64_t offset ) const;

		uint32_t read32 ( uint64_t offset ) const;

		uint64_t read64 ( uint64_t offset ) const;

		void string_text ( const found_string& item, wchar_t* out, int32_t room ) const;

		const symbol* label_at ( uint64_t address ) const;

		void poke ( uint64_t offset, uint8_t value );

		base::text path;
		base::text name;
		const uint8_t* data = nullptr;
		uint64_t size = 0;
		format type = format::NONE;
		machine arch = machine::UNKNOWN;
		uint64_t base = 0;
		uint64_t entry = 0;
		uint64_t pe_offset = 0;
		uint64_t optional_offset = 0;
		uint32_t optional_size = 0;
		uint32_t header_size = 0;
		uint16_t subsystem = 0;
		uint16_t characteristics = 0;
		uint16_t dll_flags = 0;
		uint32_t stamp = 0;
		bool dll = false;
		directory directories [ 16 ];
		int32_t directory_count = 0;
		base::list < section > sections;
		base::list < module > modules;
		base::list < import > imports;
		base::list < exported > exports;
		base::list < found_string > strings;
		base::list < symbol > functions;
		base::list < base::text > notes;
		base::text debug_path;
		uint8_t debug_guid [ 16 ] = { };
		uint32_t debug_age = 0;
		bool debug_known = false;
		base::list < symbol > labels;
		base::text symbol_file;

	private:

		bool parse_pe ( );

		bool rva_offset ( uint32_t rva, uint64_t& result ) const;

		bool read_name ( uint32_t rva, base::text& result, int32_t limit ) const;

		void read_imports ( );

		void read_exports ( );

		void read_tls ( );

		void read_debug ( );

		void scan_strings ( );

		void note ( const wchar_t* text );

		void* file = nullptr;
		void* mapping = nullptr;

	};

	const wchar_t* kind_name ( kind value );

	const wchar_t* directory_name ( int32_t index );

}