#include "decompiler/internal.hh"

namespace decompiler {

	struct sink {

		result* r = nullptr;
		bool open = false;
		bool terminal = false;
		bool spaced = false;
		namer* names = nullptr;
		uint64_t commented = 0;
		int32_t indent = 0;
		int32_t slot_note = -1;
		line current;

		wchar_t last ( int32_t back = 1 ) const {

			return r->text.count ( ) - back >= current.text ? r->text [ static_cast < size_t > ( r->text.count ( ) - back ) ] : 0;

		}

		void begin ( uint64_t address ) {

			terminal = false;
			current = line ( );
			current.text = r->text.count ( );
			current.first_span = r->spans.count ( );
			current.indent = indent;
			current.address = address;
			open = true;
			slot_note = -1;

		}

		void put ( const wchar_t* text, syntax kind = syntax::PLAIN, uint64_t target = 0 ) {

			if ( !open )
				begin ( 0 );

			bool structural = kind == syntax::PLAIN || kind == syntax::OPERATOR;

			if ( spaced && !structural && kind != syntax::COMMENT && *text && last ( ) == L')' )
				r->text.push ( L' ' );

			int32_t start = r->text.count ( );

			for ( ; *text; text++ ) {

				wchar_t c = *text;

				if ( spaced && structural && ( c == L'(' || c == L'[' ) ) {

					wchar_t before = last ( );

					if ( before && before != L' ' && before != L'(' && before != L'[' && before != L'!' && before != L'~' && before != L'*' && before != L'&' && before != L'-' )
						r->text.push ( L' ' );

					r->text.push ( c );
					r->text.push ( L' ' );

					continue;

				}

				if ( spaced && structural && ( c == L')' || c == L']' ) ) {

					if ( last ( ) != L' ' )
						r->text.push ( L' ' );

					r->text.push ( c );

					continue;

				}

				r->text.push ( c );

			}

			if ( kind != syntax::PLAIN && r->text.count ( ) > start ) {

				span& item = r->spans.emplace ( );

				item.start = start;
				item.length = r->text.count ( ) - start;
				item.kind = kind;
				item.target = target;

			}

		}

		void finish ( ) {

			if ( !open )
				return;

			if ( slot_note >= 0 ) {

				bool keep = spaced;
				wchar_t digits [ 16 ];
				int32_t length = 0;
				int32_t value = slot_note;

				do {

					digits [ length++ ] = static_cast < wchar_t > ( L'0' + value % 10 );
					value /= 10;

				} while ( value && length < 15 );

				wchar_t text [ 32 ] = L" // vfunc ";
				int32_t at = 10;

				while ( length )
					text [ at++ ] = digits [ --length ];

				text [ at ] = 0;
				spaced = false;
				slot_note = -1;
				put ( text, syntax::COMMENT );
				spaced = keep;

			}

			if ( names && current.address && current.address != commented ) {

				const wchar_t* note = names->comment ( current.address );

				commented = current.address;

				if ( note ) {

					bool keep = spaced;

					spaced = false;
					put ( L"  // ", syntax::COMMENT );
					put ( note, syntax::COMMENT );
					spaced = keep;

				}

			}

			current.length = r->text.count ( ) - current.text;
			current.span_count = r->spans.count ( ) - current.first_span;
			r->lines.push ( current );
			open = false;

		}

	};

	struct emitter {

		builder& b;
		sink out;
		bool dry = false;
		int32_t count = 0;
		base::list < base::list < int32_t > > succ;
		base::list < base::list < int32_t > > pred;
		base::list < int32_t > idom;
		base::list < int32_t > ipdom;
		base::list < int32_t > rpo_index;
		base::list < uint8_t > header;
		base::list < base::list < uint8_t > > loop_body;
		base::list < uint8_t > emitted;
		base::list < uint8_t > wanted_label;
		base::list < uint8_t > labeled;
		base::list < int32_t > var_number;
		base::list < uint8_t > referenced;
		int32_t next_number = 0;
		int32_t result_var = -1;
		bool operand = false;
		bool no_casts = false;
		bool virtual_notes = false;
		base::list < uint64_t > number_addresses;
		base::list < int64_t > number_values;
		int32_t last_occurrence = 0;

		void note_slot ( int32_t callee ) {

			if ( callee < 0 )
				return;

			if ( b.exprs [ static_cast < size_t > ( callee ) ].kind == op::VAR ) {

				int64_t var = b.exprs [ static_cast < size_t > ( callee ) ].value;
				uint64_t here = out.open ? out.current.address : 0;
				int32_t found = -1;

				for ( const statement& item : b.statements ) {

					if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 || ( here && item.address > here ) )
						continue;

					const expr& left = b.exprs [ static_cast < size_t > ( item.left ) ];

					if ( left.kind == op::VAR && left.value == var && b.exprs [ static_cast < size_t > ( item.right ) ].kind == op::LOAD )
						found = item.right;

				}

				if ( found < 0 )
					return;

				callee = found;

			}

			const expr& load = b.exprs [ static_cast < size_t > ( callee ) ];
			int64_t pointer = b.long_mode ? 8 : 4;

			if ( load.kind != op::LOAD || load.size != pointer || load.a < 0 )
				return;

			const expr& where = b.exprs [ static_cast < size_t > ( load.a ) ];
			int64_t offset = -1;

			if ( where.kind == op::LOAD )
				offset = 0;
			else if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST )
				offset = b.exprs [ static_cast < size_t > ( where.b ) ].value;

			if ( offset < 0 || offset % pointer || offset / pointer > 4096 )
				return;

