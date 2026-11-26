#include "x86/assembler/assembler.h"
#include "x86/decoder/decoder.h"

namespace x86 {

	struct named_register {

		const char* name;
		int8_t number;
		int8_t size;
		bool high;
		bool rex;

	};

	static const named_register registers [ ] = {
		{ "rax", 0, 8, false, false }, { "rcx", 1, 8, false, false }, { "rdx", 2, 8, false, false }, { "rbx", 3, 8, false, false },
		{ "rsp", 4, 8, false, false }, { "rbp", 5, 8, false, false }, { "rsi", 6, 8, false, false }, { "rdi", 7, 8, false, false },
		{ "r8", 8, 8, false, false }, { "r9", 9, 8, false, false }, { "r10", 10, 8, false, false }, { "r11", 11, 8, false, false },
		{ "r12", 12, 8, false, false }, { "r13", 13, 8, false, false }, { "r14", 14, 8, false, false }, { "r15", 15, 8, false, false },
		{ "eax", 0, 4, false, false }, { "ecx", 1, 4, false, false }, { "edx", 2, 4, false, false }, { "ebx", 3, 4, false, false },
		{ "esp", 4, 4, false, false }, { "ebp", 5, 4, false, false }, { "esi", 6, 4, false, false }, { "edi", 7, 4, false, false },
		{ "r8d", 8, 4, false, false }, { "r9d", 9, 4, false, false }, { "r10d", 10, 4, false, false }, { "r11d", 11, 4, false, false },
		{ "r12d", 12, 4, false, false }, { "r13d", 13, 4, false, false }, { "r14d", 14, 4, false, false }, { "r15d", 15, 4, false, false },
		{ "ax", 0, 2, false, false }, { "cx", 1, 2, false, false }, { "dx", 2, 2, false, false }, { "bx", 3, 2, false, false },
		{ "sp", 4, 2, false, false }, { "bp", 5, 2, false, false }, { "si", 6, 2, false, false }, { "di", 7, 2, false, false },
		{ "r8w", 8, 2, false, false }, { "r9w", 9, 2, false, false }, { "r10w", 10, 2, false, false }, { "r11w", 11, 2, false, false },
		{ "r12w", 12, 2, false, false }, { "r13w", 13, 2, false, false }, { "r14w", 14, 2, false, false }, { "r15w", 15, 2, false, false },
		{ "al", 0, 1, false, false }, { "cl", 1, 1, false, false }, { "dl", 2, 1, false, false }, { "bl", 3, 1, false, false },
		{ "spl", 4, 1, false, true }, { "bpl", 5, 1, false, true }, { "sil", 6, 1, false, true }, { "dil", 7, 1, false, true },
		{ "ah", 4, 1, true, false }, { "ch", 5, 1, true, false }, { "dh", 6, 1, true, false }, { "bh", 7, 1, true, false },
		{ "r8b", 8, 1, false, false }, { "r9b", 9, 1, false, false }, { "r10b", 10, 1, false, false }, { "r11b", 11, 1, false, false },
		{ "r12b", 12, 1, false, false }, { "r13b", 13, 1, false, false }, { "r14b", 14, 1, false, false }, { "r15b", 15, 1, false, false }
	};

	static const char* conditions [ 16 ] [ 3 ] = {
		{ "o", nullptr, nullptr }, { "no", nullptr, nullptr }, { "b", "c", "nae" }, { "ae", "nb", "nc" },
		{ "e", "z", nullptr }, { "ne", "nz", nullptr }, { "be", "na", nullptr }, { "a", "nbe", nullptr },
		{ "s", nullptr, nullptr }, { "ns", nullptr, nullptr }, { "p", "pe", nullptr }, { "np", "po", nullptr },
		{ "l", "nge", nullptr }, { "ge", "nl", nullptr }, { "le", "ng", nullptr }, { "g", "nle", nullptr }
	};

	enum class kind_of : int32_t {

		NONE,
		REG,
		MEM,
		IMM,

	};

	struct piece {

		kind_of kind = kind_of::NONE;
		int32_t reg = 0;
		int32_t size = 0;
		bool high = false;
		bool rex = false;
		int32_t base = -1;
		int32_t index = -1;
		int32_t scale = 1;
		int64_t value = 0;
		bool rip = false;
		bool absolute = false;
		bool symbol = false;

	};

