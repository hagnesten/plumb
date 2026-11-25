#include "ui/app.hh"
#include "ui/tokens.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	int32_t app::patch_index ( uint64_t offset ) const {

		int32_t low = 0;
		int32_t high = patches.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			uint64_t value = patches [ static_cast < size_t > ( middle ) ].offset;

			if ( value == offset )
				return middle;

			if ( value < offset )
				low = middle + 1;
			else
				high = middle - 1;

		}

		return -( low + 1 );

	}

	bool app::is_patched ( uint64_t address, int32_t length ) const {

		uint64_t offset = 0;

		if ( patches.empty ( ) || !file.offset_of ( address, offset ) )
			return false;

		int32_t found = patch_index ( offset );
		int32_t next = found >= 0 ? found : -found - 1;

		return found >= 0 || ( next < patches.count ( ) && patches [ static_cast < size_t > ( next ) ].offset < offset + static_cast < uint64_t > ( length ) );

	}

	void app::set_byte ( uint64_t offset, uint8_t value ) {

		int32_t found = patch_index ( offset );

		if ( found >= 0 ) {

			patched_byte& item = patches [ static_cast < size_t > ( found ) ];

			item.value = value;

			if ( item.value == item.original )
				patches.remove_at ( static_cast < size_t > ( found ) );

		} else if ( file.byte ( offset ) != value ) {

			patched_byte item;
			int32_t at = -found - 1;

			item.offset = offset;
			item.original = file.byte ( offset );
			item.value = value;
			patches.push ( item );

			for ( int32_t i = patches.count ( ) - 1; i > at; i-- ) {

				patched_byte held = patches [ static_cast < size_t > ( i ) ];

				patches [ static_cast < size_t > ( i ) ] = patches [ static_cast < size_t > ( i - 1 ) ];
				patches [ static_cast < size_t > ( i - 1 ) ] = held;

			}

		}

		file.poke ( offset, value );

	}

	bool app::patch_bytes ( uint64_t address, const uint8_t* bytes, int32_t count ) {

		uint64_t offset = 0;

		if ( !file.offset_of ( address, offset ) || offset + static_cast < uint64_t > ( count ) > file.size ) {

			log ( log_kind::WARN, L"That address isn't backed by bytes in the file, so it can't be patched." );

			return false;

		}

		for ( int32_t i = 0; i < count; i++ )
			set_byte ( offset + static_cast < uint64_t > ( i ), bytes [ i ] );

		changed ( );
		reanalyze ( );

		return true;

	}

	void app::revert_patch ( uint64_t address ) {

		uint64_t offset = 0;

		if ( !file.offset_of ( address, offset ) )
			return;

		int32_t found = patch_index ( offset );

		if ( found < 0 ) {

			int32_t next = -found - 1;
			x86::instruction ins;
			int32_t length = db && db->decode ( address, ins ) ? ins.length : 1;

			if ( next >= patches.count ( ) || patches [ static_cast < size_t > ( next ) ].offset >= offset + static_cast < uint64_t > ( length ) ) {

				log ( log_kind::INFO, L"Nothing is patched here." );

				return;

			}

			found = next;

		}

		int32_t first = found;
		int32_t last = found;

		while ( first > 0 && patches [ static_cast < size_t > ( first - 1 ) ].offset + 1 == patches [ static_cast < size_t > ( first ) ].offset )
			first--;

		while ( last + 1 < patches.count ( ) && patches [ static_cast < size_t > ( last + 1 ) ].offset == patches [ static_cast < size_t > ( last ) ].offset + 1 )
			last++;

		remember_undo ( );

		for ( int32_t i = last; i >= first; i-- ) {

			file.poke ( patches [ static_cast < size_t > ( i ) ].offset, patches [ static_cast < size_t > ( i ) ].original );
			patches.remove_at ( static_cast < size_t > ( i ) );

		}

		log ( log_kind::INFO, base::writer ( ).add ( L"Reverted " ).number ( last - first + 1 ).add ( last == first ? L" patched byte." : L" patched bytes." ).text ( ) );
		changed ( );
		reanalyze ( );

	}

	bool app::patch_text ( uint64_t address, const wchar_t* text ) {

		uint8_t bytes [ 256 ];
		int32_t count = 0;
		int32_t nibbles = 0;
		uint32_t value = 0;

		for ( const wchar_t* at = text; *at; at++ ) {

			wchar_t c = *at;

			if ( c == L' ' || c == L',' ) {

				if ( nibbles == 1 && count < 256 )
					bytes [ count++ ] = static_cast < uint8_t > ( value );

				nibbles = 0;
				value = 0;

				continue;

			}

			if ( !nibbles && c == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' ) ) {

				at++;

				continue;

			}

			int32_t digit = c >= L'0' && c <= L'9' ? c - L'0' : ( c >= L'a' && c <= L'f' ? c - L'a' + 10 : ( c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1 ) );

			if ( digit < 0 ) {

				log ( log_kind::WARN, base::writer ( ).add ( L"Couldn't read \"" ).add ( text ).add ( L"\" as hex bytes. Try  90 90  or  EB05." ).text ( ) );

				return false;

			}

			value = value * 16 + static_cast < uint32_t > ( digit );
			nibbles++;

			if ( nibbles == 2 ) {

				if ( count < 256 )
					bytes [ count++ ] = static_cast < uint8_t > ( value );

				nibbles = 0;
				value = 0;

			}

		}

		if ( nibbles == 1 && count < 256 )
			bytes [ count++ ] = static_cast < uint8_t > ( value );

		if ( !count ) {

			log ( log_kind::WARN, L"No bytes to write." );

			return false;

		}

		if ( !patch_bytes ( address, bytes, count ) )
			return false;

		log ( log_kind::INFO, base::writer ( ).add ( L"Patched " ).number ( count ).add ( count == 1 ? L" byte at " : L" bytes at " ).hex ( address ).add ( L"." ).text ( ) );

		return true;

	}

	uint64_t app::patch_target ( ) const {

		if ( showing == view::HEX ) {

			uint64_t address = 0;

			return file.address_of ( now->hex_cursor, address ) ? address : now->cursor_address;

		}

		if ( db && analysis_applied && db->is_covered ( now->cursor_address ) )
			return db->instruction_at ( now->cursor_address );

		return now->cursor_address;

	}

	void app::export_patched ( ) {

		if ( !file.loaded ( ) )
			return;

		base::writer target;
		const wchar_t* path = file.path.get ( );
		int32_t length = 0;
		int32_t dot = -1;

		while ( path [ length ] ) {

			if ( path [ length ] == L'.' )
				dot = length;
			else if ( path [ length ] == L'\\' || path [ length ] == L'/' )
				dot = -1;

			length++;

		}

		for ( int32_t i = 0; i < ( dot >= 0 ? dot : length ); i++ )
			target.add ( path [ i ] );

		target.add ( L".patched" );

		if ( dot >= 0 )
			target.add ( path + dot );

		HANDLE handle = CreateFileW ( target.text ( ), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( handle == INVALID_HANDLE_VALUE ) {

			base::writer folder;

			folder.add ( files::folder ( ) ).add ( L"\\patched" );
			CreateDirectoryW ( folder.text ( ), nullptr );
			folder.add ( L"\\" ).add ( file.name.get ( ) );
			target.clear ( );
			target.add ( folder.text ( ) );
			handle = CreateFileW ( target.text ( ), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		}

		if ( handle == INVALID_HANDLE_VALUE ) {

			log ( log_kind::FAIL, L"Couldn't write the patched copy." );

			return;

		}

		uint64_t written_total = 0;

		while ( written_total < file.size ) {

			DWORD chunk = static_cast < DWORD > ( base::min ( file.size - written_total, static_cast < uint64_t > ( 1 << 26 ) ) );
			DWORD written = 0;

			if ( !WriteFile ( handle, file.data + written_total, chunk, &written, nullptr ) || !written )
				break;

			written_total += written;

		}

		CloseHandle ( handle );
		log ( written_total == file.size ? log_kind::GOOD : log_kind::FAIL, base::writer ( ).add ( L"Wrote " ).add ( target.text ( ) ).add ( L" with " ).number ( patches.count ( ) ).add ( patches.count ( ) == 1 ? L" patched byte." : L" patched bytes." ).text ( ) );

	}

	struct assembler_names : x86::resolver {

		const app* owner = nullptr;

		bool lookup ( const wchar_t* name, uint64_t& value ) override {

			return owner->parse_address ( name, value );

		}

	};

	bool app::assemble_at ( uint64_t address, const wchar_t* text ) {

		uint8_t bytes [ 32 ];
		wchar_t problem [ 200 ];
		assembler_names names;

		names.owner = this;

		int32_t made = x86::assemble ( text, address, file.wide ( ), &names, bytes, 16, problem, 200 );

		if ( made < 0 ) {

			log ( log_kind::WARN, problem );

			return false;

		}

		x86::instruction old;
		int32_t old_length = db && db->decode ( address, old ) ? old.length : made;
		int32_t total = made;

		while ( total < old_length )
			bytes [ total++ ] = 0x90;

		if ( !patch_bytes ( address, bytes, total ) )
			return false;

		if ( made > old_length )
			log ( log_kind::WARN, base::writer ( ).add ( L"The new instruction is " ).number ( made - old_length ).add ( L" bytes longer and overwrote the start of the next one." ).text ( ) );
		else if ( made < old_length )
			log ( log_kind::INFO, base::writer ( ).add ( L"Assembled " ).number ( made ).add ( L" bytes and filled " ).number ( old_length - made ).add ( L" with nop." ).text ( ) );
		else
			log ( log_kind::INFO, base::writer ( ).add ( L"Assembled " ).number ( made ).add ( L" bytes." ).text ( ) );

		return true;

	}

	const wchar_t* app::stack_string ( uint64_t address ) const {

		if ( !db || !analysis_applied )
			return nullptr;

		int32_t owner = db->function_at ( address );

		if ( owner < 0 )
			return nullptr;

		uint64_t start = db->functions [ static_cast < size_t > ( owner ) ].start;
		bool seen = false;

		for ( uint64_t item : stack_seen ) {

			if ( item == start )
				seen = true;

		}

		if ( !seen ) {

			stack_seen.push ( start );

			struct write {

				int64_t disp;
				int32_t size;
				uint64_t value;
				uint64_t address;
				int32_t base;

			};

			base::list < write > writes;
			uint64_t at = start;
			uint64_t end = db->functions [ static_cast < size_t > ( owner ) ].end;
			uint64_t known [ 16 ] = { };
			uint32_t valid = 0;

			while ( at < end && writes.count ( ) < 4096 ) {

				x86::instruction ins;

				if ( !db->is_start ( at ) || !db->decode ( at, ins ) ) {

					at++;

					continue;

				}

				const x86::operand& target = ins.operands [ 0 ];
				const x86::operand& source = ins.operands [ 1 ];
				int32_t family = target.type == x86::kind::REG ? x86::register_family ( target.reg ) : -1;

				if ( ins.name == x86::mnemonic::mov && ins.count == 2 && target.type == x86::kind::MEM && !target.index && ( target.base == x86::RSP || target.base == x86::RBP || target.base == x86::ESP || target.base == x86::EBP ) ) {

					int32_t from = source.type == x86::kind::REG ? x86::register_family ( source.reg ) : -1;
					bool constant = source.type == x86::kind::IMM || ( from >= 0 && from < 16 && ( valid & ( 1u << from ) ) && !x86::is_vector ( source.reg ) );

					if ( constant ) {

						write item = { target.value, target.size, source.type == x86::kind::IMM ? static_cast < uint64_t > ( source.value ) : known [ from ], at, target.base };

						writes.push ( item );

					}

				} else if ( ins.name == x86::mnemonic::mov && ins.count == 2 && family >= 0 && family < 16 && source.type == x86::kind::IMM && !x86::is_vector ( target.reg ) ) {

					known [ family ] = static_cast < uint64_t > ( source.value );
					valid |= 1u << family;

				} else if ( ins.control == x86::flow::CALL )
					valid = 0;
				else if ( family >= 0 && family < 16 )
					valid &= ~( 1u << family );

				at += ins.length;

			}

			for ( int32_t i = 1; i < writes.count ( ); i++ ) {

				for ( int32_t k = i; k > 0 && ( writes [ static_cast < size_t > ( k - 1 ) ].base > writes [ static_cast < size_t > ( k ) ].base || ( writes [ static_cast < size_t > ( k - 1 ) ].base == writes [ static_cast < size_t > ( k ) ].base && writes [ static_cast < size_t > ( k - 1 ) ].disp > writes [ static_cast < size_t > ( k ) ].disp ) ); k-- ) {

					write held = writes [ static_cast < size_t > ( k ) ];

					writes [ static_cast < size_t > ( k ) ] = writes [ static_cast < size_t > ( k - 1 ) ];
					writes [ static_cast < size_t > ( k - 1 ) ] = held;

				}

			}

			int32_t i = 0;

			while ( i < writes.count ( ) ) {

				uint8_t bytes [ 512 ];
				int32_t length = 0;
				int32_t j = i;
				int64_t next_disp = writes [ static_cast < size_t > ( i ) ].disp;
				uint64_t anchor = writes [ static_cast < size_t > ( i ) ].address;

				while ( j < writes.count ( ) && writes [ static_cast < size_t > ( j ) ].base == writes [ static_cast < size_t > ( i ) ].base && writes [ static_cast < size_t > ( j ) ].disp == next_disp && length + writes [ static_cast < size_t > ( j ) ].size <= 512 ) {

					const write& item = writes [ static_cast < size_t > ( j ) ];

					for ( int32_t b = 0; b < item.size; b++ )
						bytes [ length++ ] = static_cast < uint8_t > ( item.value >> ( b * 8 ) );

					anchor = base::min ( anchor, item.address );
					next_disp += item.size;
					j++;

				}

				bool wide = length >= 8 && bytes [ 1 ] == 0 && bytes [ 3 ] == 0 && bytes [ 0 ] >= 0x20;
				int32_t step = wide ? 2 : 1;
				base::writer text;
				int32_t printable = 0;

				for ( int32_t b = 0; b + step - 1 < length; b += step ) {

					uint8_t value = bytes [ b ];

					if ( value == 0 && ( !wide || bytes [ b + 1 ] == 0 ) )
						break;

					if ( value < 0x20 || value > 0x7E || ( wide && bytes [ b + 1 ] != 0 ) ) {

						printable = 0;

						break;

					}

					text.add ( static_cast < wchar_t > ( value ) );
					printable++;

				}

				if ( printable >= 4 ) {

					stack_text& found = stack_texts.emplace ( );

					found.address = anchor;
					found.text.set ( base::writer ( ).add ( wide ? L"; stack string L\"" : L"; stack string \"" ).add ( text.text ( ) ).add ( L"\"" ).text ( ) );

				}

				i = j > i ? j : i + 1;

			}

		}

		for ( const stack_text& item : stack_texts ) {

			if ( item.address == address )
				return item.text.get ( );

		}

		return nullptr;

	}

	void app::xor_decode ( uint64_t address, const wchar_t* spec ) {

		uint64_t offset = 0;

		if ( !file.offset_of ( address, offset ) ) {

			log ( log_kind::WARN, L"Put the cursor on bytes in the file to decode them." );

			return;

		}

		uint8_t key [ 64 ];
		int32_t key_length = 0;
		int32_t limit = 0;
		bool automatic = !spec [ 0 ] || ( spec [ 0 ] == L'a' && spec [ 1 ] == L'u' && spec [ 2 ] == L't' && spec [ 3 ] == L'o' );

		if ( !automatic ) {

			const wchar_t* at = spec;

			while ( *at ) {

				while ( *at == L' ' || *at == L',' )
					at++;

				if ( !*at )
					break;

				if ( at [ 0 ] == L'0' && ( at [ 1 ] == L'x' || at [ 1 ] == L'X' ) )
					at += 2;

				int32_t digits = 0;
				uint32_t value = 0;
				bool hex_word = true;

				for ( const wchar_t* probe = at; *probe && *probe != L' ' && *probe != L','; probe++ ) {

					wchar_t c = *probe;

					if ( !( ( c >= L'0' && c <= L'9' ) || ( c >= L'a' && c <= L'f' ) || ( c >= L'A' && c <= L'F' ) ) )
						hex_word = false;

					digits++;

				}

				if ( !hex_word )
					break;

				if ( digits > 2 && key_length > 0 ) {

					for ( int32_t k = 0; k < digits; k++ )
						limit = limit * 10 + ( at [ k ] - L'0' );

					break;

				}

				for ( int32_t k = 0; k < digits; k++ ) {

					wchar_t c = at [ k ];

					value = value * 16 + static_cast < uint32_t > ( c >= L'a' ? c - L'a' + 10 : ( c >= L'A' ? c - L'A' + 10 : c - L'0' ) );

					if ( k % 2 == 1 && key_length < 64 ) {

						key [ key_length++ ] = static_cast < uint8_t > ( value );
						value = 0;

					}

				}

				if ( digits % 2 == 1 && key_length < 64 )
					key [ key_length++ ] = static_cast < uint8_t > ( value );

				at += digits;

			}

			if ( !key_length ) {

				log ( log_kind::WARN, L"Give the key as hex bytes, like  5A  or  DE AD BE EF 32  (a decimal length at the end is optional), or  auto." );

				return;

			}

		} else {

			int32_t best = -1;
			int32_t best_score = -1;

			for ( int32_t k = 1; k < 256; k++ ) {

				int32_t score = 0;

				for ( int32_t b = 0; b < 64 && offset + static_cast < uint64_t > ( b ) < file.size; b++ ) {

					uint8_t value = static_cast < uint8_t > ( file.byte ( offset + static_cast < uint64_t > ( b ) ) ^ k );
					bool word = ( value >= 'a' && value <= 'z' ) || ( value >= 'A' && value <= 'Z' ) || ( value >= '0' && value <= '9' ) || value == ' ';

					if ( value == 0 ) {

						if ( b >= 4 )
							score += 6;

						break;

					}

					if ( word )
						score += 2;
					else if ( ( value > 0x20 && value < 0x7F ) || value == '\n' || value == '\r' || value == '\t' )
						score -= 1;
					else {

						score -= 6;

						break;

					}

				}

				if ( score > best_score ) {

					best_score = score;
					best = k;

				}

			}

			if ( best_score < 4 ) {

				log ( log_kind::INFO, L"No single-byte XOR key turns these bytes into text." );

				return;

			}

			key [ 0 ] = static_cast < uint8_t > ( best );
			key_length = 1;

		}

		if ( limit <= 0 )
			limit = 256;

		base::writer text;
		int32_t shown = 0;

		for ( int32_t b = 0; b < limit && offset + static_cast < uint64_t > ( b ) < file.size; b++ ) {

			uint8_t value = static_cast < uint8_t > ( file.byte ( offset + static_cast < uint64_t > ( b ) ) ^ key [ b % key_length ] );

			if ( value == 0 && limit == 256 )
				break;

			if ( value >= 0x20 && value < 0x7F )
				text.add ( static_cast < wchar_t > ( value ) );
			else
				text.add ( L"\\x" ).hex ( value, 2 );

			shown++;

		}

		base::writer key_text;

		for ( int32_t k = 0; k < key_length; k++ )
			key_text.hex ( key [ k ], 2 );

		base::writer note;

		note.add ( L"xor " ).add ( key_text.text ( ) ).add ( L": \"" ).add ( text.text ( ) ).add ( L"\"" );
		set_user_text ( comments, address, note.text ( ) );
		changed ( );
		log ( log_kind::GOOD, base::writer ( ).add ( L"Decoded " ).number ( shown ).add ( L" bytes with key " ).add ( key_text.text ( ) ).add ( L": \"" ).add ( text.text ( ) ).add ( L"\"  (added as a comment)" ).text ( ) );

	}

	void app::search_everything ( const wchar_t* query ) {

		search_hits.clear ( );
		search_query.set ( query );

		if ( !db || !analysis_applied || !query [ 0 ] )
			return;

		double started = base::seconds ( );
		listing_names namer;
		wchar_t text [ 400 ];
		wchar_t needle [ 200 ];
		int32_t needle_length = 0;
		int32_t mode = 0;
		int64_t wanted = 0;
		const wchar_t* body = query;

		namer.owner = this;

		if ( body [ 0 ] == L'#' ) {

			mode = 1;
			body++;

			bool hex = body [ 0 ] == L'0' && ( body [ 1 ] == L'x' || body [ 1 ] == L'X' );
			bool negative = body [ 0 ] == L'-';

			if ( negative )
				body++;

			if ( hex )
				body += 2;

			for ( const wchar_t* at = body; *at; at++ ) {

				if ( ( *at >= L'a' && *at <= L'f' ) || ( *at >= L'A' && *at <= L'F' ) )
					hex = true;

			}

			for ( const wchar_t* at = body; *at; at++ ) {

				wchar_t c = *at;
				int32_t digit = c >= L'0' && c <= L'9' ? c - L'0' : ( c >= L'a' && c <= L'f' ? c - L'a' + 10 : ( c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1 ) );

				if ( digit < 0 )
					break;

				wanted = wanted * ( hex ? 16 : 10 ) + digit;

			}

			if ( negative )
				wanted = -wanted;

		} else if ( body [ 0 ] == L'c' && body [ 1 ] == L':' ) {

			mode = 2;
			body += 2;

		}

		for ( ; body [ needle_length ] && needle_length < 199; needle_length++ ) {

			wchar_t c = body [ needle_length ];

			needle [ needle_length ] = c >= L'A' && c <= L'Z' ? static_cast < wchar_t > ( c + 32 ) : c;

		}

		needle [ needle_length ] = 0;

		auto contains = [ & ] ( const wchar_t* haystack, int32_t length ) {

			for ( int32_t i = 0; i + needle_length <= length; i++ ) {

				bool match = true;

				for ( int32_t k = 0; k < needle_length && match; k++ ) {

					wchar_t c = haystack [ i + k ];

					if ( c >= L'A' && c <= L'Z' )
						c = static_cast < wchar_t > ( c + 32 );

					match = c == needle [ k ];

				}

				if ( match )
					return true;

			}

			return false;

		};

		int32_t searched = 0;
		bool out_of_time = false;

		for ( int32_t f = 0; f < db->functions.count ( ) && search_hits.count ( ) < 5000; f++ ) {

			const analysis::function_info& item = db->functions [ static_cast < size_t > ( f ) ];

			searched++;

			if ( mode == 2 ) {

				if ( ( item.flags & analysis::THUNK ) )
					continue;

				if ( base::seconds ( ) - started > 6.0 ) {

					out_of_time = true;

					break;

				}

				const decompiler::result* code = pseudocode_for ( item.start );

				if ( !code || !needle_length )
					continue;

				for ( const decompiler::line& line : code->lines ) {

					if ( !contains ( code->text.data ( ) + line.text, line.length ) )
						continue;

					search_hit& hit = search_hits.emplace ( );
					int32_t length = base::min ( line.length, 200 );

					hit.address = line.address ? line.address : item.start;
					hit.length = 1;

					for ( int32_t k = 0; k < length; k++ )
						text [ k ] = code->text [ static_cast < size_t > ( line.text + k ) ];

					text [ length ] = 0;
					hit.note.set ( text );

					if ( search_hits.count ( ) >= 5000 )
						break;

				}

				continue;

			}

			uint64_t at = item.start;

			while ( at < item.end && search_hits.count ( ) < 5000 ) {

				x86::instruction ins;

				if ( !db->decode ( at, ins ) ) {

					at++;

					continue;

				}

				bool hit_here = false;

				if ( mode == 1 ) {

					for ( int32_t i = 0; i < ins.count; i++ ) {

						const x86::operand& operand = ins.operands [ i ];

						if ( ( operand.type == x86::kind::IMM || operand.type == x86::kind::MEM ) && operand.value == wanted && !operand.rip_relative ( ) )
							hit_here = true;

					}

				} else {

					int32_t length = x86::format_text ( ins, text, 400, &namer, 0 );

					hit_here = needle_length && contains ( text, length );

				}

				if ( hit_here ) {

					search_hit& hit = search_hits.emplace ( );

					x86::format_text ( ins, text, 400, &namer, 0 );
					hit.address = at;
					hit.length = ins.length;
					hit.note.set ( text );

				}

				at += ins.length;

			}

		}

		search_seconds = base::seconds ( ) - started;
		browsing = panel::SEARCH;
		browser_open = true;
		refresh_filter ( panel::SEARCH );
		base::writer line;

		line.add ( L"Found " ).grouped ( static_cast < uint64_t > ( search_hits.count ( ) ) ).add ( search_hits.count ( ) == 1 ? L" match" : L" matches" ).add ( search_hits.count ( ) >= 5000 ? L" (stopped at 5,000)" : L"" ).add ( L" in " ).fixed ( search_seconds * 1000.0, 0 ).add ( L" ms" );

		if ( out_of_time )
			line.add ( L". Stopped after 6 seconds, " ).grouped ( static_cast < uint64_t > ( searched ) ).add ( L" of " ).grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L" functions searched." );
		else
			line.add ( L"." );

		log ( search_hits.empty ( ) ? log_kind::WARN : log_kind::INFO, line.text ( ) );

	}

}