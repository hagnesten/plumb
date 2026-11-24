#include "decompiler/internal.hh"

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

		wchar_t last ( int32_t back = 1 ) const {

			return r->text.count ( ) - back >= current.text ? r->text [ static_cast < size_t > ( r->text.count ( ) - back ) ] : 0;

		}

		void begin ( uint64_t address ) {

			terminal = -1;
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

		void wrap ( ) {

			int32_t start = current.text;
			int32_t length = r->text.count ( ) - start;

			if ( spaced || length < 4 )
				return;

			const wchar_t* text = r->text.data ( ) + start;
			int32_t width = current.indent * 2 + length;

			for ( int32_t i = 0; i + 2 < length; i++ ) {

				bool edge = i == 0 || !( ( text [ i - 1 ] >= L'a' && text [ i - 1 ] <= L'z' ) || ( text [ i - 1 ] >= L'A' && text [ i - 1 ] <= L'Z' ) || ( text [ i - 1 ] >= L'0' && text [ i - 1 ] <= L'9' ) || text [ i - 1 ] == L'_' );

				if ( !edge )
					continue;

				if ( text [ i ] == L'g' && text [ i + 1 ] == L'_' )
					width += 4;
				else if ( text [ i ] == L'f' && text [ i + 1 ] == L'n' && text [ i + 2 ] == L'_' )
					width += 1;

			}

			if ( width < ( header ? 121 : 120 ) )
				return;

			int32_t close = header ? length - 1 : length - 2;

			if ( text [ close ] != L')' || ( !header && text [ length - 1 ] != L';' ) )
				return;

			int32_t open_at = -1;
			int32_t level = 0;
			bool quoted = false;

			for ( int32_t i = close; i >= 0; i-- ) {

				wchar_t c = text [ i ];

				if ( c == L'"' && ( i == 0 || text [ i - 1 ] != L'\\' ) )
					quoted = !quoted;

				if ( quoted )
					continue;

				if ( c == L')' )
					level++;
				else if ( c == L'(' && --level == 0 ) {

					open_at = i;

					break;

				}

			}

			if ( open_at <= 0 )
				return;

			wchar_t before = text [ open_at - 1 ];

			if ( !( ( before >= L'a' && before <= L'z' ) || ( before >= L'A' && before <= L'Z' ) || ( before >= L'0' && before <= L'9' ) || before == L'_' ) )
				return;

			int32_t name_at = open_at - 1;

			while ( name_at > 0 ) {

				wchar_t c = text [ name_at - 1 ];

				if ( ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L'_' || c == L':' )
					name_at--;
				else
					break;

			}

			if ( name_at > 0 && text [ name_at - 1 ] != L' ' )
				return;

			if ( name_at >= 2 && text [ name_at - 2 ] == L'=' && name_at >= 3 && text [ name_at - 3 ] == L'=' )
				return;

			base::list < int32_t > cuts;

			level = 0;
			quoted = false;

			for ( int32_t i = open_at + 1; i < close; i++ ) {

				wchar_t c = text [ i ];

				if ( c == L'"' && text [ i - 1 ] != L'\\' )
					quoted = !quoted;

				if ( quoted )
					continue;

				if ( c == L'(' || c == L'[' )
					level++;
				else if ( c == L')' || c == L']' )
					level--;
				else if ( c == L',' && level == 0 && text [ i + 1 ] == L' ' )
					cuts.push ( i );

			}

			if ( cuts.count ( ) == 0 && open_at + 1 >= close )
				return;

			int32_t column = header ? 8 : name_at + 2;
			base::list < wchar_t > copy;

			for ( int32_t i = 0; i < length; i++ )
				copy.push ( text [ i ] );

			base::list < int32_t > moved;

			moved.fill ( static_cast < size_t > ( length + 1 ), 0 );

			while ( r->text.count ( ) > start )
				r->text.pop ( );

			base::list < int32_t > breaks;
			int32_t cut = 0;

			for ( int32_t i = 0; i < length; i++ ) {

				moved [ static_cast < size_t > ( i ) ] = r->text.count ( ) - start;
				r->text.push ( copy [ static_cast < size_t > ( i ) ] );

				bool split = i == open_at || ( cut < cuts.count ( ) && i == cuts [ static_cast < size_t > ( cut ) ] );

				if ( !split )
					continue;

				if ( i != open_at ) {

					cut++;
					i++;
					moved [ static_cast < size_t > ( i ) ] = r->text.count ( ) - start;

				}

				breaks.push ( r->text.count ( ) - start );

				for ( int32_t k = 0; k < column; k++ )
					r->text.push ( L' ' );

			}

			moved [ static_cast < size_t > ( length ) ] = r->text.count ( ) - start;

			for ( int32_t s = current.first_span; s < r->spans.count ( ); s++ ) {

				span& item = r->spans [ static_cast < size_t > ( s ) ];
				int32_t from = item.start - start;
				int32_t to = from + item.length;

				if ( from < 0 || to > length || to <= from )
					continue;

				item.start = start + moved [ static_cast < size_t > ( from ) ];
				item.length = moved [ static_cast < size_t > ( to - 1 ) ] + 1 - moved [ static_cast < size_t > ( from ) ];

			}

			int32_t from = 0;
			int32_t span_at = current.first_span;

			for ( int32_t edge : breaks ) {

				line piece = current;

				piece.text = start + from;
				piece.length = edge - from;
				piece.first_span = span_at;

				while ( span_at < r->spans.count ( ) && r->spans [ static_cast < size_t > ( span_at ) ].start < start + edge )
					span_at++;

				piece.span_count = span_at - piece.first_span;
				r->lines.push ( piece );
				from = edge;

			}

			current.text = start + from;
			current.first_span = span_at;

		}

		void finish ( ) {

			if ( !open )
				return;

			wrap ( );

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

	static void short_symbol ( wchar_t* name ) {

		if ( name [ 0 ] != L'?' )
			return;

		const wchar_t* at = name + 1;
		int32_t special = 0;

		if ( at [ 0 ] == L'?' ) {

			if ( at [ 1 ] == L'0' )
				special = 1;
			else if ( at [ 1 ] == L'1' )
				special = 2;
			else
				return;

			at += 2;

		}

		const wchar_t* parts [ 8 ];
		int32_t sizes [ 8 ];
		int32_t count = 0;

		while ( *at && *at != L'@' && count < 8 ) {

			const wchar_t* start = at;

			while ( *at && *at != L'@' ) {

				if ( !( ( *at >= L'a' && *at <= L'z' ) || ( *at >= L'A' && *at <= L'Z' ) || ( *at >= L'0' && *at <= L'9' ) || *at == L'_' ) )
					return;

				at++;

			}

			parts [ count ] = start;
			sizes [ count ] = static_cast < int32_t > ( at - start );
			count++;

			if ( *at == L'@' )
				at++;

		}

		if ( *at != L'@' || count == 0 )
			return;

		wchar_t result [ 200 ];
		int32_t length = 0;

		auto add = [ & ] ( const wchar_t* text, int32_t size ) {

			for ( int32_t i = 0; i < size && length < 190; i++ )
				result [ length++ ] = text [ i ];

		};

		for ( int32_t i = count - 1; i >= 1; i-- ) {

			add ( parts [ i ], sizes [ i ] );
			add ( L"::", 2 );

		}

		if ( special == 2 )
			add ( L"~", 1 );

		if ( special && count >= 2 )
			add ( parts [ 1 ], sizes [ 1 ] );
		else
			add ( parts [ 0 ], sizes [ 0 ] );

		if ( special && count == 1 ) {

			add ( L"::", 2 );

			if ( special == 2 )
				add ( L"~", 1 );

			add ( parts [ 0 ], sizes [ 0 ] );

		}

		result [ length ] = 0;

		for ( int32_t i = 0; i <= length; i++ )
			name [ i ] = result [ i ];

	}

	static bool flow_casts = true;
	static bool ghost_numbers = true;
	static bool exits_inline = true;
	static bool frame_deepest = true;
	static bool implicit_narrow = true;
	static bool split_selects = true;
	static bool char_split = true;
	static bool char_address = true;
	static bool char_calls = true;
	static bool vector_copies = true;
	static bool byte_elements = true;
	static bool void_needs_char = true;
	static bool void_needs_char_params = false;
	static bool return_exits = true;
	static bool trim_crt = true;
	static bool unnamed_results = true;
	static bool intrinsic_types = true;
	static bool global_arrays = true;
	static bool empty_then = true;
	static bool api_types = true;
	static bool double_pointees = true;
	static bool defer_later = true;
	static bool return_casts = true;
	static bool return_var_casts = false;
	static bool unsigned_narrow = true;
	static bool bit_tests = true;
	static bool base_first = true;
	static bool array_vars = true;
	static bool array_memsets = true;
	static bool shared_returns = true;
	static bool fold_increments = true;
	static bool wrapped_unsigned = true;
	static bool mask_forms = true;
	static bool latch_folds = true;
	static bool inferred_unsigned = true;
	static bool widen_assign = true;
	static bool sext_products = true;
	static bool collapse_casts = true;
	static bool call_untruncate = true;
	static bool store_untruncate = true;
	static bool nested_loops = true;
	static bool widened_halves = true;
	static bool void_arithmetic = true;
	static bool diamond_ternary = false;
	static bool guarded_loops = true;

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
		bool inline_mode = false;
		bool ended = false;
		bool bare_unsigned = false;
		bool vector_store = false;
		bool truth = false;
		bool in_logic = false;
		bool untruncate = false;
		uint8_t holder_size = 0;
		op holder_kind = op::CONST;
		int32_t forced_exit = -1;
		int32_t forced_outer = -1;
		int32_t nest_depth = 0;
		base::list < int32_t > array_of;
		base::list < int32_t > array_step;
		base::list < int32_t > array_count;
		base::list < int32_t > fold_at;
		base::list < int8_t > fold_how;
		base::list < int32_t > fold_cond;
		base::list < int32_t > fold_cond_var;
		base::list < int8_t > fold_cond_how;
		int32_t active_var = -1;
		int8_t active_how = 0;
		int64_t active_unit = 1;
		base::list < int64_t > fold_unit;
		base::list < int64_t > fold_cond_unit;
		int32_t active_slot = -1;
		bool active_cond = false;
		base::list < int32_t > fold_inc;
		base::list < uint8_t > fold_done;
		base::list < int32_t > fold_cond_inc;
		base::list < uint8_t > fold_cond_done;
		bool explicit_zero = false;
		base::list < uint8_t > hidden;
		base::list < uint8_t > element;
		base::list < uint8_t > boolean;
		base::list < uint8_t > vector;
		bool inline_returns = true;
		bool call_unused = false;
		bool fall_returns = true;
		bool step_needs_init = false;
		bool by_address = true;
		bool indexing = true;
		wchar_t param_type [ 64 ] = { };
		bool defer_joins = true;
		bool rotate_guards = false;
		base::list < int8_t > loop_letter;
		int32_t next_letter = 0;
		int32_t last_assign = -1;
		int32_t last_assign_line = -1;
		base::list < int32_t > recent_assign;
		base::list < int32_t > recent_line;

		bool find_init ( int64_t var, int32_t& found, int32_t& line ) {

			if ( dry )
				return false;

			int32_t expected = out.r->lines.count ( ) - 1;

			for ( int32_t i = recent_line.count ( ) - 1; i >= 0 && expected >= 0 && recent_line.count ( ) - i <= 3; i-- ) {

				if ( recent_line [ static_cast < size_t > ( i ) ] != expected )
					return false;

				const statement& item = b.statements [ static_cast < size_t > ( recent_assign [ static_cast < size_t > ( i ) ] ) ];

				if ( b.exprs [ static_cast < size_t > ( item.left ) ].value == var ) {

					found = recent_assign [ static_cast < size_t > ( i ) ];
					line = expected;

					return true;

				}

				if ( b.references ( item.right, static_cast < int32_t > ( var ), -1 ) )
					return false;

				expected--;

			}

			return false;

		}
		bool operand = false;
		bool no_casts = false;
		base::list < uint64_t > number_addresses;
		base::list < int64_t > number_values;
		int32_t last_occurrence = 0;

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
			indexing = !owner.names || owner.names->index_pointers ( );

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
			base::list < uint64_t > first_def;
			bool changed = false;

			first_def.fill ( b.vars.size ( ), ~0ull );

			for ( const statement& line : b.statements ) {

				if ( line.kind != stmt::ASSIGN || line.left < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || !line.address )
					continue;

				int64_t v = b.exprs [ static_cast < size_t > ( line.left ) ].value;

				uint64_t at = line.origin ? line.origin : line.address;

				if ( v >= 0 && v < first_def.count ( ) && at < first_def [ static_cast < size_t > ( v ) ] )
					first_def [ static_cast < size_t > ( v ) ] = at;

			}

			for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

				const variable& item = b.vars [ static_cast < size_t > ( v ) ];

				if ( ghost_numbers && item.ghost && item.born && by_address && var_number [ static_cast < size_t > ( v ) ] < 0 ) {

					first_def [ static_cast < size_t > ( v ) ] = item.born;
					order.push ( v );

					continue;

				}

				if ( var_number [ static_cast < size_t > ( v ) ] >= 0 && item.kind != var_kind::PARAMETER )
					order.push ( v );

			}

			base::list < uint64_t > extra;

			auto def_of = [ & ] ( int32_t x ) {

				return x < 0 ? extra [ static_cast < size_t > ( -x - 1 ) ] : first_def [ static_cast < size_t > ( x ) ];

			};

			base::sort ( order.data ( ), order.count ( ), [ & ] ( int32_t x, int32_t y ) {

				bool left_stack = x >= 0 && b.vars [ static_cast < size_t > ( x ) ].kind == var_kind::STACK;
				bool right_stack = y >= 0 && b.vars [ static_cast < size_t > ( y ) ].kind == var_kind::STACK;

				if ( left_stack != right_stack )
					return !left_stack;

				if ( left_stack )
					return b.vars [ static_cast < size_t > ( x ) ].offset < b.vars [ static_cast < size_t > ( y ) ].offset;

				if ( by_address && def_of ( x ) != def_of ( y ) )
					return def_of ( x ) < def_of ( y );

				if ( x < 0 || y < 0 )
					return x < y;

				return var_number [ static_cast < size_t > ( x ) ] < var_number [ static_cast < size_t > ( y ) ];

			} );

			int32_t next = b.params;

			for ( int32_t v : order ) {

				if ( v < 0 || ( b.vars [ static_cast < size_t > ( v ) ].ghost && var_number [ static_cast < size_t > ( v ) ] < 0 ) ) {

					next++;

					continue;

				}

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

			static const wchar_t* const letters [ 10 ] = { L"i", L"j", L"k", L"m", L"n", L"ii", L"jj", L"kk", L"mm", L"nn" };
			bool lettered = !user && index < loop_letter.count ( ) && loop_letter [ static_cast < size_t > ( index ) ] >= 0;

			if ( !user && index != result_var && ( lettered || ( index < b.call_names.count ( ) && !b.call_names [ static_cast < size_t > ( index ) ].empty ( ) ) ) ) {

				user = lettered ? letters [ loop_letter [ static_cast < size_t > ( index ) ] ] : b.call_names [ static_cast < size_t > ( index ) ].get ( );

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

			if ( owner.kind != var_kind::PARAMETER || owner.bits & FLOATING || !b.names || !b.names->guess_structs ( ) )
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

			if ( size == 16 && vector_store )
				text = L"__m128i";
			else if ( bits & FLOATING )
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
						text = L"__m128i";
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

			if ( index == b.va_var ) {

				out.put ( L"va_list", syntax::TYPE );

				return;

			}

			if ( index < vector.count ( ) && vector [ static_cast < size_t > ( index ) ] ) {

				out.put ( L"__m128i", syntax::TYPE );

				return;

			}

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

			if ( item.flags || ( index == result_var && b.result_width == 2 && item.size == 1 ) || ( index < boolean.count ( ) && boolean [ static_cast < size_t > ( index ) ] ) ) {

				out.put ( L"bool", syntax::TYPE );

				return;

			}

			if ( index < element.count ( ) && element [ static_cast < size_t > ( index ) ] ) {

				uint8_t shape = element [ static_cast < size_t > ( index ) ];

				if ( shape == 0x10 ) {

					out.put ( L"char", syntax::TYPE );
					out.put ( L" *" );

					return;

				}

				if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) ) {

					out.put ( L"void", syntax::TYPE );
					out.put ( L" **" );

					return;

				}

				if ( ( shape & 0x9F ) == 8 && ( shape & 0x20 ) )
					type_name ( 8, SIGNED, false );
				else if ( ( shape & 0x40 ) && !( shape & 0x80 ) )
					type_name ( shape & 0x1F, UNSIGNED, false );
				else
					type_name ( shape & 0x1F, ( shape & 0x80 ) ? FLOATING : 0, false, !( shape & 0x80 ) );
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

		uint64_t shape_target = 0;
		bool shape_ok = false;
		c_prototype* shape = nullptr;

		~emitter ( ) {

			delete shape;

		}

		bool callee_unsigned ( uint64_t target, int32_t index ) {

			if ( !target || !b.names )
				return false;

			if ( target != shape_target ) {

				shape_target = target;
				shape_ok = false;

				const wchar_t* declared = b.names->prototype ( target );
				wchar_t callee [ 200 ];

				if ( !declared && b.names->name ( target, true, callee, 200 ) )
					declared = api_prototype ( callee );

				if ( declared ) {

					if ( !shape )
						shape = new c_prototype ( );

					base::writer problem;

					shape_ok = parse_prototype ( declared, b.long_mode, *shape, problem );

				}

			}

			return shape_ok && index < shape->count && ( shape->params [ index ].bits & UNSIGNED ) && !shape->params [ index ].pointer;

		}

		bool no_return ( const expr& call ) {

			wchar_t name [ 200 ];

			if ( call.kind != op::CALL || !call.value || !b.names->name ( static_cast < uint64_t > ( call.value ), true, name, 200 ) )
				return false;

			short_symbol ( name );

			const wchar_t* text = name;

			while ( *text == L'_' )
				text++;

			static const wchar_t* const known [ ] = { L"terminate", L"abort", L"exit", L"ExitProcess", L"ExitThread", L"FatalExit", L"FatalAppExitW", L"FatalAppExitA", L"RaiseFailFastException", L"invalid_parameter_noinfo_noreturn", L"report_gsfailure", L"raise_securityfailure", L"CxxThrowException", L"amsg_exit", L"invoke_watson" };

			for ( const wchar_t* item : known ) {

				if ( base::same_text ( text, item ) )
					return true;

			}

			return false;

		}

		bool callee_wide ( uint64_t target, int32_t index ) {

			callee_unsigned ( target, index );

			return shape_ok && index < shape->count && ( shape->params [ index ].pointer || shape->params [ index ].size == 8 ) && !( shape->params [ index ].bits & FLOATING );

		}

		void number ( int64_t value, uint8_t size, bool hex_form, bool is_unsigned ) {

			wchar_t text [ 80 ];
			int32_t length = 0;
			bool negative = value < 0 && !( size < 8 && value > 0 );
			uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

			bool wrapped = false;

			if ( negative && ( is_unsigned || bare_unsigned || ( magnitude > 0x100000000ull && hex_form ) ) ) {

				wrapped = !is_unsigned && !bare_unsigned && size >= 8;
				negative = false;
				magnitude = size >= 8 ? static_cast < uint64_t > ( value ) : static_cast < uint64_t > ( value ) & ( ( 1ull << ( size * 8 ) ) - 1 );

			}

			if ( bare_unsigned && magnitude > 9 )
				hex_form = true;

			bool fits = negative ? magnitude <= 0x80000000ull : magnitude <= ( is_unsigned || bare_unsigned ? 0xFFFFFFFFull : 0x7FFFFFFFull );
			bool unsigned_suffix = ( is_unsigned && !negative && !bare_unsigned ) || ( wrapped && wrapped_unsigned );
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

			if ( b.names->literal ( address, text, 290 ) ) {

				out.put ( text, syntax::STRING, address );

				return;

			}

			if ( address != b.file->base && b.names->name ( address, true, text, 200 ) ) {

				short_symbol ( text );

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

			if ( address != b.file->base && b.names->name ( address, false, text, 200 ) ) {

				out.put ( L"&" );
				out.put ( text, syntax::GLOBAL, address );

				return;

			}

			number ( static_cast < int64_t > ( address ), 8, true, false );

		}

		bool put_folded ( int32_t var, const wchar_t* name ) {

			if ( active_var < 0 || var != active_var || var_step ( var ) != active_unit )
				return false;

			if ( active_how <= 2 )
				out.put ( active_how == 1 ? L"++" : L"--", syntax::OPERATOR );

			out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

			if ( active_how >= 3 )
				out.put ( active_how == 3 ? L"++" : L"--", syntax::OPERATOR );

			if ( active_cond && active_slot >= 0 && active_slot < fold_cond_done.count ( ) )
				fold_cond_done [ static_cast < size_t > ( active_slot ) ] = 1;
			else if ( !active_cond && active_slot >= 0 && active_slot < fold_done.count ( ) )
				fold_done [ static_cast < size_t > ( active_slot ) ] = 1;

			active_var = -1;

			return true;

		}

		int64_t var_step ( int32_t v ) const {

			if ( v < 0 || v >= element.count ( ) || !element [ static_cast < size_t > ( v ) ] )
				return 1;

			uint8_t shape = element [ static_cast < size_t > ( v ) ];

			if ( shape == 0x10 )
				return 1;

			if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) )
				return 8;

			return shape & 0x0F ? shape & 0x0F : 1;

		}

		bool char_typed_sum ( int32_t e ) const {

			int32_t var = -1;
			int32_t index = -1;
			int64_t offset = 0;

			if ( char_address && char_terms ( e, var, index, offset ) )
				return true;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			return node.kind == op::ADD && ( pointer_step ( node.a ) > 0 || ( node.b >= 0 && pointer_step ( node.b ) > 0 ) );

		}

		int32_t count_var ( int32_t e, int64_t v, int32_t depth ) const {

			if ( e < 0 || depth > 64 )
				return 0;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
			int32_t n = node.kind == op::VAR && node.value == v ? 1 : 0;

			n += count_var ( node.a, v, depth + 1 ) + count_var ( node.b, v, depth + 1 ) + count_var ( node.c, v, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				n += count_var ( b.args [ static_cast < size_t > ( node.first_arg + i ) ], v, depth + 1 );

			return n;

		}

		bool count_call ( int32_t e, int32_t depth ) const {

			if ( e < 0 || depth > 64 )
				return false;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::CALL || node.kind == op::INTRINSIC )
				return true;

			return count_call ( node.a, depth + 1 ) || count_call ( node.b, depth + 1 ) || count_call ( node.c, depth + 1 );

		}

		void plan_increments ( ) {

			fold_at.clear ( );
			fold_how.clear ( );
			fold_cond.clear ( );
			fold_cond_var.clear ( );
			fold_cond_how.clear ( );
			fold_at.fill ( b.statements.size ( ), -1 );
			fold_how.fill ( b.statements.size ( ), 0 );
			fold_inc.clear ( );
			fold_done.clear ( );
			fold_cond_inc.clear ( );
			fold_cond_done.clear ( );
			fold_inc.fill ( b.statements.size ( ), -1 );
			fold_done.fill ( b.statements.size ( ), 0 );
			fold_unit.clear ( );
			fold_cond_unit.clear ( );
			fold_unit.fill ( b.statements.size ( ), 1 );

			if ( !fold_increments )
				return;

			if ( hidden.count ( ) < b.statements.count ( ) )
				hidden.fill ( b.statements.size ( ) - hidden.size ( ), 0 );

			base::list < uint8_t > flag_read;

			flag_read.fill ( b.vars.size ( ), 0 );

			auto mark_reads = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

				if ( e < 0 || depth > 64 )
					return;

				const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

				if ( node.kind == op::VAR && node.value >= 0 && node.value < b.vars.count ( ) )
					flag_read [ static_cast < size_t > ( node.value ) ] = 1;

				self ( self, node.a, depth + 1 );
				self ( self, node.b, depth + 1 );
				self ( self, node.c, depth + 1 );

				for ( int32_t i = 0; i < node.arg_count; i++ )
					self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

			};

			for ( const statement& line : b.statements ) {

				if ( line.kind == stmt::NOP )
					continue;

				mark_reads ( mark_reads, line.right, 0 );

				if ( !( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) )
					mark_reads ( mark_reads, line.left, 0 );

			}

			for ( const block_code& block : b.code ) {

				mark_reads ( mark_reads, block.condition, 0 );
				mark_reads ( mark_reads, block.selector, 0 );
				mark_reads ( mark_reads, block.returned, 0 );

			}

			for ( int32_t k = 0; k < count; k++ ) {

				const block_code& block = b.code [ static_cast < size_t > ( k ) ];

				if ( !block.reachable )
					continue;

				base::list < int32_t > lines;

				for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

					const statement& line = b.statements [ static_cast < size_t > ( s ) ];

					if ( line.kind == stmt::NOP )
						continue;

					if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ].flags && !flag_read [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ) ] )
						continue;

					lines.push ( s );

				}

				bool latch = false;
				int32_t next_blocks [ 2 ] = { block.taken, block.end == term::BRANCH ? block.other : -1 };

				for ( int32_t n : next_blocks ) {

					if ( n >= 0 && n < count && header [ static_cast < size_t > ( n ) ] && b.graph.blocks [ static_cast < size_t > ( n ) ].start <= b.graph.blocks [ static_cast < size_t > ( k ) ].start )
						latch = true;

				}

				for ( int32_t t = 0; t < lines.count ( ); t++ ) {

					int32_t at = lines [ static_cast < size_t > ( t ) ];
					const statement& line = b.statements [ static_cast < size_t > ( at ) ];

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
						continue;

					int64_t x = b.exprs [ static_cast < size_t > ( line.left ) ].value;
					const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

					if ( x < 0 || x >= b.vars.count ( ) || b.vars [ static_cast < size_t > ( x ) ].kind != var_kind::REGISTER || ( right.bits & FLOATING ) || right.kind != op::ADD || right.a < 0 || right.b < 0 )
						continue;

					const expr& self_node = b.exprs [ static_cast < size_t > ( right.a ) ];
					const expr& amount = b.exprs [ static_cast < size_t > ( right.b ) ];

					if ( self_node.kind != op::VAR || self_node.value != x || amount.kind != op::CONST )
						continue;

					int64_t unit = amount.value < 0 ? -amount.value : amount.value;

					if ( unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16 )
						continue;

					bool up = amount.value > 0;
					bool last_line = t == lines.count ( ) - 1;

					if ( t > 0 ) {

						int32_t before = lines [ static_cast < size_t > ( t - 1 ) ];
						const statement& prior = b.statements [ static_cast < size_t > ( before ) ];
						bool defines = prior.kind == stmt::ASSIGN && prior.left >= 0 && b.exprs [ static_cast < size_t > ( prior.left ) ].kind == op::VAR;
						int32_t uses = count_var ( prior.right, x, 0 ) + ( defines ? 0 : count_var ( prior.left, x, 0 ) );
						int32_t loaded = prior.right;

						while ( loaded >= 0 && b.exprs [ static_cast < size_t > ( loaded ) ].kind == op::CAST )
							loaded = b.exprs [ static_cast < size_t > ( loaded ) ].a;

						bool memory = !defines || ( loaded >= 0 && b.exprs [ static_cast < size_t > ( loaded ) ].kind == op::LOAD );
						bool calls = prior.right < 0 || count_call ( prior.right, 0 ) || ( !defines && count_call ( prior.left, 0 ) );

						if ( ( !( last_line && latch ) || ( memory && latch_folds ) ) && !calls && ( prior.kind == stmt::ASSIGN || prior.kind == stmt::STORE ) && uses == 1 && fold_at [ static_cast < size_t > ( before ) ] < 0 && !hidden [ static_cast < size_t > ( before ) ] && !( defines && b.exprs [ static_cast < size_t > ( prior.left ) ].value == x ) ) {

							fold_at [ static_cast < size_t > ( before ) ] = static_cast < int32_t > ( x );
							fold_how [ static_cast < size_t > ( before ) ] = up ? 3 : 4;
							fold_inc [ static_cast < size_t > ( before ) ] = at;
							fold_unit [ static_cast < size_t > ( before ) ] = unit;
							hidden [ static_cast < size_t > ( at ) ] = 1;

							continue;

						}

					}

					if ( last_line && !latch && block.end == term::BRANCH && block.condition >= 0 && count_var ( block.condition, x, 0 ) == 1 ) {

						fold_cond.push ( block.condition );
						fold_cond_var.push ( static_cast < int32_t > ( x ) );
						fold_cond_how.push ( up ? 1 : 2 );
						fold_cond_inc.push ( at );
						fold_cond_unit.push ( unit );
						fold_cond_done.push ( 0 );
						hidden [ static_cast < size_t > ( at ) ] = 1;

					}

				}

			}


		}

		bool unfold_missed ( ) {

			bool missed = false;

			for ( int32_t s = 0; s < fold_at.count ( ); s++ ) {

				if ( fold_at [ static_cast < size_t > ( s ) ] < 0 || fold_done [ static_cast < size_t > ( s ) ] )
					continue;

				hidden [ static_cast < size_t > ( fold_inc [ static_cast < size_t > ( s ) ] ) ] = 0;
				fold_at [ static_cast < size_t > ( s ) ] = -1;
				missed = true;

			}

			for ( int32_t k = 0; k < fold_cond.count ( ); k++ ) {

				if ( fold_cond [ static_cast < size_t > ( k ) ] < 0 || fold_cond_done [ static_cast < size_t > ( k ) ] )
					continue;

				hidden [ static_cast < size_t > ( fold_cond_inc [ static_cast < size_t > ( k ) ] ) ] = 0;
				fold_cond [ static_cast < size_t > ( k ) ] = -1;
				missed = true;

			}

			return missed;

		}

		bool array_element ( int32_t v, uint8_t size, uint8_t bits, bool address ) {

			if ( v < 0 || v >= array_of.count ( ) || array_of [ static_cast < size_t > ( v ) ] < 0 )
				return false;

			int32_t head = array_of [ static_cast < size_t > ( v ) ];
			int64_t k = b.vars [ static_cast < size_t > ( v ) ].offset - b.vars [ static_cast < size_t > ( head ) ].offset;
			int32_t step = array_step [ static_cast < size_t > ( head ) ];
			wchar_t name [ 64 ];

			var_name ( head, name );

			if ( address ) {

				if ( k == 0 ) {

					out.put ( name, syntax::LOCAL );

					return true;

				}

				out.put ( L"&", syntax::OPERATOR );
				out.put ( name, syntax::LOCAL );
				out.put ( L"[" );
				index_number ( k % step == 0 ? k / step : k );
				out.put ( L"]" );

				return true;

			}

			if ( step > 1 && k % step == 0 && size == step ) {

				out.put ( name, syntax::LOCAL );
				out.put ( L"[" );
				index_number ( k / step );
				out.put ( L"]" );

				return true;

			}

			if ( step == 1 && size == 1 ) {

				out.put ( name, syntax::LOCAL );
				out.put ( L"[" );
				index_number ( k );
				out.put ( L"]" );

				return true;

			}

			out.put ( L"*(", syntax::OPERATOR );
			type_name ( size, bits, false, true );

			if ( step == 1 ) {

				out.put ( k ? L" *)&" : L" *)" );
				out.put ( name, syntax::LOCAL );

				if ( k ) {

					out.put ( L"[" );
					index_number ( k );
					out.put ( L"]" );

				}

			} else {

				out.put ( L" *)((char *)" );
				out.put ( name, syntax::LOCAL );
				out.put ( L" + ", syntax::OPERATOR );
				index_number ( k );
				out.put ( L")" );

			}

			return true;

		}

		int32_t bit_operand ( int32_t e ) const {

			while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( e ) ].a >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( e ) ].a ) ].kind == op::AND )
				e = b.exprs [ static_cast < size_t > ( e ) ].a;

			if ( e < 0 )
				return -1;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind != op::AND || node.b < 0 || ( node.bits & FLOATING ) || logical_pair ( node ) )
				return -1;

			const expr& mask = b.exprs [ static_cast < size_t > ( node.b ) ];

			return mask.kind == op::CONST && mask.value != 0 && !( mask.bits & POINTER ) ? e : -1;

		}

		bool logical_pair ( const expr& item ) const {

			auto logical = [ & ] ( int32_t e ) {

				if ( e < 0 )
					return false;

				op kind = b.exprs [ static_cast < size_t > ( e ) ].kind;

				return ( kind >= op::EQ && kind <= op::LOR ) || kind == op::LNOT;

			};

			return item.size == 1 && !( item.bits & FLOATING ) && logical ( item.a ) && logical ( item.b );

		}

		void select_arm ( int32_t left, int32_t arm ) {

			const expr& target = b.exprs [ static_cast < size_t > ( left ) ];
			const expr& picked = b.exprs [ static_cast < size_t > ( arm ) ];

			if ( picked.kind == op::CONST && !( picked.bits & POINTER ) && target.size == 8 && b.long_mode )
				number ( picked.value, 8, !simple_constant ( picked.value ), false );
			else
				value ( arm, 2 );

		}

		bool slot_part ( int32_t e ) const {

			if ( e < 0 )
				return false;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind != op::ADD || node.a < 0 || node.b < 0 )
				return false;

			const expr& base = b.exprs [ static_cast < size_t > ( node.a ) ];
			const expr& step = b.exprs [ static_cast < size_t > ( node.b ) ];

			return base.kind == op::ADDRESS && base.a < 0 && base.value >= 0 && base.value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( base.value ) ].kind == var_kind::STACK && step.kind == op::CONST && step.value > 0 && step.value < 16 && !( step.bits & POINTER );

		}

		void index_number ( int64_t amount ) {

			if ( amount < 0 ) {

				out.put ( L"-", syntax::NUMBER );
				amount = -amount;

			}

			out.put ( base::writer ( ).number ( amount ).text ( ), syntax::NUMBER );

		}

		int32_t pointer_step ( int32_t e ) const {

			if ( e < 0 )
				return 0;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind != op::VAR || node.value < 0 || node.value >= element.count ( ) )
				return 0;

			uint8_t shape = element [ static_cast < size_t > ( node.value ) ];

			if ( !shape )
				return 0;

			if ( shape == 0x10 )
				return 1;

			if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) )
				return 8;

			return shape & 0x0F;

		}

		bool void_element_load ( int32_t e ) const {

			if ( e < 0 || b.exprs [ static_cast < size_t > ( e ) ].kind != op::LOAD || b.exprs [ static_cast < size_t > ( e ) ].size != 8 )
				return false;

			int32_t at = b.exprs [ static_cast < size_t > ( e ) ].a;

			if ( at >= 0 && b.exprs [ static_cast < size_t > ( at ) ].kind == op::ADD && b.exprs [ static_cast < size_t > ( at ) ].b >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( at ) ].b ) ].kind == op::CONST ) {

				if ( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( at ) ].b ) ].value % 8 != 0 )
					return false;

				at = b.exprs [ static_cast < size_t > ( at ) ].a;

			}

			if ( at < 0 || b.exprs [ static_cast < size_t > ( at ) ].kind != op::VAR )
				return false;

			int64_t v = b.exprs [ static_cast < size_t > ( at ) ].value;

			return v >= 0 && v < element.count ( ) && ( element [ static_cast < size_t > ( v ) ] & 0x9F ) == 8 && ( element [ static_cast < size_t > ( v ) ] & 0x40 );

		}

		bool typed_value ( int32_t e ) const {

			if ( e < 0 )
				return false;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( pointer_step ( e ) > 0 )
				return true;

			if ( node.kind == op::ADD && node.b >= 0 && pointer_step ( node.a ) > 1 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::MUL ) {

				const expr& scaled = b.exprs [ static_cast < size_t > ( node.b ) ];

				if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == pointer_step ( node.a ) )
					return true;

			}

			return node.kind == op::ADD && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && pointer_step ( node.a ) > 0 && b.exprs [ static_cast < size_t > ( node.b ) ].value % pointer_step ( node.a ) == 0;

		}

		void pointer_cast ( int32_t v, bool twice = false ) {

			uint8_t shape = element [ static_cast < size_t > ( v ) ];

			out.put ( L"(" );

			if ( shape == 0x10 )
				out.put ( L"char", syntax::TYPE );
			else if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) ) {

				out.put ( L"void", syntax::TYPE );
				out.put ( twice ? L" ***)" : L" **)" );

				return;

			} else if ( ( shape & 0x9F ) == 8 && ( shape & 0x20 ) )
				type_name ( 8, SIGNED, false );
			else if ( ( shape & 0x40 ) && !( shape & 0x80 ) )
				type_name ( shape & 0x1F, UNSIGNED, false );
			else
				type_name ( shape & 0x1F, ( shape & 0x80 ) ? FLOATING : 0, false, !( shape & 0x80 ) );

			out.put ( twice ? L" **)" : L" *)" );

		}

		int32_t char_var ( int32_t e ) const {

			if ( e < 0 )
				return -1;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind != op::VAR || node.value < 0 || node.value >= element.count ( ) || element [ static_cast < size_t > ( node.value ) ] != 0x10 )
				return -1;

			return static_cast < int32_t > ( node.value );

		}

		bool char_terms ( int32_t e, int32_t& var, int32_t& index, int64_t& offset ) const {

			int32_t terms [ 8 ];
			int32_t count = 0;
			int32_t stack [ 8 ];
			int32_t depth = 0;
			int64_t sum = 0;

			stack [ depth++ ] = e;

			while ( depth ) {

				int32_t at = stack [ --depth ];
				const expr& node = b.exprs [ static_cast < size_t > ( at ) ];

				if ( node.kind == op::ADD && node.size == 8 && node.a >= 0 && node.b >= 0 && depth + 2 <= 8 ) {

					stack [ depth++ ] = node.b;
					stack [ depth++ ] = node.a;

				} else if ( node.kind == op::CONST && !( node.bits & POINTER ) )
					sum += node.value;
				else if ( count < 8 )
					terms [ count++ ] = at;
				else
					return false;

			}

			if ( count != 2 )
				return false;

			if ( char_var ( terms [ 0 ] ) >= 0 && char_var ( terms [ 1 ] ) < 0 && pointer_step ( terms [ 1 ] ) == 0 ) {

				var = char_var ( terms [ 0 ] );
				index = terms [ 1 ];

			} else if ( char_var ( terms [ 1 ] ) >= 0 && char_var ( terms [ 0 ] ) < 0 && pointer_step ( terms [ 0 ] ) == 0 ) {

				var = char_var ( terms [ 1 ] );
				index = terms [ 0 ];

			} else
				return false;

			if ( b.exprs [ static_cast < size_t > ( index ) ].bits & POINTER )
				return false;

			offset = sum;

			return true;

		}

		bool char_load ( const expr& item, int32_t parent ) {

			const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
			wchar_t name [ 64 ];
			int32_t var = char_var ( item.a );
			int32_t index = -1;
			int64_t offset = 0;

			if ( var < 0 && where.kind == op::ADD ) {

				const expr& right = b.exprs [ static_cast < size_t > ( where.b ) ];

				if ( right.kind == op::CONST && !( right.bits & POINTER ) ) {

					offset = right.value;
					var = char_var ( where.a );

					if ( var < 0 && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::ADD ) {

						const expr& inner = b.exprs [ static_cast < size_t > ( where.a ) ];

						var = char_var ( inner.a );
						index = inner.b;

					}

				} else {

					var = char_var ( where.a );
					index = where.b;

				}

			}

			if ( var < 0 && where.kind == op::ADD && char_split && !char_terms ( item.a, var, index, offset ) )
				var = -1;

			if ( var < 0 )
				return false;

			var_name ( var, name );

			auto pointer_type = [ & ] ( ) {

				out.put ( L"(" );
				type_name ( item.size, item.bits, false, true );
				out.put ( L" *)" );

			};

			if ( index >= 0 ) {

				if ( item.size != 1 ) {

					if ( parent > 14 )
						out.put ( L"(" );

					out.put ( L"*", syntax::OPERATOR );
					pointer_type ( );
					out.put ( L"&", syntax::OPERATOR );

				}

				out.put ( name, syntax::LOCAL );
				out.put ( L"[" );
				value ( index, 0 );

				if ( offset ) {

					out.put ( offset < 0 ? L" - " : L" + ", syntax::OPERATOR );
					index_number ( offset < 0 ? -offset : offset );

				}

				out.put ( L"]" );

				if ( item.size != 1 && parent > 14 )
					out.put ( L")" );

				return true;

			}

			if ( item.size == 1 ) {

				if ( offset == 0 ) {

					if ( parent > 14 )
						out.put ( L"(" );

					out.put ( L"*", syntax::OPERATOR );
					out.put ( name, syntax::LOCAL );

					if ( parent > 14 )
						out.put ( L")" );

				} else {

					out.put ( name, syntax::LOCAL );
					out.put ( L"[" );
					index_number ( offset );
					out.put ( L"]" );

				}

				return true;

			}

			if ( offset != 0 && offset % item.size == 0 ) {

				if ( parent > 14 )
					out.put ( L"(" );

				out.put ( L"*(", syntax::OPERATOR );
				pointer_type ( );
				out.put ( name, syntax::LOCAL );
				out.put ( L" + ", syntax::OPERATOR );
				index_number ( offset / item.size );
				out.put ( L")" );

				if ( parent > 14 )
					out.put ( L")" );

				return true;

			}

			return false;

		}

		bool global_index ( int32_t address, int32_t size, int32_t& index, uint64_t& start ) const {

			if ( !global_arrays || address < 0 )
				return false;

			const expr& sum = b.exprs [ static_cast < size_t > ( address ) ];

			if ( sum.kind != op::ADD || sum.a < 0 || sum.b < 0 || sum.size != 8 )
				return false;

			for ( int32_t side = 0; side < 2; side++ ) {

				const expr& base_node = b.exprs [ static_cast < size_t > ( side ? sum.b : sum.a ) ];
				int32_t other = side ? sum.a : sum.b;
				const expr& scaled = b.exprs [ static_cast < size_t > ( other ) ];

				if ( base_node.kind != op::CONST || !( base_node.bits & POINTER ) || b.file->section_at ( static_cast < uint64_t > ( base_node.value ) ) <= 0 )
					continue;

				if ( scaled.kind == op::MUL && scaled.a >= 0 && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == size && size > 1 ) {

					index = scaled.a;
					start = static_cast < uint64_t > ( base_node.value );

					return true;

				}

				if ( scaled.kind == op::SHL && scaled.a >= 0 && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && size > 1 && ( 1ll << b.exprs [ static_cast < size_t > ( scaled.b ) ].value ) == size ) {

					index = scaled.a;
					start = static_cast < uint64_t > ( base_node.value );

					return true;

				}

				if ( size == 1 && !( scaled.bits & POINTER ) && scaled.kind != op::CONST ) {

					index = other;
					start = static_cast < uint64_t > ( base_node.value );

					return true;

				}

			}

			return false;

		}

		void global_element ( uint64_t start, int32_t index ) {

			wchar_t label [ 200 ];

			if ( b.names->name ( start, false, label, 200 ) )
				out.put ( label, syntax::GLOBAL, start );
			else
				number ( static_cast < int64_t > ( start ), 8, true, false );

			out.put ( L"[" );

			int32_t shown = index;

			while ( shown >= 0 && b.exprs [ static_cast < size_t > ( shown ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( shown ) ].a >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( shown ) ].a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( shown ) ].size > b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( shown ) ].a ) ].size )
				shown = b.exprs [ static_cast < size_t > ( shown ) ].a;

			value ( shown, 0 );
			out.put ( L"]" );

		}

		void lvalue_or_load ( const expr& item, int32_t parent ) {

			const expr& where = b.exprs [ static_cast < size_t > ( item.a ) ];
			wchar_t name [ 64 ];

			{

				int32_t at_index = -1;
				uint64_t start = 0;

				if ( global_index ( item.a, item.size, at_index, start ) ) {

					global_element ( start, at_index );

					return;

				}

			}

			if ( where.kind == op::ADDRESS && where.value >= 0 ) {

				if ( array_element ( static_cast < int32_t > ( where.value ), item.size, item.bits, false ) )
					return;

				var_name ( static_cast < int32_t > ( where.value ), name );
				out.put ( name, syntax::LOCAL );

				return;

			}

			if ( where.kind == op::ADD && where.a >= 0 && where.b >= 0 && pointer_step ( where.a ) == item.size && item.size > 1 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::MUL && !( item.bits & FLOATING ) == !( element [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value ) ] & 0x80 ) ) {

				const expr& scaled = b.exprs [ static_cast < size_t > ( where.b ) ];

				if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == item.size ) {

					value ( where.a, 15 );
					out.put ( L"[" );
					value ( scaled.a, 0 );
					out.put ( L"]" );

					return;

				}

			}

			if ( slot_part ( item.a ) ) {

				const expr& base = b.exprs [ static_cast < size_t > ( where.a ) ];
				int64_t k = b.exprs [ static_cast < size_t > ( where.b ) ].value;

				var_name ( static_cast < int32_t > ( base.value ), name );

				if ( parent > 14 )
					out.put ( L"(" );

				if ( item.size && k % item.size == 0 ) {

					out.put ( L"*((", syntax::OPERATOR );
					type_name ( item.size, item.bits, false, true );
					out.put ( L" *)&" );
					out.put ( name, syntax::LOCAL );
					out.put ( L" + ", syntax::OPERATOR );
					index_number ( k / item.size );
					out.put ( L")" );

				} else {

					out.put ( L"*(", syntax::OPERATOR );
					type_name ( item.size, item.bits, false, true );
					out.put ( L" *)((char *)&" );
					out.put ( name, syntax::LOCAL );
					out.put ( L" + ", syntax::OPERATOR );
					index_number ( k );
					out.put ( L")" );

				}

				if ( parent > 14 )
					out.put ( L")" );

				return;

			}

			if ( !( item.bits & FLOATING ) && char_load ( item, parent ) )
				return;

			{

				int32_t base = item.a;
				int64_t field = 0;

				if ( where.kind == op::ADD && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

					base = where.a;
					field = b.exprs [ static_cast < size_t > ( where.b ) ].value;

				}

				const expr& root = b.exprs [ static_cast < size_t > ( base ) ];

				if ( root.kind == op::INTRINSIC && root.arg_count == 0 && base::same_text ( intrinsic_name ( static_cast < int32_t > ( root.value ) ), L"NtCurrentTeb" ) ) {

					static const wchar_t* const fields [ 14 ] = { L"NtTib.ExceptionList", L"NtTib.StackBase", L"NtTib.StackLimit", L"NtTib.SubSystemTib", L"NtTib.FiberData", L"NtTib.ArbitraryUserPointer", L"NtTib.Self", L"EnvironmentPointer", L"ClientId.UniqueProcess", L"ClientId.UniqueThread", L"ActiveRpcHandle", L"ThreadLocalStoragePointer", L"ProcessEnvironmentBlock", L"LastErrorValue" };
					int64_t slot = field / 8;

					if ( field >= 0 && field % 8 == 0 && slot < 14 && ( item.size == 8 || slot == 13 ) ) {

						out.put ( L"NtCurrentTeb", syntax::FUNCTION );
						out.put ( L"()->" );
						out.put ( fields [ slot ], syntax::GLOBAL );

						return;

					}

				}

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

			if ( var >= 0 && var < element.count ( ) && element [ static_cast < size_t > ( var ) ] && ( element [ static_cast < size_t > ( var ) ] & 0x1F ) == item.size ) {

				var_name ( var, name );

				if ( offset == 0 ) {

					if ( parent > 14 )
						out.put ( L"(" );

					out.put ( L"*", syntax::OPERATOR );

					if ( !put_folded ( var, name ) )
						out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

					if ( parent > 14 )
						out.put ( L")" );

				} else {

					if ( !put_folded ( var, name ) )
						out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

					out.put ( L"[" );
					index_number ( offset / item.size );
					out.put ( L"]" );

				}

				return;

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

			{

				int32_t at_index = -1;
				uint64_t start = 0;

				if ( global_index ( item.a, item.size, at_index, start ) ) {

					global_element ( start, at_index );

					return;

				}

			}

			int32_t var = -1;

			if ( where.kind == op::VAR )
				var = static_cast < int32_t > ( where.value );
			else if ( where.kind == op::ADD && b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST )
				var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( where.a ) ].value );

			bool fielded = var >= 0 && ( has_struct ( var ) || ( var < b.var_structs.count ( ) && !b.var_structs [ static_cast < size_t > ( var ) ].empty ( ) ) || ( var < element.count ( ) && ( element [ static_cast < size_t > ( var ) ] & 0x1F ) == item.size ) );

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

			bool saved = in_logic;

			if ( in_logic )
				truth = true;

			const expr& node = b.exprs [ static_cast < size_t > ( index ) ];

			in_logic = bit_tests && ( node.kind == op::LAND || node.kind == op::LOR || ( ( node.kind == op::AND || node.kind == op::OR ) && logical_pair ( node ) ) );

			uint8_t saved_size = holder_size;
			op saved_kind = holder_kind;

			value_body ( index, parent, hex_hint );
			holder_size = saved_size;
			holder_kind = saved_kind;
			in_logic = saved;

		}

		void value_body ( int32_t index, int32_t parent, bool hex_hint ) {

			for ( int32_t h = 0; h < hoisted.count ( ); h++ ) {

				if ( hoisted [ static_cast < size_t > ( h ) ] == index ) {

					out.put ( base::writer ( ).add ( L"t" ).number ( hoisted_temp [ static_cast < size_t > ( h ) ] ).text ( ), syntax::LOCAL );

					return;

				}

			}

			const expr item = b.exprs [ static_cast < size_t > ( index ) ];
			bool truthy = truth;
			uint8_t outer_size = holder_size;
			op outer_kind = holder_kind;

			holder_size = item.size;
			holder_kind = item.kind;
			truth = false;

			if ( bit_tests ) {

				int32_t tested_bits = -1;
				bool equal = false;

				if ( truthy && bit_operand ( index ) >= 0 )
					tested_bits = bit_operand ( index );
				else if ( item.kind == op::LNOT && bit_operand ( item.a ) >= 0 ) {

					tested_bits = bit_operand ( item.a );
					equal = true;

				} else if ( ( item.kind == op::EQ || item.kind == op::NE ) && item.b >= 0 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( item.b ) ].value == 0 && !( b.exprs [ static_cast < size_t > ( item.b ) ].bits & POINTER ) && bit_operand ( item.a ) >= 0 ) {

					tested_bits = bit_operand ( item.a );
					equal = item.kind == op::EQ;

				}

				if ( tested_bits >= 0 ) {

					bool wrap = parent > precedence ( op::NE );

					if ( wrap )
						out.put ( L"(" );

					const expr& masked = b.exprs [ static_cast < size_t > ( tested_bits ) ];
					int32_t subject = masked.a;
					int64_t mask = b.exprs [ static_cast < size_t > ( masked.b ) ].value;

					while ( subject >= 0 && b.exprs [ static_cast < size_t > ( subject ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( subject ) ].a >= 0 && b.exprs [ static_cast < size_t > ( subject ) ].size < b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( subject ) ].a ) ].size && b.exprs [ static_cast < size_t > ( subject ) ].size < 8 && mask > 0 && static_cast < uint64_t > ( mask ) < ( 1ull << ( b.exprs [ static_cast < size_t > ( subject ) ].size * 8 - 1 ) ) && !( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( subject ) ].a ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( subject ) ].a ) ].kind == op::VAR )
						subject = b.exprs [ static_cast < size_t > ( subject ) ].a;

					if ( subject != masked.a ) {

						out.put ( L"(" );
						value ( subject, precedence ( op::AND ) );
						out.put ( L" & ", syntax::OPERATOR );
						value ( masked.b, precedence ( op::AND ) + 1 );
						out.put ( L")" );

					} else
						value ( tested_bits, precedence ( op::NE ) + 1 );

					out.put ( equal ? L" == " : L" != ", syntax::OPERATOR );
					out.put ( L"0", syntax::NUMBER );

					if ( wrap )
						out.put ( L")" );

					return;

				}

			}

			if ( ( item.kind == op::EQ || item.kind == op::NE ) && item.b >= 0 && !( item.bits & FLOATING ) ) {

				const expr& zero = b.exprs [ static_cast < size_t > ( item.b ) ];
				const expr& tested = b.exprs [ static_cast < size_t > ( item.a ) ];

				int32_t bare = item.a;

				while ( bare >= 0 && b.exprs [ static_cast < size_t > ( bare ) ].kind == op::CAST )
					bare = b.exprs [ static_cast < size_t > ( bare ) ].a;

				bool assigning = bare >= 0 && b.exprs [ static_cast < size_t > ( bare ) ].kind == op::SET;

				if ( zero.kind == op::CONST && zero.value == 0 && !( zero.bits & POINTER ) && !( tested.bits & FLOATING ) && !explicit_zero && !assigning ) {

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

			if ( global_arrays && item.kind == op::ADD && item.a >= 0 && item.b >= 0 ) {

				int32_t scaled = b.exprs [ static_cast < size_t > ( item.a ) ].kind == op::MUL ? item.a : ( b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::MUL ? item.b : -1 );
				int64_t step = scaled >= 0 && b.exprs [ static_cast < size_t > ( scaled ) ].b >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( scaled ) ].b ) ].kind == op::CONST ? b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( scaled ) ].b ) ].value : 0;
				int32_t at_index = -1;
				uint64_t start = 0;

				if ( ( step == 2 || step == 4 || step == 8 ) && global_index ( index, static_cast < int32_t > ( step ), at_index, start ) ) {

					if ( parent > 14 )
						out.put ( L"(" );

					out.put ( L"&", syntax::OPERATOR );
					global_element ( start, at_index );

					if ( parent > 14 )
						out.put ( L")" );

					return;

				}

			}

			if ( void_arithmetic && item.kind == op::ADD && item.a >= 0 && item.b >= 0 && item.size == 8 ) {

				int32_t terms [ 8 ];
				int32_t count_terms = 0;
				int32_t stack [ 8 ];
				int32_t depth = 0;
				int64_t sum = 0;
				bool ok = true;

				stack [ depth++ ] = index;

				while ( depth && ok ) {

					int32_t at = stack [ --depth ];
					const expr& node = b.exprs [ static_cast < size_t > ( at ) ];

					if ( node.kind == op::ADD && node.size == 8 && node.a >= 0 && node.b >= 0 && depth + 2 <= 8 ) {

						stack [ depth++ ] = node.b;
						stack [ depth++ ] = node.a;

					} else if ( node.kind == op::CONST && !( node.bits & POINTER ) )
						sum += node.value;
					else if ( count_terms < 8 )
						terms [ count_terms++ ] = at;
					else
						ok = false;

				}

				if ( ok && count_terms == 2 && void_element_load ( terms [ 0 ] ) != void_element_load ( terms [ 1 ] ) && !( b.exprs [ static_cast < size_t > ( void_element_load ( terms [ 0 ] ) ? terms [ 1 ] : terms [ 0 ] ) ].bits & POINTER ) && pointer_step ( void_element_load ( terms [ 0 ] ) ? terms [ 1 ] : terms [ 0 ] ) == 0 ) {

					int32_t element_load = void_element_load ( terms [ 0 ] ) ? terms [ 0 ] : terms [ 1 ];
					int32_t offset_term = element_load == terms [ 0 ] ? terms [ 1 ] : terms [ 0 ];

					if ( parent > 12 )
						out.put ( L"(" );

					out.put ( L"(" );
					out.put ( L"char", syntax::TYPE );
					out.put ( L" *)" );
					value ( element_load, 14 );
					out.put ( L" + ", syntax::OPERATOR );
					value ( offset_term, 13 );

					if ( sum ) {

						out.put ( sum < 0 ? L" - " : L" + ", syntax::OPERATOR );
						index_number ( sum < 0 ? -sum : sum );

					}

					if ( parent > 12 )
						out.put ( L")" );

					return;

				}

			}

			if ( char_address && item.kind == op::ADD && item.a >= 0 && item.b >= 0 ) {

				int32_t base_var = -1;
				int32_t at_index = -1;
				int64_t at_offset = 0;

				if ( char_terms ( index, base_var, at_index, at_offset ) ) {

					wchar_t base_name [ 64 ];

					var_name ( base_var, base_name );

					if ( parent > 14 )
						out.put ( L"(" );

					out.put ( L"&", syntax::OPERATOR );
					out.put ( base_name, b.vars [ static_cast < size_t > ( base_var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );
					out.put ( L"[" );
					value ( at_index, at_offset ? 12 : 0 );

					if ( at_offset ) {

						out.put ( at_offset < 0 ? L" - " : L" + ", syntax::OPERATOR );
						index_number ( at_offset < 0 ? -at_offset : at_offset );

					}

					out.put ( L"]" );

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

					if ( array_element ( var, b.vars [ static_cast < size_t > ( var ) ].size, b.vars [ static_cast < size_t > ( var ) ].bits, false ) )
						break;

					if ( active_var >= 0 && var == active_var && var_step ( var ) == active_unit ) {

						var_name ( var, name );

						if ( active_how <= 2 )
							out.put ( active_how == 1 ? L"++" : L"--", syntax::OPERATOR );

						out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

						if ( active_how >= 3 )
							out.put ( active_how == 3 ? L"++" : L"--", syntax::OPERATOR );

						if ( active_cond && active_slot >= 0 && active_slot < fold_cond_done.count ( ) )
							fold_cond_done [ static_cast < size_t > ( active_slot ) ] = 1;
						else if ( !active_cond && active_slot >= 0 && active_slot < fold_done.count ( ) )
							fold_done [ static_cast < size_t > ( active_slot ) ] = 1;

						active_var = -1;

						break;

					}

					var_name ( var, name );
					out.put ( name, b.vars [ static_cast < size_t > ( var ) ].kind == var_kind::PARAMETER ? syntax::PARAMETER : syntax::LOCAL );

					break;

				}

				case op::GLOBAL:

					if ( b.file->section_at ( static_cast < uint64_t > ( item.value ) ) <= 0 && static_cast < uint64_t > ( item.value ) >= b.file->base ) {

						out.put ( L"MEMORY[" );
						out.put ( base::writer ( ).add ( L"0x" ).hex ( static_cast < uint64_t > ( item.value ) ).text ( ), syntax::NUMBER );
						out.put ( L"]" );

					} else if ( b.names->name ( static_cast < uint64_t > ( item.value ), false, name, 200 ) )
						out.put ( name, syntax::GLOBAL, static_cast < uint64_t > ( item.value ) );
					else
						number ( item.value, 8, true, false );

					break;

				case op::TEXT:
					pointer_constant ( static_cast < uint64_t > ( item.value ) );
					break;

				case op::ADDRESS:

					if ( item.a < 0 && item.value >= 0 && array_element ( static_cast < int32_t > ( item.value ), 0, 0, true ) )
						break;

					if ( item.a >= 0 && b.exprs [ static_cast < size_t > ( item.a ) ].kind == op::VAR && array_element ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.a ) ].value ), 0, 0, true ) )
						break;

					if ( item.value >= 0 && item.a < 0 ) {

						var_name ( static_cast < int32_t > ( item.value ), name );

						if ( item.value != b.va_var )
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

						short_symbol ( name );

						bool trimmed = imported && name [ 0 ] == L'_' && ( ( name [ 1 ] == L'_' && name [ 2 ] >= L'a' && name [ 2 ] <= L'z' ) || ( trim_crt && name [ 1 ] >= L'a' && name [ 1 ] <= L'z' ) );

						out.put ( trimmed ? name + 1 : name, imported ? syntax::IMPORT : syntax::FUNCTION, target );

					} else if ( target )
						number ( static_cast < int64_t > ( target ), 8, true, false );
					else {

						const expr& callee_node = b.exprs [ static_cast < size_t > ( item.a ) ];

						if ( callee_node.kind == op::LOAD && callee_node.size == 8 && callee_node.a >= 0 && b.long_mode ) {

							out.put ( L"(*(" );
							out.put ( call_unused ? L"void" : L"__int64", syntax::TYPE );
							out.put ( L" (__fastcall **)(" );

							for ( int32_t i = 0; i < item.arg_count; i++ ) {

								const expr& passed = b.exprs [ static_cast < size_t > ( b.args [ static_cast < size_t > ( item.first_arg + i ) ] ) ];

								if ( i )
									out.put ( L", " );

								if ( passed.bits & FLOATING )
									out.put ( passed.size == 4 ? L"float" : L"double", syntax::TYPE );
								else
									out.put ( passed.size == 1 ? L"char" : ( passed.size == 2 ? L"__int16" : ( passed.size == 4 ? L"int" : L"__int64" ) ), syntax::TYPE );

							}

							out.put ( L"))" );

							const expr& slot = b.exprs [ static_cast < size_t > ( callee_node.a ) ];

							if ( slot.kind == op::ADD && slot.b >= 0 && b.exprs [ static_cast < size_t > ( slot.b ) ].kind == op::CONST ) {

								out.put ( L"(" );
								value ( slot.a, 12 );
								out.put ( L" + ", syntax::OPERATOR );
								number ( b.exprs [ static_cast < size_t > ( slot.b ) ].value, 8, false, false );
								out.put ( L")" );

							} else
								value ( callee_node.a, 14 );

							out.put ( L")" );

						} else {

							out.put ( L"(" );
							value ( item.a, 14 );
							out.put ( L")" );

						}

						call_unused = false;

					}

					out.put ( L"(" );
					operand = false;

					for ( int32_t i = 0; i < item.arg_count; i++ ) {

						int32_t argument = b.args [ static_cast < size_t > ( item.first_arg + i ) ];
						const expr& passed = b.exprs [ static_cast < size_t > ( argument ) ];

						if ( i )
							out.put ( L", " );

						if ( !no_casts && passed.kind == op::CONST && b.long_mode && passed.value > 0 && b.db->function_starting ( static_cast < uint64_t > ( passed.value ) ) >= 0 ) {

							callee_unsigned ( target, i );
							out.put ( L"(" );

							if ( target && shape_ok && i < shape->count && shape->types [ i ] [ 0 ] )
								out.put ( shape->types [ i ], syntax::TYPE );
							else
								type_name ( 8, 0, false );

							out.put ( L")" );
							pointer_constant ( static_cast < uint64_t > ( passed.value ) );

							continue;

						}

						if ( !no_casts && target && b.long_mode && passed.kind == op::CAST && passed.size == 8 && passed.a >= 0 && b.exprs [ static_cast < size_t > ( passed.a ) ].size == 16 && i < 4 && callee_parameter_type ( *b.db, *b.file, b.names, target, i, param_type, 64 ) ) {

							out.put ( L"(" );
							out.put ( param_type, syntax::TYPE );
							out.put ( L")" );
							value ( passed.a, 14 );

							continue;

						}

						bool unsigned_slot = passed.kind == op::CONST && passed.value < -9 && passed.size <= 4 && callee_unsigned ( target, i );

						bare_unsigned = unsigned_slot;

						if ( inferred_unsigned && passed.kind == op::CONST && !( passed.bits & POINTER ) && passed.value > 0 && target && b.long_mode && i < 4 && ( callee_unsigned ( target, i ), !shape_ok ) && callee_parameter_type ( *b.db, *b.file, b.names, target, i, param_type, 64 ) && base::same_text ( param_type, L"unsigned __int64" ) ) {

							number ( passed.value, 8, passed.value > 9, true );
							bare_unsigned = false;

							continue;

						}

						if ( passed.kind == op::CONST && ( !( passed.bits & POINTER ) || passed.value < 0x10000 ) && passed.value > 0 && callee_unsigned ( target, i ) )
							number ( passed.value, callee_wide ( target, i ) && b.long_mode ? 8 : 4, passed.value > 9, true );
						else if ( passed.kind == op::CONST && !( passed.bits & POINTER ) && b.long_mode && callee_wide ( target, i ) )
							number ( passed.value, 8, !simple_constant ( passed.value ), false );
						else if ( b.long_mode && !no_casts && target && passed.kind != op::CONST && passed.size == 4 && !( passed.bits & FLOATING ) && ( callee_unsigned ( target, i ), !shape_ok ) && !( i < 4 && callee_argument_width ( *b.db, target, i, 0 ) == 4 ) && !( passed.kind == op::VAR && passed.value >= 0 && passed.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( passed.value ) ].bits & UNSIGNED ) && b.vars [ static_cast < size_t > ( passed.value ) ].size == 4 ) ) {

							out.put ( L"(unsigned int)" );

							if ( call_untruncate && passed.kind == op::CAST && passed.a >= 0 && b.exprs [ static_cast < size_t > ( passed.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( passed.a ) ].size == 8 )
								value ( passed.a, 14 );
							else
								value ( argument, 14 );

						} else if ( b.long_mode && !no_casts && target && passed.size == 8 && !( passed.bits & ( FLOATING | POINTER ) ) && ( passed.kind == op::LOAD || passed.kind == op::GLOBAL || passed.kind == op::ADD ) && !( passed.kind == op::ADD && char_typed_sum ( argument ) ) && ( callee_unsigned ( target, i ), !shape_ok ) && !void_element_load ( argument ) && callee_parameter_type ( *b.db, *b.file, b.names, target, i, param_type, 64 ) ) {

							if ( passed.kind == op::LOAD ) {

								out.put ( L"*(" );
								out.put ( param_type, syntax::TYPE );
								out.put ( param_type [ 0 ] && param_type [ base::text_length ( param_type ) - 1 ] == L'*' ? L"*)" : L" *)" );
								value ( passed.a, 14 );

							} else {

								out.put ( L"(" );
								out.put ( param_type, syntax::TYPE );
								out.put ( L")" );
								value ( argument, 14 );

							}

						} else if ( b.long_mode && !no_casts && target && passed.kind == op::VAR && passed.value >= 0 && passed.value < b.vars.count ( ) && passed.size == 8 && !( passed.bits & FLOATING ) && b.vars [ static_cast < size_t > ( passed.value ) ].kind != var_kind::PARAMETER && ( passed.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( passed.value ) ].empty ( ) ) && ( passed.value >= element.count ( ) || !element [ static_cast < size_t > ( passed.value ) ] ) && ( callee_unsigned ( target, i ), shape_ok ) && i < shape->count && shape->params [ i ].pointer && shape->types [ i ] [ 0 ] ) {

							out.put ( L"(" );
							out.put ( shape->types [ i ], syntax::TYPE );
							out.put ( L")" );
							value ( argument, 14 );

						} else if ( b.long_mode && !no_casts && target && passed.kind == op::VAR && passed.value >= 0 && passed.value < element.count ( ) && element [ static_cast < size_t > ( passed.value ) ] && ( callee_unsigned ( target, i ), !shape_ok ) && !( i < 4 && b.db && ( callee_pointer_shape ( *b.db, b.names, target, i, 0 ) & 0x9F ) == ( element [ static_cast < size_t > ( passed.value ) ] & 0x9F ) ) ) {

							out.put ( L"(__int64)" );
							value ( argument, 14 );

						} else {

							bool narrow_arg = call_untruncate && passed.size == 4 && !( passed.bits & FLOATING );
							bool held = untruncate;

							untruncate = untruncate || narrow_arg;
							value ( argument, 2 );
							untruncate = held;

						}

						bare_unsigned = false;

					}

					out.put ( L")" );

					break;

				}

				case op::INTRINSIC: {

					const wchar_t* called = intrinsic_name ( static_cast < int32_t > ( item.value ) );
					bool wide = item.size == 8 && called [ 0 ] == L'_' && called [ 1 ] == L'I' && called [ 2 ] == L'n' && called [ 3 ] == L't';

					out.put ( wide ? base::writer ( ).add ( called ).add ( L"64" ).text ( ) : called, syntax::FUNCTION );
					out.put ( L"(" );
					operand = false;

					int32_t whole = -1;

					if ( item.arg_count == 3 && base::same_text ( called, L"memset" ) ) {

						const expr& where = b.exprs [ static_cast < size_t > ( b.args [ static_cast < size_t > ( item.first_arg ) ] ) ];
						const expr& amount = b.exprs [ static_cast < size_t > ( b.args [ static_cast < size_t > ( item.first_arg + 2 ) ] ) ];
						int64_t v = where.kind == op::ADDRESS ? ( where.a < 0 ? where.value : ( b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( where.a ) ].value : -1 ) ) : -1;

						if ( v >= 0 && v < array_of.count ( ) && array_of [ static_cast < size_t > ( v ) ] == v && amount.kind == op::CONST && amount.value == static_cast < int64_t > ( array_step [ static_cast < size_t > ( v ) ] ) * array_count [ static_cast < size_t > ( v ) ] )
							whole = static_cast < int32_t > ( v );

					}

					for ( int32_t i = 0; i < item.arg_count; i++ ) {

						if ( i )
							out.put ( L", " );

						if ( i == 2 && whole >= 0 ) {

							wchar_t array_name [ 64 ];

							var_name ( whole, array_name );
							out.put ( L"sizeof", syntax::KEYWORD );
							out.put ( L"(" );
							out.put ( array_name, syntax::LOCAL );
							out.put ( L")" );

							continue;

						}

						value ( b.args [ static_cast < size_t > ( item.first_arg + i ) ], 2 );

					}

					out.put ( L")" );

					break;

				}

				case op::CAST: {

					const expr& inner = b.exprs [ static_cast < size_t > ( item.a ) ];
					bool float_change = ( inner.bits & FLOATING ) != ( item.bits & FLOATING );

					if ( untruncate && !float_change && inner.kind == op::VAR && inner.size > item.size && item.size == 4 ) {

						value ( item.a, parent );

						break;

					}

					bool retyped =inner.kind == op::VAR && inner.value >= 0 && inner.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( inner.value ) ].empty ( ) && b.vars [ static_cast < size_t > ( inner.value ) ].size == item.size;
					bool shown = ( float_change || inner.size != item.size ) && !no_casts && !retyped;

					if ( ( item.bits & POINTER ) && ( item.bits & FLOATING ) ) {

						out.put ( inner.size == 8 ? L"COERCE_DOUBLE" : L"COERCE_FLOAT", syntax::FUNCTION );
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

					if ( collapse_casts && !no_casts && !float_change && item.size == 8 && inner.kind == op::CAST && inner.size == 4 && inner.a >= 0 && b.exprs [ static_cast < size_t > ( inner.a ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( inner.a ) ].bits & FLOATING ) && !( inner.bits & FLOATING ) ) {

						if ( parent > 14 )
							out.put ( L"(" );

						out.put ( ( item.bits & SIGNED ) ? L"(int)" : L"(unsigned int)", syntax::TYPE );
						value ( inner.a, 14 );

						if ( parent > 14 )
							out.put ( L")" );

						break;

					}

					if ( shown && widened_halves && !float_change && item.size == 4 && inner.size == 8 && outer_size == 8 && !( item.bits & ( SIGNED | FLOATING ) ) && !( inner.bits & FLOATING ) && ( outer_kind == op::MUL || outer_kind == op::ADD || outer_kind == op::SUB || outer_kind == op::SHL || outer_kind == op::AND || outer_kind == op::OR ) ) {

						if ( parent > 14 )
							out.put ( L"(" );

						out.put ( L"(unsigned int)", syntax::TYPE );
						value ( item.a, 14 );

						if ( parent > 14 )
							out.put ( L")" );

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

				case op::SET:

					out.put ( L"(" );
					value ( item.a, 0 );
					out.put ( L" = ", syntax::OPERATOR );

					{

						const expr& stored = b.exprs [ static_cast < size_t > ( item.b ) ];
						const expr& target_var = b.exprs [ static_cast < size_t > ( item.a ) ];

						if ( stored.kind == op::CONST && !( stored.bits & POINTER ) && target_var.size == 8 && b.long_mode )
							number ( stored.value, 8, !simple_constant ( stored.value ), false );
						else
							value ( item.b, 2 );

					}

					if ( item.value == 1 && item.c >= 0 ) {

						out.put ( L", " );
						value ( item.c, 2 );

					}

					out.put ( L")" );
					break;

				case op::TERNARY: {

					auto arm = [ & ] ( int32_t e, int32_t level ) {

						const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

						bare_unsigned = node.kind == op::CONST && node.size == 4 && node.value < -9;
						value ( e, level );
						bare_unsigned = false;

					};

					const expr& test = b.exprs [ static_cast < size_t > ( item.a ) ];
					bool zero_test = ( test.kind == op::EQ || test.kind == op::NE ) && test.b >= 0 && b.exprs [ static_cast < size_t > ( test.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( test.b ) ].value == 0 && !( test.bits & FLOATING );
					bool swap = zero_test && test.kind == op::EQ;

					if ( zero_test )
						value ( test.a, 4 );
					else {

						explicit_zero = test.kind == op::EQ || test.kind == op::NE;
						value ( item.a, 4 );
						explicit_zero = false;

					}

					out.put ( L" ? ", syntax::OPERATOR );
					arm ( swap ? item.c : item.b, 4 );
					out.put ( L" : ", syntax::OPERATOR );
					arm ( swap ? item.b : item.c, 3 );
					break;

				}

				default: {

					if ( slot_part ( index ) ) {

						wchar_t slot_name [ 64 ];

						var_name ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.a ) ].value ), slot_name );
						out.put ( L"(char *)&" );
						out.put ( slot_name, syntax::LOCAL );
						out.put ( L" + ", syntax::OPERATOR );
						index_number ( b.exprs [ static_cast < size_t > ( item.b ) ].value );

						break;

					}

					if ( item.kind == op::ADD && item.a >= 0 && item.b >= 0 && pointer_step ( item.a ) > 1 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::MUL ) {

						const expr& scaled = b.exprs [ static_cast < size_t > ( item.b ) ];

						if ( scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( scaled.b ) ].value == pointer_step ( item.a ) ) {

							out.put ( L"&", syntax::OPERATOR );
							value ( item.a, 15 );
							out.put ( L"[" );
							value ( scaled.a, 0 );
							out.put ( L"]" );

							break;

						}

					}

					if ( item.kind == op::ADD && item.b >= 0 && b.exprs [ static_cast < size_t > ( item.b ) ].kind == op::CONST && !( b.exprs [ static_cast < size_t > ( item.b ) ].bits & POINTER ) ) {

						int32_t step = pointer_step ( item.a );
						int64_t amount = b.exprs [ static_cast < size_t > ( item.b ) ].value;

						if ( step > 0 && amount % step == 0 ) {

							value ( item.a, mine );
							out.put ( amount < 0 ? L" - " : L" + ", syntax::OPERATOR );
							out.put ( base::writer ( ).number ( static_cast < int64_t > ( amount < 0 ? -amount : amount ) / step ).text ( ), syntax::NUMBER );

							break;

						}

					}

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
							symbol = logical_pair ( item ) ? L" && " : L" & ";
							bitwise = !logical_pair ( item );
							break;

						case op::OR:
							symbol = logical_pair ( item ) ? L" || " : L" | ";
							bitwise = !logical_pair ( item );
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

					if ( mask_forms && item.kind == op::AND && right.kind == op::CONST && !( right.bits & POINTER ) && !( item.bits & FLOATING ) && item.a >= 0 ) {

						const expr& masked = b.exprs [ static_cast < size_t > ( item.a ) ];
						int32_t subject = item.a;

						if ( masked.kind == op::CAST && masked.a >= 0 && masked.size < b.exprs [ static_cast < size_t > ( masked.a ) ].size && masked.size <= 4 && right.value > 0 && right.value < ( 1ll << ( masked.size * 8 - 1 ) ) && b.exprs [ static_cast < size_t > ( masked.a ) ].kind == op::VAR && !( b.exprs [ static_cast < size_t > ( masked.a ) ].bits & FLOATING ) )
							subject = masked.a;

						if ( subject != item.a || ( right.value < 0 && item.size == 8 ) ) {

							value ( subject, mine, true );
							out.put ( symbol, syntax::OPERATOR );

							bool held = operand;

							operand = false;

							if ( right.value < 0 && item.size == 8 )
								number ( right.value, 8, true, true );
							else
								value ( item.b, mine + 1, true );

							operand = held;

							break;

						}

					}

					if ( base_first && item.kind == op::ADD && item.a >= 0 && !( item.bits & FLOATING ) && right.kind != op::CONST && right.kind != op::MUL && right.kind != op::SHL ) {

						const expr& scaled = b.exprs [ static_cast < size_t > ( item.a ) ];

						if ( ( scaled.kind == op::MUL || scaled.kind == op::SHL ) && scaled.b >= 0 && b.exprs [ static_cast < size_t > ( scaled.b ) ].kind == op::CONST && ( right.kind == op::LOAD || right.kind == op::VAR || right.kind == op::GLOBAL || right.kind == op::CAST || ( right.bits & POINTER ) ) ) {

							value ( item.b, mine, bitwise );
							out.put ( symbol, syntax::OPERATOR );
							value ( item.a, mine + 1, bitwise );

							break;

						}

					}

					if ( item.kind == op::ADD && right.kind == op::CONST && right.value < 0 && !( right.bits & POINTER ) ) {

						value ( item.a, mine );
						out.put ( L" - ", syntax::OPERATOR );
						number ( -right.value, right.size, !simple_constant ( -right.value ), false );

						break;

					}

					if ( item.kind == op::ADD && right.kind != op::CONST && item.a >= 0 && !( item.bits & FLOATING ) ) {

						const expr& inner = b.exprs [ static_cast < size_t > ( item.a ) ];

						if ( inner.kind == op::ADD && inner.b >= 0 && inner.size == item.size && !( inner.bits & FLOATING ) ) {

							const expr& offset = b.exprs [ static_cast < size_t > ( inner.b ) ];

							if ( offset.kind == op::CONST && !( offset.bits & POINTER ) ) {

								value ( inner.a, mine, bitwise );
								out.put ( L" + ", syntax::OPERATOR );
								value ( item.b, mine + 1, bitwise );
								out.put ( offset.value < 0 ? L" - " : L" + ", syntax::OPERATOR );
								number ( offset.value < 0 ? -offset.value : offset.value, offset.size, !simple_constant ( offset.value ), false );

								break;

							}

						}

					}

					if ( item.kind == op::MUL && right.kind == op::CONST && !( right.bits & POINTER ) && !( item.bits & FLOATING ) ) {

						bool held = operand;
						bool widened = item.size == 8 && item.a >= 0 && b.exprs [ static_cast < size_t > ( item.a ) ].size < 8;
						const expr& factor = b.exprs [ static_cast < size_t > ( item.a ) ];
						bool extended = sext_products && item.size == 8 && factor.kind == op::CAST && factor.size == 8 && ( factor.bits & SIGNED ) && factor.a >= 0 && b.exprs [ static_cast < size_t > ( factor.a ) ].size == 4 && !( b.exprs [ static_cast < size_t > ( factor.a ) ].bits & FLOATING );

						operand = !( widened || extended ) && held;
						number ( right.value, item.size == 8 ? 8 : right.size, !simple_constant ( right.value ), false );
						operand = held;
						out.put ( symbol, syntax::OPERATOR );

						if ( extended ) {

							const expr& inner = b.exprs [ static_cast < size_t > ( factor.a ) ];

							if ( inner.kind == op::CAST && inner.a >= 0 && b.exprs [ static_cast < size_t > ( inner.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( inner.a ) ].size > 4 ) {

								out.put ( L"(int)", syntax::TYPE );
								value ( inner.a, 14, bitwise );

							} else if ( inner.kind == op::LOAD )
								extended_load ( inner, mine + 1, SIGNED );
							else
								value ( factor.a, mine + 1, bitwise );

							break;

						}

						value ( item.a, mine + 1, bitwise );

						break;

					}

					if ( item.kind == op::ADD && right.kind == op::CONST && !( right.bits & POINTER ) && right.value > 0 && item.size == 8 && !( item.bits & FLOATING ) && item.a >= 0 && b.exprs [ static_cast < size_t > ( item.a ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( item.a ) ].size == 8 && !( b.exprs [ static_cast < size_t > ( item.a ) ].bits & FLOATING ) ) {

						value ( item.a, mine, bitwise );
						out.put ( symbol, syntax::OPERATOR );

						bool held = operand;

						operand = false;
						number ( right.value, 8, !simple_constant ( right.value ), false );
						operand = held;

						break;

					}

					bool compare = item.kind >= op::EQ && item.kind <= op::GE;
					bool unsigned_compare = compare && ( item.bits & UNSIGNED ) && item.kind != op::EQ && item.kind != op::NE;
					bool float_compare = compare && ( item.bits & FLOATING );
					const expr& lhs = b.exprs [ static_cast < size_t > ( item.a ) ];
					bool lhs_unsigned = lhs.kind == op::LOAD || ( lhs.bits & UNSIGNED ) || ( lhs.kind == op::VAR && lhs.value >= 0 && lhs.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( lhs.value ) ].bits & UNSIGNED ) ) || lhs.kind == op::CAST;
					bool cast_lhs = unsigned_compare && !lhs_unsigned && !no_casts && right.kind == op::CONST && lhs.size <= 8;

					bool signed_compare = compare && ( item.bits & SIGNED ) && item.kind != op::EQ && item.kind != op::NE && !( item.bits & FLOATING );

					if ( cast_lhs ) {

						out.put ( lhs.size == 1 ? L"(unsigned __int8)" : ( lhs.size == 2 ? L"(unsigned __int16)" : ( lhs.size == 4 ? L"(unsigned int)" : L"(unsigned __int64)" ) ), syntax::TYPE );
						value ( item.a, 14, bitwise );
						unsigned_compare = false;

					} else if ( unsigned_narrow && unsigned_compare && !no_casts && lhs.kind == op::CAST && lhs.a >= 0 && lhs.size < b.exprs [ static_cast < size_t > ( lhs.a ) ].size && lhs.size <= 4 && !( lhs.bits & FLOATING ) && !( b.exprs [ static_cast < size_t > ( lhs.a ) ].bits & FLOATING ) ) {

						out.put ( lhs.size == 1 ? L"(unsigned __int8)" : ( lhs.size == 2 ? L"(unsigned __int16)" : L"(unsigned int)" ), syntax::TYPE );
						value ( lhs.a, 14, bitwise );

					} else if ( signed_compare && !no_casts && lhs.kind == op::CAST && lhs.a >= 0 && lhs.size < b.exprs [ static_cast < size_t > ( lhs.a ) ].size && lhs.size <= 4 && !( lhs.bits & FLOATING ) && b.exprs [ static_cast < size_t > ( lhs.a ) ].kind == op::VAR ) {

						out.put ( lhs.size == 1 ? L"(char)" : ( lhs.size == 2 ? L"(__int16)" : L"(int)" ), syntax::TYPE );
						value ( lhs.a, 14, bitwise );

					} else
						value ( item.a, item.kind == op::SHL || item.kind == op::SHR ? mine + 1 : mine, bitwise );

					bool hex_bound = false;

					if ( unsigned_compare && lhs.kind == op::VAR && lhs.value >= 0 && lhs.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( lhs.value ) ].bits & UNSIGNED ) ) {

						unsigned_compare = false;
						hex_bound = true;

					}

					out.put ( symbol, syntax::OPERATOR );

					if ( right.kind == op::CONST && !( right.bits & POINTER ) && compare ) {

						bool held = operand;

						if ( lhs.kind == op::SET && lhs.size == 8 && b.long_mode )
							operand = false;

						if ( float_compare && right.value == 0 )
							real ( 0, right.size );
						else
							number ( right.value, lhs.kind == op::SET ? lhs.size : right.size, !simple_constant ( right.value ) || ( ( unsigned_compare || hex_bound ) && right.value > 9 ), unsigned_compare || ( hex_bound && right.value < 0 ) );

						operand = held;

					} else if ( compare && flow_casts && void_element_load ( item.b ) && lhs.kind == op::VAR && pointer_step ( item.a ) == 0 && lhs.value >= 0 && lhs.value < b.vars.count ( ) ) {

						out.put ( ( b.vars [ static_cast < size_t > ( lhs.value ) ].bits & UNSIGNED ) ? L"(unsigned __int64)" : L"(__int64)", syntax::TYPE );
						value ( item.b, 14 );

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

		void semicolon ( ) {

			if ( !inline_mode )
				out.put ( L";" );

		}

		int32_t statement_index ( const statement& item ) const {

			return static_cast < int32_t > ( &item - b.statements.data ( ) );

		}

		void inline_statement ( int32_t index ) {

			bool keep_ternary = no_ternary;

			inline_mode = true;
			no_ternary = false;
			statement_line ( b.statements [ static_cast < size_t > ( index ) ] );
			inline_mode = false;
			no_ternary = keep_ternary;

		}

		void statement_line ( const statement& item ) {

			if ( item.kind == stmt::NOP )
				return;

			int32_t index = statement_index ( item );

			if ( !inline_mode && index >= 0 && index < hidden.count ( ) && hidden [ static_cast < size_t > ( index ) ] )
				return;

			if ( dry )
				return;

			untruncate = store_untruncate && ( item.kind == stmt::STORE || item.kind == stmt::ASSIGN ) && item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( item.left ) ].size == 4 && !( b.exprs [ static_cast < size_t > ( item.left ) ].bits & FLOATING );
			active_var = -1;

			if ( !inline_mode && index >= 0 && index < fold_at.count ( ) && fold_at [ static_cast < size_t > ( index ) ] >= 0 ) {

				active_var = fold_at [ static_cast < size_t > ( index ) ];
				active_how = fold_how [ static_cast < size_t > ( index ) ];
				active_slot = index;
				active_cond = false;
				active_unit = fold_unit [ static_cast < size_t > ( index ) ];

			}

			if ( !inline_mode ) {

				hoisted.clear ( );
				hoisted_temp.clear ( );
				out.begin ( item.address );

				if ( no_ternary && item.kind == stmt::EXPR && hoist ( item.right, item.address ) )
					out.begin ( item.address );

			}

			switch ( item.kind ) {

				case stmt::ASSIGN:
				case stmt::STORE: {

					const expr left = b.exprs [ static_cast < size_t > ( item.left ) ];
					const expr right = b.exprs [ static_cast < size_t > ( item.right ) ];

					if ( right.kind == op::ADD && right.a >= 0 && left.kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST && !( right.bits & FLOATING ) ) {

						int64_t step_value = b.exprs [ static_cast < size_t > ( right.b ) ].value;
						int32_t scale = pointer_step ( item.left );

						if ( scale > 1 && step_value % scale == 0 ) {

							step_value /= scale;

							if ( step_value != 1 && step_value != -1 ) {

								value ( item.left, 0 );
								out.put ( step_value < 0 ? L" -= " : L" += ", syntax::OPERATOR );
								out.put ( base::writer ( ).number ( step_value < 0 ? -step_value : step_value ).text ( ), syntax::NUMBER );
								semicolon ( );

								break;

							}

						}

						if ( step_value == 1 || step_value == -1 ) {

							out.put ( step_value == 1 ? L"++" : L"--", syntax::OPERATOR );
							value ( item.left, 0 );

						} else {

							value ( item.left, 0 );
							out.put ( step_value < 0 ? L" -= " : L" += ", syntax::OPERATOR );
							number ( step_value < 0 ? -step_value : step_value, right.size, !simple_constant ( step_value ), false );

						}

						semicolon ( );

						break;

					}

					if ( widen_assign && item.kind == stmt::ASSIGN && left.kind == op::VAR && left.size == 8 && left.value >= 0 && left.value < b.vars.count ( ) && !( b.vars [ static_cast < size_t > ( left.value ) ].bits & ( FLOATING | POINTER ) ) && pointer_step ( item.left ) == 0 && right.size == 4 && !( right.bits & FLOATING ) && ( right.kind == op::ADD || right.kind == op::SUB || right.kind == op::MUL || right.kind == op::SHL || right.kind == op::XOR || right.kind == op::OR ) && ( left.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( left.value ) ].empty ( ) ) ) {

						value ( item.left, 0 );
						out.put ( L" = ", syntax::OPERATOR );
						out.put ( L"(unsigned int)", syntax::TYPE );
						out.put ( L"(" );
						untruncate = true;
						value ( item.right, 0 );
						untruncate = false;
						out.put ( L")" );
						semicolon ( );

						break;

					}

					if ( left.kind == op::VAR && right.kind == op::LOAD && right.size == 16 && left.value >= 0 && left.value < vector.count ( ) && vector [ static_cast < size_t > ( left.value ) ] ) {

						value ( item.left, 0 );
						out.put ( L" = ", syntax::OPERATOR );
						out.put ( L"_mm_loadu_si128", syntax::FUNCTION );
						out.put ( L"((" );
						out.put ( L"const __m128i", syntax::TYPE );
						out.put ( L" *)" );
						value ( right.a, 14 );
						out.put ( L")" );
						semicolon ( );

						break;

					}

					if ( left.kind == op::LOAD && left.size == 16 && right.kind == op::VAR && right.value >= 0 && right.value < vector.count ( ) && vector [ static_cast < size_t > ( right.value ) ] ) {

						vector_store = true;
						value ( item.left, 0 );
						vector_store = false;
						out.put ( L" = ", syntax::OPERATOR );
						value ( item.right, 2 );
						semicolon ( );

						break;

					}

					if ( left.kind == op::LOAD && right.kind == op::LOAD && left.size == right.size && ( left.bits & FLOATING ) && ( right.bits & FLOATING ) && ( left.size == 4 || left.size == 8 ) ) {

						expr plain_left = left;
						expr plain_right = right;

						plain_left.bits = static_cast < uint8_t > ( plain_left.bits & ~FLOATING );
						plain_right.bits = static_cast < uint8_t > ( plain_right.bits & ~FLOATING );
						lvalue_or_load ( plain_left, 0 );
						out.put ( L" = ", syntax::OPERATOR );
						lvalue_or_load ( plain_right, 2 );
						semicolon ( );

						break;

					}

					bool self_var = left.kind == op::VAR && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.a ) ].size == left.size;
					bool self_load = left.kind == op::LOAD && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::LOAD && b.exprs [ static_cast < size_t > ( right.a ) ].size == left.size && b.same ( right.a, item.left );

					if ( ( self_var || self_load ) && right.b >= 0 && right.kind != op::CAST ) {

						const wchar_t* compound = nullptr;

						switch ( right.kind ) {

							case op::SUB: compound = L" -= "; break;
							case op::MUL: compound = L" *= "; break;
							case op::DIV: compound = L" /= "; break;
							case op::MOD: compound = L" %= "; break;
							case op::AND: compound = L" &= "; break;
							case op::OR: compound = L" |= "; break;
							case op::XOR: compound = L" ^= "; break;
							case op::SHL: compound = L" <<= "; break;
							case op::SHR: compound = L" >>= "; break;
							case op::ADD: compound = ( right.bits & FLOATING ) || b.exprs [ static_cast < size_t > ( right.b ) ].kind != op::CONST || self_load ? L" += " : nullptr; break;
							default: break;

						}

						const expr& amount = b.exprs [ static_cast < size_t > ( right.b ) ];

						if ( compound && self_load && right.kind == op::ADD && amount.kind == op::CONST && !( right.bits & FLOATING ) && ( amount.value == 1 || amount.value == -1 ) ) {

							out.put ( amount.value == 1 ? L"++" : L"--", syntax::OPERATOR );
							lvalue_or_load ( left, 14 );
							semicolon ( );

							break;

						}

						if ( compound && self_load && right.kind == op::ADD && amount.kind == op::CONST && amount.value < 0 && !( right.bits & FLOATING ) ) {

							lvalue_or_load ( left, 0 );
							out.put ( L" -= ", syntax::OPERATOR );
							number ( -amount.value, left.size, !simple_constant ( -amount.value ), false );
							semicolon ( );

							break;

						}

						if ( compound ) {

							if ( left.kind == op::LOAD )
								lvalue_or_load ( left, 0 );
							else
								value ( item.left, 0 );

							out.put ( compound, syntax::OPERATOR );

							int32_t operand_value = right.b;

							if ( implicit_narrow && left.size <= 4 && !( right.bits & FLOATING ) && right.kind != op::SHL && right.kind != op::SHR ) {

								while ( operand_value >= 0 && b.exprs [ static_cast < size_t > ( operand_value ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( operand_value ) ].a >= 0 && b.exprs [ static_cast < size_t > ( operand_value ) ].size == left.size && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( operand_value ) ].a ) ].size > left.size && !( b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( operand_value ) ].a ) ].bits & FLOATING ) )
									operand_value = b.exprs [ static_cast < size_t > ( operand_value ) ].a;

							}

							value ( operand_value, 2 );
							semicolon ( );

							break;

						}

					}

					if ( !inline_mode && left.kind == op::VAR && right.kind == op::TERNARY && right.a >= 0 && right.b >= 0 && right.c >= 0 ) {

						const expr& yes_arm = b.exprs [ static_cast < size_t > ( strip ( right.b ) ) ];
						const expr& no_arm = b.exprs [ static_cast < size_t > ( strip ( right.c ) ) ];
						bool keep_no = no_arm.kind == op::VAR && no_arm.value == left.value;
						bool keep_yes = yes_arm.kind == op::VAR && yes_arm.value == left.value;

						if ( split_selects && !keep_no && !keep_yes && !b.same ( right.b, right.c ) ) {

							value ( item.left, 0 );
							out.put ( L" = ", syntax::OPERATOR );
							select_arm ( item.left, right.c );
							semicolon ( );
							out.begin ( item.address );
							out.put ( L"if", syntax::CONTROL );
							paren_open ( );
							value ( right.a, 0 );
							paren_close ( );
							out.finish ( );
							out.indent++;
							out.begin ( item.address );
							out.indent--;
							value ( item.left, 0 );
							out.put ( L" = ", syntax::OPERATOR );
							select_arm ( item.left, right.b );
							semicolon ( );

							break;

						}

						if ( keep_no != keep_yes ) {

							out.put ( L"if", syntax::CONTROL );
							paren_open ( );
							value ( keep_no ? right.a : b.negate ( right.a ), 0 );
							paren_close ( );
							out.finish ( );
							out.indent++;
							out.begin ( item.address );
							out.indent--;
							value ( item.left, 0 );
							out.put ( L" = ", syntax::OPERATOR );

							int32_t chosen = keep_no ? right.b : right.c;
							const expr& picked = b.exprs [ static_cast < size_t > ( chosen ) ];

							if ( picked.kind == op::CONST && !( picked.bits & POINTER ) && left.size == 8 && b.long_mode )
								number ( picked.value, 8, !simple_constant ( picked.value ), false );
							else
								value ( chosen, 2 );

							semicolon ( );

							break;

						}

					}

					if ( left.kind == op::VAR && right.kind == op::CAST && ( right.bits & POINTER ) && ( right.bits & FLOATING ) && right.a >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].size <= 8 ) {

						uint8_t width = b.exprs [ static_cast < size_t > ( right.a ) ].size;

						out.put ( width == 8 ? L"*(_QWORD *)&" : L"LODWORD(", width == 8 ? syntax::OPERATOR : syntax::FUNCTION );
						value ( item.left, 0 );

						if ( width != 8 )
							out.put ( L")" );

						out.put ( L" = ", syntax::OPERATOR );
						value ( right.a, 2 );
						semicolon ( );

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

					if ( left.kind == op::LOAD && ( left.bits & FLOATING ) && right.kind == op::CONST && left.size == 16 ) {

						expr plain_left = left;

						plain_left.bits = static_cast < uint8_t > ( plain_left.bits & ~FLOATING );
						lvalue_or_load ( plain_left, 0 );

					} else if ( left.kind == op::LOAD )
						lvalue_or_load ( left, 0 );
					else
						value ( item.left, 0 );

					out.put ( L" = ", syntax::OPERATOR );

					if ( right.kind == op::CONST && !( right.bits & POINTER ) && ( left.size == 4 || left.size == 8 ) && float_target ( item.left ) )
						real ( right.value, left.size == 4 ? 4 : 8 );
					else if ( right.kind == op::LOAD && right.size < left.size && !( left.bits & FLOATING ) && !float_target ( item.left ) )
						extended_load ( right, 2, UNSIGNED );
					else if ( right.kind == op::CAST && right.a >= 0 && right.size == left.size && !( right.bits & ( FLOATING | POINTER ) ) && b.exprs [ static_cast < size_t > ( right.a ) ].size > right.size && !( b.exprs [ static_cast < size_t > ( right.a ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR )
						value ( right.a, 2 );
					else if ( right.kind == op::CONST && !( right.bits & POINTER ) && ( left.size == 8 || left.size == 16 ) && b.long_mode && !float_target ( item.left ) && !( left.kind == op::VAR && left.value == result_var && b.result_width ) ) {

						int64_t shown_value = right.size == 4 && right.value < 0 && left.kind == op::VAR ? static_cast < int64_t > ( static_cast < uint32_t > ( right.value ) ) : right.value;

						number ( shown_value, 8, !simple_constant ( shown_value ), false );

					}
					else if ( flow_casts && left.kind == op::VAR && pointer_step ( item.left ) > 0 && !typed_value ( item.right ) && right.kind != op::CAST && ( right.kind != op::CALL || ( char_calls && element [ static_cast < size_t > ( left.value ) ] == 0x10 && right.value ) ) ) {

						if ( right.kind == op::LOAD && right.size == 8 && !void_element_load ( item.right ) && element [ static_cast < size_t > ( left.value ) ] != 0x10 && !( ( element [ static_cast < size_t > ( left.value ) ] & 0x9F ) == 8 && ( element [ static_cast < size_t > ( left.value ) ] & 0x40 ) ) ) {

							out.put ( L"*", syntax::OPERATOR );
							pointer_cast ( static_cast < int32_t > ( left.value ), true );
							value ( right.a, 14 );

						} else {

							pointer_cast ( static_cast < int32_t > ( left.value ) );
							value ( item.right, 14 );

						}

					} else if ( flow_casts && left.kind == op::VAR && left.size == 8 && pointer_step ( item.left ) == 0 && void_element_load ( item.right ) && !( left.bits & FLOATING ) && !( left.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( left.value ) ].empty ( ) ) ) {

						bool unsigned_target = left.value >= 0 && left.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( left.value ) ].bits & UNSIGNED );

						out.put ( unsigned_target ? L"(unsigned __int64)" : L"(__int64)", syntax::TYPE );
						value ( item.right, 14 );

					} else if ( flow_casts && ( left.kind == op::VAR || left.kind == op::GLOBAL ) && left.size == 8 && pointer_step ( item.left ) == 0 && typed_value ( item.right ) && !( left.bits & FLOATING ) ) {

						bool unsigned_target = left.kind == op::VAR && left.value >= 0 && left.value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( left.value ) ].bits & UNSIGNED );

						out.put ( unsigned_target ? L"(unsigned __int64)" : L"(__int64)", syntax::TYPE );
						value ( item.right, 14 );

					} else if ( implicit_narrow && left.kind == op::VAR && left.size <= 4 && !( left.bits & FLOATING ) && ( right.kind == op::AND || right.kind == op::OR || right.kind == op::XOR || right.kind == op::ADD || right.kind == op::SUB ) && !( right.bits & FLOATING ) && right.size == left.size ) {

						auto loose = [ & ] ( int32_t e ) {

							if ( e < 0 )
								return e;

							const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

							if ( node.kind == op::CAST && node.a >= 0 && node.size < b.exprs [ static_cast < size_t > ( node.a ) ].size && !( node.bits & ( FLOATING | POINTER ) ) && !( b.exprs [ static_cast < size_t > ( node.a ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR )
								return node.a;

							return e;

						};

						expr copy = right;

						copy.a = loose ( right.a );
						copy.b = loose ( right.b );

						if ( copy.a != right.a || copy.b != right.b ) {

							b.exprs.push ( copy );
							value ( b.exprs.count ( ) - 1, 2 );

						} else
							value ( item.right, 2 );

					} else
						value ( item.right, 2 );

					semicolon ( );

					break;

				}

				case stmt::EXPR:
					call_unused = item.right >= 0 && b.exprs [ static_cast < size_t > ( item.right ) ].kind == op::CALL;
					value ( item.right, 0 );
					call_unused = false;
					semicolon ( );

					if ( item.right >= 0 && no_return ( b.exprs [ static_cast < size_t > ( item.right ) ] ) ) {

						if ( !inline_mode )
							out.finish ( );

						ended = true;

						return;

					}

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

			if ( inline_mode )
				return;

			out.finish ( );
			hoisted.clear ( );
			hoisted_temp.clear ( );

			const expr& target = b.exprs [ static_cast < size_t > ( item.left >= 0 ? item.left : 0 ) ];

			if ( item.kind == stmt::ASSIGN && item.left >= 0 && target.kind == op::VAR ) {

				last_assign = index;
				last_assign_line = out.r->lines.count ( ) - 1;
				recent_assign.push ( index );
				recent_line.push ( last_assign_line );

			}

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

			if ( inline_return ( block, address ) )
				return;

			wanted_label [ static_cast < size_t > ( block ) ] = 1;
			gotos++;
			ended = true;

			if ( dry || dead_exit ( ) )
				return;

			out.begin ( address );
			out.put ( L"goto", syntax::CONTROL );
			out.put ( L" " );
			out.put ( base::writer ( ).add ( L"LABEL_" ).number ( block + 1 ).text ( ), syntax::LABEL );
			out.put ( L";" );
			out.finish ( );
			out.terminal = out.indent;

		}

		bool inline_return ( int32_t block, uint64_t address ) {

			const block_code& target = b.code [ static_cast < size_t > ( block ) ];

			if ( inline_returns && target.end == term::RETURN && empty_block ( block ) && ( target.returned < 0 || b.exprs [ static_cast < size_t > ( target.returned ) ].kind == op::CONST || b.exprs [ static_cast < size_t > ( target.returned ) ].kind == op::VAR ) ) {

				return_line ( target );

				return true;

			}

			const block_code* ending = &target;

			if ( target.end == term::FALL && target.taken >= 0 && b.code [ static_cast < size_t > ( target.taken ) ].end == term::RETURN && empty_block ( target.taken ) )
				ending = &b.code [ static_cast < size_t > ( target.taken ) ];

			if ( inline_returns && ending->end == term::RETURN && ending->returned >= 0 && b.returns_value && b.exprs [ static_cast < size_t > ( ending->returned ) ].kind == op::VAR ) {

				int32_t only = -1;
				int32_t live = 0;

				for ( int32_t s = target.first; s < target.first + target.count; s++ ) {

					if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP ) {

						only = s;
						live++;

					}

				}

				if ( live == 1 ) {

					const statement& line = b.statements [ static_cast < size_t > ( only ) ];

					if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == b.exprs [ static_cast < size_t > ( ending->returned ) ].value ) {

						const expr& source = b.exprs [ static_cast < size_t > ( line.right ) ];

						if ( source.kind == op::VAR || source.kind == op::CONST ) {

							ended = true;

							if ( dry || dead_exit ( ) )
								return true;

							out.begin ( address );
							out.put ( L"return", syntax::CONTROL );
							out.put ( L" " );

							if ( source.kind == op::CONST && b.long_mode && !b.result_width )
								number ( source.value, 8, !simple_constant ( source.value ), false );
							else
								value ( line.right, 0 );

							out.put ( L";" );
							out.finish ( );
							out.terminal = out.indent;

							return true;

						}

					}

				}

			}

			return false;

		}

		void body ( int32_t block ) {

			ended = false;
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

			untruncate = false;
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
				else {

					active_var = -1;

					if ( keyword [ 0 ] == L'i' || keyword [ 0 ] == L'e' ) {

						for ( int32_t k = 0; k < fold_cond.count ( ); k++ ) {

							if ( fold_cond [ static_cast < size_t > ( k ) ] == condition ) {

								active_var = fold_cond_var [ static_cast < size_t > ( k ) ];
								active_how = fold_cond_how [ static_cast < size_t > ( k ) ];
								active_slot = k;
								active_cond = true;
								active_unit = fold_cond_unit [ static_cast < size_t > ( k ) ];

							}

						}

					}

					truth = bit_tests;
					value ( shown, 0 );
					active_var = -1;

				}

				paren_close ( );

			}

			out.finish ( );
			brace ( L"{" );
			out.indent++;
			hoisted.clear ( );
			hoisted_temp.clear ( );

		}

		void close_brace ( ) {

			ended = false;
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
			untruncate = false;
			truth = bit_tests;
			value ( condition, 0 );
			paren_close ( );
			out.put ( L";" );
			out.finish ( );

		}

		bool dead_exit ( ) const {

			return out.terminal >= 0 && out.terminal == out.indent;

		}

		void return_line ( const block_code& item ) {

			ended = true;

			if ( dry || dead_exit ( ) )
				return;

			int32_t test = -1;
			int32_t yes = -1;
			int32_t no = -1;
			bool shown = item.returned >= 0 && ( b.returns_value || item.end == term::TAIL );

			untruncate = false;

			hoisted.clear ( );
			hoisted_temp.clear ( );
			out.begin ( item.last );

			if ( no_ternary && shown && b.returns_value && split_ternary ( item.returned, test, yes, no ) ) {

				branch_return ( item.returned, item.last );
				out.finish ( );
				out.terminal = out.indent;
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

				if ( back.kind == op::CONST && !( back.bits & POINTER ) && !b.has_prototype && !b.returns_float && b.long_mode ) {

					bool narrow = b.result_width == 1 || b.result_width == 2 || ( !b.result_text.empty ( ) && b.return_size <= 4 );
					int64_t shown_value = back.value;

					if ( b.result_width == 1 || b.result_width == 2 )
						shown_value = static_cast < int8_t > ( back.value );
					else if ( !narrow && back.size == 4 && back.value < 0 )
						shown_value = static_cast < int64_t > ( static_cast < uint32_t > ( back.value ) );

					number ( shown_value, narrow ? 4 : 8, !simple_constant ( shown_value ), false );

				} else {

					const expr& top = b.exprs [ static_cast < size_t > ( item.returned ) ];
					bool keep = explicit_zero;
					int32_t inner = top.kind == op::CAST ? top.a : -1;
					bool widened = return_casts && b.long_mode && !b.has_prototype && !b.returns_float && b.result_width == 0 && b.result_text.empty ( ) && top.kind == op::CAST && top.size == 8 && inner >= 0 && b.exprs [ static_cast < size_t > ( inner ) ].size == 4 && !( top.bits & ( SIGNED | FLOATING | POINTER ) ) && !( b.exprs [ static_cast < size_t > ( inner ) ].bits & FLOATING ) && b.exprs [ static_cast < size_t > ( inner ) ].kind != op::CONST && !( b.exprs [ static_cast < size_t > ( inner ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( inner ) ].value >= 0 && b.exprs [ static_cast < size_t > ( inner ) ].value < b.vars.count ( ) && ( b.vars [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( inner ) ].value ) ].bits & UNSIGNED ) );

					explicit_zero = ( b.result_width == 1 || b.result_width == 2 ) && ( top.kind == op::EQ || top.kind == op::NE );

					if ( !widened && return_casts && b.long_mode && !b.has_prototype && !b.returns_float && b.result_width == 0 && b.result_text.empty ( ) && top.size == 4 && ( top.kind == op::ADD || top.kind == op::SUB || top.kind == op::MUL || top.kind == op::SHL || top.kind == op::AND || top.kind == op::OR || top.kind == op::XOR || ( return_var_casts && top.kind == op::VAR && top.value >= 0 && top.value < b.vars.count ( ) && b.vars [ static_cast < size_t > ( top.value ) ].size == 4 && !( b.vars [ static_cast < size_t > ( top.value ) ].bits & ( UNSIGNED | FLOATING ) ) && !( top.value < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( top.value ) ].empty ( ) ) ) ) && !( top.bits & ( FLOATING | POINTER ) ) ) {

						widened = true;
						inner = item.returned;

					}

					if ( widened ) {

						out.put ( L"(unsigned int)", syntax::TYPE );
						value ( inner, 14 );

					} else
						value ( item.returned, 0 );

					explicit_zero = keep;

				}

			}

			out.put ( L";" );
			out.finish ( );
			out.terminal = out.indent;
			hoisted.clear ( );
			hoisted_temp.clear ( );

		}

		void jump_line ( const wchar_t* keyword, uint64_t address ) {

			ended = true;

			if ( dry || dead_exit ( ) )
				return;

			out.begin ( address );
			out.put ( keyword, syntax::CONTROL );
			out.put ( L";" );
			out.finish ( );
			out.terminal = out.indent;

		}

		struct frame {

			int32_t loop = -1;
			int32_t exit = -1;
			int32_t outer = -1;

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

			if ( target == context.outer && context.outer >= 0 ) {

				if ( stop != target )
					go_to ( target, address );

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
						else if ( fall_returns && item.taken >= 0 && item.taken != stop && pred [ static_cast < size_t > ( item.taken ) ].count ( ) > 1 && b.code [ static_cast < size_t > ( item.taken ) ].end != term::RETURN && inline_return ( item.taken, item.last ) )
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
				follow = exits_inline && stop == context.loop ? context.loop : context.exit;

			if ( follow < 0 )
				follow = stop;

			return follow;

		}

		bool silent_return ( int32_t block ) {

			if ( block < 0 || block >= count )
				return false;

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];

			return item.end == term::RETURN && !b.returns_value && empty_block ( block ) && pred [ static_cast < size_t > ( block ) ].count ( ) == 1 && !emitted [ static_cast < size_t > ( block ) ];

		}

		static op swapped ( op kind ) {

			switch ( kind ) {

				case op::LT: return op::GT;
				case op::GT: return op::LT;
				case op::LE: return op::GE;
				case op::GE: return op::LE;
				default: return kind;

			}

		}

		bool equivalent ( int32_t guard, int32_t test, int32_t var, int32_t seed, int32_t depth ) {

			if ( guard < 0 || test < 0 || depth > 24 )
				return guard == test;

			while ( b.exprs [ static_cast < size_t > ( guard ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( test ) ].kind != op::CAST )
				guard = b.exprs [ static_cast < size_t > ( guard ) ].a;

			const expr& g = b.exprs [ static_cast < size_t > ( guard ) ];
			const expr& t = b.exprs [ static_cast < size_t > ( test ) ];

			if ( var >= 0 && t.kind == op::VAR && t.value == var ) {

				if ( b.same ( guard, seed ) )
					return true;

				const expr& s = b.exprs [ static_cast < size_t > ( seed ) ];

				return g.kind == op::CONST && s.kind == op::CONST && g.value == s.value;

			}

			if ( b.same ( guard, test ) )
				return true;

			bool compare = t.kind == op::LT || t.kind == op::LE || t.kind == op::GT || t.kind == op::GE || t.kind == op::EQ || t.kind == op::NE;

			if ( g.kind == t.kind && g.value == t.value && g.arg_count == 0 && t.arg_count == 0 && g.kind != op::CALL && g.kind != op::INTRINSIC && g.kind != op::CONST && g.kind != op::VAR ) {

				if ( equivalent ( g.a, t.a, var, seed, depth + 1 ) && equivalent ( g.b, t.b, var, seed, depth + 1 ) && ( g.c < 0 && t.c < 0 ) )
					return true;

			}

			if ( compare && g.kind == swapped ( t.kind ) && ( g.bits & ( SIGNED | UNSIGNED ) ) == ( t.bits & ( SIGNED | UNSIGNED ) ) )
				return equivalent ( g.a, t.b, var, seed, depth + 1 ) && equivalent ( g.b, t.a, var, seed, depth + 1 );

			return false;

		}

		int32_t sinkable_step ( int32_t latch, int32_t test ) {

			const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
			int32_t last = -1;

			for ( int32_t s = tail.first; s < tail.first + tail.count; s++ ) {

				if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
					last = s;

			}

			for ( int32_t s = tail.first; s < tail.first + tail.count; s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || s == last )
					continue;

				const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];
				int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value );

				if ( !( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == var && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST ) )
					continue;

				if ( !b.references ( test, var, -1 ) )
					continue;

				bool used = false;

				for ( int32_t k = s + 1; k < tail.first + tail.count && !used; k++ ) {

					const statement& later = b.statements [ static_cast < size_t > ( k ) ];

					if ( later.kind == stmt::NOP || later.kind == stmt::ASM ) {

						used = used || later.kind == stmt::ASM;

						continue;

					}

					used = b.references ( later.right, var, -1 ) || ( later.left >= 0 && b.references ( later.left, var, -1 ) );

				}

				if ( !used )
					return s;

			}

			return -1;

		}

		int32_t single_assign ( int32_t block, int32_t follow ) {

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];

			if ( item.end != term::FALL || item.taken != follow || pred [ static_cast < size_t > ( block ) ].count ( ) != 1 || wanted_label [ static_cast < size_t > ( block ) ] || header [ static_cast < size_t > ( block ) ] || emitted [ static_cast < size_t > ( block ) ] )
				return -1;

			int32_t found = -1;

			for ( int32_t s = item.first; s < item.first + item.count; s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				if ( found >= 0 || line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || ( s < hidden.count ( ) && hidden [ static_cast < size_t > ( s ) ] ) )
					return -1;

				found = s;

			}

			return found;

		}

		int32_t counted_step ( int32_t latch, int32_t test ) {

			const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

			for ( int32_t s = tail.first + tail.count - 1; s >= tail.first; s-- ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
					return -1;

				const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];
				int64_t var = b.exprs [ static_cast < size_t > ( line.left ) ].value;

				if ( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == var && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST && b.references ( test, static_cast < int32_t > ( var ), -1 ) )
					return s;

				return -1;

			}

			return -1;

		}

		int32_t strip ( int32_t e ) const {

			while ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::CAST )
				e = b.exprs [ static_cast < size_t > ( e ) ].a;

			return e;

		}

		bool guarded_entry ( int32_t start, int64_t init, op kind, int32_t bound ) {

			if ( !guarded_loops || start < 0 )
				return false;

			int32_t pre = -1;

			for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

				if ( loop_body [ static_cast < size_t > ( start ) ] [ static_cast < size_t > ( p ) ] )
					continue;

				pre = pre < 0 ? p : -2;

			}

			if ( pre < 0 )
				return false;

			int32_t gate = pre;
			int32_t into = start;

			if ( b.code [ static_cast < size_t > ( pre ) ].end != term::BRANCH ) {

				if ( pred [ static_cast < size_t > ( pre ) ].count ( ) != 1 )
					return false;

				gate = pred [ static_cast < size_t > ( pre ) ] [ 0 ];
				into = pre;

			}

			const block_code& g = b.code [ static_cast < size_t > ( gate ) ];

			if ( g.end != term::BRANCH || g.condition < 0 || ( g.taken != into && g.other != into ) || g.taken == g.other )
				return false;

			int32_t condition = g.taken == into ? g.condition : b.negate ( g.condition );
			const expr& c = b.exprs [ static_cast < size_t > ( condition ) ];

			if ( c.a < 0 || c.b < 0 )
				return false;

			int32_t target = strip ( bound );

			if ( target < 0 || b.exprs [ static_cast < size_t > ( target ) ].kind != op::VAR )
				return false;

			for ( int32_t s = b.code [ static_cast < size_t > ( pre ) ].first; s < b.code [ static_cast < size_t > ( pre ) ].first + b.code [ static_cast < size_t > ( pre ) ].count && pre != gate; s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && target >= 0 && b.exprs [ static_cast < size_t > ( target ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == b.exprs [ static_cast < size_t > ( target ) ].value && b.exprs [ static_cast < size_t > ( strip ( line.right ) ) ].kind == op::VAR )
					target = strip ( line.right );

			}

			int32_t left = strip ( c.a );
			int32_t right = strip ( c.b );
			op relation = c.kind;
			int64_t k = 0;

			if ( right >= 0 && b.exprs [ static_cast < size_t > ( right ) ].kind == op::CONST && left >= 0 && ( b.same ( left, target ) || b.same ( left, strip ( bound ) ) ) )
				k = b.exprs [ static_cast < size_t > ( right ) ].value;
			else if ( left >= 0 && b.exprs [ static_cast < size_t > ( left ) ].kind == op::CONST && right >= 0 && ( b.same ( right, target ) || b.same ( right, strip ( bound ) ) ) ) {

				k = b.exprs [ static_cast < size_t > ( left ) ].value;

				switch ( relation ) {

					case op::LT: relation = op::GT; break;
					case op::LE: relation = op::GE; break;
					case op::GT: relation = op::LT; break;
					case op::GE: relation = op::LE; break;
					default: break;

				}

			} else
				return false;

			bool above = ( relation == op::GT && k >= init ) || ( relation == op::GE && k > init );

			switch ( kind ) {

				case op::NE:
					return above || ( relation == op::NE && k == init ) || ( relation == op::LT && k <= init );

				case op::LT:
					return above;

				case op::LE:
					return above || ( relation == op::GE && k >= init );

				default:
					return false;

			}

		}

		bool entry_true ( int32_t test, int32_t step, int32_t start = -1 ) {

			const statement& grow = b.statements [ static_cast < size_t > ( step ) ];
			int32_t found = -1;
			int32_t line = -1;

			if ( !find_init ( b.exprs [ static_cast < size_t > ( grow.left ) ].value, found, line ) )
				return false;

			const statement& seed = b.statements [ static_cast < size_t > ( found ) ];

			if ( seed.left < 0 || seed.right < 0 || b.exprs [ static_cast < size_t > ( seed.left ) ].value != b.exprs [ static_cast < size_t > ( grow.left ) ].value || b.exprs [ static_cast < size_t > ( seed.right ) ].kind != op::CONST )
				return false;

			const expr& t = b.exprs [ static_cast < size_t > ( test ) ];

			if ( t.a < 0 || t.b < 0 )
				return false;

			int32_t subject = strip ( t.a );
			int32_t other_side = t.b;
			op test_kind = t.kind;

			if ( b.exprs [ static_cast < size_t > ( subject ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( subject ) ].value != b.exprs [ static_cast < size_t > ( seed.left ) ].value ) {

				int32_t flipped = strip ( t.b );

				if ( flipped < 0 || b.exprs [ static_cast < size_t > ( flipped ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( flipped ) ].value != b.exprs [ static_cast < size_t > ( seed.left ) ].value )
					return false;

				subject = flipped;
				other_side = t.a;

				switch ( test_kind ) {

					case op::LT: test_kind = op::GT; break;
					case op::LE: test_kind = op::GE; break;
					case op::GT: test_kind = op::LT; break;
					case op::GE: test_kind = op::LE; break;
					default: break;

				}

			}

			if ( b.exprs [ static_cast < size_t > ( other_side ) ].kind != op::CONST )
				return guarded_entry ( start, b.exprs [ static_cast < size_t > ( seed.right ) ].value, test_kind, other_side );

			if ( other_side != t.b )
				return false;

			int64_t x = b.exprs [ static_cast < size_t > ( seed.right ) ].value;
			int64_t y = b.exprs [ static_cast < size_t > ( t.b ) ].value;
			bool is_unsigned = ( t.bits & UNSIGNED ) != 0;
			uint64_t ux = static_cast < uint64_t > ( x );
			uint64_t uy = static_cast < uint64_t > ( y );

			switch ( t.kind ) {

				case op::EQ: return x == y;
				case op::NE: return x != y;
				case op::LT: return is_unsigned ? ux < uy : x < y;
				case op::LE: return is_unsigned ? ux <= uy : x <= y;
				case op::GT: return is_unsigned ? ux > uy : x > y;
				case op::GE: return is_unsigned ? ux >= uy : x >= y;
				default: return false;

			}

		}

		int32_t rotate ( int32_t block, int32_t entry, int32_t leave, int32_t guard_test, const frame& context ) {

			int32_t pre = -1;
			int32_t head = entry;

			if ( !header [ static_cast < size_t > ( head ) ] ) {

				const block_code& passage = b.code [ static_cast < size_t > ( entry ) ];

				if ( passage.end != term::FALL || pred [ static_cast < size_t > ( entry ) ].count ( ) != 1 || passage.taken < 0 || !header [ static_cast < size_t > ( passage.taken ) ] )
					return -2;

				pre = entry;
				head = passage.taken;

			}

			if ( emitted [ static_cast < size_t > ( head ) ] )
				return -2;

			const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( head ) ];
			int32_t latch = -1;

			for ( int32_t p : pred [ static_cast < size_t > ( head ) ] ) {

				if ( !members.empty ( ) && members [ static_cast < size_t > ( p ) ] )
					latch = latch < 0 ? p : -2;
				else if ( p != ( pre >= 0 ? pre : block ) )
					return -2;

			}

			if ( latch < 0 )
				return -2;

			const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

			if ( tail.end != term::BRANCH || tail.condition < 0 )
				return -2;

			bool back_taken = tail.taken == head;

			if ( ( back_taken ? tail.other : tail.taken ) != leave || ( back_taken ? tail.taken : tail.other ) != head )
				return -2;

			int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
			bool matched = equivalent ( guard_test, test, -1, -1, 0 );
			int32_t seed_statement = -1;

			const int32_t sources [ 2 ] = { pre, block };

			for ( int32_t source : sources ) {

				if ( matched || source < 0 )
					continue;

				const block_code& owner = b.code [ static_cast < size_t > ( source ) ];

				for ( int32_t s = owner.first + owner.count - 1; s >= owner.first && !matched; s-- ) {

					const statement& line = b.statements [ static_cast < size_t > ( s ) ];

					if ( line.kind != stmt::ASSIGN || line.left < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR )
						continue;

					int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value );

					if ( equivalent ( guard_test, test, var, line.right, 0 ) ) {

						matched = true;
						seed_statement = s;

					}

				}

			}

			if ( !matched )
				return -2;

			( void ) seed_statement;

			if ( pre >= 0 )
				body ( pre );

			int32_t step = -1;

			for ( int32_t s = tail.first + tail.count - 1; s >= tail.first; s-- ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				if ( line.kind == stmt::ASSIGN && line.left >= 0 && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR ) {

					const expr& grow = b.exprs [ static_cast < size_t > ( line.right ) ];

					if ( ( grow.kind == op::ADD || grow.kind == op::SUB ) && grow.a >= 0 && b.exprs [ static_cast < size_t > ( grow.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( grow.a ) ].value == b.exprs [ static_cast < size_t > ( line.left ) ].value && grow.b >= 0 && b.exprs [ static_cast < size_t > ( grow.b ) ].kind == op::CONST && b.references ( test, static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( line.left ) ].value ), -1 ) )
						step = s;

				}

				break;

			}

			frame inner;

			inner.loop = head;
			inner.exit = leave;

			label ( head );

			if ( step >= 0 && open_for ( tail.last, test, false, step, false ) )
				hidden [ static_cast < size_t > ( step ) ] = 1;
			else
				open_brace ( tail.last, L"while", test, false );

			emitted [ static_cast < size_t > ( latch ) ] = 1;

			if ( latch != head ) {

				region ( head, latch, inner );

				if ( !empty_block ( latch ) )
					body ( latch );

			} else
				body ( head );

			emitted [ static_cast < size_t > ( latch ) ] = 1;
			close_brace ( );
			ended = false;

			( void ) context;

			return leave;

		}

		bool deferred ( int32_t target, int32_t from ) {

			if ( target < 0 || target >= count || emitted [ static_cast < size_t > ( target ) ] || pred [ static_cast < size_t > ( target ) ].count ( ) < 2 || header [ static_cast < size_t > ( target ) ] )
				return false;

			uint64_t start = b.graph.blocks [ static_cast < size_t > ( target ) ].start;

			for ( int32_t p : pred [ static_cast < size_t > ( target ) ] ) {

				if ( p != from && !emitted [ static_cast < size_t > ( p ) ] && ( b.graph.blocks [ static_cast < size_t > ( p ) ].end == start || ( defer_later && b.graph.blocks [ static_cast < size_t > ( p ) ].start < start ) ) && b.code [ static_cast < size_t > ( p ) ].reachable && !dominates ( target, p ) )
					return true;

			}

			return false;

		}

		int32_t branch ( int32_t block, int32_t stop, const frame& context, bool chained ) {

			const block_code& item = b.code [ static_cast < size_t > ( block ) ];
			int32_t taken = item.taken;
			int32_t other = item.other;
			int32_t follow = follow_of ( block, stop, context );
			bool taken_jump = taken == context.loop || taken == context.exit;
			bool other_jump = other == context.loop || other == context.exit;

			if ( shared_returns && follow < 0 && context.loop < 0 && taken >= 0 && other >= 0 && taken < count && other < count ) {

				int32_t last = -1;

				for ( int32_t i = 0; i < count; i++ ) {

					if ( b.code [ static_cast < size_t > ( i ) ].reachable && ( last < 0 || b.graph.blocks [ static_cast < size_t > ( i ) ].start > b.graph.blocks [ static_cast < size_t > ( last ) ].start ) )
						last = i;

				}

				auto same_value = [ & ] ( int32_t x ) {

					const block_code& tail = b.code [ static_cast < size_t > ( x ) ];

					if ( tail.returned < 0 )
						return true;

					const expr& back = b.exprs [ static_cast < size_t > ( tail.returned ) ];

					if ( back.kind == op::CONST )
						return true;

					if ( back.kind != op::VAR )
						return false;

					for ( int32_t p : pred [ static_cast < size_t > ( x ) ] ) {

						const block_code& from = b.code [ static_cast < size_t > ( p ) ];

						for ( int32_t s = from.first; s < from.first + from.count && s < b.statements.count ( ); s++ ) {

							const statement& line = b.statements [ static_cast < size_t > ( s ) ];

							if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == back.value )
								return false;

						}

					}

					return true;

				};

				auto falls_in = [ & ] ( int32_t x ) {

					for ( int32_t p : pred [ static_cast < size_t > ( x ) ] ) {

						if ( b.graph.blocks [ static_cast < size_t > ( p ) ].end == b.graph.blocks [ static_cast < size_t > ( x ) ].start && b.code [ static_cast < size_t > ( p ) ].end != term::BRANCH )
							return true;

					}

					return false;

				};

				auto sets_return = [ & ] ( int32_t x, int32_t end_block ) {

					const block_code& tail = b.code [ static_cast < size_t > ( end_block ) ];

					if ( tail.returned < 0 )
						return true;

					const expr& back = b.exprs [ static_cast < size_t > ( tail.returned ) ];

					if ( back.kind == op::CONST )
						return true;

					if ( back.kind != op::VAR )
						return false;

					const block_code& mine = b.code [ static_cast < size_t > ( x ) ];

					for ( int32_t s = mine.first; s < mine.first + mine.count && s < b.statements.count ( ); s++ ) {

						const statement& line = b.statements [ static_cast < size_t > ( s ) ];

						if ( line.kind == stmt::ASSIGN && line.left >= 0 && b.exprs [ static_cast < size_t > ( line.left ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( line.left ) ].value == back.value && line.right >= 0 && b.exprs [ static_cast < size_t > ( line.right ) ].kind == op::CONST )
							return true;

					}

					return false;

				};

				auto shared = [ & ] ( int32_t x ) {

					if ( emitted [ static_cast < size_t > ( x ) ] || pred [ static_cast < size_t > ( x ) ].count ( ) < 2 || !small_block ( x ) || !falls_in ( x ) || header [ static_cast < size_t > ( x ) ] )
						return false;

					if ( exit_block ( x ) )
						return same_value ( x ) && ( x == last || sets_return ( x, x ) );

					const block_code& step = b.code [ static_cast < size_t > ( x ) ];

					if ( step.end != term::FALL || step.taken < 0 || step.taken >= count || !exit_block ( step.taken ) || !empty_block ( step.taken ) )
						return false;

					return sets_return ( x, step.taken );

				};

				if ( shared ( taken ) && !exit_block ( other ) )
					follow = taken;
				else if ( shared ( other ) && !exit_block ( taken ) )
					follow = other;

			}

			if ( context.loop >= 0 && stop == context.loop && ( taken == context.loop ) != ( other == context.loop ) ) {

				int32_t away = taken == context.loop ? other : taken;
				const base::list < uint8_t >& members = loop_body [ static_cast < size_t > ( context.loop ) ];
				bool outside = away == context.exit || emitted [ static_cast < size_t > ( away ) ] || ( !members.empty ( ) && !members [ static_cast < size_t > ( away ) ] );

				if ( outside ) {

					open_brace ( item.last, chained ? L"else if" : L"if", item.condition, taken == context.loop );

					if ( !special ( away, -2, context, item.last ) ) {

						if ( emitted [ static_cast < size_t > ( away ) ] )
							go_to ( away, item.last );
						else
							region ( away, context.loop, context );

					}

					close_brace ( );

					return -1;

				}

			}

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

			auto leads = [ & ] ( int32_t x, int32_t target ) {

				for ( int32_t hops = 0; hops < 8 && x >= 0 && x < count; hops++ ) {

					if ( x == target )
						return true;

					const block_code& step = b.code [ static_cast < size_t > ( x ) ];

					if ( !empty_block ( x ) || step.end != term::FALL || header [ static_cast < size_t > ( x ) ] || wanted_label [ static_cast < size_t > ( x ) ] )
						return false;

					x = step.taken;

				}

				return x == target;

			};

			if ( empty_then && then_part != follow && else_part != follow && stop >= 0 && ( leads ( then_part, stop ) || ( follow >= 0 && leads ( then_part, follow ) ) ) && !leads ( else_part, stop ) ) {

				then_part = other;
				else_part = taken;
				negated = true;

				if ( follow < 0 || leads ( else_part, stop ) )
					follow = stop;

				if ( leads ( else_part, follow ) )
					else_part = follow;

			} else if ( then_part == follow ) {

				then_part = other;
				else_part = taken;
				negated = true;

			}

			if ( diamond_ternary && !no_ternary && follow >= 0 && then_part >= 0 && else_part >= 0 && then_part != follow && else_part != follow ) {

				int32_t then_line = single_assign ( then_part, follow );
				int32_t else_line = single_assign ( else_part, follow );

				if ( then_line >= 0 && else_line >= 0 ) {

					const statement& yes = b.statements [ static_cast < size_t > ( then_line ) ];
					const statement& no = b.statements [ static_cast < size_t > ( else_line ) ];
					const expr& yes_left = b.exprs [ static_cast < size_t > ( yes.left ) ];
					const expr& no_left = b.exprs [ static_cast < size_t > ( no.left ) ];

					if ( yes_left.kind == op::VAR && no_left.kind == op::VAR && yes_left.value == no_left.value ) {

						int32_t test = negated ? b.negate ( item.condition ) : item.condition;
						int32_t select = b.make ( op::TERNARY, yes_left.size, test, yes.right );

						b.exprs [ static_cast < size_t > ( select ) ].c = no.right;
						b.deepen ( select );
						label ( then_part );
						label ( else_part );
						emitted [ static_cast < size_t > ( then_part ) ] = 1;
						emitted [ static_cast < size_t > ( else_part ) ] = 1;

						if ( !dry ) {

							out.begin ( yes.address );
							value ( yes.left, 0 );
							out.put ( L" = ", syntax::OPERATOR );
							value ( select, 2 );
							out.put ( L";" );
							out.finish ( );

						}

						ended = false;

						return follow;

					}

				}

			}

			if ( follow < 0 && then_part >= 0 && else_part >= 0 && !chained ) {

				for ( int32_t side = 0; side < 2; side++ ) {

					int32_t bare = side == 0 ? else_part : then_part;
					int32_t work = side == 0 ? then_part : else_part;
					const block_code& empty_ret = b.code [ static_cast < size_t > ( bare ) ];
					const block_code& full_ret = b.code [ static_cast < size_t > ( work ) ];

					if ( empty_ret.end != term::RETURN || full_ret.end != term::RETURN || !empty_block ( bare ) || empty_block ( work ) || emitted [ static_cast < size_t > ( work ) ] || emitted [ static_cast < size_t > ( bare ) ] || pred [ static_cast < size_t > ( work ) ].count ( ) != 1 )
						continue;

					bool alike = ( empty_ret.returned < 0 && full_ret.returned < 0 ) || ( empty_ret.returned >= 0 && full_ret.returned >= 0 && b.same ( empty_ret.returned, full_ret.returned ) );

					if ( !alike )
						continue;

					bool toward = side == 0 ? negated : !negated;

					open_brace ( item.last, L"if", item.condition, toward );
					body ( work );
					close_brace ( );

					return bare;

				}

			}

			if ( rotate_guards && !chained && then_part >= 0 && else_part >= 0 && then_part != follow && else_part != context.loop && else_part != context.exit ) {

				int32_t after = rotate ( block, then_part, else_part, negated ? b.negate ( item.condition ) : item.condition, context );

				if ( after != -2 )
					return after;

			}

			bool skip_else = false;

			if ( follow < 0 && context.loop < 0 && silent_return ( then_part ) && !silent_return ( else_part ) && else_part >= 0 ) {

				int32_t held = then_part;

				then_part = else_part;
				else_part = held;
				negated = !negated;

			}

			if ( else_part != follow && follow < 0 && context.loop < 0 && silent_return ( else_part ) ) {

				skip_else = true;
				emitted [ static_cast < size_t > ( else_part ) ] = 1;

			}

			open_brace ( item.last, chained ? L"else if" : L"if", item.condition, negated );

			ended = false;

			if ( !special ( then_part, -2, context, item.last ) ) {

				if ( defer_joins && deferred ( then_part, block ) )
					go_to ( then_part, item.last );
				else
					region ( then_part, follow, context );

			}

			bool then_ends = ended;

			out.indent--;

			if ( skip_else ) {

				brace ( L"}" );
				ended = false;

			} else if ( then_ends && else_part != follow && else_part >= 0 ) {

				brace ( L"}" );
				ended = false;

				if ( !special ( else_part, -2, context, item.last ) )
					region ( else_part, follow, context );

			} else if ( else_part != follow && else_part >= 0 ) {

				bool chain = !emitted [ static_cast < size_t > ( else_part ) ] && b.code [ static_cast < size_t > ( else_part ) ].end == term::BRANCH && empty_block ( else_part ) && pred [ static_cast < size_t > ( else_part ) ].count ( ) == 1 && !header [ static_cast < size_t > ( else_part ) ] && follow_of ( else_part, stop, context ) == follow && !wanted_label [ static_cast < size_t > ( else_part ) ];

				if ( chain ) {

					brace ( L"}" );
					body ( else_part );

					int32_t next = branch ( else_part, stop, context, true );

					( void ) next;
					ended = then_ends && ended;

				} else {

					brace ( L"}" );
					else_line ( );
					brace ( L"{" );
					out.indent++;
					ended = false;

					if ( !special ( else_part, -2, context, item.last ) )
						region ( else_part, follow, context );

					bool else_ends = ended;

					close_brace ( );
					ended = then_ends && else_ends;

				}

			} else {

				brace ( L"}" );
				ended = false;

			}

			return follow;

		}

		int32_t loop_step ( int32_t start, const base::list < uint8_t >& members, int32_t condition ) {

			int32_t latch = -1;

			for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

				if ( members [ static_cast < size_t > ( p ) ] )
					latch = latch < 0 ? p : -2;

			}

			if ( latch < 0 || latch == start )
				return -1;

			const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];

			if ( tail.end != term::FALL || tail.taken != start )
				return -1;

			int32_t last = -1;

			for ( int32_t s = tail.first; s < tail.first + tail.count && s < b.statements.count ( ); s++ ) {

				if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
					last = s;

			}

			if ( last < 0 )
				return -1;

			const statement& line = b.statements [ static_cast < size_t > ( last ) ];

			if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 )
				return -1;

			const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];
			const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

			step_needs_init = false;

			if ( left.kind != op::VAR || ( right.bits & FLOATING ) )
				return -1;

			bool counted = right.kind == op::ADD && right.a >= 0 && right.b >= 0 && b.exprs [ static_cast < size_t > ( right.a ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( right.a ) ].value == left.value && b.exprs [ static_cast < size_t > ( right.b ) ].kind == op::CONST;

			if ( !counted ) {

				if ( condition < 0 || !b.references ( line.right, static_cast < int32_t > ( left.value ), -1 ) )
					return -1;

				step_needs_init = true;

			}

			if ( condition >= 0 && !b.references ( condition, static_cast < int32_t > ( left.value ), -1 ) )
				return -1;

			return last;

		}

		void name_loop_var ( int32_t var ) {

			if ( var < 0 || var >= loop_letter.count ( ) || loop_letter [ static_cast < size_t > ( var ) ] >= 0 || var == result_var )
				return;

			const variable& item = b.vars [ static_cast < size_t > ( var ) ];

			if ( item.kind == var_kind::PARAMETER || next_letter >= 10 )
				return;

			loop_letter [ static_cast < size_t > ( var ) ] = static_cast < int8_t > ( next_letter++ );

		}

		bool open_for ( uint64_t address, int32_t condition, bool negated, int32_t step, bool need_init ) {

			int32_t var = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( b.statements [ static_cast < size_t > ( step ) ].left ) ].value );
			int32_t init = -1;
			int32_t init_line = -1;

			if ( !find_init ( var, init, init_line ) )
				init = -1;

			if ( need_init && ( dry || init < 0 ) )
				return false;

			name_loop_var ( var );

			if ( dry ) {

				out.indent++;

				return true;

			}

			if ( init >= 0 ) {

				if ( init_line == out.r->lines.count ( ) - 1 ) {

					const line held = out.r->lines.back ( );

					out.r->lines.pop ( );
					out.r->text.resize ( static_cast < size_t > ( held.text ) );
					out.r->spans.resize ( static_cast < size_t > ( held.first_span ) );

				} else
					out.r->lines.remove_at ( static_cast < size_t > ( init_line ) );

				last_assign = -1;
				last_assign_line = -1;
				recent_assign.clear ( );
				recent_line.clear ( );

			}

			hoisted.clear ( );
			hoisted_temp.clear ( );
			out.begin ( address );
			out.put ( L"for", syntax::CONTROL );
			paren_open ( );

			if ( init >= 0 )
				inline_statement ( init );

			out.put ( condition >= 0 ? L"; " : L"; ;" );

			if ( condition >= 0 ) {

				int32_t shown = negated ? b.negate ( condition ) : condition;
				const expr test = b.exprs [ static_cast < size_t > ( shown ) ];
				bool relation = test.kind >= op::EQ && test.kind <= op::GE;

				if ( relation && test.a >= 0 && test.b >= 0 && strip ( test.b ) >= 0 && b.exprs [ static_cast < size_t > ( strip ( test.b ) ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( strip ( test.b ) ) ].value == var && !( strip ( test.a ) >= 0 && b.exprs [ static_cast < size_t > ( strip ( test.a ) ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( strip ( test.a ) ) ].value == var ) ) {

					op turned = test.kind;

					switch ( test.kind ) {

						case op::LT: turned = op::GT; break;
						case op::LE: turned = op::GE; break;
						case op::GT: turned = op::LT; break;
						case op::GE: turned = op::LE; break;
						default: break;

					}

					shown = b.make ( turned, 1, test.b, test.a, 0, test.bits );

				}

				value ( shown, 0 );
				out.put ( L";" );

			}

			out.put ( L" " );
			inline_statement ( step );
			paren_close ( );
			out.finish ( );
			brace ( L"{" );
			out.indent++;

			return true;

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

			if ( forced_exit >= 0 ) {

				exit = forced_exit;
				forced_exit = -1;

			}

			if ( return_exits && exit >= 0 && exit < count && exit_block ( exit ) && small_block ( exit ) ) {

				int32_t other = -1;

				for ( int32_t v = 0; v < count && other != -2; v++ ) {

					if ( !members [ static_cast < size_t > ( v ) ] )
						continue;

					for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

						if ( members [ static_cast < size_t > ( s ) ] || s == exit || exit_block ( s ) )
							continue;

						if ( other == -1 )
							other = s;
						else if ( other != s )
							other = -2;

					}

				}

				if ( other >= 0 )
					exit = other;

			}

			frame inner;

			inner.loop = start;
			inner.exit = exit;
			inner.outer = forced_outer;
			forced_outer = -1;

			const block_code& item = b.code [ static_cast < size_t > ( start ) ];

			if ( item.end == term::BRANCH && ( item.taken == start || item.other == start ) ) {

				bool back_taken = item.taken == start;
				int32_t leaving = back_taken ? item.other : item.taken;
				int32_t test = back_taken ? item.condition : b.negate ( item.condition );
				int32_t step = counted_step ( start, test );

				if ( step >= 0 && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true ) ) {

					hidden [ static_cast < size_t > ( step ) ] = 1;
					body ( start );
					close_brace ( );

					return leaving;

				}

				int32_t moved = sinkable_step ( start, test );

				open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

				if ( moved >= 0 )
					hidden [ static_cast < size_t > ( moved ) ] = 1;

				body ( start );

				if ( moved >= 0 ) {

					hidden [ static_cast < size_t > ( moved ) ] = 0;
					statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

				}

				close_brace ( );
				while_tail ( item.last, test );

				return leaving;

			}

			int32_t latch = -1;

			for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

				if ( members [ static_cast < size_t > ( p ) ] )
					latch = latch < 0 ? p : -2;

			}

			if ( latch >= 0 && latch != start && b.code [ static_cast < size_t > ( latch ) ].end == term::BRANCH ) {

				const block_code& tail = b.code [ static_cast < size_t > ( latch ) ];
				bool back_taken = tail.taken == start;
				int32_t leaving = back_taken ? tail.other : tail.taken;

				if ( leaving == exit ) {

					int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
					int32_t step = counted_step ( latch, test );

					if ( step >= 0 && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true ) ) {

						hidden [ static_cast < size_t > ( step ) ] = 1;
						emitted [ static_cast < size_t > ( latch ) ] = 1;
						region ( start, latch, inner );

						if ( !empty_block ( latch ) )
							body ( latch );

						emitted [ static_cast < size_t > ( latch ) ] = 1;
						close_brace ( );

						return exit;

					}

					bool plain = empty_block ( latch );

					open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

					emitted [ static_cast < size_t > ( latch ) ] = 1;
					region ( start, latch, inner );

					if ( !plain ) {

						int32_t moved = sinkable_step ( latch, test );

						if ( moved >= 0 )
							hidden [ static_cast < size_t > ( moved ) ] = 1;

						body ( latch );

						if ( moved >= 0 ) {

							hidden [ static_cast < size_t > ( moved ) ] = 0;
							statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

						}

					}

					emitted [ static_cast < size_t > ( latch ) ] = 1;
					close_brace ( );
					while_tail ( tail.last, test );

					return exit;

				}

			}

			if ( nested_loops && latch == -2 && nest_depth < 3 ) {

				int32_t outer_latch = -1;

				for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

					if ( members [ static_cast < size_t > ( p ) ] && p != start && ( outer_latch < 0 || b.graph.blocks [ static_cast < size_t > ( p ) ].start > b.graph.blocks [ static_cast < size_t > ( outer_latch ) ].start ) )
						outer_latch = p;

				}

				const block_code& tail = b.code [ static_cast < size_t > ( outer_latch >= 0 ? outer_latch : start ) ];
				bool usable = outer_latch >= 0 && tail.end == term::BRANCH && ( tail.taken == start ) != ( tail.other == start ) && ( tail.taken == start ? tail.other : tail.taken ) == exit;

				base::list < uint8_t > inner_set;

				if ( usable ) {

					inner_set.fill ( static_cast < size_t > ( count ), 0 );

					base::list < int32_t > work;

					inner_set [ static_cast < size_t > ( start ) ] = 1;

					for ( int32_t p : pred [ static_cast < size_t > ( start ) ] ) {

						if ( members [ static_cast < size_t > ( p ) ] && p != outer_latch && !inner_set [ static_cast < size_t > ( p ) ] ) {

							inner_set [ static_cast < size_t > ( p ) ] = 1;
							work.push ( p );

						}

					}

					while ( !work.empty ( ) ) {

						int32_t v = work.back ( );

						work.pop ( );

						for ( int32_t p : pred [ static_cast < size_t > ( v ) ] ) {

							if ( members [ static_cast < size_t > ( p ) ] && !inner_set [ static_cast < size_t > ( p ) ] ) {

								inner_set [ static_cast < size_t > ( p ) ] = 1;
								work.push ( p );

							}

						}

					}

					if ( inner_set [ static_cast < size_t > ( outer_latch ) ] )
						usable = false;

				}

				int32_t resume = -1;

				if ( usable ) {

					for ( int32_t v = 0; v < count && resume != -2; v++ ) {

						if ( !inner_set [ static_cast < size_t > ( v ) ] )
							continue;

						for ( int32_t s : succ [ static_cast < size_t > ( v ) ] ) {

							if ( inner_set [ static_cast < size_t > ( s ) ] )
								continue;

							if ( !members [ static_cast < size_t > ( s ) ] ) {

								if ( !exit_block ( s ) && s != exit )
									resume = -2;

								continue;

							}

							if ( resume == -1 )
								resume = s;
							else if ( resume != s )
								resume = -2;

						}

					}

					usable = resume >= 0;

				}

				if ( usable ) {

					bool back_taken = tail.taken == start;
					int32_t test = back_taken ? tail.condition : b.negate ( tail.condition );
					base::list < uint8_t > saved = loop_body [ static_cast < size_t > ( start ) ];

					int32_t step = counted_step ( outer_latch, test );
					bool counted = step >= 0 && entry_true ( test, step, start ) && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, test, false, step, true );

					if ( counted )
						hidden [ static_cast < size_t > ( step ) ] = 1;
					else
						open_brace ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, L"do", -1, false );

					loop_body [ static_cast < size_t > ( start ) ] = inner_set;
					forced_exit = resume;
					forced_outer = exit;
					nest_depth++;

					int32_t after = loop ( start, inner );

					nest_depth--;
					forced_exit = -1;
					loop_body [ static_cast < size_t > ( start ) ] = saved;
					emitted [ static_cast < size_t > ( outer_latch ) ] = 1;

					if ( after >= 0 && after != outer_latch && !emitted [ static_cast < size_t > ( after ) ] )
						region ( after, outer_latch, inner );

					int32_t moved = counted ? -1 : sinkable_step ( outer_latch, test );

					if ( moved >= 0 )
						hidden [ static_cast < size_t > ( moved ) ] = 1;

					if ( !empty_block ( outer_latch ) )
						body ( outer_latch );

					if ( moved >= 0 ) {

						hidden [ static_cast < size_t > ( moved ) ] = 0;
						statement_line ( b.statements [ static_cast < size_t > ( moved ) ] );

					}

					close_brace ( );

					if ( !counted )
						while_tail ( tail.last, test );

					return exit;

				}

			}

			if ( item.end == term::BRANCH && empty_block ( start ) && ( ( item.taken == exit && members [ static_cast < size_t > ( item.other ) ] ) || ( item.other == exit && members [ static_cast < size_t > ( item.taken ) ] ) ) ) {

				bool stay_taken = item.other == exit;
				int32_t step = loop_step ( start, members, item.condition );

				label ( start );
				emitted [ static_cast < size_t > ( start ) ] = 1;

				if ( step >= 0 && open_for ( item.last, item.condition, !stay_taken, step, step_needs_init ) )
					hidden [ static_cast < size_t > ( step ) ] = 1;
				else
					open_brace ( item.last, L"while", item.condition, !stay_taken );

				region ( stay_taken ? item.taken : item.other, start, inner );
				close_brace ( );

				return exit;

			}

			int32_t forever_step = loop_step ( start, members, -1 );

			if ( forever_step >= 0 && open_for ( b.graph.blocks [ static_cast < size_t > ( start ) ].start, -1, false, forever_step, true ) )
				hidden [ static_cast < size_t > ( forever_step ) ] = 1;
			else
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

				const analysis::edge& link = b.graph.edges [ static_cast < size_t > ( node.first_out + e ) ];
				int32_t to = link.to;
				bool seen = false;

				if ( link.kind == analysis::edge_kind::JUMP && to == follow )
					continue;

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

						if ( link.kind == analysis::edge_kind::JUMP )
							out.put ( L"default", syntax::CONTROL );
						else {

							out.put ( L"case", syntax::CONTROL );
							out.put ( L" " );
							number ( link.value, 4, false, false );

						}

						out.put ( L":" );
						out.finish ( );

					}

				}

				out.indent++;

				if ( target != follow && !emitted [ static_cast < size_t > ( target ) ] )
					region ( target, follow, inner );
				else if ( target != follow )
					go_to ( target, 0 );

				if ( out.terminal < 0 )
					jump_line ( L"break", 0 );

				out.indent--;

			}

			close_brace ( );

			return follow;

		}

		void run ( ) {

			emitted.fill ( static_cast < size_t > ( count ), 0 );
			labeled.fill ( static_cast < size_t > ( count ), 0 );

			if ( !dry )
				referenced.fill ( b.vars.size ( ), 0 );

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
			hidden.fill ( b.statements.size ( ), 0 );
			loop_letter.fill ( b.vars.size ( ), -1 );
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

		bool small_block ( int32_t v ) {

			const block_code& item = b.code [ static_cast < size_t > ( v ) ];
			int32_t live = 0;

			for ( int32_t s = item.first; s < item.first + item.count && s < b.statements.count ( ); s++ ) {

				if ( b.statements [ static_cast < size_t > ( s ) ].kind != stmt::NOP )
					live++;

			}

			return live <= 1;

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

			if ( i > 0 && line_is ( r, head, L"else" ) && line_is ( r, r.lines [ static_cast < size_t > ( i - 1 ) ], L"}" ) && !skip [ static_cast < size_t > ( i - 1 ) ] && r.lines [ static_cast < size_t > ( i - 1 ) ].indent == head.indent )
				continue;

			if ( i + 5 < total && !line_is ( r, head, L"else" ) && line_is ( r, r.lines [ static_cast < size_t > ( i + 4 ) ], L"else" ) && r.lines [ static_cast < size_t > ( i + 4 ) ].indent == head.indent && line_is ( r, r.lines [ static_cast < size_t > ( i + 5 ) ], L"{" ) ) {

				bool single_else = i + 7 < total && line_is ( r, r.lines [ static_cast < size_t > ( i + 7 ) ], L"}" ) && r.lines [ static_cast < size_t > ( i + 7 ) ].indent == head.indent && r.lines [ static_cast < size_t > ( i + 6 ) ].indent == head.indent + 1 && simple_line ( r, r.lines [ static_cast < size_t > ( i + 6 ) ] );

				if ( !single_else )
					continue;

			}

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

		if ( !length || ( type [ length - 1 ] == L'*' && !double_pointees ) || length >= room )
			return false;

		int32_t start = 0;

		if ( length > 6 && type [ 0 ] == L'c' && type [ 1 ] == L'o' && type [ 2 ] == L'n' && type [ 3 ] == L's' && type [ 4 ] == L't' && type [ 5 ] == L' ' )
			start = 6;

		int32_t k = 0;

		for ( int32_t i = start; i < length; i++ )
			out [ k++ ] = type [ i ];

		out [ k ] = 0;

		return k > 0 && !base::same_text ( out, L"void" ) && !base::same_text ( out, L"void *" ) && !base::same_text ( out, L"void*" );

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
			wchar_t callee [ 200 ];

			if ( !declared && api_types && target && b.names->name ( target, true, callee, 200 ) )
				declared = api_prototype ( callee );

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

				while ( api_types && index >= 0 && b.exprs [ static_cast < size_t > ( index ) ].kind == op::CAST && b.exprs [ static_cast < size_t > ( index ) ].a >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( index ) ].a ) ].kind == op::ADDRESS )
					index = b.exprs [ static_cast < size_t > ( index ) ].a;

				if ( index < 0 )
					continue;

				const expr& passed = b.exprs [ static_cast < size_t > ( index ) ];

				if ( passed.kind == op::VAR ) {

					bool constants_only = b.vars [ static_cast < size_t > ( passed.value ) ].kind != var_kind::PARAMETER;
					bool written = false;

					for ( const statement& line : b.statements ) {

						if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( line.left ) ].value != passed.value )
							continue;

						written = true;

						if ( b.exprs [ static_cast < size_t > ( line.right ) ].kind != op::CONST )
							constants_only = false;

					}

					if ( !( constants_only && written ) )
						offer ( static_cast < int32_t > ( passed.value ), shape->types [ i ] );

				}
				else if ( passed.kind == op::ADDRESS && ( passed.a < 0 || ( api_types && b.exprs [ static_cast < size_t > ( passed.a ) ].kind == op::VAR ) ) ) {

					wchar_t inner [ 64 ];
					int64_t target_var = passed.a < 0 ? passed.value : b.exprs [ static_cast < size_t > ( passed.a ) ].value;

					if ( pointee_of ( shape->types [ i ], inner, 64 ) )
						offer ( static_cast < int32_t > ( target_var ), inner );

				}

			}

		}

		delete shape;

		if ( intrinsic_types ) {

			for ( const expr& item : b.exprs ) {

				if ( item.kind != op::INTRINSIC || item.arg_count != 3 )
					continue;

				const wchar_t* called = intrinsic_name ( static_cast < int32_t > ( item.value ) );
				bool fill = base::same_text ( called, L"memset" );

				if ( !fill && !base::same_text ( called, L"qmemcpy" ) )
					continue;

				const wchar_t* types [ 3 ] = { L"void *", fill ? L"char" : L"const void *", L"unsigned __int64" };

				for ( int32_t i = 0; i < 3; i++ ) {

					int32_t index = b.args [ static_cast < size_t > ( item.first_arg + i ) ];

					while ( index >= 0 && b.exprs [ static_cast < size_t > ( index ) ].kind == op::CAST )
						index = b.exprs [ static_cast < size_t > ( index ) ].a;

					if ( index < 0 || b.exprs [ static_cast < size_t > ( index ) ].kind != op::VAR )
						continue;

					int32_t v = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( index ) ].value );

					if ( v < 0 || v >= b.vars.count ( ) || b.vars [ static_cast < size_t > ( v ) ].kind != var_kind::PARAMETER || !out [ static_cast < size_t > ( v ) ].empty ( ) )
						continue;

					if ( i == 1 && fill && b.vars [ static_cast < size_t > ( v ) ].size >= 1 ) {

						out [ static_cast < size_t > ( v ) ].set ( L"char" );

						continue;

					}

					offer ( v, types [ i ] );

				}

			}

		}

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

	static const wchar_t* returned_pointer ( const builder& b ) {

		const wchar_t* found = nullptr;

		for ( const block_code& item : b.code ) {

			if ( !item.reachable || item.end != term::RETURN )
				continue;

			if ( item.returned < 0 )
				return nullptr;

			const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];

			if ( back.kind != op::VAR || back.value < 0 || back.value >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( back.value ) ].empty ( ) )
				return nullptr;

			const wchar_t* text = b.var_types [ static_cast < size_t > ( back.value ) ].get ( );
			bool star = false;

			for ( const wchar_t* p = text; *p; p++ )
				star = star || *p == L'*';

			if ( !star || ( found && !base::same_text ( found, text ) ) )
				return nullptr;

			found = text;

		}

		return found;

	}

	static bool unsigned_result ( const builder& b ) {

		int32_t seen = 0;

		for ( const block_code& item : b.code ) {

			if ( !item.reachable || item.end != term::RETURN || item.returned < 0 )
				continue;

			const expr& back = b.exprs [ static_cast < size_t > ( item.returned ) ];
			bool wide_constant = back.kind == op::CONST && back.size == 8 && back.value < 0 && back.value < -0x100000000ll;
			bool marked = back.kind != op::CONST && ( back.bits & UNSIGNED ) && !( back.bits & SIGNED ) && back.size == 8;

			if ( back.kind == op::VAR && back.value >= 0 && back.value < b.vars.count ( ) )
				marked = ( b.vars [ static_cast < size_t > ( back.value ) ].bits & UNSIGNED ) && b.vars [ static_cast < size_t > ( back.value ) ].size == 8;

			if ( back.kind == op::CONST && !wide_constant && back.value >= 0 && back.value < 0x100000000ll )
				continue;

			if ( !marked && !wide_constant )
				return false;

			seen++;

		}

		return seen > 0;

	}

	static bool pass_typed = true;
	static bool callee_shapes = true;
	static bool element_flavors = true;
	static bool typed_locals = true;
	static bool char_locals = true;

	static bool untype_passed = true;
	static bool detect_bools = true;

	static void stack_arrays ( builder& b, base::list < int32_t >& array_of, base::list < int32_t >& array_step, base::list < int32_t >& array_count ) {

		int32_t total = b.vars.count ( );
		base::list < int32_t > reads;
		base::list < uint8_t > addressed;
		base::list < uint8_t > accessed;

		array_of.clear ( );
		array_step.clear ( );
		array_count.clear ( );
		array_of.fill ( static_cast < size_t > ( total ), -1 );
		array_step.fill ( static_cast < size_t > ( total ), 0 );
		array_count.fill ( static_cast < size_t > ( total ), 0 );

		if ( !array_vars )
			return;

		reads.fill ( static_cast < size_t > ( total ), 0 );
		addressed.fill ( static_cast < size_t > ( total ), 0 );
		accessed.fill ( static_cast < size_t > ( total ), 0 );

		auto slot_of = [ & ] ( int32_t e ) -> int32_t {

			if ( e < 0 )
				return -1;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind != op::ADDRESS )
				return -1;

			int64_t v = node.a < 0 ? node.value : ( b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( node.a ) ].value : -1 );

			return v >= 0 && v < total ? static_cast < int32_t > ( v ) : -1;

		};

		auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::VAR && node.value >= 0 && node.value < total ) {

				reads [ static_cast < size_t > ( node.value ) ]++;
				accessed [ static_cast < size_t > ( node.value ) ] = 1;

				return;

			}

			if ( node.kind == op::LOAD && slot_of ( node.a ) >= 0 ) {

				reads [ static_cast < size_t > ( slot_of ( node.a ) ) ]++;
				accessed [ static_cast < size_t > ( slot_of ( node.a ) ) ] = 1;

				return;

			}

			if ( node.kind == op::ADDRESS && slot_of ( e ) >= 0 ) {

				addressed [ static_cast < size_t > ( slot_of ( e ) ) ] = 1;

				return;

			}

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				walk ( walk, line.right, 0 );

				if ( line.left < 0 )
					continue;

				const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];

				if ( left.kind == op::VAR && left.value >= 0 && left.value < total )
					accessed [ static_cast < size_t > ( left.value ) ] = 1;
				else if ( left.kind == op::LOAD && slot_of ( left.a ) >= 0 )
					accessed [ static_cast < size_t > ( slot_of ( left.a ) ) ] = 1;
				else
					walk ( walk, line.left, 0 );

			}

			walk ( walk, block.condition, 0 );
			walk ( walk, block.selector, 0 );
			walk ( walk, block.returned, 0 );

		}

		base::list < int32_t > stack;

		for ( int32_t v = 0; v < total; v++ ) {

			if ( b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::STACK && ( accessed [ static_cast < size_t > ( v ) ] || addressed [ static_cast < size_t > ( v ) ] ) )
				stack.push ( v );

		}

		base::sort ( stack.data ( ), stack.count ( ), [ & ] ( int32_t x, int32_t y ) { return b.vars [ static_cast < size_t > ( x ) ].offset < b.vars [ static_cast < size_t > ( y ) ].offset; } );

		for ( int32_t i = 0; i < stack.count ( ); i++ ) {

			int32_t head = stack [ static_cast < size_t > ( i ) ];
			const variable& item = b.vars [ static_cast < size_t > ( head ) ];

			if ( !addressed [ static_cast < size_t > ( head ) ] || item.cookie || item.saved || head == b.va_var || array_of [ static_cast < size_t > ( head ) ] >= 0 || ( head < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( head ) ].empty ( ) ) )
				continue;

			int64_t from_rbp = frame_deepest ? ( b.has_frame ? item.offset - b.frame_at : item.offset ) : item.offset + b.pushed;
			int64_t end = item.offset - from_rbp;

			if ( from_rbp >= 0 )
				continue;

			int32_t last = i;

			for ( int32_t j = i + 1; j < stack.count ( ); j++ ) {

				const variable& other = b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ];

				if ( other.offset >= end )
					break;

				if ( reads [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ] || addressed [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ] || other.offset + other.size > end ) {

					end = other.offset;

					break;

				}

				last = j;

			}

			int64_t span = end - item.offset;

			if ( span <= item.size || span > 0x10000 )
				continue;

			int32_t step = accessed [ static_cast < size_t > ( head ) ] ? item.size : 0;
			bool uniform = true;

			for ( int32_t j = i + 1; j <= last; j++ ) {

				const variable& other = b.vars [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ];

				if ( !step )
					step = other.size;

				if ( other.size != step || ( other.offset - item.offset ) % step != 0 )
					uniform = false;

			}

			if ( !uniform && accessed [ static_cast < size_t > ( head ) ] )
				continue;

			if ( last == i && accessed [ static_cast < size_t > ( head ) ] && item.size == 16 )
				continue;

			if ( head < b.call_names.count ( ) && !b.call_names [ static_cast < size_t > ( head ) ].empty ( ) )
				continue;

			if ( !uniform || !step || ( step != 1 && step != 2 && step != 4 && step != 8 && step != 16 ) || ( ( item.bits & FLOATING ) && step != 16 ) )
				step = 1;

			span -= span % step;

			array_of [ static_cast < size_t > ( head ) ] = head;
			array_step [ static_cast < size_t > ( head ) ] = step;
			array_count [ static_cast < size_t > ( head ) ] = static_cast < int32_t > ( span / step );

			for ( int32_t j = i + 1; j <= last; j++ )
				array_of [ static_cast < size_t > ( stack [ static_cast < size_t > ( j ) ] ) ] = head;

		}

	}

	static void stack_memsets ( builder& b, const base::list < int32_t >& array_of ) {

		if ( !array_memsets )
			return;

		int32_t total = b.vars.count ( );

		auto target_of = [ & ] ( const statement& line, int32_t& var, uint8_t& size ) -> bool {

			if ( ( line.kind != stmt::ASSIGN && line.kind != stmt::STORE ) || line.left < 0 || line.right < 0 )
				return false;

			const expr& right = b.exprs [ static_cast < size_t > ( line.right ) ];

			if ( right.kind != op::CONST || right.value != 0 || ( right.bits & POINTER ) )
				return false;

			const expr& left = b.exprs [ static_cast < size_t > ( line.left ) ];

			if ( left.kind == op::VAR ) {

				var = static_cast < int32_t > ( left.value );
				size = static_cast < uint8_t > ( left.size );

			} else if ( left.kind == op::LOAD && left.a >= 0 && b.exprs [ static_cast < size_t > ( left.a ) ].kind == op::ADDRESS ) {

				const expr& where = b.exprs [ static_cast < size_t > ( left.a ) ];

				var = static_cast < int32_t > ( where.a < 0 ? where.value : ( b.exprs [ static_cast < size_t > ( where.a ) ].kind == op::VAR ? b.exprs [ static_cast < size_t > ( where.a ) ].value : -1 ) );
				size = static_cast < uint8_t > ( left.size );

			} else
				return false;

			return var >= 0 && var < total && var < array_of.count ( ) && array_of [ static_cast < size_t > ( var ) ] >= 0;

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			int32_t head = -1;
			int64_t low = 0;
			int64_t high = 0;
			int32_t first_line = -1;
			int32_t low_var = -1;
			base::list < int32_t > run;

			auto close = [ & ] ( ) {

				if ( run.count ( ) >= 2 && high - low >= 24 && first_line >= 0 ) {

					int32_t place = b.make ( op::ADDRESS, 8, -1, -1, low_var, POINTER );
					int32_t zero = b.make ( op::CONST, 4, -1, -1, 0, 0 );
					int32_t amount = b.make ( op::CONST, 4, -1, -1, high - low, 0 );
					int32_t call = b.make ( op::INTRINSIC, 8, -1, -1, 1, 0 );
					int32_t first = b.args.count ( );

					b.args.push ( place );
					b.args.push ( zero );
					b.args.push ( amount );
					b.exprs [ static_cast < size_t > ( call ) ].first_arg = first;
					b.exprs [ static_cast < size_t > ( call ) ].arg_count = 3;

					for ( int32_t s : run )
						b.statements [ static_cast < size_t > ( s ) ].kind = stmt::NOP;

					statement& kept = b.statements [ static_cast < size_t > ( first_line ) ];

					kept.kind = stmt::EXPR;
					kept.left = -1;
					kept.right = call;

				}

				run.clear ( );
				head = -1;
				first_line = -1;

			};

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				int32_t var = -1;
				uint8_t size = 0;

				if ( !target_of ( line, var, size ) ) {

					close ( );

					continue;

				}

				int32_t owner = array_of [ static_cast < size_t > ( var ) ];
				int64_t from = b.vars [ static_cast < size_t > ( var ) ].offset - b.vars [ static_cast < size_t > ( owner ) ].offset;

				if ( head == owner && from == high ) {

					high += size;
					run.push ( s );

					continue;

				}

				if ( head == owner && from + size == low ) {

					low = from;
					low_var = var;
					run.push ( s );

					continue;

				}

				close ( );
				head = owner;
				low = from;
				high = from + size;
				low_var = var;
				first_line = s;
				run.push ( s );

			}

			close ( );

		}

	}

	static void vector_vars ( builder& b, base::list < uint8_t >& vector ) {

		int32_t total = b.vars.count ( );
		base::list < uint8_t > bad;
		base::list < int32_t > loads;
		base::list < int32_t > stores;

		vector.clear ( );
		vector.fill ( static_cast < size_t > ( total ), 0 );

		if ( !vector_copies )
			return;

		bad.fill ( static_cast < size_t > ( total ), 0 );
		loads.fill ( static_cast < size_t > ( total ), 0 );
		stores.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( item.kind != var_kind::REGISTER || item.size < 8 || !( item.bits & FLOATING ) || item.flags || ( v < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( v ) ].empty ( ) ) )
				bad [ static_cast < size_t > ( v ) ] = 1;

		}

		auto taint = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::VAR && node.value >= 0 && node.value < total )
				bad [ static_cast < size_t > ( node.value ) ] = 1;

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& line = b.statements [ static_cast < size_t > ( s ) ];

				if ( line.kind == stmt::NOP )
					continue;

				const expr* left = line.left >= 0 ? &b.exprs [ static_cast < size_t > ( line.left ) ] : nullptr;
				const expr* right = line.right >= 0 ? &b.exprs [ static_cast < size_t > ( line.right ) ] : nullptr;

				if ( line.kind == stmt::ASSIGN && left && left->kind == op::VAR && left->value >= 0 && left->value < total ) {

					if ( right && right->kind == op::LOAD && right->size == 16 ) {

						loads [ static_cast < size_t > ( left->value ) ]++;
						taint ( taint, right->a, 0 );

					} else {

						bad [ static_cast < size_t > ( left->value ) ] = 1;
						taint ( taint, line.right, 0 );

					}

					continue;

				}

				if ( ( line.kind == stmt::STORE || line.kind == stmt::ASSIGN ) && left && left->kind == op::LOAD && left->size == 16 && right && right->kind == op::VAR && right->value >= 0 && right->value < total ) {

					stores [ static_cast < size_t > ( right->value ) ]++;
					taint ( taint, left->a, 0 );

					continue;

				}

				taint ( taint, line.left, 0 );
				taint ( taint, line.right, 0 );

			}

			taint ( taint, block.condition, 0 );
			taint ( taint, block.selector, 0 );
			taint ( taint, block.returned, 0 );

		}

		for ( int32_t v = 0; v < total; v++ ) {

			if ( !bad [ static_cast < size_t > ( v ) ] && loads [ static_cast < size_t > ( v ) ] > 0 && stores [ static_cast < size_t > ( v ) ] > 0 )
				vector [ static_cast < size_t > ( v ) ] = 1;

		}

	}

	static void boolean_vars ( builder& b, base::list < uint8_t >& boolean ) {

		int32_t total = b.vars.count ( );
		base::list < uint8_t > state;

		boolean.clear ( );
		boolean.fill ( static_cast < size_t > ( total ), 0 );
		state.fill ( static_cast < size_t > ( total ), 0 );

		if ( !detect_bools )
			return;

		auto self_truth = [ & ] ( int32_t e, bool& compared ) -> int32_t {

			if ( e < 0 )
				return 0;

			op kind = b.exprs [ static_cast < size_t > ( e ) ].kind;

			if ( ( kind >= op::EQ && kind <= op::LOR ) || kind == op::LNOT ) {

				compared = true;

				return 1;

			}

			return 0;

		};

		auto truth = [ & ] ( int32_t e, bool& compared ) -> int32_t {

			if ( e < 0 )
				return 0;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			switch ( node.kind ) {

				case op::EQ:
				case op::NE:
				case op::LT:
				case op::LE:
				case op::GT:
				case op::GE:
				case op::LNOT:
				case op::LAND:
				case op::LOR:
					compared = true;
					return 1;

				case op::CONST:
					return node.value == 0 || node.value == 1 ? 1 : 0;

				case op::VAR:
					return node.value >= 0 && node.value < total ? 2 : 0;

				case op::AND:
				case op::OR:
				case op::XOR: {

					bool left = false;
					bool right = false;
					int32_t x = self_truth ( node.a, left );
					int32_t y = self_truth ( node.b, right );

					if ( x == 1 && y == 1 && left && right ) {

						compared = true;

						return 1;

					}

					return 0;

				}

				case op::CAST:
					return node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind >= op::EQ && b.exprs [ static_cast < size_t > ( node.a ) ].kind <= op::LOR ? ( compared = true, 1 ) : 0;

				default:
					return 0;

			}

		};

		base::list < int32_t > copy_from;
		base::list < int32_t > copy_to;

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( item.kind != stmt::ASSIGN || item.left < 0 || b.exprs [ static_cast < size_t > ( item.left ) ].kind != op::VAR )
					continue;

				int64_t v = b.exprs [ static_cast < size_t > ( item.left ) ].value;

				if ( v < 0 || v >= total )
					continue;

				bool compared = false;
				int32_t kind = truth ( item.right, compared );

				if ( kind == 0 )
					state [ static_cast < size_t > ( v ) ] |= 4;
				else if ( kind == 2 ) {

					copy_from.push ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.right ) ].value ) );
					copy_to.push ( static_cast < int32_t > ( v ) );

				}

				if ( compared )
					state [ static_cast < size_t > ( v ) ] |= 1;

			}

		}

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( item.flags || item.kind != var_kind::REGISTER || ( item.bits & FLOATING ) || ( v < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( v ) ].empty ( ) ) || item.cookie || item.family < 0 )
				state [ static_cast < size_t > ( v ) ] |= 4;

		}

		for ( int32_t round = 0; round < 8; round++ ) {

			bool changed = false;

			for ( int32_t i = 0; i < copy_from.count ( ); i++ ) {

				int32_t from = copy_from [ static_cast < size_t > ( i ) ];
				int32_t to = copy_to [ static_cast < size_t > ( i ) ];
				bool from_bool = !b.vars [ static_cast < size_t > ( from ) ].flags ? ( state [ static_cast < size_t > ( from ) ] & 4 ) == 0 : true;

				if ( !from_bool && !( state [ static_cast < size_t > ( to ) ] & 4 ) ) {

					state [ static_cast < size_t > ( to ) ] |= 4;
					changed = true;

				}

				if ( from_bool && b.vars [ static_cast < size_t > ( from ) ].flags && !( state [ static_cast < size_t > ( to ) ] & 1 ) ) {

					state [ static_cast < size_t > ( to ) ] |= 1;
					changed = true;

				}

			}

			if ( !changed )
				break;

		}

		for ( int32_t v = 0; v < total; v++ ) {

			boolean [ static_cast < size_t > ( v ) ] = state [ static_cast < size_t > ( v ) ] == 1 ? 1 : 0;

			if ( boolean [ static_cast < size_t > ( v ) ] )
				b.vars [ static_cast < size_t > ( v ) ].size = 1;

		}

	}

	static bool flow_typing = true;
	static bool flow_seeded_only = true;
	static bool flow_calls = true;
	static bool flow_loads = true;
	static bool flow_index = true;

	static void flow_pointers ( builder& b, base::list < uint8_t >& element ) {

		int32_t total = b.vars.count ( );
		base::list < uint8_t > bad;
		base::list < int32_t > parent;
		base::list < uint8_t > shape;
		base::list < uint8_t > conflict;
		base::list < uint8_t > direct;
		base::list < uint8_t > seeded;
		base::list < int64_t > steps;
		base::list < int32_t > edge_from;
		base::list < int32_t > edge_to;

		bad.fill ( static_cast < size_t > ( total ), 0 );
		parent.fill ( static_cast < size_t > ( total ), 0 );
		shape.fill ( static_cast < size_t > ( total ), 0 );
		conflict.fill ( static_cast < size_t > ( total ), 0 );
		direct.fill ( static_cast < size_t > ( total ), 0 );
		seeded.fill ( static_cast < size_t > ( total ), 0 );

		base::list < uint8_t > stepped;
		base::list < uint8_t > params_in;
		base::list < uint8_t > called;
		base::list < uint8_t > loaded_in;

		called.fill ( static_cast < size_t > ( total ), 0 );
		loaded_in.fill ( static_cast < size_t > ( total ), 0 );

		stepped.fill ( static_cast < size_t > ( total ), 0 );
		params_in.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];

			parent [ static_cast < size_t > ( v ) ] = v;

			if ( item.size != 8 || ( item.bits & FLOATING ) || item.flags || item.cookie || item.kind == var_kind::STACK || ( v < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( v ) ].empty ( ) ) || ( v < b.var_structs.count ( ) && !b.var_structs [ static_cast < size_t > ( v ) ].empty ( ) ) )
				bad [ static_cast < size_t > ( v ) ] = 1;

		}

		auto var_of = [ & ] ( int32_t e ) -> int32_t {

			if ( e < 0 || b.exprs [ static_cast < size_t > ( e ) ].kind != op::VAR )
				return -1;

			int64_t v = b.exprs [ static_cast < size_t > ( e ) ].value;

			return v >= 0 && v < total && b.exprs [ static_cast < size_t > ( e ) ].size == 8 ? static_cast < int32_t > ( v ) : -1;

		};

		auto constant_of = [ & ] ( int32_t e, int64_t& out ) {

			if ( e < 0 || b.exprs [ static_cast < size_t > ( e ) ].kind != op::CONST || ( b.exprs [ static_cast < size_t > ( e ) ].bits & POINTER ) )
				return false;

			out = b.exprs [ static_cast < size_t > ( e ) ].value;

			return true;

		};

		auto exprs_mul = [ & ] ( int32_t e, int64_t& out ) {

			return e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::MUL && constant_of ( b.exprs [ static_cast < size_t > ( e ) ].b, out );

		};

		struct access {

			int32_t var;
			int64_t offset;
			uint8_t form;
			bool indexed;

		};

		base::list < access > accesses;

		auto deref = [ & ] ( int32_t v, int64_t offset, uint8_t form, bool indexed ) {

			access a;

			a.var = v;
			a.offset = offset;
			a.form = form;
			a.indexed = indexed;
			accesses.push ( a );

		};

		auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];
			int64_t k = 0;

			if ( node.kind == op::VAR ) {

				if ( node.value >= 0 && node.value < total )
					bad [ static_cast < size_t > ( node.value ) ] = 1;

				return;

			}

			if ( node.kind == op::LOAD && node.a >= 0 ) {

				const expr& where = b.exprs [ static_cast < size_t > ( node.a ) ];
				uint8_t form = static_cast < uint8_t > ( node.size | ( ( node.bits & FLOATING ) ? 0x80 : 0 ) );

				if ( var_of ( node.a ) >= 0 ) {

					deref ( var_of ( node.a ), 0, form, false );

					return;

				}

				if ( where.kind == op::ADD && var_of ( where.a ) >= 0 && constant_of ( where.b, k ) ) {

					deref ( var_of ( where.a ), k, form, false );

					return;

				}

				if ( where.kind == op::ADD && var_of ( where.a ) >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::MUL && constant_of ( b.exprs [ static_cast < size_t > ( where.b ) ].b, k ) && k == node.size ) {

					deref ( var_of ( where.a ), 0, form, true );
					self ( self, b.exprs [ static_cast < size_t > ( where.b ) ].a, depth + 1 );

					return;

				}

			}

			if ( node.kind == op::ADD && var_of ( node.a ) >= 0 && constant_of ( node.b, k ) ) {

				steps.push ( k );
				edge_from.push ( var_of ( node.a ) );
				edge_to.push ( -1 );

				return;

			}

			if ( flow_index && node.kind == op::ADD && var_of ( node.a ) >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::MUL && constant_of ( b.exprs [ static_cast < size_t > ( node.b ) ].b, k ) && k > 1 ) {

				steps.push ( k );
				edge_from.push ( var_of ( node.a ) );
				edge_to.push ( -1 );
				self ( self, b.exprs [ static_cast < size_t > ( node.b ) ].a, depth + 1 );

				return;

			}

			if ( ( node.kind == op::EQ || node.kind == op::NE || node.kind == op::LT || node.kind == op::LE || node.kind == op::GT || node.kind == op::GE ) && ( var_of ( node.a ) >= 0 || var_of ( node.b ) >= 0 ) ) {

				bool left_ok = var_of ( node.a ) >= 0;
				bool right_ok = var_of ( node.b ) >= 0 || ( constant_of ( node.b, k ) && k == 0 );

				if ( left_ok && right_ok )
					return;

			}

			if ( node.kind == op::LNOT && var_of ( node.a ) >= 0 )
				return;

			if ( node.kind == op::CALL ) {

				uint64_t target = static_cast < uint64_t > ( node.value );
				bool api = false;
				wchar_t callee [ 200 ];

				if ( target && b.names && b.names->name ( target, true, callee, 200 ) && api_prototype ( callee ) )
					api = true;

				if ( !node.value || ( target && b.names && b.names->prototype ( target ) ) )
					api = true;

				if ( !node.value )
					self ( self, node.a, depth + 1 );

				for ( int32_t i = 0; i < node.arg_count; i++ ) {

					int32_t passed = b.args [ static_cast < size_t > ( node.first_arg + i ) ];
					int32_t v = var_of ( passed );

					if ( v < 0 || api ) {

						self ( self, passed, depth + 1 );

						continue;

					}

					if ( i < 4 && b.db && b.long_mode ) {

						uint8_t inner = callee_pointer_shape ( *b.db, b.names, target, i, 0 );

						if ( inner )
							deref ( v, 0, static_cast < uint8_t > ( inner & 0x9F ), false );

					}

				}

				return;

			}

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( item.kind == stmt::NOP || item.kind == stmt::ASM )
					continue;

				int32_t x = item.kind == stmt::ASSIGN ? var_of ( item.left ) : -1;

				if ( item.kind == stmt::ASSIGN && item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::VAR ) {

					int32_t y = var_of ( item.right );
					const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];
					int64_t k = 0;

					if ( x >= 0 && y >= 0 ) {

						steps.push ( 0 );
						edge_from.push ( y );
						edge_to.push ( x );

						continue;

					}

					if ( x >= 0 && right.kind == op::ADD && var_of ( right.a ) >= 0 && constant_of ( right.b, k ) ) {

						steps.push ( k );
						edge_from.push ( var_of ( right.a ) );
						edge_to.push ( x );

						continue;

					}

					if ( flow_index && x >= 0 && right.kind == op::ADD && var_of ( right.a ) >= 0 && right.b >= 0 && exprs_mul ( right.b, k ) && k > 1 ) {

						steps.push ( k );
						edge_from.push ( var_of ( right.a ) );
						edge_to.push ( x );
						walk ( walk, b.exprs [ static_cast < size_t > ( right.b ) ].a, 0 );

						continue;

					}

					if ( y >= 0 )
						continue;

					if ( right.kind == op::ADD && var_of ( right.a ) >= 0 && constant_of ( right.b, k ) ) {

						steps.push ( k );
						edge_from.push ( var_of ( right.a ) );
						edge_to.push ( -1 );

						continue;

					}

					if ( flow_loads && x >= 0 && right.kind == op::LOAD && right.size == 8 && !( right.bits & FLOATING ) && !called [ static_cast < size_t > ( x ) ] )
						called [ static_cast < size_t > ( x ) ] = 2;

					if ( x >= 0 && right.kind == op::CALL && right.value && b.names && !b.names->prototype ( static_cast < uint64_t > ( right.value ) ) ) {

						wchar_t callee [ 200 ];

						if ( !( b.names->name ( static_cast < uint64_t > ( right.value ), true, callee, 200 ) && api_prototype ( callee ) ) )
							called [ static_cast < size_t > ( x ) ] = 1;

					}

					walk ( walk, item.right, 0 );

					continue;

				}

				if ( item.kind == stmt::STORE || item.kind == stmt::ASSIGN ) {

					if ( item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::LOAD )
						walk ( walk, item.left, 0 );
					else if ( item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind != op::GLOBAL )
						walk ( walk, item.left, 0 );

					int64_t k = 0;
					const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

					if ( var_of ( item.right ) >= 0 )
						continue;

					if ( right.kind == op::ADD && var_of ( right.a ) >= 0 && constant_of ( right.b, k ) ) {

						steps.push ( k );
						edge_from.push ( var_of ( right.a ) );
						edge_to.push ( -1 );

						continue;

					}

					walk ( walk, item.right, 0 );

					continue;

				}

				walk ( walk, item.left, 0 );
				walk ( walk, item.right, 0 );

			}

			walk ( walk, block.condition, 0 );
			walk ( walk, block.selector, 0 );

			if ( block.end == term::TAIL )
				walk ( walk, block.returned, 0 );
			else if ( b.returns_value && block.returned >= 0 && var_of ( block.returned ) < 0 )
				walk ( walk, block.returned, 0 );

		}

		auto find = [ & ] ( int32_t v ) {

			while ( parent [ static_cast < size_t > ( v ) ] != v ) {

				parent [ static_cast < size_t > ( v ) ] = parent [ static_cast < size_t > ( parent [ static_cast < size_t > ( v ) ] ) ];
				v = parent [ static_cast < size_t > ( v ) ];

			}

			return v;

		};

		for ( int32_t i = 0; i < edge_from.count ( ); i++ ) {

			int32_t from = edge_from [ static_cast < size_t > ( i ) ];
			int32_t to = edge_to [ static_cast < size_t > ( i ) ];

			if ( to < 0 || bad [ static_cast < size_t > ( from ) ] || bad [ static_cast < size_t > ( to ) ] )
				continue;

			int32_t x = find ( from );
			int32_t y = find ( to );

			if ( x != y )
				parent [ static_cast < size_t > ( y ) ] = x;

		}

		for ( int32_t v = 0; v < total; v++ ) {

			if ( !bad [ static_cast < size_t > ( v ) ] && b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER && ( !flow_seeded_only || element [ static_cast < size_t > ( v ) ] ) )
				seeded [ static_cast < size_t > ( find ( v ) ) ] = 1;

			if ( !bad [ static_cast < size_t > ( v ) ] && b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER )
				params_in [ static_cast < size_t > ( find ( v ) ) ] = 1;

			if ( flow_calls && !bad [ static_cast < size_t > ( v ) ] && called [ static_cast < size_t > ( v ) ] == 1 )
				seeded [ static_cast < size_t > ( find ( v ) ) ] = 1;

			if ( !bad [ static_cast < size_t > ( v ) ] && called [ static_cast < size_t > ( v ) ] == 2 )
				loaded_in [ static_cast < size_t > ( find ( v ) ) ] = 1;

		}

		for ( const access& a : accesses ) {

			if ( bad [ static_cast < size_t > ( a.var ) ] )
				continue;

			int32_t root = find ( a.var );
			uint8_t size = a.form & 0x1F;
			uint8_t& slot = shape [ static_cast < size_t > ( root ) ];

			if ( ( size != 1 && size != 2 && size != 4 && size != 8 ) || a.offset % size != 0 || ( slot && slot != a.form ) ) {

				conflict [ static_cast < size_t > ( root ) ] = 1;

				continue;

			}

			slot = a.form;

			if ( a.offset == 0 || a.indexed )
				direct [ static_cast < size_t > ( root ) ] = 1;

		}

		for ( int32_t i = 0; i < edge_from.count ( ); i++ ) {

			int32_t from = edge_from [ static_cast < size_t > ( i ) ];

			if ( bad [ static_cast < size_t > ( from ) ] )
				continue;

			int32_t root = find ( from );
			uint8_t size = shape [ static_cast < size_t > ( root ) ] & 0x1F;

			if ( size && steps [ static_cast < size_t > ( i ) ] % size != 0 )
				conflict [ static_cast < size_t > ( root ) ] = 1;

			if ( steps [ static_cast < size_t > ( i ) ] != 0 )
				stepped [ static_cast < size_t > ( root ) ] = 1;

		}

		for ( int32_t v = 0; v < total; v++ ) {

			if ( bad [ static_cast < size_t > ( v ) ] )
				continue;

			int32_t root = find ( v );
			uint8_t form = shape [ static_cast < size_t > ( root ) ];

			if ( !form || conflict [ static_cast < size_t > ( root ) ] || !direct [ static_cast < size_t > ( root ) ] || !( seeded [ static_cast < size_t > ( root ) ] || ( stepped [ static_cast < size_t > ( root ) ] && params_in [ static_cast < size_t > ( root ) ] && ( form & 0x1F ) < 8 ) || ( loaded_in [ static_cast < size_t > ( root ) ] && ( form & 0x1F ) < 8 ) ) )
				continue;

			uint8_t& slot = element [ static_cast < size_t > ( v ) ];

			if ( !slot )
				slot = form;
			else if ( ( slot & 0x9F ) != form )
				continue;

		}

	}

	static void flavor_elements ( builder& b, base::list < uint8_t >& element ) {

		int32_t total = b.vars.count ( );
		base::list < int32_t > source;
		base::list < uint8_t > mixed;
		base::list < uint8_t > based;
		base::list < uint8_t > void_call;
		base::list < uint8_t > wide_access;

		wide_access.fill ( static_cast < size_t > ( total ), 0 );
		source.fill ( static_cast < size_t > ( total ), -1 );
		mixed.fill ( static_cast < size_t > ( total ), 0 );
		based.fill ( static_cast < size_t > ( total ), 0 );
		void_call.fill ( static_cast < size_t > ( total ), 0 );

		auto owner = [ & ] ( int32_t e ) -> int32_t {

			if ( e < 0 )
				return -1;

			const expr& load = b.exprs [ static_cast < size_t > ( e ) ];

			if ( load.kind != op::LOAD || load.size != 8 || ( load.bits & FLOATING ) || load.a < 0 )
				return -1;

			const expr& where = b.exprs [ static_cast < size_t > ( load.a ) ];
			int32_t base_expr = where.kind == op::ADD && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ? where.a : load.a;

			if ( base_expr < 0 || b.exprs [ static_cast < size_t > ( base_expr ) ].kind != op::VAR )
				return -1;

			int32_t v = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( base_expr ) ].value );

			if ( v < 0 || v >= total || ( element [ static_cast < size_t > ( v ) ] & 0x9F ) != 8 )
				return -1;

			return v;

		};

		auto loaded_from = [ & ] ( int32_t e ) -> int32_t {

			int32_t v = owner ( e );

			if ( v >= 0 )
				return v;

			if ( e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::VAR ) {

				int64_t x = b.exprs [ static_cast < size_t > ( e ) ].value;

				if ( x >= 0 && x < total )
					return source [ static_cast < size_t > ( x ) ];

			}

			return -1;

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( item.kind == stmt::ASSIGN && item.left >= 0 && b.exprs [ static_cast < size_t > ( item.left ) ].kind == op::VAR ) {

					int64_t x = b.exprs [ static_cast < size_t > ( item.left ) ].value;
					int32_t v = owner ( item.right );

					if ( x >= 0 && x < total && v >= 0 )
						source [ static_cast < size_t > ( x ) ] = v;

					const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];

					if ( x >= 0 && x < total && v < 0 && char_calls && right.kind == op::CALL && right.value && callee_void_result ( *b.db, *b.file, b.names, static_cast < uint64_t > ( right.value ), 0 ) )
						void_call [ static_cast < size_t > ( x ) ] = 1;
					else if ( x >= 0 && x < total && v < 0 )
						mixed [ static_cast < size_t > ( x ) ] = 1;

				}

			}

		}

		auto mark = [ & ] ( int32_t v, uint8_t flag ) {

			if ( v >= 0 && v < total && element [ static_cast < size_t > ( v ) ] )
				element [ static_cast < size_t > ( v ) ] |= flag;

		};

		auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( ( node.kind == op::ADD || node.kind == op::SUB || node.kind == op::MUL ) && !( node.bits & FLOATING ) ) {

				mark ( loaded_from ( node.a ), 0x20 );
				mark ( loaded_from ( node.b ), 0x20 );

			}

			if ( node.kind == op::LOAD && node.a >= 0 && b.exprs [ static_cast < size_t > ( node.a ) ].kind == op::ADD ) {

				int32_t at = node.a;

				while ( at >= 0 && b.exprs [ static_cast < size_t > ( at ) ].kind == op::ADD )
					at = b.exprs [ static_cast < size_t > ( at ) ].a;

				if ( at >= 0 && b.exprs [ static_cast < size_t > ( at ) ].kind == op::VAR ) {

					int64_t x = b.exprs [ static_cast < size_t > ( at ) ].value;
					const expr& sum = b.exprs [ static_cast < size_t > ( node.a ) ];
					bool variable_index = sum.b >= 0 && b.exprs [ static_cast < size_t > ( sum.b ) ].kind != op::CONST;

					if ( x >= 0 && x < total && ( variable_index || b.exprs [ static_cast < size_t > ( sum.a ) ].kind == op::ADD ) ) {

						based [ static_cast < size_t > ( x ) ] = 1;
						wide_access [ static_cast < size_t > ( x ) ] |= node.size != 1 || ( node.bits & FLOATING ) ? 1 : 0;

					}

				}

			}

			if ( node.kind == op::CALL && node.value ) {

				for ( int32_t i = 0; i < node.arg_count && i < 4; i++ ) {

					int32_t passed = b.args [ static_cast < size_t > ( node.first_arg + i ) ];
					int32_t v = loaded_from ( passed );

					if ( v < 0 )
						continue;

					wchar_t type [ 64 ];

					if ( callee_parameter_type ( *b.db, *b.file, b.names, static_cast < uint64_t > ( node.value ), i, type, 64 ) && ( base::same_text ( type, L"void *" ) || base::same_text ( type, L"const void *" ) ) )
						mark ( v, 0x40 );

				}

			}

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( item.kind == stmt::NOP || item.kind == stmt::ASM )
					continue;

				walk ( walk, item.left, 0 );
				walk ( walk, item.right, 0 );

			}

			walk ( walk, block.condition, 0 );
			walk ( walk, block.selector, 0 );

			if ( b.returns_value || block.end == term::TAIL )
				walk ( walk, block.returned, 0 );

		}

		if ( !char_locals )
			return;

		for ( int32_t x = 0; x < total; x++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( x ) ];
			int32_t from = source [ static_cast < size_t > ( x ) ];

			if ( item.kind != var_kind::REGISTER || item.size != 8 || item.flags || ( item.bits & FLOATING ) || mixed [ static_cast < size_t > ( x ) ] || !based [ static_cast < size_t > ( x ) ] || element [ static_cast < size_t > ( x ) ] )
				continue;

			if ( from < 0 && void_call [ static_cast < size_t > ( x ) ] && ( x >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( x ) ].empty ( ) ) ) {

				element [ static_cast < size_t > ( x ) ] = wide_access [ static_cast < size_t > ( x ) ] ? 0x10 : 0x01;

				continue;

			}

			if ( from < 0 )
				continue;

			if ( ( element [ static_cast < size_t > ( from ) ] & 0x40 ) && ( x >= b.var_types.count ( ) || b.var_types [ static_cast < size_t > ( x ) ].empty ( ) ) )
				element [ static_cast < size_t > ( x ) ] = byte_elements && !wide_access [ static_cast < size_t > ( x ) ] ? 0x01 : 0x10;

		}

		if ( !void_needs_char )
			return;

		base::list < uint8_t > keep;

		keep.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t x = 0; x < total; x++ ) {

			int32_t from = source [ static_cast < size_t > ( x ) ];

			if ( from >= 0 && element [ static_cast < size_t > ( x ) ] == 0x10 )
				keep [ static_cast < size_t > ( from ) ] = 1;

		}

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( ( item.kind != stmt::STORE && item.kind != stmt::ASSIGN ) || item.left < 0 || item.right < 0 )
					continue;

				const expr& target = b.exprs [ static_cast < size_t > ( item.left ) ];
				const expr& stored = b.exprs [ static_cast < size_t > ( item.right ) ];

				if ( target.kind != op::LOAD || target.size != 8 || stored.kind != op::VAR || stored.value < 0 || stored.value >= total || element [ static_cast < size_t > ( stored.value ) ] != 0x10 )
					continue;

				int32_t at = target.a;

				if ( at >= 0 && b.exprs [ static_cast < size_t > ( at ) ].kind == op::ADD && b.exprs [ static_cast < size_t > ( at ) ].b >= 0 && b.exprs [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( at ) ].b ) ].kind == op::CONST )
					at = b.exprs [ static_cast < size_t > ( at ) ].a;

				if ( at >= 0 && b.exprs [ static_cast < size_t > ( at ) ].kind == op::VAR && b.exprs [ static_cast < size_t > ( at ) ].value >= 0 && b.exprs [ static_cast < size_t > ( at ) ].value < total )
					keep [ static_cast < size_t > ( b.exprs [ static_cast < size_t > ( at ) ].value ) ] = 1;

			}

		}

		for ( int32_t v = 0; v < total; v++ ) {

			uint8_t shape = element [ static_cast < size_t > ( v ) ];

			if ( ( shape & 0x9F ) == 8 && ( shape & 0x40 ) && !keep [ static_cast < size_t > ( v ) ] && ( b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER || !void_needs_char_params ) )
				element [ static_cast < size_t > ( v ) ] = 0;

		}

	}

	static void typed_pointers ( builder& b, base::list < uint8_t >& element ) {

		int32_t total = b.vars.count ( );
		base::list < uint8_t > bad;

		element.clear ( );
		element.fill ( static_cast < size_t > ( total ), 0 );
		bad.fill ( static_cast < size_t > ( total ), 0 );

		for ( int32_t v = 0; v < total; v++ ) {

			const variable& item = b.vars [ static_cast < size_t > ( v ) ];

			if ( ( item.kind != var_kind::PARAMETER && !( typed_locals && item.kind == var_kind::REGISTER && !item.flags && !item.cookie ) ) || item.size != 8 || ( item.bits & FLOATING ) || ( v < b.var_types.count ( ) && !b.var_types [ static_cast < size_t > ( v ) ].empty ( ) ) )
				bad [ static_cast < size_t > ( v ) ] = 1;

		}

		base::list < uint8_t > direct;
		base::list < uint8_t > passed_on;

		direct.fill ( static_cast < size_t > ( total ), 0 );
		passed_on.fill ( static_cast < size_t > ( total ), 0 );

		auto record = [ & ] ( int32_t v, const expr& load, int64_t offset ) {

			if ( v < 0 || v >= total || bad [ static_cast < size_t > ( v ) ] )
				return;

			if ( offset == 0 )
				direct [ static_cast < size_t > ( v ) ] = 1;

			uint8_t size = load.size;

			if ( size != 1 && size != 2 && size != 4 && size != 8 ) {

				bad [ static_cast < size_t > ( v ) ] = 1;

				return;

			}

			uint8_t shape = static_cast < uint8_t > ( size | ( ( load.bits & FLOATING ) ? 0x80 : 0 ) );
			uint8_t& slot = element [ static_cast < size_t > ( v ) ];

			if ( offset % size != 0 || offset < 0 || ( slot && ( slot & 0x9F ) != ( shape & 0x9F ) ) ) {

				bad [ static_cast < size_t > ( v ) ] = 1;

				return;

			}

			slot = static_cast < uint8_t > ( slot | shape );

		};

		auto var_of = [ & ] ( int32_t e ) {

			return e >= 0 && b.exprs [ static_cast < size_t > ( e ) ].kind == op::VAR ? static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( e ) ].value ) : -1;

		};

		auto walk = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 96 )
				return;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::VAR ) {

				if ( node.value >= 0 && node.value < total )
					bad [ static_cast < size_t > ( node.value ) ] = 1;

				return;

			}

			if ( node.kind == op::LOAD && node.a >= 0 ) {

				const expr& where = b.exprs [ static_cast < size_t > ( node.a ) ];

				if ( where.kind == op::VAR ) {

					record ( static_cast < int32_t > ( where.value ), node, 0 );

					return;

				}

				if ( where.kind == op::ADD && var_of ( where.a ) >= 0 && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ) {

					record ( var_of ( where.a ), node, b.exprs [ static_cast < size_t > ( where.b ) ].value );

					return;

				}

			}

			if ( ( node.kind == op::EQ || node.kind == op::NE ) && var_of ( node.a ) >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( node.b ) ].value == 0 )
				return;

			if ( pass_typed && node.kind == op::CALL && node.value && !b.names->prototype ( static_cast < uint64_t > ( node.value ) ) ) {

				wchar_t callee [ 200 ];

				if ( !( b.names->name ( static_cast < uint64_t > ( node.value ), true, callee, 200 ) && api_prototype ( callee ) ) ) {

					self ( self, node.a, depth + 1 );

					for ( int32_t i = 0; i < node.arg_count; i++ ) {

						int32_t passed = b.args [ static_cast < size_t > ( node.first_arg + i ) ];

						if ( var_of ( passed ) < 0 )
							self ( self, passed, depth + 1 );
						else if ( callee_shapes && i < 4 && b.long_mode && b.db ) {

							uint8_t shape = callee_pointer_shape ( *b.db, b.names, static_cast < uint64_t > ( node.value ), i, 0 );

							if ( shape ) {

								expr probe;

								probe.size = shape & 0x1F;
								probe.bits = ( shape & 0x80 ) ? FLOATING : 0;
								record ( var_of ( passed ), probe, 0 );

								if ( ( shape & 0x40 ) && var_of ( passed ) < total && element [ static_cast < size_t > ( var_of ( passed ) ) ] )
									element [ static_cast < size_t > ( var_of ( passed ) ) ] |= 0x40;

								if ( ( shape & 0x1F ) == 8 && var_of ( passed ) < total )
									passed_on [ static_cast < size_t > ( var_of ( passed ) ) ] = 1;

							}

						}

					}

					return;

				}

			}

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( item.kind == stmt::NOP || item.kind == stmt::ASM )
					continue;

				if ( !( typed_locals && item.kind == stmt::ASSIGN && var_of ( item.left ) >= 0 ) )
					walk ( walk, item.left, 0 );

				if ( typed_locals && item.kind == stmt::ASSIGN && var_of ( item.left ) >= 0 && var_of ( item.right ) >= 0 && b.vars [ static_cast < size_t > ( var_of ( item.left ) ) ].kind != var_kind::PARAMETER )
					continue;

				walk ( walk, item.right, 0 );

			}

			walk ( walk, block.condition, 0 );
			walk ( walk, block.selector, 0 );

			if ( b.returns_value || block.end == term::TAIL )
				walk ( walk, block.returned, 0 );

		}

		for ( int32_t v = 0; v < total; v++ ) {

			if ( bad [ static_cast < size_t > ( v ) ] || !direct [ static_cast < size_t > ( v ) ] )
				element [ static_cast < size_t > ( v ) ] = 0;

		}

		if ( typed_locals ) {

			base::list < uint8_t > copied;

			copied.fill ( static_cast < size_t > ( total ), 0 );

			for ( const block_code& block : b.code ) {

				if ( !block.reachable )
					continue;

				for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

					const statement& item = b.statements [ static_cast < size_t > ( s ) ];
					int32_t v = item.kind == stmt::ASSIGN ? var_of ( item.left ) : -1;

					if ( v < 0 || v >= total || b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER )
						continue;

					int32_t from = var_of ( item.right );

					if ( from < 0 || from >= total || b.vars [ static_cast < size_t > ( from ) ].kind != var_kind::PARAMETER || !element [ static_cast < size_t > ( from ) ] || ( copied [ static_cast < size_t > ( v ) ] && copied [ static_cast < size_t > ( v ) ] != element [ static_cast < size_t > ( from ) ] ) )
						copied [ static_cast < size_t > ( v ) ] = 0xFF;
					else if ( !copied [ static_cast < size_t > ( v ) ] )
						copied [ static_cast < size_t > ( v ) ] = element [ static_cast < size_t > ( from ) ];

				}

			}

			for ( int32_t v = 0; v < total; v++ ) {

				if ( b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER || !element [ static_cast < size_t > ( v ) ] )
					continue;

				uint8_t from = copied [ static_cast < size_t > ( v ) ];

				element [ static_cast < size_t > ( v ) ] = from != 0xFF && ( from & 0x9F ) == ( element [ static_cast < size_t > ( v ) ] & 0x9F ) ? from : 0;

			}

		}

		if ( flow_typing )
			flow_pointers ( b, element );

		if ( element_flavors )
			flavor_elements ( b, element );

		if ( untype_passed ) {

			for ( int32_t v = 0; v < total; v++ ) {

				uint8_t& slot = element [ static_cast < size_t > ( v ) ];

				if ( passed_on [ static_cast < size_t > ( v ) ] && ( slot & 0x9F ) == 8 && ( slot & 0x60 ) )
					slot = 0;

			}

		}

	}

	static bool unique_name ( const base::list < base::text >& taken, const wchar_t* base_name, wchar_t* out ) {

		int32_t length = 0;

		while ( base_name [ length ] && length < 70 ) {

			out [ length ] = base_name [ length ];
			length++;

		}

		out [ length ] = 0;

		for ( int32_t suffix = -1; suffix < 26; suffix++ ) {

			if ( suffix >= 0 ) {

				out [ length ] = static_cast < wchar_t > ( L'a' + suffix );
				out [ length + 1 ] = 0;

			}

			bool clash = false;

			for ( const base::text& other : taken )
				clash = clash || base::same_text ( other.get ( ), out );

			if ( !clash )
				return true;

		}

		return false;

	}

	static bool last_call_names = true;

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

			wchar_t chosen [ 80 ];

			if ( !unique_name ( taken, rest, chosen ) )
				continue;

			taken.push ( base::text ( chosen ) );
			b.call_names [ static_cast < size_t > ( v ) ].set ( chosen );

		}

		for ( const statement& item : b.statements ) {

			if ( item.kind != stmt::ASSIGN || item.left < 0 || item.right < 0 || b.exprs [ static_cast < size_t > ( item.left ) ].kind != op::VAR )
				continue;

			const expr& right = b.exprs [ static_cast < size_t > ( item.right ) ];
			int32_t v = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( item.left ) ].value );

			if ( right.kind != op::LOAD || right.a < 0 || v < 0 || v >= b.vars.count ( ) || v == result_var || !b.call_names [ static_cast < size_t > ( v ) ].empty ( ) )
				continue;

			const expr& where = b.exprs [ static_cast < size_t > ( right.a ) ];
			int32_t base = where.kind == op::ADD ? where.a : right.a;
			int64_t field = where.kind == op::ADD && where.b >= 0 && b.exprs [ static_cast < size_t > ( where.b ) ].kind == op::CONST ? b.exprs [ static_cast < size_t > ( where.b ) ].value : 0;
			const expr& root = b.exprs [ static_cast < size_t > ( base ) ];

			if ( root.kind != op::INTRINSIC || root.arg_count != 0 || !base::same_text ( intrinsic_name ( static_cast < int32_t > ( root.value ) ), L"NtCurrentTeb" ) || field < 0 || field % 8 || field / 8 >= 14 )
				continue;

			static const wchar_t* const short_fields [ 14 ] = { L"ExceptionList", L"StackBase", L"StackLimit", L"SubSystemTib", L"FiberData", L"ArbitraryUserPointer", L"Self", L"EnvironmentPointer", L"UniqueProcess", L"UniqueThread", L"ActiveRpcHandle", L"ThreadLocalStoragePointer", L"ProcessEnvironmentBlock", L"LastErrorValue" };
			wchar_t chosen [ 80 ];

			if ( !unique_name ( taken, short_fields [ field / 8 ], chosen ) )
				continue;

			taken.push ( base::text ( chosen ) );
			b.call_names [ static_cast < size_t > ( v ) ].set ( chosen );

		}

		base::list < int32_t > calls;

		auto gather = [ & ] ( auto& self, int32_t e, int32_t depth ) -> void {

			if ( e < 0 || depth > 64 )
				return;

			const expr& node = b.exprs [ static_cast < size_t > ( e ) ];

			if ( node.kind == op::CALL )
				calls.push ( e );

			self ( self, node.a, depth + 1 );
			self ( self, node.b, depth + 1 );
			self ( self, node.c, depth + 1 );

			for ( int32_t i = 0; i < node.arg_count; i++ )
				self ( self, b.args [ static_cast < size_t > ( node.first_arg + i ) ], depth + 1 );

		};

		for ( const block_code& block : b.code ) {

			if ( !block.reachable )
				continue;

			for ( int32_t s = block.first; s < block.first + block.count && s < b.statements.count ( ); s++ ) {

				const statement& item = b.statements [ static_cast < size_t > ( s ) ];

				if ( item.kind == stmt::NOP || item.kind == stmt::ASM )
					continue;

				gather ( gather, item.right, 0 );

				if ( item.kind == stmt::STORE )
					gather ( gather, item.left, 0 );

			}

			gather ( gather, block.condition, 0 );
			gather ( gather, block.selector, 0 );
			gather ( gather, block.returned, 0 );

		}

		for ( int32_t order = 0; order < calls.count ( ) * 2; order++ ) {

			bool backward = order < calls.count ( );

			if ( backward && !last_call_names )
				continue;

			int32_t call = calls [ static_cast < size_t > ( backward ? calls.count ( ) - 1 - order : order - calls.count ( ) ) ];
			const expr node = b.exprs [ static_cast < size_t > ( call ) ];

			if ( !node.value || !node.arg_count )
				continue;

			const wchar_t* text = b.names->prototype ( static_cast < uint64_t > ( node.value ) );
			wchar_t callee [ 200 ];

			if ( !text && b.names->name ( static_cast < uint64_t > ( node.value ), true, callee, 200 ) )
				text = api_prototype ( callee );

			if ( !text )
				continue;

			c_prototype shape;
			base::writer problem;

			if ( !parse_prototype ( text, b.long_mode, shape, problem ) )
				continue;

			for ( int32_t i = 0; i < node.arg_count && i < shape.count; i++ ) {

				int32_t arg = b.args [ static_cast < size_t > ( node.first_arg + i ) ];
				bool address = false;

				while ( arg >= 0 && b.exprs [ static_cast < size_t > ( arg ) ].kind == op::CAST )
					arg = b.exprs [ static_cast < size_t > ( arg ) ].a;

				if ( arg < 0 )
					continue;

				const expr& item = b.exprs [ static_cast < size_t > ( arg ) ];
				int32_t v = -1;

				if ( item.kind == op::VAR )
					v = static_cast < int32_t > ( item.value );
				else if ( item.kind == op::ADDRESS && item.a < 0 ) {

					v = static_cast < int32_t > ( item.value );
					address = true;

				}

				if ( v < 0 || v >= b.vars.count ( ) || v == result_var || !b.call_names [ static_cast < size_t > ( v ) ].empty ( ) )
					continue;

				if ( backward && address )
					continue;

				bool only_constants = b.vars [ static_cast < size_t > ( v ) ].kind != var_kind::PARAMETER;
				bool any_write = false;
				bool call_result = false;

				for ( const statement& line : b.statements ) {

					if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( line.left ) ].value != v )
						continue;

					any_write = true;

					if ( b.exprs [ static_cast < size_t > ( line.right ) ].kind != op::CONST )
						only_constants = false;

					int32_t source = line.right;

					while ( source >= 0 && b.exprs [ static_cast < size_t > ( source ) ].kind == op::CAST )
						source = b.exprs [ static_cast < size_t > ( source ) ].a;

					if ( source >= 0 && b.exprs [ static_cast < size_t > ( source ) ].kind == op::CALL )
						call_result = true;

				}

				if ( only_constants && any_write && !address )
					continue;

				if ( call_result && !address && unnamed_results )
					continue;

				const wchar_t* rest = shape.names [ i ];

				if ( !rest [ 0 ] )
					continue;

				if ( address ) {

					if ( rest [ 0 ] == L'l' && rest [ 1 ] == L'p' && rest [ 2 ] >= L'A' && rest [ 2 ] <= L'Z' )
						rest += 2;
					else if ( rest [ 0 ] == L'p' && rest [ 1 ] >= L'A' && rest [ 1 ] <= L'Z' )
						rest += 1;

				}

				if ( !base::is_identifier ( rest ) )
					continue;

				wchar_t chosen [ 80 ];

				if ( !unique_name ( taken, rest, chosen ) )
					continue;

				taken.push ( base::text ( chosen ) );
				b.call_names [ static_cast < size_t > ( v ) ].set ( chosen );

			}

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

		if ( b.va_var >= 0 && b.va_var < b.call_names.count ( ) )
			b.call_names [ static_cast < size_t > ( b.va_var ) ].set ( L"va" );
		typed_pointers ( b, body_emitter.element );
		boolean_vars ( b, body_emitter.boolean );
		vector_vars ( b, body_emitter.vector );
		stack_arrays ( b, body_emitter.array_of, body_emitter.array_step, body_emitter.array_count );
		stack_memsets ( b, body_emitter.array_of );
		body_emitter.plan_increments ( );

		if ( !body_emitter.indexing ) {

			for ( uint8_t& slot : body_emitter.element )
				slot = 0;

		}

		body_emitter.out.r = &body;
		body_emitter.dry = true;
		body_emitter.out.indent = 1;
		body_emitter.run ( );
		body_emitter.dry = false;
		body_emitter.out.indent = 1;
		body_emitter.run ( );

		bool refolded = body_emitter.unfold_missed ( );
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

		for ( int32_t pass = 0; pass < 3 && ( typed || renumbered || refolded ); pass++ ) {

			int32_t gotos = body_emitter.gotos;

			body.text.clear ( );
			body.spans.clear ( );
			body.lines.clear ( );
			body_emitter.out.indent = 1;
			body_emitter.run ( );
			body_emitter.gotos = gotos;
			typed = false;
			renumbered = false;
			refolded = body_emitter.unfold_missed ( );

		}

		result& out = *b.out;
		emitter head ( b );
		wchar_t name [ 300 ];

		head.var_number = body_emitter.var_number;
		head.next_number = body_emitter.next_number;
		head.result_var = body_emitter.result_var;
		head.element = body_emitter.element;
		head.boolean = body_emitter.boolean;
		head.vector = body_emitter.vector;
		head.array_of = body_emitter.array_of;
		head.array_step = body_emitter.array_step;
		head.array_count = body_emitter.array_count;
		head.loop_letter = body_emitter.loop_letter;
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
			else if ( b.result_width == 0 && !b.result_text.empty ( ) )
				head.out.put ( b.result_text.get ( ), syntax::TYPE );
			else if ( b.result_width == 0 && returned_pointer ( b ) )
				head.out.put ( returned_pointer ( b ), syntax::TYPE );
			else if ( b.result_width == 0 && b.long_mode && unsigned_result ( b ) )
				head.out.put ( L"unsigned __int64", syntax::TYPE );
			else if ( b.result_width == 2 )
				head.out.put ( L"bool", syntax::TYPE );
			else if ( b.result_width == 3 )
				head.out.put ( L"_BOOL8", syntax::TYPE );
			else
				head.type_name ( b.result_width == 1 ? 1 : ( b.long_mode ? 8 : 4 ), b.result_width == 1 ? SIGNED : 0, false );

		} else
			head.out.put ( L"void", syntax::KEYWORD );

		head.name_after_type ( );

		if ( !b.names->name ( b.function, true, name, 300 ) )
			name [ 0 ] = 0;

		short_symbol ( name );

		if ( b.long_mode && b.params > 0 && !( b.has_prototype && b.prototype.variadic ) && b.variadic_from < 0 )
			head.out.put ( L"__fastcall ", syntax::KEYWORD );

		bool exits = false;

		for ( const block_code& item : b.code ) {

			if ( item.reachable && ( item.end == term::RETURN || item.end == term::TAIL ) )
				exits = true;

		}

		bool ends_program = false;

		for ( const statement& item : b.statements ) {

			if ( !exits && item.kind == stmt::EXPR && item.right >= 0 && head.no_return ( b.exprs [ static_cast < size_t > ( item.right ) ] ) )
				ends_program = true;

		}

		if ( !exits && ends_program && !b.returns_value && !b.has_prototype )
			head.out.put ( L"__noreturn ", syntax::KEYWORD );

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

		if ( ( b.has_prototype && b.prototype.variadic ) || b.variadic_from >= 0 ) {

			if ( shown_params++ )
				head.out.put ( L", " );

			head.out.put ( L"..." );

		}

		head.out.put ( L")" );
		head.out.header = true;
		head.out.finish ( );
		head.out.header = false;
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

		if ( frame_deepest )
			lowest = base::min ( lowest, b.deepest );

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

			bool is_array = v < head.array_of.count ( ) && head.array_of [ static_cast < size_t > ( v ) ] == v;

			if ( is_array ) {

				int32_t step = head.array_step [ static_cast < size_t > ( v ) ];

				head.out.put ( step == 8 ? L"__int64" : ( step == 4 ? L"int" : ( step == 2 ? L"__int16" : ( step == 16 ? L"__int128" : L"char" ) ) ), syntax::TYPE );
				head.out.put ( L" " );

			} else {

				head.var_type ( v );
				head.name_after_type ( );

			}

			head.var_name ( v, var );
			head.out.put ( var, syntax::LOCAL );

			if ( is_array )
				head.out.put ( base::writer ( ).add ( L"[" ).number ( head.array_count [ static_cast < size_t > ( v ) ] ).add ( L"]" ).text ( ) );

			head.out.put ( L";" );

			if ( item.kind == var_kind::REGISTER && item.family >= 0 ) {

				static const wchar_t* const wide [ 16 ] = { L"rax", L"rcx", L"rdx", L"rbx", L"rsp", L"rbp", L"rsi", L"rdi", L"r8", L"r9", L"r10", L"r11", L"r12", L"r13", L"r14", L"r15" };
				static const wchar_t* const dword [ 8 ] = { L"eax", L"ecx", L"edx", L"ebx", L"esp", L"ebp", L"esi", L"edi" };
				static const wchar_t* const word [ 8 ] = { L"ax", L"cx", L"dx", L"bx", L"sp", L"bp", L"si", L"di" };
				static const wchar_t* const byte [ 8 ] = { L"al", L"cl", L"dl", L"bl", L"spl", L"bpl", L"sil", L"dil" };
				base::writer note;

				note.add ( L" // " );

				if ( item.family >= 16 ) {

					bool partial = head.vector.count ( ) <= v || !head.vector [ static_cast < size_t > ( v ) ];

					note.add ( L"xmm" ).number ( item.family - 16 );

					if ( partial && item.size == 8 )
						note.add ( L"_8" );
					else if ( partial && item.size == 4 )
						note.add ( L"_4" );

				}
				else if ( item.family >= 8 )
					note.add ( wide [ item.family ] ).add ( item.size == 4 ? L"d" : L"" );
				else if ( item.size == 8 && b.long_mode )
					note.add ( wide [ item.family ] );
				else
					note.add ( item.size == 2 ? word [ item.family ] : ( item.size == 1 ? byte [ item.family ] : dword [ item.family ] ) );

				head.out.put ( note.text ( ), syntax::COMMENT );

			} else if ( item.flags ) {

				int64_t code = item.offset;
				const wchar_t* flag = code == 2 || code == 3 ? L" // cf" : ( code == 8 || code == 9 ? L" // sf" : ( code == 0 || code == 1 ? L" // of" : L" // zf" ) );

				head.out.put ( flag, syntax::COMMENT );

			} else if ( item.kind == var_kind::STACK ) {

				int64_t from_rsp = item.offset - lowest;
				int64_t from_rbp = frame_deepest ? ( b.has_frame ? item.offset - b.frame_at : item.offset ) : item.offset + saved_size;
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

		{

			base::list < int32_t > targets;

			auto label_number = [ & ] ( int32_t at, int32_t end ) -> int32_t {

				int32_t value = 0;
				bool any = false;

				while ( at < end && body.text [ static_cast < size_t > ( at ) ] >= L'0' && body.text [ static_cast < size_t > ( at ) ] <= L'9' ) {

					value = value * 10 + ( body.text [ static_cast < size_t > ( at ) ] - L'0' );
					any = true;
					at++;

				}

				return any ? value : -1;

			};

			for ( const line& here : body.lines ) {

				for ( int32_t at = here.text; at + 11 < here.text + here.length; at++ ) {

					static const wchar_t needle [ ] = L"goto LABEL_";
					bool hit = true;

					for ( int32_t k = 0; k < 11 && hit; k++ )
						hit = body.text [ static_cast < size_t > ( at + k ) ] == needle [ k ];

					if ( hit )
						targets.push ( label_number ( at + 11, here.text + here.length ) );

				}

			}

			for ( int32_t i = body.lines.count ( ) - 1; i >= 0; i-- ) {

				const line& here = body.lines [ static_cast < size_t > ( i ) ];

				if ( here.indent != 0 || here.length < 8 || body.text [ static_cast < size_t > ( here.text ) ] != L'L' || body.text [ static_cast < size_t > ( here.text + 5 ) ] != L'_' || body.text [ static_cast < size_t > ( here.text + here.length - 1 ) ] != L':' )
					continue;

				int32_t number = label_number ( here.text + 6, here.text + here.length - 1 );
				bool used = false;

				for ( int32_t target : targets )
					used = used || target == number;

				if ( number >= 0 && !used )
					body.lines.remove_at ( static_cast < size_t > ( i ) );

			}

		}

		if ( b.va_var >= 0 && b.variadic_from > 0 ) {

			int32_t last = -1;

			for ( int32_t v = 0; v < b.vars.count ( ); v++ ) {

				if ( b.vars [ static_cast < size_t > ( v ) ].kind == var_kind::PARAMETER && b.vars [ static_cast < size_t > ( v ) ].position == b.variadic_from - 1 )
					last = v;

			}

			wchar_t fixed [ 64 ] = { };
			wchar_t list [ 64 ] = { };

			if ( last >= 0 )
				body_emitter.var_name ( last, fixed );

			body_emitter.var_name ( b.va_var, list );

			base::writer text;

			text.add ( L"va_start(" ).add ( list ).add ( L", " );

			if ( fixed [ 0 ] )
				text.add ( fixed );
			else
				text.add ( L"a" ).number ( b.variadic_from );

			text.add ( L");" );

			line made;

			made.text = body.text.count ( );
			made.length = base::text_length ( text.text ( ) );
			made.first_span = body.spans.count ( );
			made.span_count = 0;
			made.indent = 1;
			made.address = b.function;

			for ( int32_t k = 0; k < made.length; k++ )
				body.text.push ( text.text ( ) [ k ] );

			body.lines.insert ( 0, made );

		}

		if ( !body.lines.empty ( ) ) {

			const line& tail = body.lines.back ( );

			if ( tail.indent == 1 && tail.length == 7 && body.text [ static_cast < size_t > ( tail.text ) ] == L'r' && body.text [ static_cast < size_t > ( tail.text + 6 ) ] == L';' )
				body.lines.pop ( );

		}

		if ( !b.returns_value ) {

			for ( int32_t i = 0; i < body.lines.count ( ); i++ ) {

				const line here = body.lines [ static_cast < size_t > ( i ) ];

				if ( here.length < 7 || body.text [ static_cast < size_t > ( here.text ) ] != L'L' || body.text [ static_cast < size_t > ( here.text + here.length - 1 ) ] != L':' )
					continue;

				bool closing = i + 1 >= body.lines.count ( );
				int32_t depth = 1;

				if ( !closing ) {

					const line& next = body.lines [ static_cast < size_t > ( i + 1 ) ];

					closing = next.length == 1 && body.text [ static_cast < size_t > ( next.text ) ] == L'}';
					depth = next.indent + 1;

				}

				if ( !closing )
					continue;

				line made = here;

				made.text = body.text.count ( );
				made.first_span = body.spans.count ( );
				made.span_count = 0;
				made.indent = depth;

				for ( const wchar_t* c = L"return;"; *c; c++ )
					body.text.push ( *c );

				made.length = body.text.count ( ) - made.text;
				body.lines.insert ( static_cast < size_t > ( i + 1 ), made );

			}

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

					if ( item.reachable && item.end == term::TAIL && item.returned >= 0 && !b->bare_return )
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