	struct writer {

		uint8_t* out;
		int32_t room;
		int32_t length = 0;
		bool failed = false;

		void put ( uint8_t value ) {

			if ( length < room )
				out [ length++ ] = value;
			else
				failed = true;

		}

		void put_value ( int64_t value, int32_t bytes ) {

			for ( int32_t i = 0; i < bytes; i++ )
				put ( static_cast < uint8_t > ( static_cast < uint64_t > ( value ) >> ( i * 8 ) ) );

		}

	};

	static bool same ( const char* a, const char* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	static bool fits8 ( int64_t value ) {

		return value >= -128 && value <= 127;

	}

	static bool fits32 ( int64_t value ) {

		return value >= -2147483648ll && value <= 2147483647ll;

	}

	static const named_register* find_register ( const char* word ) {

		for ( const named_register& item : registers ) {

			if ( same ( item.name, word ) )
				return &item;

		}

		return nullptr;

	}

	static int32_t find_condition ( const char* word ) {

		for ( int32_t i = 0; i < 16; i++ ) {

			for ( int32_t k = 0; k < 3; k++ ) {

				if ( conditions [ i ] [ k ] && same ( conditions [ i ] [ k ], word ) )
					return i;

			}

		}

		return -1;

	}

	static bool parse_number ( const char* text, int64_t& value ) {

		int32_t length = 0;
		bool hex = false;
		uint64_t result = 0;

		while ( text [ length ] )
			length++;

		if ( !length )
			return false;

		if ( length > 2 && text [ 0 ] == '0' && text [ 1 ] == 'x' ) {

			text += 2;
			length -= 2;
			hex = true;

		} else if ( length > 1 && text [ length - 1 ] == 'h' ) {

			length--;
			hex = true;

		} else if ( text [ 0 ] >= '0' && text [ 0 ] <= '9' ) {

			hex = length >= 8;

			for ( int32_t i = 0; i < length; i++ )
				hex = hex || ( text [ i ] >= 'a' && text [ i ] <= 'f' );

		}

		for ( int32_t i = 0; i < length; i++ ) {

			char c = text [ i ];
			int32_t digit = c >= '0' && c <= '9' ? c - '0' : ( hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1 );

			if ( digit < 0 )
				return false;

			result = result * ( hex ? 16 : 10 ) + static_cast < uint64_t > ( digit );

		}

		value = static_cast < int64_t > ( result );

		return true;

	}

	int32_t assemble ( const wchar_t* text, uint64_t address, bool long_mode, resolver* names, uint8_t* out, int32_t room, wchar_t* problem, int32_t problem_room ) {

		char line [ 256 ];
		int32_t length = 0;

		auto fail = [ & ] ( const wchar_t* message ) {

			int32_t i = 0;

			for ( ; message [ i ] && i < problem_room - 1; i++ )
				problem [ i ] = message [ i ];

			problem [ i ] = 0;

			return -1;

		};

		for ( ; text [ length ] && length < 255; length++ ) {

			wchar_t c = text [ length ];

			if ( c >= L'A' && c <= L'Z' )
				c = static_cast < wchar_t > ( c + 32 );

			line [ length ] = c < 128 ? static_cast < char > ( c ) : '?';

		}

		line [ length ] = 0;

		char mnemonic [ 32 ];
		int32_t at = 0;
		int32_t m = 0;

		while ( line [ at ] == ' ' || line [ at ] == '\t' )
			at++;

		while ( line [ at ] && line [ at ] != ' ' && line [ at ] != '\t' && m < 31 )
			mnemonic [ m++ ] = line [ at++ ];

		mnemonic [ m ] = 0;

		if ( !m )
			return fail ( L"Type an instruction, like  mov eax, 1  or  jmp 140001A00." );

		bool lock = false;

		if ( same ( mnemonic, "lock" ) ) {

			lock = true;
			m = 0;

			while ( line [ at ] == ' ' )
				at++;

			while ( line [ at ] && line [ at ] != ' ' && m < 31 )
				mnemonic [ m++ ] = line [ at++ ];

			mnemonic [ m ] = 0;

		}

		piece operands [ 3 ];
		int32_t count = 0;

		while ( line [ at ] && count < 3 ) {

			while ( line [ at ] == ' ' || line [ at ] == ',' )
				at++;

			if ( !line [ at ] )
				break;

			char part [ 128 ];
			int32_t p = 0;
			int32_t depth = 0;

			while ( line [ at ] && ( line [ at ] != ',' || depth > 0 ) && p < 127 ) {

				if ( line [ at ] == '[' )
					depth++;
				else if ( line [ at ] == ']' )
					depth--;

				part [ p++ ] = line [ at++ ];

			}

			while ( p && part [ p - 1 ] == ' ' )
				p--;

			part [ p ] = 0;

			piece& item = operands [ count++ ];
			char* cursor = part;
			int32_t size_hint = 0;

			static const char* const size_words [ 4 ] = { "byte", "word", "dword", "qword" };

			for ( int32_t s = 0; s < 4; s++ ) {

				int32_t n = 0;

				while ( size_words [ s ] [ n ] && cursor [ n ] == size_words [ s ] [ n ] )
					n++;

				if ( !size_words [ s ] [ n ] && ( cursor [ n ] == ' ' || cursor [ n ] == '[' ) ) {

					size_hint = 1 << s;
					cursor += n;

					while ( *cursor == ' ' )
						cursor++;

					if ( cursor [ 0 ] == 'p' && cursor [ 1 ] == 't' && cursor [ 2 ] == 'r' ) {

						cursor += 3;

						while ( *cursor == ' ' )
							cursor++;

					}

				}

			}

			if ( *cursor == '[' ) {

				item.kind = kind_of::MEM;
				item.size = size_hint;
				cursor++;

				char term [ 64 ];
				int32_t sign = 1;

				while ( *cursor && *cursor != ']' ) {

					while ( *cursor == ' ' )
						cursor++;

					if ( *cursor == '+' || *cursor == '-' ) {

						sign = *cursor == '-' ? -1 : 1;
						cursor++;

						continue;

					}

					int32_t n = 0;

					while ( *cursor && *cursor != '+' && *cursor != '-' && *cursor != ']' && *cursor != ' ' && n < 63 )
						term [ n++ ] = *cursor++;

					term [ n ] = 0;

					if ( !n )
						break;

					char* star = nullptr;

					for ( int32_t k = 0; term [ k ]; k++ ) {

						if ( term [ k ] == '*' )
							star = term + k;

					}

					if ( star ) {

						*star = 0;

						const named_register* reg = find_register ( term );
						int64_t factor = 0;

						if ( !reg ) {

							reg = find_register ( star + 1 );
							parse_number ( term, factor );

						} else
							parse_number ( star + 1, factor );

						if ( !reg || ( factor != 1 && factor != 2 && factor != 4 && factor != 8 ) || item.index >= 0 )
							return fail ( L"Couldn't read the memory operand. Use [base + index*scale + offset]." );

						item.index = reg->number;
						item.scale = static_cast < int32_t > ( factor );

						continue;

					}

					if ( same ( term, "rip" ) ) {

						item.rip = true;

						continue;

					}

					const named_register* reg = find_register ( term );

					if ( reg ) {

						if ( item.base < 0 )
							item.base = reg->number;
						else if ( item.index < 0 )
							item.index = reg->number;
						else
							return fail ( L"Too many registers in the memory operand." );

						continue;

					}

					int64_t number = 0;

					if ( parse_number ( term, number ) ) {

						item.value += sign * number;

						continue;

					}

					wchar_t wide [ 64 ];
					uint64_t resolved = 0;

					for ( int32_t k = 0; k <= n; k++ )
						wide [ k ] = static_cast < wchar_t > ( static_cast < uint8_t > ( term [ k ] ) );

					if ( names && names->lookup ( wide, resolved ) ) {

						item.value += sign * static_cast < int64_t > ( resolved );
						item.symbol = true;

						continue;

					}

					return fail ( L"Unknown name or number inside the brackets." );

				}

				if ( item.base < 0 && item.index < 0 )
					item.absolute = true;

				continue;

			}

			const named_register* reg = find_register ( cursor );

			if ( reg ) {

				item.kind = kind_of::REG;
				item.reg = reg->number;
				item.size = reg->size;
				item.high = reg->high;
				item.rex = reg->rex;

				continue;

			}

			item.kind = kind_of::IMM;
			item.size = size_hint;

			int64_t total = 0;
			int32_t sign = 1;

			while ( *cursor ) {

				while ( *cursor == ' ' )
					cursor++;

				if ( *cursor == '+' || *cursor == '-' ) {

					sign = *cursor == '-' ? -1 : 1;
					cursor++;

					continue;

				}

				char term [ 64 ];
				int32_t n = 0;

				while ( *cursor && *cursor != '+' && *cursor != '-' && *cursor != ' ' && n < 63 )
					term [ n++ ] = *cursor++;

				term [ n ] = 0;

				if ( !n )
					break;

				int64_t number = 0;

				if ( parse_number ( term, number ) ) {

					total += sign * number;

					continue;

				}

				wchar_t wide [ 64 ];
				uint64_t resolved = 0;

				for ( int32_t k = 0; k <= n; k++ )
					wide [ k ] = static_cast < wchar_t > ( static_cast < uint8_t > ( term [ k ] ) );

				if ( names && names->lookup ( wide, resolved ) ) {

					total += sign * static_cast < int64_t > ( resolved );
					item.symbol = true;

					continue;

				}

				return fail ( L"Unknown register, number or name." );

			}

			item.value = total;

		}

		writer w = { out, room };

		auto rex_for = [ & ] ( bool wide, int32_t reg_field, const piece& rm ) {

			uint8_t rex = 0;

			if ( wide )
				rex |= 8;

			if ( reg_field & 8 )
				rex |= 4;

			if ( rm.kind == kind_of::REG ) {

				if ( rm.reg & 8 )
					rex |= 1;

			} else {

				if ( rm.index >= 8 )
					rex |= 2;

				if ( rm.base >= 8 )
					rex |= 1;

			}

			return rex;

		};

		auto encode = [ & ] ( int32_t size, const uint8_t* opcode, int32_t opcode_length, int32_t reg_field, bool reg_rex8, const piece& rm, int32_t immediate_bytes, int64_t immediate, bool default64 ) {

			int32_t start = w.length;

			if ( lock )
				w.put ( 0xF0 );

			if ( size == 2 )
				w.put ( 0x66 );

			uint8_t rex = long_mode ? rex_for ( size == 8 && !default64, reg_field, rm ) : 0;

			if ( long_mode && ( reg_rex8 || ( rm.kind == kind_of::REG && rm.rex ) ) )
				rex |= 0x40;

			if ( rex )
				w.put ( static_cast < uint8_t > ( 0x40 | rex ) );

			for ( int32_t i = 0; i < opcode_length; i++ )
				w.put ( opcode [ i ] );

			int32_t field = reg_field & 7;

			if ( rm.kind == kind_of::REG ) {

				w.put ( static_cast < uint8_t > ( 0xC0 | ( field << 3 ) | ( rm.reg & 7 ) ) );
				w.put_value ( immediate, immediate_bytes );

				return w.length - start;

			}

			bool rip_relative = rm.rip || ( long_mode && rm.absolute );

			if ( rip_relative ) {

				w.put ( static_cast < uint8_t > ( ( field << 3 ) | 5 ) );

				int32_t after = w.length + 4 + immediate_bytes;

				if ( rm.rip && !rm.symbol )
					w.put_value ( rm.value, 4 );
				else
					w.put_value ( rm.value - static_cast < int64_t > ( address + static_cast < uint64_t > ( after ) ), 4 );

				w.put_value ( immediate, immediate_bytes );

				return w.length - start;

			}

			if ( rm.absolute ) {

				w.put ( static_cast < uint8_t > ( ( field << 3 ) | 5 ) );
				w.put_value ( rm.value, 4 );
				w.put_value ( immediate, immediate_bytes );

				return w.length - start;

			}

			int32_t base = rm.base;
			int32_t index = rm.index;
			int64_t displacement = rm.value;
			int32_t mod = 0;

			if ( base < 0 ) {

				int32_t scale_bits = rm.scale == 8 ? 3 : ( rm.scale == 4 ? 2 : ( rm.scale == 2 ? 1 : 0 ) );

				w.put ( static_cast < uint8_t > ( ( field << 3 ) | 4 ) );
				w.put ( static_cast < uint8_t > ( ( scale_bits << 6 ) | ( ( index & 7 ) << 3 ) | 5 ) );
				w.put_value ( displacement, 4 );
				w.put_value ( immediate, immediate_bytes );

				return w.length - start;

			}

			if ( displacement == 0 && ( base & 7 ) != 5 )
				mod = 0;
			else if ( fits8 ( displacement ) )
				mod = 1;
			else
				mod = 2;

			if ( index >= 0 || ( base & 7 ) == 4 ) {

				int32_t scale_bits = rm.scale == 8 ? 3 : ( rm.scale == 4 ? 2 : ( rm.scale == 2 ? 1 : 0 ) );
				int32_t index_bits = index >= 0 ? ( index & 7 ) : 4;

				w.put ( static_cast < uint8_t > ( ( mod << 6 ) | ( field << 3 ) | 4 ) );
				w.put ( static_cast < uint8_t > ( ( scale_bits << 6 ) | ( index_bits << 3 ) | ( base & 7 ) ) );

			} else
				w.put ( static_cast < uint8_t > ( ( mod << 6 ) | ( field << 3 ) | ( base & 7 ) ) );

			if ( mod == 1 )
				w.put_value ( displacement, 1 );
			else if ( mod == 2 )
				w.put_value ( displacement, 4 );

			w.put_value ( immediate, immediate_bytes );

			return w.length - start;

		};

		auto size_of = [ & ] ( const piece& a, const piece& b ) {

			if ( a.kind == kind_of::REG )
				return a.size;

			if ( b.kind == kind_of::REG )
				return b.size;

			return a.size ? a.size : b.size;

		};

		auto imm_bytes = [ ] ( int32_t size ) {

			return size == 1 ? 1 : ( size == 2 ? 2 : 4 );

		};

		const piece& a = operands [ 0 ];
		const piece& b = operands [ 1 ];
		const piece& c = operands [ 2 ];

		static const char* const alu [ 8 ] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };

		for ( int32_t n = 0; n < 8; n++ ) {

			if ( !same ( mnemonic, alu [ n ] ) )
				continue;

			if ( count != 2 )
				return fail ( L"This instruction takes two operands." );

			int32_t size = size_of ( a, b );

			if ( !size )
				return fail ( L"Say the operand size, like  dword ptr [rcx+8]." );

			if ( b.kind == kind_of::IMM ) {

				if ( size == 1 ) {

					uint8_t op = 0x80;

					encode ( 1, &op, 1, n, false, a, 1, b.value, false );

				} else if ( fits8 ( b.value ) ) {

					uint8_t op = 0x83;

					encode ( size, &op, 1, n, false, a, 1, b.value, false );

				} else {

					uint8_t op = 0x81;

					encode ( size, &op, 1, n, false, a, imm_bytes ( size ), b.value, false );

				}

			} else if ( b.kind == kind_of::REG ) {

				uint8_t op = static_cast < uint8_t > ( n * 8 + ( size == 1 ? 0 : 1 ) );

				encode ( size, &op, 1, b.reg, b.rex, a, 0, 0, false );

			} else if ( a.kind == kind_of::REG ) {

				uint8_t op = static_cast < uint8_t > ( n * 8 + ( size == 1 ? 2 : 3 ) );

				encode ( size, &op, 1, a.reg, a.rex, b, 0, 0, false );

			} else
				return fail ( L"Two memory operands aren't allowed." );

			goto done;

		}

		if ( same ( mnemonic, "mov" ) || same ( mnemonic, "movabs" ) ) {

			if ( count != 2 )
				return fail ( L"mov takes two operands." );

			int32_t size = size_of ( a, b );

			if ( !size )
				return fail ( L"Say the operand size, like  dword ptr [rcx+8]." );

			if ( b.kind == kind_of::IMM ) {

				if ( a.kind == kind_of::REG ) {

					if ( size == 8 && fits32 ( b.value ) && !b.symbol ) {

						uint8_t op = 0xC7;

						encode ( 8, &op, 1, 0, false, a, 4, b.value, false );

					} else {

						uint8_t op = static_cast < uint8_t > ( ( size == 1 ? 0xB0 : 0xB8 ) + ( a.reg & 7 ) );

						if ( long_mode ) {

							uint8_t rex = static_cast < uint8_t > ( ( size == 8 ? 8 : 0 ) | ( a.reg & 8 ? 1 : 0 ) );

							if ( size == 2 )
								w.put ( 0x66 );

							if ( rex || a.rex )
								w.put ( static_cast < uint8_t > ( 0x40 | rex ) );

						} else if ( size == 2 )
							w.put ( 0x66 );

						w.put ( op );
						w.put_value ( b.value, size == 8 ? 8 : imm_bytes ( size ) );

					}

				} else {

					uint8_t op = size == 1 ? 0xC6 : 0xC7;

					encode ( size, &op, 1, 0, false, a, imm_bytes ( size ), b.value, false );

				}

			} else if ( b.kind == kind_of::REG ) {

				uint8_t op = size == 1 ? 0x88 : 0x89;

				encode ( size, &op, 1, b.reg, b.rex, a, 0, 0, false );

			} else if ( a.kind == kind_of::REG ) {

				uint8_t op = size == 1 ? 0x8A : 0x8B;

				encode ( size, &op, 1, a.reg, a.rex, b, 0, 0, false );

			} else
				return fail ( L"Two memory operands aren't allowed." );

			goto done;

		}

		if ( same ( mnemonic, "lea" ) ) {

			if ( count != 2 || a.kind != kind_of::REG || b.kind != kind_of::MEM )
				return fail ( L"lea takes a register and a memory operand." );

			uint8_t op = 0x8D;

			encode ( a.size, &op, 1, a.reg, false, b, 0, 0, false );

			goto done;

		}

		if ( same ( mnemonic, "test" ) || same ( mnemonic, "xchg" ) ) {

			bool test = mnemonic [ 0 ] == 't';
			int32_t size = size_of ( a, b );

			if ( count != 2 || !size )
				return fail ( L"Give two operands with a known size." );

			if ( test && b.kind == kind_of::IMM ) {

				uint8_t op = size == 1 ? 0xF6 : 0xF7;

				encode ( size, &op, 1, 0, false, a, imm_bytes ( size ), b.value, false );

			} else {

				const piece& reg = b.kind == kind_of::REG ? b : a;
				const piece& rm = b.kind == kind_of::REG ? a : b;

				if ( reg.kind != kind_of::REG )
					return fail ( L"One operand has to be a register." );

				uint8_t op = static_cast < uint8_t > ( ( test ? 0x84 : 0x86 ) + ( size == 1 ? 0 : 1 ) );

				encode ( size, &op, 1, reg.reg, reg.rex, rm, 0, 0, false );

			}

			goto done;

		}

		if ( same ( mnemonic, "push" ) || same ( mnemonic, "pop" ) ) {

			bool push = mnemonic [ 1 ] == 'u';

			if ( count != 1 )
				return fail ( L"push and pop take one operand." );

			if ( a.kind == kind_of::REG ) {

				if ( a.reg & 8 )
					w.put ( 0x41 );

				w.put ( static_cast < uint8_t > ( ( push ? 0x50 : 0x58 ) + ( a.reg & 7 ) ) );

			} else if ( a.kind == kind_of::IMM && push ) {

				if ( fits8 ( a.value ) ) {

					w.put ( 0x6A );
					w.put_value ( a.value, 1 );

				} else {

					w.put ( 0x68 );
					w.put_value ( a.value, 4 );

				}

			} else {

				uint8_t op = push ? 0xFF : 0x8F;

				encode ( long_mode ? 8 : 4, &op, 1, push ? 6 : 0, false, a, 0, 0, true );

			}

			goto done;

		}

		{

			static const char* const unary [ 8 ] = { "inc", "dec", "not", "neg", "mul", "imul", "div", "idiv" };
			static const int32_t fields [ 8 ] = { 0, 1, 2, 3, 4, 5, 6, 7 };

			for ( int32_t n = 0; n < 8; n++ ) {

				if ( !same ( mnemonic, unary [ n ] ) || count != 1 )
					continue;

				int32_t size = a.size;

				if ( !size )
					return fail ( L"Say the operand size, like  dword ptr [rcx]." );

				uint8_t op = n < 2 ? ( size == 1 ? 0xFE : 0xFF ) : ( size == 1 ? 0xF6 : 0xF7 );

				encode ( size, &op, 1, fields [ n ], false, a, 0, 0, false );

				goto done;

			}

		}

		{

			static const char* const shifts [ 8 ] = { "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar" };
			static const int32_t fields [ 8 ] = { 0, 1, 2, 3, 4, 5, 4, 7 };

			for ( int32_t n = 0; n < 8; n++ ) {

				if ( !same ( mnemonic, shifts [ n ] ) )
					continue;

				int32_t size = a.size;

				if ( !size )
					return fail ( L"Say the operand size, like  dword ptr [rcx]." );

				if ( count == 1 || ( b.kind == kind_of::IMM && b.value == 1 ) ) {

					uint8_t op = size == 1 ? 0xD0 : 0xD1;

					encode ( size, &op, 1, fields [ n ], false, a, 0, 0, false );

				} else if ( b.kind == kind_of::IMM ) {

					uint8_t op = size == 1 ? 0xC0 : 0xC1;

					encode ( size, &op, 1, fields [ n ], false, a, 1, b.value, false );

				} else if ( b.kind == kind_of::REG && b.reg == 1 && b.size == 1 ) {

					uint8_t op = size == 1 ? 0xD2 : 0xD3;

					encode ( size, &op, 1, fields [ n ], false, a, 0, 0, false );

				} else
					return fail ( L"Shift by 1, an immediate or cl." );

				goto done;

			}

		}

		if ( same ( mnemonic, "movzx" ) || same ( mnemonic, "movsx" ) ) {

			if ( count != 2 || a.kind != kind_of::REG )
				return fail ( L"The destination has to be a register." );

			int32_t from = b.size ? b.size : 1;
			uint8_t op [ 2 ] = { 0x0F, static_cast < uint8_t > ( ( mnemonic [ 3 ] == 'z' ? 0xB6 : 0xBE ) + ( from == 2 ? 1 : 0 ) ) };

			encode ( a.size, op, 2, a.reg, false, b, 0, 0, false );

			goto done;

		}

		if ( same ( mnemonic, "xadd" ) || same ( mnemonic, "cmpxchg" ) ) {

			int32_t size = size_of ( a, b );

			if ( count != 2 || b.kind != kind_of::REG || !size )
				return fail ( L"The second operand has to be a register." );

			uint8_t op [ 2 ] = { 0x0F, static_cast < uint8_t > ( ( mnemonic [ 0 ] == 'x' ? 0xC0 : 0xB0 ) + ( size == 1 ? 0 : 1 ) ) };

			encode ( size, op, 2, b.reg, b.rex, a, 0, 0, false );

			goto done;

		}

		if ( same ( mnemonic, "movsxd" ) ) {

			if ( count != 2 || a.kind != kind_of::REG )
				return fail ( L"movsxd needs a 64-bit register destination." );

			uint8_t op = 0x63;

			encode ( 8, &op, 1, a.reg, false, b, 0, 0, false );

			goto done;

		}

		if ( same ( mnemonic, "imul" ) && count >= 2 ) {

			if ( a.kind != kind_of::REG )
				return fail ( L"The destination has to be a register." );

			if ( count == 3 && c.kind == kind_of::IMM ) {

				uint8_t op = fits8 ( c.value ) ? 0x6B : 0x69;

				encode ( a.size, &op, 1, a.reg, false, b, fits8 ( c.value ) ? 1 : imm_bytes ( a.size ), c.value, false );

			} else {

				uint8_t op [ 2 ] = { 0x0F, 0xAF };

				encode ( a.size, op, 2, a.reg, false, b, 0, 0, false );

			}

			goto done;

		}

		if ( mnemonic [ 0 ] == 'c' && mnemonic [ 1 ] == 'm' && mnemonic [ 2 ] == 'o' && mnemonic [ 3 ] == 'v' && find_condition ( mnemonic + 4 ) >= 0 ) {

			if ( count != 2 || a.kind != kind_of::REG )
				return fail ( L"cmov needs a register destination." );

			uint8_t op [ 2 ] = { 0x0F, static_cast < uint8_t > ( 0x40 + find_condition ( mnemonic + 4 ) ) };

			encode ( a.size, op, 2, a.reg, false, b, 0, 0, false );

			goto done;

		}

		if ( mnemonic [ 0 ] == 's' && mnemonic [ 1 ] == 'e' && mnemonic [ 2 ] == 't' && find_condition ( mnemonic + 3 ) >= 0 ) {

			uint8_t op [ 2 ] = { 0x0F, static_cast < uint8_t > ( 0x90 + find_condition ( mnemonic + 3 ) ) };

			encode ( 1, op, 2, 0, false, a, 0, 0, false );

			goto done;

		}

		if ( same ( mnemonic, "jmp" ) || same ( mnemonic, "call" ) || ( mnemonic [ 0 ] == 'j' && find_condition ( mnemonic + 1 ) >= 0 ) ) {

			if ( count != 1 )
				return fail ( L"Jumps and calls take one target." );

			bool call = mnemonic [ 0 ] == 'c';
			bool jump = same ( mnemonic, "jmp" );

			if ( a.kind != kind_of::IMM ) {

				if ( !jump && !call )
					return fail ( L"Conditional jumps need an address or label." );

				uint8_t op = 0xFF;

				encode ( long_mode ? 8 : 4, &op, 1, call ? 2 : 4, false, a, 0, 0, true );

				goto done;

			}

			int64_t target = a.value;

			if ( call ) {

				w.put ( 0xE8 );
				w.put_value ( target - static_cast < int64_t > ( address + 5 ), 4 );

			} else if ( jump ) {

				int64_t near = target - static_cast < int64_t > ( address + 2 );

				if ( fits8 ( near ) ) {

					w.put ( 0xEB );
					w.put_value ( near, 1 );

				} else {

					w.put ( 0xE9 );
					w.put_value ( target - static_cast < int64_t > ( address + 5 ), 4 );

				}

			} else {

				int32_t condition = find_condition ( mnemonic + 1 );
				int64_t near = target - static_cast < int64_t > ( address + 2 );

				if ( fits8 ( near ) ) {

					w.put ( static_cast < uint8_t > ( 0x70 + condition ) );
					w.put_value ( near, 1 );

				} else {

					w.put ( 0x0F );
					w.put ( static_cast < uint8_t > ( 0x80 + condition ) );
					w.put_value ( target - static_cast < int64_t > ( address + 6 ), 4 );

				}

			}

			goto done;

		}

		{

			struct simple {

				const char* name;
				uint8_t bytes [ 3 ];
				int32_t length;

			};

			static const simple plain [ ] = {
				{ "ret", { 0xC3 }, 1 }, { "retn", { 0xC3 }, 1 }, { "nop", { 0x90 }, 1 }, { "int3", { 0xCC }, 1 }, { "leave", { 0xC9 }, 1 },
				{ "hlt", { 0xF4 }, 1 }, { "cdq", { 0x99 }, 1 }, { "cqo", { 0x48, 0x99 }, 2 }, { "cwde", { 0x98 }, 1 }, { "cdqe", { 0x48, 0x98 }, 2 },
				{ "pushfq", { 0x9C }, 1 }, { "popfq", { 0x9D }, 1 }, { "clc", { 0xF8 }, 1 }, { "stc", { 0xF9 }, 1 }, { "cld", { 0xFC }, 1 },
				{ "syscall", { 0x0F, 0x05 }, 2 }, { "ud2", { 0x0F, 0x0B }, 2 }, { "cpuid", { 0x0F, 0xA2 }, 2 }, { "rdtsc", { 0x0F, 0x31 }, 2 },
				{ "pause", { 0xF3, 0x90 }, 2 }, { "int1", { 0xF1 }, 1 }
			};

			for ( const simple& item : plain ) {

				if ( !same ( mnemonic, item.name ) )
					continue;

				if ( count == 1 && a.kind == kind_of::IMM && item.bytes [ 0 ] == 0xC3 ) {

					w.put ( 0xC2 );
					w.put_value ( a.value, 2 );

				} else {

					for ( int32_t i = 0; i < item.length; i++ )
						w.put ( item.bytes [ i ] );

				}

				goto done;

			}

			if ( same ( mnemonic, "int" ) && count == 1 && a.kind == kind_of::IMM ) {

				w.put ( 0xCD );
				w.put_value ( a.value, 1 );

				goto done;

			}

		}

		return fail ( L"Plumb's assembler doesn't know that instruction yet. Patch the bytes directly with Ctrl+Alt+B." );

	done:

		if ( w.failed )
			return fail ( L"The encoding is too long." );

		instruction check;

		if ( !decode ( out, static_cast < size_t > ( w.length ), address, long_mode, check ) || check.length != w.length )
			return fail ( L"That combination of operands can't be encoded." );

		return w.length;

	}

}