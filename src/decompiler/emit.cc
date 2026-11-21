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
		int32_t next_number = 1;
		int32_t gotos = 0;
		int32_t depth = 0;
		bool no_ternary = false;
		int32_t next_temp = 1;
		base::list < int32_t > hoisted;
		base::list < int32_t > hoisted_temp;

		explicit emitter ( builder& owner ) : b ( owner ) {

			out.spaced = owner.names && owner.names->spaced ( );
			out.names = owner.names;
			no_ternary = owner.names && owner.names->expand_ternary ( );

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

		void var_name ( int32_t index, wchar_t* text ) {

			default_name ( index, text );

			const variable& item = b.vars [ static_cast < size_t > ( index ) ];
			const wchar_t* user = b.names ? b.names->local_name ( b.function, text ) : nullptr;

			if ( !user && item.kind == var_kind::PARAMETER && b.has_prototype && item.position >= 0 && item.position < b.prototype.count && b.prototype.names [ item.position ] [ 0 ] )
				user = b.prototype.names [ item.position ];

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
			wchar_t digits [ 24 ];

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

			} else if ( item.kind == var_kind::STACK ) {

				hex ( static_cast < uint64_t > ( item.offset < 0 ? -item.offset : item.offset ), digits );
				add ( item.offset < 0 ? L"local_" : L"home_" );
				add ( digits );

			} else {

				if ( var_number [ static_cast < size_t > ( index ) ] == 0 )
					var_number [ static_cast < size_t > ( index ) ] = next_number++;

				add ( item.bits & FLOATING ? L"f" : L"v" );
				decimal ( var_number [ static_cast < size_t > ( index ) ] );

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

			type_name ( item.size, item.bits, ( item.bits & POINTER ) && item.size == ( b.long_mode ? 8 : 4 ) && !( item.bits & FLOATING ) );

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

		void number ( int64_t value, uint8_t size, bool hex_form, bool unsigned_suffix ) {

			wchar_t text [ 40 ];
			int32_t length = 0;
			bool negative = value < 0 && !( size < 8 && value > 0 );
			uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

			if ( negative && magnitude > 0x100000000ull && hex_form ) {

				negative = false;
				magnitude = size >= 8 ? static_cast < uint64_t > ( value ) : static_cast < uint64_t > ( value ) & ( ( 1ull << ( size * 8 ) ) - 1 );

			}

			if ( negative )
				text [ length++ ] = L'-';

			if ( hex_form && magnitude >= 10 ) {

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

			if ( unsigned_suffix && !negative )
				text [ length++ ] = L'u';

			text [ length ] = 0;
			out.put ( text, syntax::NUMBER );

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

			if ( magnitude >= 0x80000000ull )
				return false;

			if ( magnitude >= 0x100 && ( magnitude & ( magnitude - 1 ) ) == 0 )
				return false;

			if ( magnitude >= 0xFF && ( ( magnitude + 1 ) & magnitude ) == 0 )
				return false;

			return true;

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
			int32_t mine = precedence ( item.kind );
			bool wrap = mine < parent;
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

						out.put ( L"(" );
						value ( item.a, 14 );
						out.put ( L")" );

					}

					out.put ( L"(" );

					for ( int32_t i = 0; i < item.arg_count; i++ ) {

						if ( i )
							out.put ( L", " );

						value ( b.args [ static_cast < size_t > ( item.first_arg + i ) ], 2 );

					}

					out.put ( L")" );

					break;

				}

				case op::INTRINSIC:

					out.put ( intrinsic_name ( static_cast < int32_t > ( item.value ) ), syntax::FUNCTION );
					out.put ( L"(" );

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
					bool shown = float_change || inner.size != item.size;

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

					if ( shown ) {

						out.put ( L"(" );
						type_name ( item.size, item.bits, false );
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
			out.put ( L" (" );
			value ( test, 0 );
			out.put ( L") {" );
			out.finish ( );
			out.indent++;
			out.begin ( address );

		}

		void branch_else ( uint64_t address ) {

			out.finish ( );
			out.indent--;
			out.begin ( 0 );
			out.put ( L"} " );
			out.put ( L"else", syntax::CONTROL );
			out.put ( L" {" );
			out.finish ( );
			out.indent++;
			out.begin ( address );

		}

		void branch_close ( ) {

			out.finish ( );
			out.indent--;
			out.begin ( 0 );
			out.put ( L"}" );

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

			wchar_t digits [ 20 ];
			int32_t saved = out.indent;

			hex ( b.graph.blocks [ static_cast < size_t > ( block ) ].start - b.function, digits );
			out.indent = base::max ( saved - 1, 0 );
			out.begin ( b.graph.blocks [ static_cast < size_t > ( block ) ].start );
			out.put ( base::writer ( ).add ( L"L_" ).add ( digits ).text ( ), syntax::LABEL );
			out.put ( L":" );
			out.finish ( );
			out.indent = saved;

		}

		void go_to ( int32_t block, uint64_t address ) {

			wanted_label [ static_cast < size_t > ( block ) ] = 1;
			gotos++;

			if ( dry )
				return;

			wchar_t digits [ 20 ];

			hex ( b.graph.blocks [ static_cast < size_t > ( block ) ].start - b.function, digits );
			out.begin ( address );
			out.put ( L"goto", syntax::CONTROL );
			out.put ( L" " );
			out.put ( base::writer ( ).add ( L"L_" ).add ( digits ).text ( ), syntax::LABEL );
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

				out.put ( L" (" );

				if ( test.kind == op::CONST && !negated )
					out.put ( test.value ? L"true" : L"false", syntax::KEYWORD );
				else
					value ( shown, 0 );

				out.put ( L")" );

			}

			out.put ( L" {" );
			out.finish ( );
			out.indent++;
			hoisted.clear ( );
			hoisted_temp.clear ( );

		}

		void close_brace ( const wchar_t* tail = nullptr ) {

			out.indent--;

			if ( dry )
				return;

			out.begin ( 0 );
			out.put ( L"}" );

			if ( tail )
				out.put ( tail );

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

				out.put ( L" " );
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

					if ( !dry ) {

						out.begin ( 0 );
						out.put ( L"}" );
						out.finish ( );

					}

					body ( else_part );

					int32_t next = branch ( else_part, stop, context, true );

					( void ) next;

				} else {

					if ( !dry ) {

						out.begin ( 0 );
						out.put ( L"} " );
						out.put ( L"else", syntax::CONTROL );
						out.put ( L" {" );
						out.finish ( );

					}

					out.indent++;

					if ( !special ( else_part, -2, context, item.last ) )
						region ( else_part, follow, context );

					close_brace ( );

				}

			} else if ( !dry ) {

				out.begin ( 0 );
				out.put ( L"}" );
				out.finish ( );

			}

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
				out.indent--;

				if ( !dry ) {

					out.begin ( item.last );
					out.put ( L"} " );
					out.put ( L"while", syntax::CONTROL );
					out.put ( L" (" );
					value ( back_taken ? item.condition : b.negate ( item.condition ), 0 );
					out.put ( L");" );
					out.finish ( );

				}

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
					out.indent--;

					if ( !dry ) {

						out.begin ( tail.last );
						out.put ( L"} " );
						out.put ( L"while", syntax::CONTROL );
						out.put ( L" (" );
						value ( back_taken ? tail.condition : b.negate ( tail.condition ), 0 );
						out.put ( L");" );
						out.finish ( );

					}

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
				out.put ( L" (" );
				value ( item.selector, 0 );
				out.put ( L") {" );
				out.finish ( );

			}

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

					out.indent--;
					out.begin ( b.graph.blocks [ static_cast < size_t > ( target ) ].start );

					int32_t shown = 0;

					for ( int32_t e = 0; e < node.out_count; e++ ) {

						const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];

						if ( link.to != target )
							continue;

						if ( shown++ )
							out.put ( L" " );

						if ( shown > 6 ) {

							out.put ( L"..." );

							break;

						}

						out.put ( L"case", syntax::CONTROL );
						out.put ( L" " );
						number ( link.value, 4, false, false );
						out.put ( L":" );

					}

					out.finish ( );
					out.indent++;

				}

				if ( target != follow && !emitted [ static_cast < size_t > ( target ) ] )
					region ( target, follow, inner );
				else if ( target != follow )
					go_to ( target, 0 );

				if ( !out.terminal )
					jump_line ( L"break", 0 );

			}

			out.indent--;

			if ( !dry ) {

				out.begin ( 0 );
				out.put ( L"}" );
				out.finish ( );

			}

			return follow;

		}

		void run ( ) {

			emitted.fill ( static_cast < size_t > ( count ), 0 );
			labeled.fill ( static_cast < size_t > ( count ), 0 );
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
			var_number.fill ( b.vars.size ( ), 0 );
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

	bool emit ( builder& b ) {

		emitter body_emitter ( b );
		result body;

		body_emitter.analyze ( );
		body_emitter.out.r = &body;
		body_emitter.dry = true;
		body_emitter.out.indent = 1;
		body_emitter.run ( );
		body_emitter.dry = false;
		body_emitter.out.indent = 1;
		body_emitter.run ( );
		body_emitter.dry = true;
		b.var_types.resize ( b.vars.size ( ) );
		b.var_structs.resize ( b.vars.size ( ) );

		bool typed = false;

		for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

			variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( item.kind == var_kind::REGISTER && body_emitter.var_number [ static_cast < size_t > ( v ) ] == 0 )
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

		if ( typed ) {

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
				head.type_name ( b.return_size ? b.return_size : ( b.long_mode ? 8 : 4 ), 0, false );

		} else
			head.out.put ( L"void", syntax::KEYWORD );

		head.out.put ( L" " );

		if ( !b.names->name ( b.function, true, name, 300 ) )
			name [ 0 ] = 0;

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

		if ( !shown_params )
			head.out.put ( L"void", syntax::KEYWORD );

		head.out.put ( L")" );
		head.out.finish ( );
		head.out.begin ( b.function );
		head.out.put ( L"{" );
		head.out.finish ( );
		head.out.indent = 1;

		int32_t declared = 0;

		for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( item.kind == var_kind::PARAMETER || item.cookie || item.saved )
				continue;

			if ( !body_emitter.referenced [ static_cast < size_t > ( v ) ] )
				continue;

			wchar_t var [ 64 ];

			head.out.begin ( 0 );
			head.var_type ( v );
			head.name_after_type ( );
			head.var_name ( v, var );
			head.out.put ( var, syntax::LOCAL );
			head.out.put ( L";" );

			if ( item.kind == var_kind::REGISTER && item.family >= 0 ) {

				static const wchar_t* const gpr [ 16 ] = { L"rax", L"rcx", L"rdx", L"rbx", L"rsp", L"rbp", L"rsi", L"rdi", L"r8", L"r9", L"r10", L"r11", L"r12", L"r13", L"r14", L"r15" };
				static const wchar_t* const narrow [ 8 ] = { L"eax", L"ecx", L"edx", L"ebx", L"esp", L"ebp", L"esi", L"edi" };

				head.out.put ( L"  " );

				if ( item.family < 16 )
					head.out.put ( base::writer ( ).add ( L"// " ).add ( b.long_mode || item.family >= 8 ? gpr [ item.family ] : narrow [ item.family ] ).text ( ), syntax::COMMENT );
				else
					head.out.put ( base::writer ( ).add ( L"// xmm" ).number ( item.family - 16 ).text ( ), syntax::COMMENT );

			} else if ( item.kind == var_kind::STACK ) {

				head.out.put ( L"  " );
				head.out.put ( base::writer ( ).add ( L"// [rsp" ).add ( item.offset < 0 ? L"-0x" : L"+0x" ).hex ( static_cast < uint64_t > ( item.offset < 0 ? -item.offset : item.offset ) ).add ( item.offset < 0 ? L"] at entry" : L"] at entry, the caller's home space" ).text ( ), syntax::COMMENT );

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