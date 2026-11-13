#pragma once

#include "base/base.hh"
#include "loader/binary.hh"
#include "x86/x86.hh"

namespace analysis {

	enum function_bits : uint32_t {

		FROM_ENTRY = 1,
		FROM_EXPORT = 2,
		FROM_TLS = 4,
		FROM_PDATA = 8,
		FROM_CALL = 16,
		FROM_POINTER = 32,
		FROM_PROLOGUE = 64,
		THUNK = 128,
		NORETURN = 256,
		LEAF = 512,
		NAMED = 1024,
		FROM_JUMP = 2048,
		FROM_SYMBOLS = 4096,

	};

	enum class ref : uint8_t {

		CALL,
		JUMP,
		BRANCH,
		READ,
		WRITE,
		ADDRESS,
		POINTER,
		TABLE,

	};

	enum class table_kind : uint8_t {

		IMAGE_BASED,
		TABLE_BASED,
		POINTERS,

	};

	struct function_info {

		uint64_t start = 0;
		uint64_t end = 0;
		uint32_t flags = 0;
		uint32_t instructions = 0;
		uint32_t blocks = 0;
		uint32_t callers = 0;
		int32_t name = -1;
		int32_t import = -1;

	};

	struct chunk_info {

		uint64_t start = 0;
		uint64_t end = 0;
		uint64_t owner = 0;

	};

	struct xref {

		uint32_t target = 0;
		uint32_t source = 0;
		ref type = ref::CALL;

	};

	struct jump_table {

		uint64_t jump = 0;
		uint64_t table = 0;
		uint64_t index_table = 0;
		uint32_t count = 0;
		uint32_t index_count = 0;
		uint64_t anchor = 0;
		uint8_t entry_size = 4;
		table_kind type = table_kind::IMAGE_BASED;

	};

	struct code_map {

		uint64_t start = 0;
		uint64_t size = 0;
		uint64_t decodable = 0;
		uint64_t offset = 0;
		int32_t section = 0;
		uint64_t* starts = nullptr;
		uint64_t* covered = nullptr;
		uint64_t* labels = nullptr;
		uint32_t* start_rank = nullptr;
		uint32_t* label_rank = nullptr;
		uint64_t words = 0;

	};

	struct class_info {

		base::text name;
		uint64_t locator = 0;
		uint64_t vtable = 0;
		int32_t methods = 0;
		int32_t offset = 0;
		int32_t first_base = 0;
		int32_t base_count = 0;

	};

	struct virtual_slot {

		uint64_t function = 0;
		int32_t owner = 0;
		int32_t slot = 0;

	};

	struct progress {

		volatile long stage = 0;
		volatile long done = 0;
		volatile long total = 0;

	};

	class database {

	public:

		database ( ) = default;

		database ( const database& ) = delete;

		database& operator= ( const database& ) = delete;

		~database ( );

		void clear ( );

		bool build ( const loader::binary& file, progress* report );

		int32_t map_of ( uint64_t address ) const;

		bool is_start ( uint64_t address ) const;

		bool is_label ( uint64_t address ) const;

		bool is_covered ( uint64_t address ) const;

		uint64_t row_rank ( uint64_t address ) const;

		uint64_t start_rank_at ( uint64_t address ) const;

		uint64_t row_address ( uint64_t from, uint64_t to, uint64_t row, bool& label ) const;

		uint64_t instruction_at ( uint64_t address ) const;

		uint64_t covered_end ( uint64_t address, uint64_t limit ) const;

		uint64_t uncovered_end ( uint64_t address, uint64_t limit ) const;

		int32_t function_at ( uint64_t address ) const;

		int32_t function_starting ( uint64_t address ) const;

		int32_t chunk_at ( uint64_t address ) const;

		int32_t table_at ( uint64_t address ) const;

		int32_t table_for_jump ( uint64_t address ) const;

		int32_t class_at_vtable ( uint64_t address ) const;

		int32_t virtual_of ( uint64_t function ) const;

		void find_classes ( );

		void refs_to ( uint64_t target, int32_t& first, int32_t& count ) const;

		bool decode ( uint64_t address, x86::instruction& out ) const;

		const loader::binary* file = nullptr;
		bool long_mode = true;
		bool ready = false;
		double seconds = 0.0;
		int32_t threads = 1;
		uint64_t decoded = 0;
		uint64_t code_bytes = 0;
		base::list < code_map > maps;
		base::list < function_info > functions;
		base::list < chunk_info > chunks;
		base::list < xref > refs;
		base::list < jump_table > tables;
		base::list < base::text > names;
		base::list < class_info > classes;
		base::list < base::text > class_bases;
		base::list < virtual_slot > virtuals;

	};

	bool is_noreturn_name ( const wchar_t* name );

	uint64_t rank ( const uint64_t* words, const uint32_t* directory, uint64_t index );

}