			out.slot_note = static_cast < int32_t > ( offset / pointer );

		}
		int32_t gotos = 0;
		int32_t depth = 0;
		bool no_ternary = false;
		int32_t next_temp = 1;
		base::list < int32_t > hoisted;
		base::list < int32_t > hoisted_temp;

		explicit emitter ( builder& owner ) : b ( owner ) {

			out.spaced = owner.names && owner.names->spaced ( );
			out.names = owner.names;
			next_number = owner.params;
			no_ternary = owner.names && owner.names->expand_ternary ( );
			no_casts = owner.names && owner.names->hide_casts ( );
			virtual_notes = owner.names && owner.names->virtual_notes ( );

		}

		void hex ( uint64_t value, wchar_t* out_text ) {

			static const wchar_t digits [ ] = L"0123456789ABCDEF";
			wchar_t reverse [ 20 ];
			int32_t n = 0;
			int32_t length = 0;

			do {

				reverse [ n++ ] = digits [ value & 15 ];
				value >>= 4;

			} while ( value );

			while ( n )
				out_text [ length++ ] = reverse [ --n ];

			out_text [ length ] = 0;

		}

		bool renumber ( ) {

			base::list < int32_t > order;
			bool changed = false;

			for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

				if ( var_number [ static_cast < size_t > ( v ) ] >= 0 && b.vars [ static_cast < size_t > ( v ) ].kind != var_kind::PARAMETER )
					order.push ( v );

			}

			base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t x, int32_t y ) {

				const variable& left = b.vars [ static_cast < size_t > ( x ) ];
				const variable& right = b.vars [ static_cast < size_t > ( y ) ];
				bool left_stack = left.kind == var_kind::STACK;
				bool right_stack = right.kind == var_kind::STACK;

				if ( left_stack != right_stack )
					return !left_stack;

				if ( left_stack )
					return left.offset < right.offset;

				return var_number [ static_cast < size_t > ( x ) ] < var_number [ static_cast < size_t > ( y ) ];

			} );

			int32_t next = b.params;

			for ( int32_t v : order ) {

				changed = changed || var_number [ static_cast < size_t > ( v ) ] != next;
				var_number [ static_cast < size_t > ( v ) ] = next++;

			}

			next_number = next;

			return changed;

		}

		void var_name ( int32_t index, wchar_t* text ) {

			default_name ( index, text );

			const variable& item = b.vars [ static_cast < size_t > ( index ) ];
			const wchar_t* user = b.names ? b.names->local_name ( b.function, text ) : nullptr;

			if ( !user && item.kind == var_kind::PARAMETER && b.has_prototype && item.position >= 0 && item.position < b.prototype.count && b.prototype.names [ item.position ] [ 0 ] )
				user = b.prototype.names [ item.position ];

			if ( !user && index != result_var && index < b.call_names.count ( ) && !b.call_names [ static_cast < size_t > ( index ) ].empty ( ) ) {

				user = b.call_names [ static_cast < size_t > ( index ) ].get ( );

				if ( b.out ) {

					bool known = false;

					for ( const name_alias& alias : b.out->aliases )
						known = known || base::same_text ( alias.key, text );

					if ( !known ) {

						name_alias& made = b.out->aliases.emplace ( );

						for ( int32_t k = 0; user [ k ] && k < 63; k++ )
							made.shown [ k ] = user [ k ];

						for ( int32_t k = 0; text [ k ] && k < 63; k++ )
							made.key [ k ] = text [ k ];

					}

				}

			}

			if ( user ) {

				int32_t length = 0;

				for ( ; user [ length ] && length < 60; length++ )
					text [ length ] = user [ length ];

				text [ length ] = 0;

			}

		}

		void default_name ( int32_t index, wchar_t* text ) {

			const variable& item = b.vars [ static_cast < size_t > ( index ) ];
			int32_t length = 0;

			if ( !dry && index < referenced.count ( ) )
				referenced [ static_cast < size_t > ( index ) ] = 1;

			auto add = [ & ] ( const wchar_t* part ) {

				for ( ; *part && length < 30; part++ )
					text [ length++ ] = *part;

			};

			auto decimal = [ & ] ( int32_t value ) {

				wchar_t reverse [ 12 ];
				int32_t n = 0;

				do {

					reverse [ n++ ] = static_cast < wchar_t > ( L'0' + value % 10 );
					value /= 10;

				} while ( value && n < 11 );

				while ( n && length < 30 )
					text [ length++ ] = reverse [ --n ];

			};

			if ( item.kind == var_kind::PARAMETER ) {

				add ( L"a" );
				decimal ( item.position + 1 );

			} else {

				if ( var_number [ static_cast < size_t > ( index ) ] < 0 )
					var_number [ static_cast < size_t > ( index ) ] = next_number++;

				if ( index == result_var )
					add ( L"result" );
				else {

					add ( L"v" );
					decimal ( var_number [ static_cast < size_t > ( index ) ] );

				}

			}

			text [ length ] = 0;

		}

		bool has_struct ( int32_t var ) {

			const variable& owner = b.vars [ static_cast < size_t > ( var ) ];

			if ( var < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( var ) ].empty ( ) )
				return false;

			if ( owner.kind != var_kind::PARAMETER || owner.bits & FLOATING )
				return false;

			for ( const statement& item : b.statements ) {

				if ( item.kind == stmt::ASSIGN && item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( item.left ) ].value == var )
					return false;

			}

			int32_t distinct = 0;

			for ( const field_use& item : b.fields ) {

				if ( item.var == var )
					distinct++;

			}

			return distinct >= 2;

		}

		const field_use* field_of ( int32_t var, int64_t offset ) {

			if ( !has_struct ( var ) )
				return nullptr;

			for ( const field_use& item : b.fields ) {

				if ( item.var == var && item.offset == offset )
					return &item;

			}

			return nullptr;

		}

		void type_name ( uint8_t size, uint8_t bits, bool pointer, bool memory = false ) {

			const wchar_t* text = L"__int64";

			if ( bits & FLOATING )
				text = size == 4 ? L"float" : ( size == 8 ? L"double" : L"__m128" );
			else if ( pointer )
				text = L"_BYTE *";
			else if ( memory && !( bits & SIGNED ) ) {

				switch ( size ) {

					case 1:
						text = L"_BYTE";
						break;

					case 2:
						text = L"_WORD";
						break;

					case 4:
						text = L"_DWORD";
						break;

					case 16:
						text = L"_OWORD";
						break;

					default:
						text = L"_QWORD";
						break;

				}

			} else {

				bool is_unsigned = ( bits & UNSIGNED ) != 0;

				switch ( size ) {

					case 1:
						text = is_unsigned ? L"unsigned __int8" : L"char";
						break;

					case 2:
						text = is_unsigned ? L"unsigned __int16" : L"__int16";
						break;

					case 4:
						text = is_unsigned ? L"unsigned int" : L"int";
						break;

					case 16:
						text = L"__m128";
						break;

					default:
						text = is_unsigned ? L"unsigned __int64" : L"__int64";
						break;

				}

			}

			out.put ( text, syntax::TYPE );

		}

		void name_after_type ( ) {

			if ( out.last ( ) != L'*' )
				out.put ( L" " );

		}

		void var_type ( int32_t index ) {

			const variable& item = b.vars [ static_cast < size_t > ( index ) ];

			if ( index < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( index ) ].empty ( ) ) {

				out.put ( b.var_types [ static_cast < size_t > ( index ) ].get ( ), syntax::TYPE );

				return;

			}

			if ( has_struct ( index ) ) {

				wchar_t name [ 64 ];

				var_name ( index, name );
				out.put ( L"struct", syntax::KEYWORD );
				out.put ( L" " );
				out.put ( base::writer ( ).add ( name ).add ( L"_t" ).text ( ), syntax::TYPE );
				out.put ( L" *" );

				return;

			}

			type_name ( item.size, item.bits & ~POINTER, false );

		}

		static int32_t precedence ( op kind ) {

			switch ( kind ) {

				case op::TERNARY:
					return 3;

				case op::LOR:
					return 4;

				case op::LAND:
					return 5;

				case op::OR:
					return 6;

				case op::XOR:
					return 7;

				case op::AND:
					return 8;

				case op::EQ:
				case op::NE:
					return 9;

				case op::LT:
				case op::LE:
				case op::GT:
				case op::GE:
					return 10;

				case op::SHL:
				case op::SHR:
					return 11;

				case op::ADD:
				case op::SUB:
					return 12;

				case op::MUL:
				case op::DIV:
				case op::MOD:
					return 13;

				case op::NOT:
				case op::NEG:
				case op::LNOT:
				case op::CAST:
				case op::LOAD:
				case op::ADDRESS:
					return 14;

				default:
					return 16;

			}

		}

		void number ( int64_t value, uint8_t size, bool hex_form, bool is_unsigned ) {

			wchar_t text [ 80 ];
			int32_t length = 0;
			bool negative = value < 0 && !( size < 8 && value > 0 );
			uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

			if ( negative && ( is_unsigned || ( magnitude > 0x100000000ull && hex_form ) ) ) {

				negative = false;
				magnitude = size >= 8 ? static_cast < uint64_t > ( value ) : static_cast < uint64_t > ( value ) & ( ( 1ull << ( size * 8 ) ) - 1 );

			}

			bool fits = negative ? magnitude <= 0x80000000ull : magnitude <= ( is_unsigned ? 0xFFFFFFFFull : 0x7FFFFFFFull );
			bool unsigned_suffix = is_unsigned && !negative;
			int32_t occurrence = next_occurrence ( value );
			wchar_t form = b.names && !dry ? b.names->number_form ( out.open ? out.current.address : 0, value, occurrence ) : 0;

			hex_form = hex_form || unsigned_suffix || magnitude > 0xFFFFFFFFull;

			if ( form == L'h' )
				hex_form = true;
			else if ( form == L'd' )
				hex_form = false;
			else if ( form == L'c' && char_literal ( value, text, 40 ) ) {

				put_number ( text, value );

				return;

			} else if ( form == L'b' ) {

				uint64_t bits = negative ? static_cast < uint64_t > ( value ) & ( size >= 8 ? ~0ull : ( ( 1ull << ( size * 8 ) ) - 1 ) ) : magnitude;
				int32_t top = 63;

				while ( top > 0 && !( ( bits >> top ) & 1 ) )
					top--;

				text [ length++ ] = L'0';
				text [ length++ ] = L'b';

				for ( int32_t bit = top; bit >= 0 && length < 78; bit-- )
					text [ length++ ] = ( bits >> bit ) & 1 ? L'1' : L'0';

				text [ length ] = 0;
				put_number ( text, value );

				return;

			}

			if ( negative )
				text [ length++ ] = L'-';

			if ( hex_form && ( magnitude >= 10 || form == L'h' ) ) {

				wchar_t digits [ 20 ];

				hex ( magnitude, digits );
				text [ length++ ] = L'0';
				text [ length++ ] = L'x';

				for ( int32_t i = 0; digits [ i ]; i++ )
					text [ length++ ] = digits [ i ];

			} else {

				wchar_t reverse [ 24 ];
				int32_t n = 0;

				do {

					reverse [ n++ ] = static_cast < wchar_t > ( L'0' + magnitude % 10 );
					magnitude /= 10;

				} while ( magnitude );

				while ( n )
					text [ length++ ] = reverse [ --n ];

			}

			if ( size == 8 && !( operand && fits ) ) {

				if ( unsigned_suffix )
					text [ length++ ] = L'u';

				text [ length++ ] = L'i';
				text [ length++ ] = L'6';
				text [ length++ ] = L'4';

			} else if ( unsigned_suffix )
				text [ length++ ] = L'u';

			text [ length ] = 0;
			put_number ( text, value );

		}

		void put_number ( const wchar_t* text, int64_t value ) {

			int32_t before = out.r->spans.count ( );

			out.put ( text, syntax::NUMBER );

			if ( out.r->spans.count ( ) > before ) {

				out.r->spans.back ( ).value = value;
				out.r->spans.back ( ).occurrence = last_occurrence;

			}

		}

		int32_t next_occurrence ( int64_t value ) {

			uint64_t address = out.open ? out.current.address : 0;
			int32_t count = 0;

			if ( dry )
				return 0;

			for ( int32_t i = 0; i < number_addresses.count ( ); i++ ) {

				if ( number_addresses [ static_cast < size_t > ( i ) ] == address && number_values [ static_cast < size_t > ( i ) ] == value )
					count++;

			}

			number_addresses.push ( address );
			number_values.push ( value );
			last_occurrence = count;

			return count;

		}

		void real ( int64_t bits, uint8_t size ) {

			double value = 0.0;

			if ( size == 4 ) {

				uint32_t raw = static_cast < uint32_t > ( bits );

				value = static_cast < double > ( __builtin_bit_cast ( float, raw ) );

			} else
				value = __builtin_bit_cast ( double, bits );

			base::writer text;

			if ( value != value )
				text.add ( L"NAN" );
			else if ( value > 1e300 || value < -1e300 )
				text.add ( value < 0 ? L"-INFINITY" : L"INFINITY" );
			else {

				int32_t decimals = 1;

				for ( ; decimals < 9; decimals++ ) {

					double scale = 1.0;

					for ( int32_t i = 0; i < decimals; i++ )
						scale *= 10.0;

					double rounded = static_cast < double > ( static_cast < int64_t > ( value * scale + ( value < 0 ? -0.5 : 0.5 ) ) ) / scale;

					if ( size == 4 ? static_cast < float > ( rounded ) == static_cast < float > ( value ) : rounded == value )
						break;

				}

				if ( value > 1e15 || value < -1e15 )
					text.fixed ( value, 0 );
				else
					text.fixed ( value, decimals );

			}

			if ( size == 4 )
				text.add ( L"f" );

			out.put ( text.text ( ), syntax::NUMBER );

		}

		bool simple_constant ( int64_t value ) {

			uint64_t magnitude = static_cast < uint64_t > ( value < 0 ? -value : value );

			return magnitude < 0x80000000ull;

		}

		void pointer_constant ( uint64_t address ) {

			wchar_t text [ 300 ];

			if ( b.names->literal ( address, text + 1, 280 ) ) {

				text [ 0 ] = L'"';

				int32_t length = 1;

				while ( text [ length ] )
					length++;

				text [ length++ ] = L'"';
				text [ length ] = 0;
				out.put ( text, syntax::STRING, address );

				return;

			}

			if ( b.names->name ( address, true, text, 200 ) ) {

				bool code = b.db->function_starting ( address ) >= 0;

				if ( code ) {

					const analysis::function_info& info = b.db->functions [ static_cast < size_t > ( b.db->function_starting ( address ) ) ];

					out.put ( text, ( info.flags & analysis::THUNK ) ? syntax::IMPORT : syntax::FUNCTION, address );

				} else {

					out.put ( L"&" );
					out.put ( text, syntax::GLOBAL, address );

				}

				return;

			}

			number ( static_cast < int64_t > ( address ), 8, true, false );

		}

		void lvalue_or_load ( const expr& item, int32_t parent ) {

			const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
			wchar_t name [ 64 ];

			if ( where.kind == op::ADDRESS && where.value >= 0 ) {

				var_name ( static_cast < int32_t > ( where.value ), name );
				out.put ( name, syntax::LOCAL );

				return;

			}

			int32_t var = -1;
			int64_t offset = 0;

			if ( where.kind == op::VAR )
				var = static_cast < int32_t > ( where.value );
			else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

				var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );
				offset = b.exprs [ static_cast < size_t > ( where.b ) ].value;

			}

			if ( var >= 0 && var < b.var_structs.count ( ) && !b.var_structs [ static_cast < size_t > ( var ) ].empty ( ) ) {

				wchar_t field [ 64 ];
				uint64_t tag = 0;
				int32_t field_size = 0;

				if ( b.names->struct_field ( b.var_structs [ static_cast < size_t > ( var ) ].get ( ), offset, field, 64, tag, field_size ) ) {

					bool exact = field_size == item.size || field_size == 0;

					if ( !exact ) {

						out.put ( L"*(" );
						type_name ( item.size, item.bits, false, true );
						out.put ( L" *)&" );

					}

					var_name ( var, name );
					out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
					out.put ( L"->", syntax::OPERATOR );
					out.put ( field, syntax::GLOBAL, tag );

					return;

				}

			}

			if ( var >= 0 && field_of ( var, offset ) ) {

				wchar_t field [ 32 ] = L"field_";

				var_name ( var, name );
				hex ( static_cast < uint64_t > ( offset ), field + 6 );
				out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
				out.put ( L"->", syntax::OPERATOR );
				out.put ( field, syntax::GLOBAL );

				return;

			}

			if ( where.kind == op::ADD && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::MUL ) {

				const expr& scaled = b.exprs [ static_cast < size_t > ( where.b ) ];

				if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == item.size ) {

					out.put ( L"((" );
					type_name ( item.size, item.bits, false, true );
					out.put ( L" *)" );
					value ( where.a, 14 );
					out.put ( L")[" );
					value ( scaled.a, 0 );
					out.put ( L"]" );

					return;

				}

			}

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"*(", syntax::OPERATOR );
			type_name ( item.size, item.bits, false, true );
			out.put ( L" *)" );
			value ( item.a, 14 );

			if ( parent > 14 )
				out.put ( L")" );

		}

		void extended_load ( const expr& item, int32_t parent, uint8_t sign ) {

			const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];

			int32_t var = -1;

			if ( where.kind == op::VAR )
				var = static_cast < int32_t > ( where.value );
			else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST )
				var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );

			bool fielded = var >= 0 && ( has_struct ( var ) || ( var < b.var_structs.count ( ) && !b.var_structs [ static_cast < size_t > ( var ) ].empty ( ) ) );

			if ( item.bits & FLOATING || fielded || ( where.kind == op::ADDRESS && where.value >= 0 ) ) {

				lvalue_or_load ( item, parent );

				return;

			}

			if ( parent > 14 )
				out.put ( L"(" );

			out.put ( L"*(", syntax::OPERATOR );
			type_name ( item.size, sign, false );
			out.put ( L" *)" );
			value ( item.a, 14 );

			if ( parent > 14 )
				out.put ( L")" );

		}

		void value ( int32_t index, int32_t parent, bool hex_hint = false ) {

			if ( index < 0 ) {

				out.put ( L"?" );

				return;

			}

			for ( int32_t h = 0; h < hoisted.count ( ); h++ ) {

				if ( hoisted [ static_cast < size_t > ( h ) ] == index ) {

					out.put ( base::writer ( ).add ( L"t" ).number ( hoisted_temp [ static_cast < size_t > ( h ) ] ).text ( ), syntax::LOCAL );

					return;

				}

			}

			const expr item = b.exprs [ static_cast < size_t > ( index ) ];

			if ( ( item.kind == op::EQ || item.kind == op::NE ) && item.b >= 0 && !( item.bits & FLOATING ) ) {

				const expr& zero = b.exprs [ static_cast < size_t > ( item.b ) ];
				const expr& tested = b.exprs [ static_cast < size_t > ( item.a ) ];

				if ( zero.kind == op::CONST && zero.value == 0 && !( zero.bits & POINTER ) && !( tested.bits & FLOATING ) ) {

					if ( item.kind == op::NE ) {

						value ( item.a, parent );

						return;

					}

					if ( parent > 14 )
						out.put ( L"(" );

					out.put ( L"!", syntax::OPERATOR );
					value ( item.a, 14 );

					if ( parent > 14 )
						out.put ( L")" );

					return;

				}

			}

			int32_t mine = precedence ( item.kind );
			bool wrap = mine < parent;
			bool outer = operand;
			wchar_t name [ 300 ];

			if ( wrap )
				out.put ( L"(" );

			switch ( item.kind ) {

				case op::CONST:

					if ( item.bits & POINTER )
						pointer_constant ( static_cast < uint64_t > ( item.value ) );
					else
						number ( item.value, item.size, hex_hint || !simple_constant ( item.value ), false );

					break;

				case op::REAL:
					real ( item.value, item.size );
					break;

				case op::VAR: {

					int32_t var = static_cast < int32_t > ( item.value );

					var_name ( var, name );
					out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

					break;

				}

				case op::GLOBAL:

					if ( b.names->name ( static_cast < uint64_t > ( item.value ), false, name, 200 ) )
						out.put ( name, syntax::GLOBAL, static_cast < uint64_t > ( item.value ) );
					else
						number ( item.value, 8, true, false );

					break;

				case op::TEXT:
					pointer_constant ( static_cast < uint64_t > ( item.value ) );
					break;

				case op::ADDRESS:

					if ( item.value >= 0 && item.a < 0 ) {

						var_name ( static_cast < int32_t > ( item.value ), name );
						out.put ( L"&", syntax::OPERATOR );
						out.put ( name, syntax::LOCAL );

					} else {

						out.put ( L"&", syntax::OPERATOR );
						value ( item.a, 14 );

					}

					break;

				case op::LOAD:
					lvalue_or_load ( item, parent );
					break;

				case op::CALL: {

					uint64_t target = static_cast < uint64_t > ( item.value );

					if ( target && b.names->name ( target, true, name, 200 ) ) {

						int32_t callee = b.db->function_starting ( target );
						bool imported = callee < 0 || ( b.db->functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK );

						out.put ( name, imported ? syntax::IMPORT : syntax::FUNCTION, target );

					} else if ( target )
						number ( static_cast < int64_t > ( target ), 8, true, false );
					else {

						if ( virtual_notes && !dry )
							note_slot ( item.a );

						out.put ( L"(" );
						value ( item.a, 14 );
						out.put ( L")" );

					}

					out.put ( L"(" );
					operand = false;

					for ( int32_t i = 0; i < item.arg_count; i++ ) {

						int32_t argument = b.args [ static_cast < size_t > ( item.first_arg + i ) ];
						const expr& passed = b.exprs [ static_cast < size_t > ( argument ) ];

						if ( i )
							out.put ( L", " );

						if ( !no_casts && passed.kind == op::CONST && ( passed.bits & POINTER ) && b.db->function_starting ( static_cast < uint64_t > ( passed.value ) ) >= 0 && !( target && b.names->prototype ( target ) ) ) {

							out.put ( L"(" );
							type_name ( 8, 0, false );
							out.put ( L")" );

						}

						value ( argument, 2 );

					}

					out.put ( L")" );

					break;

				}

				case op::INTRINSIC:

					out.put ( intrinsic_name ( static_cast < int32_t > ( item.value ) ), syntax::FUNCTION );
					out.put ( L"(" );
					operand = false;

					for ( int32_t i = 0; i < item.arg_count; i++ ) {

						if ( i )
							out.put ( L", " );

						value ( b.args [ static_cast < size_t > ( item.first_arg + i ) ], 2 );

					}

					out.put ( L")" );

					break;

				case op::CAST: {

					const expr& inner = b.exprs [ static_cast < size_t > ( item.a ) ];
					bool float_change = ( inner.bits & FLOATING ) != ( item.bits & FLOATING );
					bool shown = ( float_change || inner.size != item.size ) && !no_casts;

					if ( ( item.bits & POINTER ) && ( item.bits & FLOATING ) ) {

						out.put ( L"__bitcast", syntax::FUNCTION );
						out.put ( L"(" );
						value ( item.a, 0 );
						out.put ( L")" );

						break;

					}

					if ( ( item.bits & POINTER ) && !float_change ) {

						value ( item.a, parent );

						break;

					}

					if ( inner.kind == op::LOAD && item.size > inner.size && !float_change && !( item.bits & FLOATING ) ) {

						extended_load ( inner, parent, item.bits & SIGNED ? SIGNED : UNSIGNED );

						break;

					}

					if ( shown ) {

						bool narrow = !float_change && item.size < inner.size && !( item.bits & ( SIGNED | UNSIGNED ) );

						out.put ( L"(" );
						type_name ( item.size, item.bits, false, narrow );
						out.put ( L")" );

					}

					value ( item.a, shown ? 14 : parent );

					break;

				}

				case op::NOT:
					out.put ( L"~", syntax::OPERATOR );
					value ( item.a, 14, true );
					break;

				case op::NEG:
					out.put ( L"-", syntax::OPERATOR );
					value ( item.a, 14 );
					break;

				case op::LNOT:
					out.put ( L"!", syntax::OPERATOR );
					value ( item.a, 14 );
					break;

				case op::TERNARY:
					value ( item.a, 4 );
					out.put ( L" ? ", syntax::OPERATOR );
					value ( item.b, 4 );
					out.put ( L" : ", syntax::OPERATOR );
					value ( item.c, 3 );
					break;

				default: {

					const wchar_t* symbol = L"?";
					bool bitwise = false;

					switch ( item.kind ) {

						case op::ADD:
							symbol = L" + ";
							break;

						case op::SUB:
							symbol = L" - ";
							break;

						case op::MUL:
							symbol = L" * ";
							break;

						case op::DIV:
							symbol = L" / ";
							break;

						case op::MOD:
							symbol = L" % ";
							break;

						case op::AND:
							symbol = L" & ";
							bitwise = true;
							break;

						case op::OR:
							symbol = L" | ";
							bitwise = true;
							break;

						case op::XOR:
							symbol = L" ^ ";
							bitwise = true;
							break;

						case op::SHL:
							symbol = L" << ";
							break;

						case op::SHR:
							symbol = L" >> ";
							break;

						case op::EQ:
							symbol = L" == ";
							break;

						case op::NE:
							symbol = L" != ";
							break;

						case op::LT:
							symbol = L" < ";
							break;

						case op::LE:
							symbol = L" <= ";
							break;

						case op::GT:
							symbol = L" > ";
							break;

						case op::GE:
							symbol = L" >= ";
							break;

						case op::LAND:
							symbol = L" && ";
							break;

						case op::LOR:
							symbol = L" || ";
							break;

						default:
							break;

					}

					const expr& right = b.exprs [ static_cast < size_t > ( item.b ) ];

					operand = true;

					if ( item.kind == op::ADD && right.kind == op::CONST && right.value < 0 && !( right.bits & POINTER ) ) {

						value ( item.a, mine );
						out.put ( L" - ", syntax::OPERATOR );
						number ( -right.value, right.size, !simple_constant ( -right.value ), false );

						break;

					}

					bool compare = item.kind >= op::EQ && item.kind <= op::GE;
					bool unsigned_compare = compare && ( item.bits & UNSIGNED ) && item.kind != op::EQ && item.kind != op::NE;
					bool float_compare = compare && ( item.bits & FLOATING );

					value ( item.a, item.kind == op::SHL || item.kind == op::SHR ? mine + 1 : mine, bitwise );
					out.put ( symbol, syntax::OPERATOR );

					if ( right.kind == op::CONST && !( right.bits & POINTER ) && compare ) {

						if ( float_compare && right.value == 0 )
							real ( 0, right.size );
						else
							number ( right.value, right.size, !simple_constant ( right.value ), unsigned_compare );

					} else
						value ( item.b, mine + 1, bitwise );

					break;

				}

			}

			if ( wrap )
				out.put ( L")" );

			operand = outer;

		}

		bool float_target ( int32_t left ) {

			const expr& item = b.exprs [ static_cast < size_t > ( left ) ];

			if ( item.bits & FLOATING )
				return true;

			if ( item.kind == op::VAR )
				return ( b.vars [ static_cast < size_t > ( item.value ) ].bits & FLOATING ) != 0;

			if ( item.kind == op::GLOBAL ) {

				for ( uint64_t address : b.float_globals ) {

					if ( address == static_cast < uint64_t > ( item.value ) )
						return true;

				}

			}

			if ( item.kind == op::LOAD ) {

				const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
				int32_t var = -1;
				int64_t offset = 0;

				if ( where.kind == op::VAR )
					var = static_cast < int32_t > ( where.value );
				else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

					var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );
					offset = b.exprs [ static_cast < size_t > ( where.b ) ].value;

				}

				for ( const field_use& field : b.fields ) {

					if ( field.var == var && field.offset == offset && ( field.bits & FLOATING ) )
						return true;

				}

			}

			return false;

		}

		void collect_ternaries ( int32_t index, base::list < int32_t >& found, bool top ) {

			if ( index < 0 || found.count ( ) >= 8 )
				return;

			const expr item = b.exprs [ static_cast < size_t > ( index ) ];

			for ( int32_t done : hoisted ) {

				if ( done == index )
					return;

			}

			switch ( item.kind ) {

				case op::TERNARY:

					if ( !top )
						found.push ( index );

					return;

				case op::LAND:
				case op::LOR:
					collect_ternaries ( item.a, found, false );
					return;

				case op::CALL:
				case op::INTRINSIC:

					if ( item.kind == op::CALL && !item.value )
						collect_ternaries ( item.a, found, false );

					for ( int32_t i = 0; i < item.arg_count; i++ )
						collect_ternaries ( b.args [ static_cast < size_t > ( item.first_arg + i ) ], found, false );

					return;

				case op::CONST:
				case op::REAL:
				case op::VAR:
				case op::GLOBAL:
				case op::TEXT:
					return;

				default:
					collect_ternaries ( item.a, found, false );
					collect_ternaries ( item.b, found, false );
					return;

			}

		}

		bool hoist ( int32_t index, uint64_t address ) {

			base::list < int32_t > found;

			collect_ternaries ( index, found, false );

			for ( int32_t chosen : found ) {

				const expr item = b.exprs [ static_cast < size_t > ( chosen ) ];
				int32_t number = next_temp++;

				hoist ( item.a, address );
				out.begin ( address );
				type_name ( item.size, item.bits, false );
				name_after_type ( );
				out.put ( base::writer ( ).add ( L"t" ).number ( number ).text ( ), syntax::LOCAL );
				out.put ( L";" );
				out.finish ( );
				out.begin ( address );
				branch_open ( item.a, address );
				branch_assign ( -1, item.b, address, number );
				branch_else ( address );
				branch_assign ( -1, item.c, address, number );
				branch_close ( );
				out.finish ( );
				hoisted.push ( chosen );
				hoisted_temp.push ( number );

			}

			return !found.empty ( );

		}

		void assign_target ( int32_t left ) {

			if ( b.exprs [ static_cast < size_t > ( left ) ].kind == op::LOAD )
				lvalue_or_load ( b.exprs [ static_cast < size_t > ( left ) ], 0 );
			else
				value ( left, 0 );

		}

		void branch_open ( int32_t test, uint64_t address ) {

			out.put ( L"if", syntax::CONTROL );
			paren_open ( );
			value ( test, 0 );
			paren_close ( );
			out.finish ( );
			out.indent++;
			out.begin ( address );

		}

		void branch_else ( uint64_t address ) {

			out.finish ( );
			out.indent--;
			out.begin ( 0 );
			out.put ( L"else", syntax::CONTROL );
			out.finish ( );
			out.indent++;
			out.begin ( address );

		}

		void branch_close ( ) {

			out.finish ( );
			out.indent--;

		}

		bool split_ternary ( int32_t chosen, int32_t& test, int32_t& yes, int32_t& no ) {

			const expr& pick = b.exprs [ static_cast < size_t > ( chosen ) ];

			if ( pick.kind == op::TERNARY ) {

				test = pick.a;
				yes = pick.b;
				no = pick.c;

				return true;

			}

			if ( pick.kind != op::CAST || pick.a < 0 || b.exprs [ static_cast < size_t > ( pick.a ) ].kind != op::TERNARY )
				return false;

			uint8_t size = pick.size;
			uint8_t bits = pick.bits;
			const expr& inner = b.exprs [ static_cast < size_t > ( pick.a ) ];
			int32_t inner_yes = inner.b;
			int32_t inner_no = inner.c;

			test = inner.a;
			yes = b.cast ( inner_yes, size, bits );
			no = b.cast ( inner_no, size, bits );

			return true;

		}

		void branch_assign ( int32_t left, int32_t chosen, uint64_t address, int32_t temp = 0 ) {

			int32_t test = -1;
			int32_t yes = -1;
			int32_t no = -1;

			if ( split_ternary ( chosen, test, yes, no ) ) {

				if ( hoist ( test, address ) )
					out.begin ( address );

				branch_open ( test, address );
				branch_assign ( left, yes, address, temp );
				branch_else ( address );
				branch_assign ( left, no, address, temp );
				branch_close ( );

				return;

			}

			bool lifted = hoist ( chosen, address );

			if ( temp <= 0 )
				lifted = hoist ( left, address ) || lifted;

			if ( lifted )
				out.begin ( address );

			const expr& pick = b.exprs [ static_cast < size_t > ( chosen ) ];
			int32_t size = temp > 0 ? 0 : b.exprs [ static_cast < size_t > ( left ) ].size;
			bool float_constant = temp <= 0 && pick.kind == op::CONST && !( pick.bits & POINTER ) && ( size == 4 || size == 8 ) && float_target ( left );
			int64_t constant = pick.value;

			if ( temp > 0 )
				out.put ( base::writer ( ).add ( L"t" ).number ( temp ).text ( ), syntax::LOCAL );
			else
				assign_target ( left );

			out.put ( L" = ", syntax::OPERATOR );

			if ( float_constant )
				real ( constant, size == 4 ? 4 : 8 );
			else
				value ( chosen, 2 );

			out.put ( L";" );

		}

		void branch_return ( int32_t chosen, uint64_t address ) {

			int32_t test = -1;
			int32_t yes = -1;
			int32_t no = -1;

			if ( split_ternary ( chosen, test, yes, no ) ) {

				if ( hoist ( test, address ) )
					out.begin ( address );

				branch_open ( test, address );
				branch_return ( yes, address );
				branch_else ( address );
				branch_return ( no, address );
				branch_close ( );

				return;

			}

			if ( hoist ( chosen, address ) )
				out.begin ( address );

			out.put ( L"return", syntax::CONTROL );
			out.put ( L" " );
			value ( chosen, 0 );
			out.put ( L";" );

		}

		void statement_line ( const statement& item ) {

			if ( item.kind == stmt::NOP )
				return;

			if ( dry )
				return;

			hoisted.clear ( );
			hoisted_temp.clear ( );
			out.begin ( item.address );

			if ( no_ternary && item.kind == stmt::EXPR && hoist ( item.right, item.address ) )
				out.begin ( item.address );

			switch ( item.kind ) {

				case stmt::ASSIGN:
				case stmt::STORE: {

					const expr left = b.exprs [ static_cast < size_t > ( item.left ) ];
					const expr right = b.exprs [ static_cast < size_t > ( item.right ) ];

					if ( right.kind == op::ADD && right.a >= 0 && left.kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST && !( right.bits & FLOATING ) ) {

						int64_t step_value = b.exprs [ static_cast < size_t > ( right.b ) ].value;

						value ( item.left, 0 );

						if ( step_value == 1 || step_value == -1 )
							out.put ( step_value == 1 ? L"++" : L"--", syntax::OPERATOR );
						else {

							out.put ( step_value < 0 ? L" -= " : L" += ", syntax::OPERATOR );
							number ( step_value < 0 ? -step_value : step_value, right.size, !simple_constant ( step_value ), false );

						}

						out.put ( L";" );

						break;

					}

					int32_t test = -1;
					int32_t yes = -1;
					int32_t no = -1;

					if ( no_ternary && split_ternary ( item.right, test, yes, no ) ) {

						if ( hoist ( item.left, item.address ) )
							out.begin ( item.address );

						branch_assign ( item.left, item.right, item.address );

						break;

					}

					if ( no_ternary ) {

						bool lifted = hoist ( item.left, item.address );

						lifted = hoist ( item.right, item.address ) || lifted;

						if ( lifted )
							out.begin ( item.address );

					}

					if ( left.kind == op::LOAD )
						lvalue_or_load ( left, 0 );
					else
						value ( item.left, 0 );

					out.put ( L" = ", syntax::OPERATOR );

					if ( right.kind == op::CONST && !( right.bits & POINTER ) && ( left.size == 4 || left.size == 8 ) && float_target ( item.left ) )
						real ( right.value, left.size == 4 ? 4 : 8 );
					else if ( right.kind == op::LOAD && right.size < left.size && !( left.bits & FLOATING ) && !float_target ( item.left ) )
						extended_load ( right, 2, UNSIGNED );
					else
						value ( item.right, 2 );

					out.put ( L";" );

					break;

				}

				case stmt::EXPR:
					value ( item.right, 0 );
					out.put ( L";" );
					break;

				case stmt::ASM:
					out.put ( L"__asm", syntax::KEYWORD );
					out.put ( L" { " );
					out.put ( b.asm_text [ static_cast < size_t > ( item.left ) ].get ( ), syntax::COMMENT );
					out.put ( L" }" );
					break;

				default:
					break;

			}

			out.finish ( );
			hoisted.clear ( );
			hoisted_temp.clear ( );

		}

		void emit_line ( uint64_t address, const wchar_t* text, syntax kind ) {

			if ( dry )
				return;

			out.begin ( address );
			out.put ( text, kind );
			out.finish ( );

		}

		void label ( int32_t block ) {

			if ( dry || !wanted_label [ static_cast < size_t > ( block ) ] || labeled [ static_cast < size_t > ( block ) ] )
				return;

			labeled [ static_cast < size_t > ( block ) ] = 1;

			int32_t saved = out.indent;

			out.indent = 0;
			out.begin ( b.graph.blocks [ static_cast < size_t > ( block ) ].start );
			out.put ( base::writer ( ).add ( L"LABEL_" ).number ( block + 1 ).text ( ), syntax::LABEL );
			out.put ( L":" );
			out.finish ( );
			out.indent = saved;

		}

		void go_to ( int32_t block, uint64_t address ) {

			wanted_label [ static_cast < size_t > ( block ) ] = 1;
			gotos++;

			if ( dry )
				return;

			out.begin ( address );
			out.put ( L"goto", syntax::CONTROL );
			out.put ( L" " );
			out.put ( base::writer ( ).add ( L"LABEL_" ).number ( block + 1 ).text ( ), syntax::LABEL );
			out.put ( L";" );
			out.finish ( );
			out.terminal = true;

		}

		void body ( int32_t block ) {

			label ( block );
			emitted [ static_cast < size_t > ( block ) ] = 1;

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];

			for ( int32_t i = 0; i < item.count; i++ )
				statement_line ( b.statements [ static_cast < size_t > ( item.first + i ) ] );

		}

		bool empty_block ( int32_t block ) {

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];

			for ( int32_t i = 0; i < item.count; i++ ) {

				if ( b.statements [ static_cast < size_t > ( item.first + i ) ].kind != stmt::NOP )
					return false;

			}

			return true;

		}

		void open_brace ( uint64_t address, const wchar_t* keyword, int32_t condition, bool negated ) {

			if ( dry ) {

				out.indent++;

				return;

			}

			int32_t shown = condition >= 0 && negated ? b.negate ( condition ) : condition;

			hoisted.clear ( );
			hoisted_temp.clear ( );

			if ( no_ternary && shown >= 0 && keyword [ 0 ] == L'i' && keyword [ 1 ] == L'f' && !keyword [ 2 ] )
				hoist ( shown, address );

			out.begin ( address );
			out.put ( keyword, syntax::CONTROL );

			if ( condition >= 0 ) {

				const expr& test = b.exprs [ static_cast < size_t > ( condition ) ];

				paren_open ( );

				if ( test.kind == op::CONST && !negated )
					out.put ( test.value ? L"1" : L"0", syntax::NUMBER );
				else
					value ( shown, 0 );

				paren_close ( );

			}

			out.finish ( );
			brace ( L"{" );
			out.indent++;
			hoisted.clear ( );
			hoisted_temp.clear ( );

		}

		void close_brace ( ) {

			out.indent--;

			if ( dry )
				return;

			brace ( L"}" );

		}

		void brace ( const wchar_t* text ) {

			if ( dry )
				return;

			out.begin ( 0 );
			out.put ( text );
			out.finish ( );

		}

		void paren_open ( ) {

			bool keep = out.spaced;

			out.spaced = false;
			out.put ( L" ( " );
			out.spaced = keep;

		}

		void paren_close ( ) {

			bool keep = out.spaced;

			out.spaced = false;
			out.put ( L" )" );
			out.spaced = keep;

		}

		void else_line ( ) {

			if ( dry )
				return;

			out.begin ( 0 );
			out.put ( L"else", syntax::CONTROL );
			out.finish ( );

		}

		void while_tail ( uint64_t address, int32_t condition ) {

			if ( dry )
				return;

			out.begin ( address );
			out.put ( L"while", syntax::CONTROL );
			paren_open ( );
			value ( condition, 0 );
			paren_close ( );
			out.put ( L";" );
			out.finish ( );

		}

		void return_line ( const block_code& item ) {

			if ( dry )
				return;

			int32_t test = -1;
			int32_t yes = -1;
			int32_t no = -1;
			bool shown = item.returned >= 0 && ( b.returns_value || item.end == term::TAIL );

			hoisted.clear ( );
			hoisted_temp.clear ( );
			out.begin ( item.last );

			if ( no_ternary && shown && b.returns_value && split_ternary ( item.returned, test, yes, no ) ) {

				branch_return ( item.returned, item.last );
				out.finish ( );
				out.terminal = true;
				hoisted.clear ( );
				hoisted_temp.clear ( );

				return;

			}

			if ( no_ternary && shown && hoist ( item.returned, item.last ) )
				out.begin ( item.last );

			if ( item.end == term::TAIL && !b.returns_value && item.returned >= 0 ) {

				value ( item.returned, 0 );
				out.put ( L";" );
				out.finish ( );
				hoisted.clear ( );
				hoisted_temp.clear ( );

				return;

			}

			out.put ( L"return", syntax::CONTROL );

			if ( shown ) {

				const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];

				out.put ( L" " );

				if ( back.kind == op::CONST && !( back.bits & POINTER ) && !b.has_prototype && !b.returns_float && b.long_mode )
					number ( back.value, 8, !simple_constant ( back.value ), false );
				else
					value ( item.returned, 0 );

			}

			out.put ( L";" );
			out.finish ( );
			out.terminal = true;
			hoisted.clear ( );
			hoisted_temp.clear ( );

		}

		void jump_line ( const wchar_t* keyword, uint64_t address ) {

			if ( dry )
				return;

			out.begin ( address );
			out.put ( keyword, syntax::CONTROL );
			out.put ( L";" );
			out.finish ( );
			out.terminal = true;

		}

		struct frame {

			int32_t loop = -1;
			int32_t exit = -1;

		};

		bool in_loop ( int32_t loop, int32_t block ) {

			return loop >= 0 && loop_body [ static_cast < size_t > ( loop ) ] [ static_cast < size_t > ( block ) ];

		}

		bool special ( int32_t target, int32_t stop, const frame& context, uint64_t address ) {

			if ( target == context.loop && context.loop >= 0 ) {

				if ( stop != target )
					jump_line ( L"continue", address );

				return true;

			}

			if ( target == context.exit && context.exit >= 0 ) {

				if ( stop != target )
					jump_line ( L"break", address );

				return true;

			}

			return false;

		}

		void region ( int32_t block, int32_t stop, frame context ) {

			if ( ++depth > 400 ) {

				depth--;

				return;

			}

			while ( block >= 0 && block != stop ) {

				if ( emitted [ static_cast < size_t > ( block ) ] ) {

					go_to ( block, 0 );

					break;

				}

				if ( header [ static_cast < size_t > ( block ) ] && context.loop != block ) {

					block = loop ( block, context );

					continue;

				}

				body ( block );

				const block_code& item = b.code [ static_cast < size_t > ( block ) ];

				switch ( item.end ) {

					case term::RETURN:
					case term::TAIL:
						return_line ( item );
						block = -1;
						break;

					case term::STOP:
						block = -1;
						break;

					case term::FALL:

						if ( special ( item.taken, stop, context, item.last ) )
							block = -1;
						else
							block = item.taken;

						break;

					case term::BRANCH:
						block = branch ( block, stop, context, false );
						break;

					case term::SWITCH:
						block = choice ( block, stop, context );
						break;

					default:
						block = -1;
						break;

				}

			}

			depth--;

		}

		int32_t follow_of ( int32_t block, int32_t stop, const frame& context ) {

			int32_t follow = ipdom [ static_cast < size_t > ( block ) ];

			if ( follow == count )
				follow = -1;

			if ( context.loop >= 0 && follow >= 0 && !in_loop ( context.loop, follow ) )
				follow = context.exit;

			if ( follow < 0 )
				follow = stop;

			return follow;

		}

		int32_t branch ( int32_t block, int32_t stop, const frame& context, bool chained ) {

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];
			int32_t taken = item.taken;
			int32_t other = item.other;
			int32_t follow = follow_of ( block, stop, context );
			bool taken_jump = taken == context.loop || taken == context.exit;
			bool other_jump = other == context.loop || other == context.exit;

			if ( taken_jump && !other_jump ) {

				open_brace ( item.last, chained ? L"else if" : L"if", item.condition, false );
				special ( taken, -2, context, item.last );
				close_brace ( );

				return other;

			}

			if ( other_jump && !taken_jump ) {

				open_brace ( item.last, chained ? L"else if" : L"if", item.condition, true );
				special ( other, -2, context, item.last );
				close_brace ( );

				return taken;

			}

			bool negated = false;
			int32_t then_part = taken;
			int32_t else_part = other;

			if ( then_part == follow ) {

				then_part = other;
				else_part = taken;
				negated = true;

			}

			open_brace ( item.last, chained ? L"else if" : L"if", item.condition, negated );

			if ( !special ( then_part, -2, context, item.last ) )
				region ( then_part, follow, context );

			out.indent--;

			if ( else_part != follow && else_part >= 0 ) {

				bool chain = !emitted [ static_cast < size_t > ( else_part ) ] && b.code [ static_cast < size_t > ( else_part ) ].end == term::BRANCH && empty_block ( else_part ) && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && !header [ static_cast < size_t > ( else_part ) ] && follow_of ( else_part, stop, context ) == follow && !wanted_label [ static_cast < size_t > ( else_part ) ];

				if ( chain ) {

					brace ( L"}" );
					body ( else_part );

					int32_t next = branch ( else_part, stop, context, true );

					( void ) next;

				} else {

					brace ( L"}" );
					else_line ( );
					brace ( L"{" );
					out.indent++;

					if ( !special ( else_part, -2, context, item.last ) )
						region ( else_part, follow, context );

					close_brace ( );

				}

			} else
				brace ( L"}" );

			return follow;

		}

		int32_t loop ( int32_t start, const frame& outer ) {

			const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( start ) ];
			int32_t exit = -1;

			for ( int32_t v = 0; v < count && exit < 0; v++ ) {

				if ( !members [ static_cast < size_t > ( v ) ] )
					continue;

				for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

					if ( !members [ static_cast < size_t > ( s ) ] ) {

						exit = s;

						break;

					}

				}

			}

			int32_t pd = ipdom [ static_cast < size_t > ( start ) ];

			if ( pd >= 0 && pd < count && !members [ static_cast < size_t > ( pd ) ] )
				exit = pd;

			frame inner;

			inner.loop = start;
			inner.exit = exit;

			const block_code& item = b.code [ static_cast < size_t > ( start ) ];

			if ( item.end == term::BRANCH && ( item.taken == start || item.other == start ) ) {

				bool back_taken = item.taken == start;
				int32_t leaving = back_taken ? item.other : item.taken;

				open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );
				body ( start );
				close_brace ( );
				while_tail ( item.last, back_taken ? item.condition : b.negate ( item.condition ) );

				return leaving;

			}

			if ( item.end == term::BRANCH && empty_block ( start ) && ( ( item.taken == exit && members [ static_cast < size_t > ( item.other ) ] ) || ( item.other == exit && members [ static_cast < size_t > ( item.taken ) ] ) ) ) {

				bool stay_taken = item.other == exit;

				label ( start );
				emitted [ static_cast < size_t > ( start ) ] = 1;
				open_brace ( item.last, L"while", item.condition, !stay_taken );
				region ( stay_taken ? item.taken : item.other, start, inner );
				close_brace ( );

				return exit;

			}

			int32_t latch = -1;

			for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

				if ( members [ static_cast < size_t > ( p ) ] )
					latch = latch < 0 ? p : -2;

			}

			if ( latch >= 0 && latch != start && b.code [ static_cast < size_t > ( latch ) ].end == term::BRANCH && empty_block ( latch ) ) {

				const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
				bool back_taken = tail.taken == start;
				int32_t leaving = back_taken ? tail.other : tail.taken;

				if ( leaving == exit ) {

					open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

					emitted [ static_cast < size_t > ( latch ) ] = 1;
					region ( start, latch, inner );
					emitted [ static_cast < size_t > ( latch ) ] = 1;
					close_brace ( );
					while_tail ( tail.last, back_taken ? tail.condition : b.negate ( tail.condition ) );

					return exit;

				}

			}

			open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"while", b.constant ( 1, 1 ), false );

			frame body_frame = inner;

			emitted [ static_cast < size_t > ( start ) ] = 0;
			region_header ( start, body_frame );
			close_brace ( );

			( void ) outer;

			return exit;

		}

		void region_header ( int32_t start, frame context ) {

			body ( start );

			const block_code& item = b.code [ static_cast < size_t > ( start ) ];
			int32_t next = -1;

			switch ( item.end ) {

				case term::RETURN:
				case term::TAIL:
					return_line ( item );
					break;

				case term::FALL:

					if ( !special ( item.taken, start, context, item.last ) )
						next = item.taken;

					break;

				case term::BRANCH:
					next = branch ( start, start, context, false );
					break;

				case term::SWITCH:
					next = choice ( start, start, context );
					break;

				default:
					break;

			}

			if ( next >= 0 && next != start )
				region ( next, start, context );

		}

		int32_t choice ( int32_t block, int32_t stop, const frame& context ) {

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];
			const analysis::block& node = b.graph.blocks [ static_cast < size_t > ( block ) ];
			int32_t follow = follow_of ( block, stop, context );

			if ( !dry ) {

				out.begin ( item.last );
				out.put ( L"switch", syntax::CONTROL );
				paren_open ( );
				value ( item.selector, 0 );
				paren_close ( );
				out.finish ( );

			}

			brace ( L"{" );
			out.indent++;

			base::list < int32_t > targets;

			for ( int32_t e = 0; e < node.out_count; e++ ) {

				int32_t to = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ].to;
				bool seen = false;

				for ( int32_t t : targets ) {

					if ( t == to )
						seen = true;

				}

				if ( !seen )
					targets.push ( to );

			}

			frame inner = context;

			inner.exit = follow;

			for ( int32_t target : targets ) {

				if ( !dry ) {

					for ( int32_t e = 0; e < node.out_count; e++ ) {

						const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

						if ( link.to != target )
							continue;

						out.begin ( b.graph.blocks [ static_cast < size_t > ( target ) ].start );
						out.put ( L"case", syntax::CONTROL );
						out.put ( L" " );
						number ( link.value, 4, false, false );
						out.put ( L":" );
						out.finish ( );

					}

				}

				out.indent++;

				if ( target != follow && !emitted [ static_cast < size_t > ( target ) ] )
					region ( target, follow, inner );
				else if ( target != follow )
					go_to ( target, 0 );

				if ( !out.terminal )
					jump_line ( L"break", 0 );

				out.indent--;

			}

			close_brace ( );

			return follow;

		}

		void run ( ) {

			emitted.fill ( static_cast < size_t > ( count ), 0 );
			labeled.fill ( static_cast < size_t > ( count ), 0 );
			number_addresses.clear ( );
			number_values.clear ( );
			gotos = 0;
			depth = 0;
			next_temp = 1;

			frame top;

			region ( 0, -1, top );

			for ( int32_t i = 0; i < count; i++ ) {

				if ( !emitted [ static_cast < size_t > ( i ) ] && b.code [ static_cast < size_t > ( i ) ].reachable ) {

					wanted_label [ static_cast < size_t > ( i ) ] = 1;
					region ( i, -1, top );

				}

			}

		}

		void analyze ( ) {

			count = b.graph.blocks.count ( );
			succ.resize ( static_cast < size_t > ( count ) );
			pred.resize ( static_cast < size_t > ( count ) );

			for ( const analysis::edge& link : b.graph.edges ) {

				bool seen = false;

				for ( int32_t s : succ [ static_cast < size_t > ( link.from ) ] ) {

					if ( s == link.to )
						seen = true;

				}

				if ( seen )
					continue;

				succ [ static_cast < size_t > ( link.from ) ].push ( link.to );
				pred [ static_cast < size_t > ( link.to ) ].push ( link.from );

			}

			base::list < int32_t > order;

			postorder ( count, 0, false, order );

			base::list < int32_t > rpo;

			for ( int32_t i = order.count ( ) - 1; i >= 0; i-- )
				rpo.push ( order [ static_cast < size_t > ( i ) ] );

			rpo_index.fill ( static_cast < size_t > ( count + 1 ), -1 );

			for ( int32_t i = 0; i < rpo.count ( ); i++ )
				rpo_index [ static_cast < size_t > ( rpo [ static_cast < size_t > ( i ) ] ) ] = i;

			dominate ( rpo, false, idom );

			base::list < int32_t > reverse_order;

			postorder ( count + 1, count, true, reverse_order );

			base::list < int32_t > reverse_rpo;

			for ( int32_t i = reverse_order.count ( ) - 1; i >= 0; i-- )
				reverse_rpo.push ( reverse_order [ static_cast < size_t > ( i ) ] );

			rpo_index.fill ( static_cast < size_t > ( count + 1 ), -1 );

			for ( int32_t i = 0; i < reverse_rpo.count ( ); i++ )
				rpo_index [ static_cast < size_t > ( reverse_rpo [ static_cast < size_t > ( i ) ] ) ] = i;

			dominate ( reverse_rpo, true, ipdom );

			header.fill ( static_cast < size_t > ( count ), 0 );
			loop_body.resize ( static_cast < size_t > ( count ) );

			for ( int32_t u = 0; u < count; u++ ) {

				for ( int32_t h : succ [ static_cast < size_t > ( u ) ] ) {

					if ( !dominates ( h, u ) )
						continue;

					header [ static_cast < size_t > ( h ) ] = 1;

					base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( h ) ];

					if ( members.empty ( ) )
						members.fill ( static_cast < size_t > ( count ), 0 );

					members [ static_cast < size_t > ( h ) ] = 1;

					base::list < int32_t > work;

					if ( !members [ static_cast < size_t > ( u ) ] ) {

						members [ static_cast < size_t > ( u ) ] = 1;
						work.push ( u );

					}

					while ( !work.empty ( ) ) {

						int32_t v = work.back ( );

						work.pop ( );

						for ( int32_t p : pred [ static_cast < size_t > ( v ) ] ) {

							if ( !members [ static_cast < size_t > ( p ) ] ) {

								members [ static_cast < size_t > ( p ) ] = 1;
								work.push ( p );

							}

						}

					}

				}

			}

			wanted_label.fill ( static_cast < size_t > ( count ), 0 );
			var_number.fill ( b.vars.size ( ), -1 );
			referenced.fill ( b.vars.size ( ), 0 );

		}

		bool dominates ( int32_t a, int32_t v ) {

			int32_t steps = 0;

			while ( v >= 0 && steps++ < 100000 ) {

				if ( v == a )
					return true;

				int32_t up = idom [ static_cast < size_t > ( v ) ];

				if ( up == v )
					return false;

				v = up;

			}

			return false;

		}

		void postorder ( int32_t total, int32_t root, bool reverse, base::list < int32_t >& order ) {

			base::list < uint8_t > state;
			base::list < int32_t > stack;
			base::list < int32_t > cursor;

			state.fill ( static_cast < size_t > ( total ), 0 );
			cursor.fill ( static_cast < size_t > ( total ), 0 );
			stack.push ( root );
			state [ static_cast < size_t > ( root ) ] = 1;

			while ( !stack.empty ( ) ) {

				int32_t v = stack.back ( );
				base::list < int32_t > next;

				neighbors ( v, reverse, next );

				int32_t& at = cursor [ static_cast < size_t > ( v ) ];

				if ( at < next.count ( ) ) {

					int32_t to = next [ static_cast < size_t > ( at++ ) ];

					if ( !state [ static_cast < size_t > ( to ) ] ) {

						state [ static_cast < size_t > ( to ) ] = 1;
						stack.push ( to );

					}

					continue;

				}

				order.push ( v );
				stack.pop ( );

			}

		}

		bool exit_block ( int32_t v ) {

			term end = b.code [ static_cast < size_t > ( v ) ].end;

			return succ [ static_cast < size_t > ( v ) ].empty ( ) || end == term::RETURN || end == term::TAIL || end == term::STOP;

		}

		void neighbors ( int32_t v, bool reverse, base::list < int32_t >& out_list ) {

			out_list.clear ( );

			if ( !reverse ) {

				if ( v < count )
					out_list = succ [ static_cast < size_t > ( v ) ];

				return;

			}

			if ( v == count ) {

				for ( int32_t i = 0; i < count; i++ ) {

					if ( exit_block ( i ) )
						out_list.push ( i );

				}

				return;

			}

			out_list = pred [ static_cast < size_t > ( v ) ];

		}

		void incoming ( int32_t v, bool reverse, base::list < int32_t >& out_list ) {

			out_list.clear ( );

			if ( !reverse ) {

				out_list = pred [ static_cast < size_t > ( v ) ];

				return;

			}

			if ( v == count )
				return;

			out_list = succ [ static_cast < size_t > ( v ) ];

			if ( exit_block ( v ) )
				out_list.push ( count );

		}

		void dominate ( const base::list < int32_t >& rpo, bool reverse, base::list < int32_t >& result_list ) {

			int32_t total = count + 1;
			int32_t root = rpo.empty ( ) ? 0 : rpo [ 0 ];

			result_list.fill ( static_cast < size_t > ( total ), -1 );
			result_list [ static_cast < size_t > ( root ) ] = root;

			bool changed = true;
			base::list < int32_t > preds;

			while ( changed ) {

				changed = false;

				for ( int32_t i = 1; i < rpo.count ( ); i++ ) {

					int32_t v = rpo [ static_cast < size_t > ( i ) ];
					int32_t chosen = -1;

					incoming ( v, reverse, preds );

					for ( int32_t p : preds ) {

						if ( result_list [ static_cast < size_t > ( p ) ] < 0 )
							continue;

						if ( chosen < 0 )
							chosen = p;
						else {

							int32_t x = p;
							int32_t y = chosen;

							while ( x != y ) {

								while ( rpo_index [ static_cast < size_t > ( x ) ] > rpo_index [ static_cast < size_t > ( y ) ] )
									x = result_list [ static_cast < size_t > ( x ) ];

								while ( rpo_index [ static_cast < size_t > ( y ) ] > rpo_index [ static_cast < size_t > ( x ) ] )
									y = result_list [ static_cast < size_t > ( y ) ];

							}

							chosen = x;

						}

					}

					if ( chosen >= 0 && result_list [ static_cast < size_t > ( v ) ] != chosen ) {

						result_list [ static_cast < size_t > ( v ) ] = chosen;
						changed = true;

					}

				}

			}

		}

	};

	static void append_result ( result& target, const result& source ) {

		int32_t text_base = target.text.count ( );
		int32_t span_base = target.spans.count ( );

		for ( wchar_t c : source.text )
			target.text.push ( c );

		for ( span item : source.spans ) {

			item.start += text_base;
			target.spans.push ( item );

		}

		for ( line item : source.lines ) {

			item.text += text_base;
			item.first_span += span_base;
			target.lines.push ( item );

		}

	}

	bool char_literal ( int64_t value, wchar_t* out, int32_t room ) {

		uint64_t raw = static_cast < uint64_t > ( value );
		uint8_t bytes [ 8 ];
		int32_t count = 0;

		if ( value < 0 || room < 12 )
			return false;

		do {

			bytes [ count++ ] = static_cast < uint8_t > ( raw & 0xFF );
			raw >>= 8;

		} while ( raw && count < 8 );

		if ( raw )
			return false;

		int32_t length = 0;

		out [ length++ ] = L'\'';

		for ( int32_t i = count - 1; i >= 0; i-- ) {

			uint8_t c = bytes [ i ];
			const wchar_t* escape = nullptr;

			switch ( c ) {

				case 0:
					escape = L"\\0";
					break;

				case '\n':
					escape = L"\\n";
					break;

				case '\r':
					escape = L"\\r";
					break;

				case '\t':
					escape = L"\\t";
					break;

				case '\'':
					escape = L"\\'";
					break;

				case '\\':
					escape = L"\\\\";
					break;

				default:
					break;

			}

			if ( length + 6 >= room )
				return false;

			if ( escape ) {

				if ( count > 1 && c != 0 && c != '\'' && c != '\\' )
					return false;

				for ( ; *escape; escape++ )
					out [ length++ ] = *escape;

			} else if ( c >= 0x20 && c <= 0x7E )
				out [ length++ ] = static_cast < wchar_t > ( c );
			else if ( count == 1 ) {

				static const wchar_t digits [ ] = L"0123456789ABCDEF";

				out [ length++ ] = L'\\';
				out [ length++ ] = L'x';
				out [ length++ ] = digits [ c >> 4 ];
				out [ length++ ] = digits [ c & 15 ];

			} else
				return false;

		}

		out [ length++ ] = L'\'';
		out [ length ] = 0;

		return true;

	}

	static bool line_starts ( const result& r, const line& item, const wchar_t* text ) {

		for ( int32_t n = 0; text [ n ]; n++ ) {

			if ( n >= item.length || r.text [ static_cast < size_t > ( item.text + n ) ] != text [ n ] )
				return false;

		}

		return true;

	}

	static bool line_is ( const result& r, const line& item, const wchar_t* text ) {

		int32_t n = 0;

		while ( text [ n ] )
			n++;

		return n == item.length && line_starts ( r, item, text );

	}

	static bool control_head ( const result& r, const line& item ) {

		return line_starts ( r, item, L"if ( " ) || line_starts ( r, item, L"else if ( " ) || line_starts ( r, item, L"while ( " ) || line_starts ( r, item, L"for ( " ) || line_is ( r, item, L"else" );

	}

	static bool simple_line ( const result& r, const line& item ) {

		static const wchar_t* const compound [ ] = { L"if ( ", L"else", L"while ( ", L"switch ( ", L"for ( " };

		if ( !item.length || r.text [ static_cast < size_t > ( item.text + item.length - 1 ) ] != L';' )
			return false;

		for ( const wchar_t* word : compound ) {

			if ( line_starts ( r, item, word ) )
				return false;

		}

		return true;

	}

	static void drop_braces ( result& r ) {

		int32_t total = r.lines.count ( );
		base::list < uint8_t > skip;
		base::list < line > kept;

		skip.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t i = 0; i + 2 < total; i++ ) {

			const line& head = r.lines [ static_cast < size_t > ( i ) ];
			const line& open = r.lines [ static_cast < size_t > ( i + 1 ) ];
			const line& close = r.lines [ static_cast < size_t > ( i + 2 ) ];

			if ( line_is ( r, head, L"else" ) && line_is ( r, open, L"{" ) && line_is ( r, close, L"}" ) && open.indent == head.indent && close.indent == head.indent ) {

				skip [ static_cast < size_t > ( i ) ] = 1;
				skip [ static_cast < size_t > ( i + 1 ) ] = 1;
				skip [ static_cast < size_t > ( i + 2 ) ] = 1;

			}

		}

		for ( int32_t i = 0; i + 3 < total; i++ ) {

			if ( skip [ static_cast < size_t > ( i ) ] )
				continue;


			const line& head = r.lines [ static_cast < size_t > ( i ) ];
			const line& open = r.lines [ static_cast < size_t > ( i + 1 ) ];
			const line& only = r.lines [ static_cast < size_t > ( i + 2 ) ];
			const line& close = r.lines [ static_cast < size_t > ( i + 3 ) ];

			if ( !control_head ( r, head ) || !line_is ( r, open, L"{" ) || open.indent != head.indent )
				continue;

			if ( only.indent != head.indent + 1 || !simple_line ( r, only ) || !line_is ( r, close, L"}" ) || close.indent != head.indent )
				continue;

			skip [ static_cast < size_t > ( i + 1 ) ] = 1;
			skip [ static_cast < size_t > ( i + 3 ) ] = 1;

		}

		for ( int32_t i = 0; i < total; i++ ) {

			if ( !skip [ static_cast < size_t > ( i ) ] )
				kept.push ( r.lines [ static_cast < size_t > ( i ) ] );

		}

		r.lines = kept;

	}

	static bool pointee_of ( const wchar_t* type, wchar_t* out, int32_t room ) {

		static const wchar_t* const pairs [ ] [ 2 ] = {
			{ L"LPDWORD", L"DWORD" }, { L"PDWORD", L"DWORD" }, { L"LPWORD", L"WORD" }, { L"PWORD", L"WORD" }, { L"LPLONG", L"LONG" }, { L"PLONG", L"LONG" },
			{ L"PULONG", L"ULONG" }, { L"PUINT", L"UINT" }, { L"LPBOOL", L"BOOL" }, { L"PBOOL", L"BOOL" }, { L"PSIZE_T", L"SIZE_T" }, { L"PULONG_PTR", L"ULONG_PTR" },
			{ L"PDWORD_PTR", L"DWORD_PTR" }, { L"PDWORD64", L"DWORD64" }, { L"LPHANDLE", L"HANDLE" }, { L"PHANDLE", L"HANDLE" }, { L"PHKEY", L"HKEY" },
			{ L"PLARGE_INTEGER", L"LARGE_INTEGER" }, { L"PULARGE_INTEGER", L"ULARGE_INTEGER" }, { L"LPFILETIME", L"FILETIME" }, { L"PFILETIME", L"FILETIME" },
			{ L"PLUID", L"LUID" }, { L"PREGHANDLE", L"REGHANDLE" },
		};

		for ( const auto& pair : pairs ) {

			if ( base::same_text ( pair [ 0 ], type ) ) {

				int32_t k = 0;

				for ( ; pair [ 1 ] [ k ] && k < room - 1; k++ )
					out [ k ] = pair [ 1 ] [ k ];

				out [ k ] = 0;

				return true;

			}

		}

		int32_t length = 0;

		while ( type [ length ] )
			length++;

		while ( length && type [ length - 1 ] == L' ' )
			length--;

		if ( !length || type [ length - 1 ] != L'*' )
			return false;

		length--;

		while ( length && type [ length - 1 ] == L' ' )
			length--;

		if ( !length || type [ length - 1 ] == L'*' || length >= room )
			return false;

		int32_t start = 0;

		if ( length > 6 && type [ 0 ] == L'c' && type [ 1 ] == L'o' && type [ 2 ] == L'n' && type [ 3 ] == L's' && type [ 4 ] == L't' && type [ 5 ] == L' ' )
			start = 6;

		int32_t k = 0;

		for ( int32_t i = start; i < length; i++ )
			out [ k++ ] = type [ i ];

		out [ k ] = 0;

		return k > 0 && !base::same_text ( out, L"void" );

	}

	static void infer_call_types ( builder& b, base::list < base::text >& out ) {

		out.clear ( );
		out.resize ( b.vars.size ( ) );

		if ( !b.names )
			return;

		uint64_t cached_target = 0;
		bool cached_ok = false;
		c_prototype* shape = new c_prototype ( );

		auto load = [ & ] ( uint64_t target ) {

			if ( target == cached_target )
				return cached_ok;

			cached_target = target;
			cached_ok = false;

			const wchar_t* declared = target ? b.names->prototype ( target ) : nullptr;
			base::writer problem;

			if ( declared && parse_prototype ( declared, b.long_mode, *shape, problem ) )
				cached_ok = true;

			return cached_ok;

		};

		auto offer = [ & ] ( int32_t v, const wchar_t* type ) {

			if ( v < 0 || v >= b.vars.count ( ) || !type || !type [ 0 ] || !out [ static_cast < size_t > ( v ) ].empty ( ) )
				return;

			c_type parsed;
			wchar_t clean [ 64 ];

			if ( !parse_type ( type, b.long_mode, parsed, clean, 64 ) || parsed.empty || !parsed.size )
				return;

			if ( parsed.size != b.vars [ static_cast < size_t > ( v ) ].size )
				return;

			out [ static_cast < size_t > ( v ) ].set ( clean );

		};

		for ( const statement& item : b.statements ) {

			if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 )
				continue;

			const expr& left = b.exprs [ static_cast < size_t > ( item.left ) ];
			const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

			if ( left.kind != op::VAR || right.kind != op::CALL || !load ( static_cast < uint64_t > ( right.value ) ) )
				continue;

			if ( !shape->result_type.empty )
				offer ( static_cast < int32_t > ( left.value ), shape->result );

		}

		for ( const expr& item : b.exprs ) {

			if ( item.kind != op::CALL || !item.value || !load ( static_cast < uint64_t > ( item.value ) ) )
				continue;

			for ( int32_t i = 0; i < item.arg_count && i < shape->count; i++ ) {

				int32_t index = b.args [ static_cast < size_t > ( item.first_arg + i ) ];

				if ( index < 0 )
					continue;

				const expr& passed = b.exprs [ static_cast < size_t > ( index ) ];

				if ( passed.kind == op::VAR )
					offer ( static_cast < int32_t > ( passed.value ), shape->types [ i ] );
				else if ( passed.kind == op::ADDRESS && passed.a < 0 ) {

					wchar_t inner [ 64 ];

					if ( pointee_of ( shape->types [ i ], inner, 64 ) )
						offer ( static_cast < int32_t > ( passed.value ), inner );

				}

			}

		}

		delete shape;

		for ( int32_t round = 0; round < 4; round++ ) {

			bool moved = false;

			for ( const statement& item : b.statements ) {

				if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 )
					continue;

				const expr& left = b.exprs [ static_cast < size_t > ( item.left ) ];
				const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

				if ( left.kind != op::VAR || right.kind != op::VAR )
					continue;

				int32_t into = static_cast < int32_t > ( left.value );
				int32_t from = static_cast < int32_t > ( right.value );

				if ( into < 0 || from < 0 || into >= out.count ( ) || from >= out.count ( ) )
					continue;

				if ( out [ static_cast < size_t > ( into ) ].empty ( ) && !out [ static_cast < size_t > ( from ) ].empty ( ) && b.vars [ static_cast < size_t > ( into ) ].size == b.vars [ static_cast < size_t > ( from ) ].size ) {

					out [ static_cast < size_t > ( into ) ].set ( out [ static_cast < size_t > ( from ) ].get ( ) );
					moved = true;

				} else if ( out [ static_cast < size_t > ( from ) ].empty ( ) && !out [ static_cast < size_t > ( into ) ].empty ( ) && b.vars [ static_cast < size_t > ( into ) ].size == b.vars [ static_cast < size_t > ( from ) ].size ) {

					out [ static_cast < size_t > ( from ) ].set ( out [ static_cast < size_t > ( into ) ].get ( ) );
					moved = true;

				}

			}

			if ( !moved )
				break;

		}

	}

	static void name_from_calls ( builder& b, int32_t result_var ) {

		b.call_names.clear ( );
		b.call_names.resize ( b.vars.size ( ) );

		if ( !b.names )
			return;

		static const wchar_t* const prefixes [ ] = { L"Get", L"get_", L"_get_", L"Create", L"Load" };
		base::list < base::text > taken;

		for ( const statement& item : b.statements ) {

			if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 )
				continue;

			const expr& left = b.exprs [ static_cast < size_t > ( item.left ) ];
			const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

			if ( left.kind != op::VAR || right.kind != op::CALL || !right.value )
				continue;

			int32_t v = static_cast < int32_t > ( left.value );

			if ( v < 0 || v >= b.vars.count ( ) || v == result_var || b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER || !b.call_names [ static_cast < size_t > ( v ) ].empty ( ) )
				continue;

			wchar_t callee [ 200 ];

			if ( !b.names->name ( static_cast < uint64_t > ( right.value ), true, callee, 200 ) )
				continue;

			const wchar_t* rest = nullptr;

			for ( const wchar_t* prefix : prefixes ) {

				int32_t k = 0;

				while ( prefix [ k ] && prefix [ k ] == callee [ k ] )
					k++;

				if ( prefix [ k ] )
					continue;

				wchar_t next = callee [ k ];
				bool upper = next >= L'A' && next <= L'Z';
				bool snake = prefix [ k - 1 ] == L'_' && ( ( next >= L'a' && next <= L'z' ) || upper );

				if ( upper || snake ) {

					rest = callee + k;

					break;

				}

			}

			if ( !rest || !base::is_identifier ( rest ) || !rest [ 1 ] )
				continue;

			bool clash = false;

			for ( const base::text& other : taken )
				clash = clash || base::same_text ( other.get ( ), rest );

			if ( clash )
				continue;

			taken.push ( base::text ( rest ) );
			b.call_names [ static_cast < size_t > ( v ) ].set ( rest );

		}

	}

	bool emit ( builder& b ) {

		emitter body_emitter ( b );
		result body;

		body_emitter.analyze ( );

		if ( b.returns_value && !b.returns_float ) {

			int32_t chosen = -1;

			for ( const block_code& item : b.code ) {

				if ( item.end != term::RETURN || item.returned < 0 || !item.reachable )
					continue;

				const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];
				int32_t var = back.kind == op::VAR ? static_cast < int32_t > ( back.value ) : -1;

				if ( back.kind == op::CONST )
					continue;

				if ( var < 0 || b.vars [ static_cast < size_t > ( var ) ].kind != var_kind::REGISTER || b.vars [ static_cast < size_t > ( var ) ].family != 0 || ( chosen >= 0 && chosen != var ) ) {

					chosen = -2;

					break;

				}

				chosen = var;

			}

			body_emitter.result_var = chosen >= 0 ? chosen : -1;

		}

		name_from_calls ( b, body_emitter.result_var );

		body_emitter.out.r = &body;
		body_emitter.dry = true;
		body_emitter.out.indent = 1;
		body_emitter.run ( );
		body_emitter.dry = false;
		body_emitter.out.indent = 1;
		body_emitter.run ( );

		bool renumbered = body_emitter.renumber ( );

		body_emitter.dry = true;
		b.var_types.resize ( b.vars.size ( ) );
		b.var_structs.resize ( b.vars.size ( ) );

		base::list < base::text > inferred;

		infer_call_types ( b, inferred );

		bool typed = false;

		for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

			variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( item.kind == var_kind::REGISTER && body_emitter.var_number [ static_cast < size_t > ( v ) ] < 0 )
				continue;

			wchar_t key [ 64 ];
			wchar_t clean [ 64 ];
			c_type shape;
			const wchar_t* declared = nullptr;

			body_emitter.default_name ( v, key );

			if ( b.names )
				declared = b.names->local_type ( b.function, key );

			if ( !declared && item.kind == var_kind::PARAMETER && b.has_prototype && item.position >= 0 && item.position < b.prototype.count )
				declared = b.prototype.types [ item.position ];

			if ( !declared && !( item.kind == var_kind::PARAMETER && b.has_prototype ) && v < inferred.count ( ) && !inferred [ static_cast < size_t > ( v ) ].empty ( ) )
				declared = inferred [ static_cast < size_t > ( v ) ].get ( );

			if ( !declared || !parse_type ( declared, b.long_mode, shape, clean, 64 ) || shape.empty )
				continue;

			item.size = shape.size;
			item.bits = static_cast < uint8_t > ( shape.bits );
			b.var_types [ static_cast < size_t > ( v ) ].set ( clean );

			if ( shape.pointer && shape.pointee [ 0 ] )
				b.var_structs [ static_cast < size_t > ( v ) ].set ( shape.pointee );

			typed = true;

		}

		body_emitter.dry = false;

		if ( typed || renumbered ) {

			int32_t gotos = body_emitter.gotos;

			body.text.clear ( );
			body.spans.clear ( );
			body.lines.clear ( );
			body_emitter.out.indent = 1;
			body_emitter.run ( );
			body_emitter.gotos = gotos;

		}

		result& out = *b.out;
		emitter head ( b );
		wchar_t name [ 300 ];

		head.var_number = body_emitter.var_number;
		head.next_number = body_emitter.next_number;
		head.result_var = body_emitter.result_var;
		head.out.r = &out;

		for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

			if ( !head.has_struct ( v ) )
				continue;

			if ( !body_emitter.referenced [ static_cast < size_t > ( v ) ] )
				continue;

			wchar_t var [ 64 ];

			head.var_name ( v, var );
			head.out.begin ( 0 );
			head.out.put ( L"struct", syntax::KEYWORD );
			head.out.put ( L" " );
			head.out.put ( base::writer ( ).add ( var ).add ( L"_t" ).text ( ), syntax::TYPE );
			head.out.put ( L" {" );
			head.out.finish ( );
			head.out.indent = 1;

			base::list < field_use > fields;

			for ( const field_use& item : b.fields ) {

				if ( item.var == v )
					fields.push ( item );

			}

			base::sort ( fields.data ( ), fields.count ( ), [ ] ( const field_use& a, const field_use& c ) { return a.offset < c.offset; } );

			for ( const field_use& item : fields ) {

				wchar_t digits [ 20 ];

				head.hex ( static_cast < uint64_t > ( item.offset ), digits );
				head.out.begin ( 0 );
				head.type_name ( item.size, item.bits, false );
				head.out.put ( L" " );
				head.out.put ( base::writer ( ).add ( L"field_" ).add ( digits ).text ( ), syntax::GLOBAL );
				head.out.put ( L";  " );
				head.out.put ( base::writer ( ).add ( L"// +0x" ).add ( digits ).add ( L", used " ).number ( item.count ).add ( item.count == 1 ? L" time" : L" times" ).text ( ), syntax::COMMENT );
				head.out.finish ( );

			}

			head.out.indent = 0;
			head.out.begin ( 0 );
			head.out.put ( L"};" );
			head.out.finish ( );
			head.out.begin ( 0 );
			head.out.finish ( );

		}

		head.out.begin ( b.function );

		if ( b.has_prototype ) {

			if ( b.prototype.result_type.empty )
				head.out.put ( L"void", syntax::KEYWORD );
			else
				head.out.put ( b.prototype.result, syntax::TYPE );

		} else if ( b.returns_value ) {

			if ( b.returns_float )
				head.type_name ( b.return_size ? b.return_size : 4, FLOATING, false );
			else
				head.type_name ( b.long_mode ? 8 : 4, 0, false );

		} else
			head.out.put ( L"void", syntax::KEYWORD );

		head.name_after_type ( );

		if ( !b.names->name ( b.function, true, name, 300 ) )
			name [ 0 ] = 0;

		if ( b.long_mode && b.params > 0 && !( b.has_prototype && b.prototype.variadic ) )
			head.out.put ( L"__fastcall ", syntax::KEYWORD );

		head.out.put ( name, syntax::FUNCTION, b.function );
		head.out.put ( L"(" );

		int32_t shown_params = 0;

		for ( int32_t position = 0; position < b.params; position++ ) {

			int32_t found = -1;

			for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

				if ( b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER && b.vars [ static_cast < size_t > ( v ) ].position == position )
					found = v;

			}

			if ( shown_params++ )
				head.out.put ( L", " );

			if ( found >= 0 ) {

				wchar_t var [ 64 ];

				head.var_type ( found );
				head.name_after_type ( );
				head.var_name ( found, var );
				head.out.put ( var, syntax::PARAMETER );

			} else if ( b.has_prototype && position < b.prototype.count ) {

				head.out.put ( b.prototype.types [ position ], syntax::TYPE );
				head.name_after_type ( );

				if ( b.prototype.names [ position ] [ 0 ] )
					head.out.put ( b.prototype.names [ position ], syntax::PARAMETER );
				else
					head.out.put ( base::writer ( ).add ( L"a" ).number ( position + 1 ).text ( ), syntax::PARAMETER );

			} else {

				head.type_name ( b.long_mode ? 8 : 4, 0, false );
				head.name_after_type ( );
				head.out.put ( base::writer ( ).add ( L"a" ).number ( position + 1 ).text ( ), syntax::PARAMETER );

			}

		}

		if ( b.has_prototype && b.prototype.variadic ) {

			if ( shown_params++ )
				head.out.put ( L", " );

			head.out.put ( L"..." );

		}

		head.out.put ( L")" );
		head.out.finish ( );
		head.out.begin ( b.function );
		head.out.put ( L"{" );
		head.out.finish ( );
		head.out.indent = 1;

		int32_t declared = 0;
		base::list < int32_t > shown_vars;
		int64_t lowest = 0;
		int64_t saved_size = b.pushed;

		for ( const block_code& item : b.code ) {

			if ( item.delta_known && item.reachable )
				lowest = base::min ( lowest, item.delta );

		}

		for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( item.kind == var_kind::PARAMETER || item.cookie || item.saved )
				continue;

			if ( body_emitter.referenced [ static_cast < size_t > ( v ) ] )
				shown_vars.push ( v );

		}

		base::sort ( shown_vars.data ( ), shown_vars.count ( ), [ & ] ( int32_t x, int32_t y ) { return head.var_number [ static_cast < size_t > ( x ) ] < head.var_number [ static_cast < size_t > ( y ) ]; } );

		if ( !shown_vars.empty ( ) && b.names && b.names->collapse_locals ( ) ) {

			head.out.begin ( b.function );
			head.out.put ( L"// [COLLAPSED LOCAL DECLARATIONS. PRESS KEYPAD CTRL-\"+\" TO EXPAND]", syntax::COMMENT );
			head.out.finish ( );
			declared = shown_vars.count ( );
			shown_vars.clear ( );

		}

		for ( int32_t v : shown_vars ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];
			wchar_t var [ 64 ];

			head.out.begin ( 0 );
			head.var_type ( v );
			head.name_after_type ( );
			head.var_name ( v, var );
			head.out.put ( var, syntax::LOCAL );
			head.out.put ( L";" );

			if ( item.kind == var_kind::REGISTER && item.family >= 0 ) {

				static const wchar_t* const wide [ 16 ] = { L"rax", L"rcx", L"rdx", L"rbx", L"rsp", L"rbp", L"rsi", L"rdi", L"r8", L"r9", L"r10", L"r11", L"r12", L"r13", L"r14", L"r15" };
				static const wchar_t* const dword [ 8 ] = { L"eax", L"ecx", L"edx", L"ebx", L"esp", L"ebp", L"esi", L"edi" };
				static const wchar_t* const word [ 8 ] = { L"ax", L"cx", L"dx", L"bx", L"sp", L"bp", L"si", L"di" };
				static const wchar_t* const byte [ 8 ] = { L"al", L"cl", L"dl", L"bl", L"spl", L"bpl", L"sil", L"dil" };
				base::writer note;

				note.add ( L" // " );

				if ( item.family >= 16 )
					note.add ( L"xmm" ).number ( item.family - 16 );
				else if ( item.family >= 8 )
					note.add ( wide [ item.family ] ).add ( item.size == 4 ? L"d" : ( item.size == 2 ? L"w" : ( item.size == 1 ? L"b" : L"" ) ) );
				else if ( item.size == 8 && b.long_mode )
					note.add ( wide [ item.family ] );
				else
					note.add ( item.size == 2 ? word [ item.family ] : ( item.size == 1 ? byte [ item.family ] : dword [ item.family ] ) );

				head.out.put ( note.text ( ), syntax::COMMENT );

			} else if ( item.kind == var_kind::STACK ) {

				int64_t from_rsp = item.offset - lowest;
				int64_t from_rbp = item.offset + saved_size;
				bool by_reference = false;

				for ( const expr& use : b.exprs ) {

					if ( use.kind == op::ADDRESS && use.a < 0 && use.value == v )
						by_reference = true;

				}

				base::writer note;

				note.add ( L" // [rsp+" ).hex ( static_cast < uint64_t > ( from_rsp < 0 ? 0 : from_rsp ) ).add ( L"h] [rbp" ).add ( from_rbp < 0 ? L"-" : L"+" ).hex ( static_cast < uint64_t > ( from_rbp < 0 ? -from_rbp : from_rbp ) ).add ( L"h]" );

				if ( by_reference )
					note.add ( L" BYREF" );

				head.out.put ( note.text ( ), syntax::COMMENT );

			}

			head.out.finish ( );
			declared++;

		}

		if ( declared ) {

			head.out.begin ( 0 );
			head.out.finish ( );

		}

		if ( !body.lines.empty ( ) ) {

			const line& tail = body.lines.back ( );

			if ( tail.indent == 1 && tail.length == 7 && body.text [ static_cast < size_t > ( tail.text ) ] == L'r' && body.text [ static_cast < size_t > ( tail.text + 6 ) ] == L';' )
				body.lines.pop ( );

		}

		drop_braces ( body );
		append_result ( out, body );

		head.out.indent = 0;
		head.out.begin ( 0 );
		head.out.put ( L"}" );
		head.out.finish ( );

		out.gotos = body_emitter.gotos / 2;
		out.locals = declared;
		out.hints.clear ( );

		for ( const field_use& item : b.fields ) {

			if ( item.var < 0 || item.var >= b.vars.count ( ) || !body_emitter.referenced [ static_cast < size_t > ( item.var ) ] )
				continue;

			field_hint& hint = out.hints.emplace ( );

			head.var_name ( item.var, hint.var );
			hint.offset = item.offset;
			hint.size = item.size;
			hint.bits = item.bits;

		}

		return true;

	}

	bool decompile ( const analysis::database& db, uint64_t function, namer& names, result& out ) {

		double started = base::seconds ( );
		builder* b = new builder ( );

		out.text.clear ( );
		out.spans.clear ( );
		out.lines.clear ( );
		out.hints.clear ( );
		out.aliases.clear ( );
		out.function = function;
		b->db = &db;
		b->file = db.file;
		b->names = &names;
		b->out = &out;
		b->long_mode = db.long_mode;
		b->function = function;

		bool ok = b->lift ( );

		if ( ok ) {

			const wchar_t* declared = names.prototype ( function );

			if ( declared ) {

				base::writer problem;

				if ( parse_prototype ( declared, b->long_mode, b->prototype, problem ) ) {

					b->has_prototype = true;
					b->params = b->prototype.count;
					b->returns_value = !b->prototype.result_type.empty;
					b->returns_float = ( b->prototype.result_type.bits & FLOATING ) != 0;

					if ( b->returns_value )
						b->return_size = b->prototype.result_type.size;

				}

			}

			if ( !b->has_prototype && b->long_mode ) {

				for ( const block_code& item : b->code ) {

					if ( item.reachable && item.end == term::TAIL && item.returned >= 0 )
						b->returns_value = true;

				}

			}

			for ( const analysis::block& item : b->graph.blocks )
				out.instructions += item.instructions;

			out.blocks = b->graph.blocks.count ( );
			ok = emit ( *b );

		}

		out.ok = ok;
		out.seconds = base::seconds ( ) - started;
		delete b;

		return ok;

	}

}