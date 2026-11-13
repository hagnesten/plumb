#include "analysis/signatures.hh"
#include <windows.h>

namespace analysis {

	static int32_t hex_value ( wchar_t c ) {

		if ( c >= L'0' && c <= L'9' )
			return c - L'0';

		if ( c >= L'a' && c <= L'f' )
			return c - L'a' + 10;

		if ( c >= L'A' && c <= L'F' )
			return c - L'A' + 10;

		return -1;

	}

	static void add_byte ( pattern& out, uint8_t value, bool fixed ) {

		out.bytes.push ( fixed ? value : 0 );
		out.mask.push ( fixed ? 1 : 0 );

	}

	bool parse_pattern ( const wchar_t* text, pattern& out ) {

		out.bytes.clear ( );
		out.mask.clear ( );

		while ( *text == L' ' || *text == L'\t' )
			text++;

		if ( *text == L'"' ) {

			const wchar_t* at = text + 1;
			bool escaped = false;

			for ( const wchar_t* probe = at; *probe && *probe != L'"'; probe++ ) {

				if ( probe [ 0 ] == L'\\' && ( probe [ 1 ] == L'x' || probe [ 1 ] == L'X' ) )
					escaped = true;

			}

			if ( escaped ) {

				while ( *at && *at != L'"' ) {

					if ( at [ 0 ] == L'\\' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' ) && hex_value ( at [ 2 ] ) >= 0 && hex_value ( at [ 3 ] ) >= 0 ) {

						add_byte ( out, static_cast < uint8_t > ( hex_value ( at [ 2 ] ) * 16 + hex_value ( at [ 3 ] ) ), true );
						at += 4;

					} else
						return false;

				}

				if ( *at == L'"' )
					at++;

				while ( *at == L' ' || *at == L',' )
					at++;

				if ( *at == L'"' ) {

					at++;

					for ( int32_t i = 0; *at && *at != L'"' && i < out.mask.count ( ); i++, at++ )
						out.mask [ static_cast < size_t > ( i ) ] = *at == L'?' ? 0 : 1;

				}

			} else {

				while ( *at && *at != L'"' ) {

					if ( *at > 0xFF )
						return false;

					add_byte ( out, static_cast < uint8_t > ( *at ), true );
					at++;

				}

			}

		} else if ( *text == L'{' ) {

			const wchar_t* at = text + 1;

			while ( *at && *at != L'}' ) {

				while ( *at == L' ' || *at == L',' )
					at++;

				if ( *at == L'}' || !*at )
					break;

				if ( *at == L'-' || *at == L'?' ) {

					add_byte ( out, 0, false );

					while ( *at && *at != L',' && *at != L'}' )
						at++;

					continue;

				}

				if ( at [ 0 ] == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' ) )
					at += 2;

				int32_t value = 0;
				int32_t digits = 0;

				while ( hex_value ( *at ) >= 0 ) {

					value = value * 16 + hex_value ( *at );
					digits++;
					at++;

				}

				if ( !digits || value > 0xFF )
					return false;

				add_byte ( out, static_cast < uint8_t > ( value ), true );

			}

		} else {

			const wchar_t* at = text;

			while ( *at ) {

				if ( *at == L' ' || *at == L',' || *at == L'\t' ) {

					at++;

					continue;

				}

				if ( *at == L'?' ) {

					add_byte ( out, 0, false );
					at++;

					if ( *at == L'?' )
						at++;

					continue;

				}

				if ( at [ 0 ] == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' ) )
					at += 2;

				if ( hex_value ( at [ 0 ] ) < 0 || hex_value ( at [ 1 ] ) < 0 )
					return false;

				add_byte ( out, static_cast < uint8_t > ( hex_value ( at [ 0 ] ) * 16 + hex_value ( at [ 1 ] ) ), true );
				at += 2;

			}

		}

		bool fixed = false;

		for ( uint8_t value : out.mask ) {

			if ( value )
				fixed = true;

		}

		return !out.bytes.empty ( ) && fixed;

	}

	struct scan_task {

		const uint8_t* data;
		uint64_t length;
		uint64_t offset;

	};

	struct scan_job {

		const pattern* find;
		base::list < scan_task > tasks;
		volatile long next;
		volatile long found;
		int32_t limit;
		int32_t anchor;

	};

	struct scan_worker {

		scan_job* job;
		base::list < uint64_t > hits;

	};

	static unsigned long __stdcall scan_entry ( void* parameter ) {

		scan_worker& worker = *static_cast < scan_worker* > ( parameter );
		scan_job& job = *worker.job;
		const uint8_t* bytes = job.find->bytes.data ( );
		const uint8_t* mask = job.find->mask.data ( );
		uint64_t size = job.find->bytes.size ( );
		uint8_t first = bytes [ job.anchor ];

		for ( ;; ) {

			long index = __atomic_fetch_add ( &job.next, 1, __ATOMIC_RELAXED );

			if ( index >= job.tasks.count ( ) || __atomic_load_n ( &job.found, __ATOMIC_RELAXED ) >= job.limit )
				break;

			const scan_task& task = job.tasks [ static_cast < size_t > ( index ) ];

			if ( task.length < size )
				continue;

			uint64_t last = task.length - size;

			for ( uint64_t i = 0; i <= last; i++ ) {

				if ( task.data [ i + static_cast < uint64_t > ( job.anchor ) ] != first )
					continue;

				bool match = true;

				for ( uint64_t k = 0; k < size; k++ ) {

					if ( mask [ k ] && task.data [ i + k ] != bytes [ k ] ) {

						match = false;

						break;

					}

				}

				if ( !match )
					continue;

				worker.hits.push ( task.offset + i );

				if ( __atomic_add_fetch ( &job.found, 1, __ATOMIC_RELAXED ) >= job.limit )
					break;

			}

		}

		return 0;

	}

	static int32_t scan_offsets ( const loader::binary& file, const pattern& find, bool code_only, base::list < uint64_t >& hits, int32_t limit ) {

		hits.clear ( );

		if ( find.bytes.empty ( ) )
			return 0;

		scan_job job;

		job.find = &find;
		job.next = 0;
		job.found = 0;
		job.limit = limit;
		job.anchor = 0;

		for ( int32_t i = 0; i < find.mask.count ( ); i++ ) {

			if ( find.mask [ static_cast < size_t > ( i ) ] ) {

				job.anchor = i;

				break;

			}

		}

		uint64_t overlap = find.bytes.size ( ) - 1;
		const uint64_t chunk = 1ull << 21;

		auto add_range = [ & ] ( uint64_t offset, uint64_t length ) {

			for ( uint64_t at = 0; at < length; at += chunk ) {

				scan_task task;

				task.offset = offset + at;
				task.data = file.data + task.offset;
				task.length = base::min ( chunk + overlap, length - at );
				job.tasks.push ( task );

			}

		};

		if ( file.type == loader::format::RAW )
			add_range ( 0, file.size );
		else {

			for ( int32_t s = 0; s < file.sections.count ( ); s++ ) {

				const loader::section& item = file.sections [ static_cast < size_t > ( s ) ];

				if ( !item.raw || ( code_only && item.type != loader::kind::CODE ) || ( !code_only && s == 0 ) )
					continue;

				add_range ( item.offset, base::min ( item.raw, file.size - item.offset ) );

			}

		}

		SYSTEM_INFO system = { };

		GetSystemInfo ( &system );

		int32_t count = base::clamp ( static_cast < int32_t > ( system.dwNumberOfProcessors ), 1, 32 );

		count = base::min ( count, base::max ( job.tasks.count ( ), 1 ) );

		base::list < scan_worker > workers;
		HANDLE handles [ 32 ];
		int32_t started = 0;

		workers.resize ( static_cast < size_t > ( count ) );

		for ( scan_worker& worker : workers )
			worker.job = &job;

		for ( int32_t i = 1; i < count; i++ ) {

			HANDLE handle = CreateThread ( nullptr, 0, scan_entry, &workers [ static_cast < size_t > ( i ) ], 0, nullptr );

			if ( handle )
				handles [ started++ ] = handle;

		}

		scan_entry ( &workers [ 0 ] );

		if ( started )
			WaitForMultipleObjects ( static_cast < DWORD > ( started ), handles, TRUE, INFINITE );

		for ( int32_t i = 0; i < started; i++ )
			CloseHandle ( handles [ i ] );

		for ( scan_worker& worker : workers ) {

			for ( uint64_t value : worker.hits )
				hits.push ( value );

		}

		for ( int32_t i = 1; i < hits.count ( ); i++ ) {

			uint64_t held = hits [ static_cast < size_t > ( i ) ];
			int32_t j = i - 1;

			while ( j >= 0 && hits [ static_cast < size_t > ( j ) ] > held ) {

				hits [ static_cast < size_t > ( j + 1 ) ] = hits [ static_cast < size_t > ( j ) ];
				j--;

			}

			hits [ static_cast < size_t > ( j + 1 ) ] = held;

		}

		size_t kept = 0;

		for ( size_t i = 0; i < hits.size ( ); i++ ) {

			if ( !kept || hits [ kept - 1 ] != hits [ i ] )
				hits [ kept++ ] = hits [ i ];

		}

		hits.resize ( base::min ( kept, static_cast < size_t > ( limit ) ) );

		return hits.count ( );

	}

	int32_t scan ( const loader::binary& file, const pattern& find, bool code_only, base::list < uint64_t >& hits, int32_t limit ) {

		base::list < uint64_t > offsets;

		scan_offsets ( file, find, code_only, offsets, limit );
		hits.clear ( );

		for ( uint64_t offset : offsets ) {

			uint64_t address = 0;

			if ( file.type == loader::format::RAW )
				hits.push ( offset );
			else if ( file.address_of ( offset, address ) )
				hits.push ( address );

		}

		return hits.count ( );

	}

	static void wildcard ( pattern& out, size_t from, int32_t at, int32_t size ) {

		for ( int32_t i = 0; i < size; i++ ) {

			size_t index = from + static_cast < size_t > ( at + i );

			if ( index < out.mask.size ( ) ) {

				out.mask [ index ] = 0;
				out.bytes [ index ] = 0;

			}

		}

	}

	bool masked_instruction ( const database& db, const x86::instruction& ins, pattern& out ) {

		const loader::binary& file = *db.file;
		uint64_t offset = 0;

		if ( !file.offset_of ( ins.address, offset ) || offset + ins.length > file.size )
			return false;

		size_t from = out.bytes.size ( );

		for ( int32_t i = 0; i < ins.length; i++ )
			add_byte ( out, file.data [ offset + static_cast < uint64_t > ( i ) ], true );

		bool relative_branch = ins.count && ins.operands [ 0 ].type == x86::kind::REL;

		if ( relative_branch && ins.immediate_size >= 2 )
			wildcard ( out, from, ins.immediate_offset, ins.immediate_size );

		if ( ins.displacement_size == 4 ) {

			bool rip = false;
			bool absolute = false;

			for ( int32_t i = 0; i < ins.count; i++ ) {

				const x86::operand& item = ins.operands [ i ];

				if ( item.type != x86::kind::MEM )
					continue;

				if ( item.rip_relative ( ) )
					rip = true;

				uint64_t value = static_cast < uint64_t > ( item.value ) & ( db.long_mode ? ~0ull : 0xFFFFFFFFull );

				if ( !db.long_mode && file.section_at ( value ) > 0 )
					absolute = true;

			}

			if ( rip || absolute )
				wildcard ( out, from, ins.displacement_offset, ins.displacement_size );

		} else if ( ins.displacement_size == 8 )
			wildcard ( out, from, ins.displacement_offset, ins.displacement_size );

		if ( !relative_branch && ins.immediate_size >= 4 ) {

			for ( int32_t i = 0; i < ins.count; i++ ) {

				const x86::operand& item = ins.operands [ i ];

				if ( item.type != x86::kind::IMM )
					continue;

				uint64_t value = static_cast < uint64_t > ( item.value ) & ( db.long_mode ? ~0ull : 0xFFFFFFFFull );

				if ( file.section_at ( value ) > 0 )
					wildcard ( out, from, ins.immediate_offset, ins.immediate_size );

			}

		}

		return true;

	}

	static bool grow_unique ( const database& db, uint64_t start, uint64_t limit, int32_t max_bytes, pattern& out, int32_t& matches ) {

		const loader::binary& file = *db.file;
		base::list < uint64_t > candidates;
		uint64_t own = 0;
		uint64_t at = start;
		bool scanned = false;

		out.bytes.clear ( );
		out.mask.clear ( );
		matches = 0;

		if ( !file.offset_of ( start, own ) )
			return false;

		while ( at < limit && out.bytes.count ( ) < max_bytes ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				break;

			size_t before = out.bytes.size ( );

			if ( !masked_instruction ( db, ins, out ) )
				break;

			at = ins.next ( );

			bool fixed = false;

			for ( size_t i = 0; i < out.mask.size ( ); i++ ) {

				if ( out.mask [ i ] )
					fixed = true;

			}

			if ( !fixed )
				continue;

			if ( !scanned ) {

				if ( out.bytes.size ( ) < 4 && ins.control == x86::flow::NONE )
					continue;

				scan_offsets ( file, out, true, candidates, 1 << 20 );
				scanned = true;

			} else {

				size_t kept = 0;

				for ( size_t c = 0; c < candidates.size ( ); c++ ) {

					uint64_t base_offset = candidates [ c ];
					bool match = base_offset + out.bytes.size ( ) <= file.size;

					for ( size_t k = before; k < out.bytes.size ( ) && match; k++ ) {

						if ( out.mask [ k ] && file.data [ base_offset + k ] != out.bytes [ k ] )
							match = false;

					}

					if ( match )
						candidates [ kept++ ] = base_offset;

				}

				candidates.resize ( kept );

			}

			matches = candidates.count ( );

			if ( matches == 1 && candidates [ 0 ] == own && out.bytes.count ( ) >= 5 )
				break;

			if ( ins.control == x86::flow::RETURN || ins.control == x86::flow::STOP )
				break;

		}

		while ( !out.mask.empty ( ) && !out.mask.back ( ) ) {

			out.mask.pop ( );
			out.bytes.pop ( );

		}

		return matches == 1;

	}

	bool make_signature ( const database& db, uint64_t address, signature& out ) {

		out = signature ( );

		if ( !db.file || !db.is_covered ( address ) )
			return false;

		uint64_t start = db.instruction_at ( address );
		int32_t owner = db.function_at ( start );
		uint64_t limit = owner >= 0 ? db.functions [ static_cast < size_t > ( owner ) ].end : start + 512;

		out.target = address;
		out.start = start;
		out.target_offset = static_cast < int32_t > ( address - start );
		out.unique = grow_unique ( db, start, limit, 160, out.bytes, out.matches );

		if ( out.unique || owner < 0 )
			return out.unique;

		uint64_t function_start = db.functions [ static_cast < size_t > ( owner ) ].start;

		if ( address != function_start )
			return false;

		int32_t first = 0;
		int32_t count = 0;
		signature best;
		bool found = false;

		db.refs_to ( function_start, first, count );

		for ( int32_t i = 0; i < count && i < 48; i++ ) {

			const xref& item = db.refs [ static_cast < size_t > ( first + i ) ];

			if ( item.type != ref::CALL && item.type != ref::JUMP )
				continue;

			uint64_t site = db.file->base + item.source;
			int32_t caller = db.function_at ( site );
			uint64_t caller_end = caller >= 0 ? db.functions [ static_cast < size_t > ( caller ) ].end : site + 256;
			signature attempt;
			x86::instruction call;

			if ( !db.decode ( site, call ) )
				continue;

			if ( !grow_unique ( db, site, caller_end, 96, attempt.bytes, attempt.matches ) )
				continue;

			attempt.unique = true;
			attempt.via_reference = true;
			attempt.reference = site;
			attempt.start = site;
			attempt.target = function_start;
			attempt.target_offset = call.immediate_offset;

			if ( !found || attempt.bytes.bytes.count ( ) < best.bytes.bytes.count ( ) ) {

				best = attempt;
				found = true;

			}

		}

		if ( found ) {

			out = best;

			return true;

		}

		return false;

	}

	void format_pattern ( const pattern& value, pattern_style style, base::list < wchar_t >& out ) {

		static const wchar_t digits [ ] = L"0123456789ABCDEF";

		out.clear ( );

		auto add = [ & ] ( const wchar_t* text ) {

			for ( ; *text; text++ )
				out.push ( *text );

		};

		auto add_hex = [ & ] ( uint8_t byte ) {

			out.push ( digits [ byte >> 4 ] );
			out.push ( digits [ byte & 15 ] );

		};

		int32_t count = value.bytes.count ( );

		switch ( style ) {

			case pattern_style::IDA:
			case pattern_style::X64DBG:

				for ( int32_t i = 0; i < count; i++ ) {

					if ( i )
						out.push ( L' ' );

					if ( value.mask [ static_cast < size_t > ( i ) ] )
						add_hex ( value.bytes [ static_cast < size_t > ( i ) ] );
					else
						add ( style == pattern_style::IDA ? L"?" : L"??" );

				}

				break;

			case pattern_style::CODE:

				out.push ( L'"' );

				for ( int32_t i = 0; i < count; i++ ) {

					add ( L"\\x" );
					add_hex ( value.mask [ static_cast < size_t > ( i ) ] ? value.bytes [ static_cast < size_t > ( i ) ] : 0 );

				}

				add ( L"\" \"" );

				for ( int32_t i = 0; i < count; i++ )
					out.push ( value.mask [ static_cast < size_t > ( i ) ] ? L'x' : L'?' );

				out.push ( L'"' );

				break;

			default:

				add ( L"{ " );

				for ( int32_t i = 0; i < count; i++ ) {

					if ( i )
						add ( L", " );

					if ( value.mask [ static_cast < size_t > ( i ) ] ) {

						add ( L"0x" );
						add_hex ( value.bytes [ static_cast < size_t > ( i ) ] );

					} else
						add ( L"-1" );

				}

				add ( L" }" );

				break;

		}

		out.push ( 0 );

	}

	bool function_key ( const database& db, int32_t index, uint64_t& key ) {

		const function_info& info = db.functions [ static_cast < size_t > ( index ) ];
		uint64_t length = info.end - info.start;
		pattern body;
		int32_t instructions = 0;

		if ( length < 12 || length > 0x10000 || ( info.flags & THUNK ) )
			return false;

		for ( uint64_t at = info.start; at < info.end && body.bytes.size ( ) < 1024; ) {

			x86::instruction ins;

			if ( !db.is_start ( at ) || !db.decode ( at, ins ) ) {

				at++;

				continue;

			}

			if ( !masked_instruction ( db, ins, body ) )
				return false;

			instructions++;
			at = ins.next ( );

		}

		if ( instructions < 5 )
			return false;

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( size_t i = 0; i < body.bytes.size ( ); i++ ) {

			hash ^= body.mask [ i ] ? static_cast < uint64_t > ( body.bytes [ i ] ) : 0x100ull;
			hash *= 0x100000001B3ull;

		}

		hash ^= length * 0x9E3779B97F4A7C15ull;
		key = hash ? hash : 1;

		return true;

	}

}