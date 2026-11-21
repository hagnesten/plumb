#include "script/script.hh"

namespace script {

	const wchar_t* kind_name ( kind type ) {

		switch ( type ) {

			case kind::NUMBER:
				return L"number";

			case kind::TEXT:
				return L"text";

			case kind::LIST:
				return L"list";

			default:
				return L"none";

		}

	}

	static void append ( base::list < wchar_t >& out, const wchar_t* text, int32_t length = -1 ) {

		for ( int32_t i = 0; length < 0 ? text [ i ] != 0 : i < length; i++ )
			out.push ( text [ i ] );

	}

	static void append_unsigned ( base::list < wchar_t >& out, uint64_t value, int32_t radix, bool upper, int32_t width, wchar_t pad ) {

		static const wchar_t lower_digits [ ] = L"0123456789abcdef";
		static const wchar_t upper_digits [ ] = L"0123456789ABCDEF";
		wchar_t reverse [ 70 ];
		int32_t n = 0;

		do {

			reverse [ n++ ] = ( upper ? upper_digits : lower_digits ) [ value % static_cast < uint64_t > ( radix ) ];
			value /= static_cast < uint64_t > ( radix );

		} while ( value && n < 68 );

		for ( int32_t i = n; i < width; i++ )
			out.push ( pad );

		while ( n )
			out.push ( reverse [ --n ] );

	}

	static void append_signed ( base::list < wchar_t >& out, int64_t value ) {

		if ( value < 0 ) {

			out.push ( L'-' );
			append_unsigned ( out, static_cast < uint64_t > ( 0 ) - static_cast < uint64_t > ( value ), 10, false, 0, L'0' );

		} else
			append_unsigned ( out, static_cast < uint64_t > ( value ), 10, false, 0, L'0' );

	}

	value engine::text ( const wchar_t* chars, int32_t length ) {

		value made;
		int32_t index = store ( chars, length );

		if ( index < 0 ) {

			fail ( L"The script used more than 64 million characters of text." );

			return made;

		}

		made.type = kind::TEXT;
		made.ref = index;
		made.number = index;

		return made;

	}

	value engine::list ( ) {

		value made;

		lists.emplace ( );
		made.type = kind::LIST;
		made.ref = lists.count ( ) - 1;
		made.number = made.ref;

		return made;

	}

	void engine::push ( value list_value, value item ) {

		if ( list_value.type == kind::LIST )
			lists [ static_cast < size_t > ( list_value.ref ) ].push ( item );

	}

	const wchar_t* engine::text_of ( value item ) const {

		return item.type == kind::TEXT ? texts [ static_cast < size_t > ( item.ref ) ].data ( ) : L"";

	}

	int32_t engine::text_length ( value item ) const {

		return item.type == kind::TEXT ? texts [ static_cast < size_t > ( item.ref ) ].count ( ) - 1 : 0;

	}

	int32_t engine::list_count ( value item ) const {

		return item.type == kind::LIST ? lists [ static_cast < size_t > ( item.ref ) ].count ( ) : 0;

	}

	value engine::list_item ( value item, int32_t index ) const {

		if ( item.type != kind::LIST || index < 0 || index >= lists [ static_cast < size_t > ( item.ref ) ].count ( ) )
			return value ( );

		return lists [ static_cast < size_t > ( item.ref ) ] [ static_cast < size_t > ( index ) ];

	}

	bool engine::fail ( const wchar_t* message ) {

		if ( error.set )
			return false;

		error.set = true;
		error.line = 0;
		error.column = 0;

		int32_t k = 0;

		for ( ; message [ k ] && k < 299; k++ )
			error.message [ k ] = message [ k ];

		error.message [ k ] = 0;

		return false;

	}

	bool engine::runtime_error ( const node& at, const wchar_t* message ) {

		if ( !error.set )
			fail ( message );

		if ( !error.line ) {

			error.line = at.line;
			error.column = at.column;

			const wchar_t* path = at.file >= 0 && at.file < files.count ( ) ? files [ static_cast < size_t > ( at.file ) ].get ( ) : L"";
			int32_t k = 0;

			for ( ; path [ k ] && k < 259; k++ )
				error.file [ k ] = path [ k ];

			error.file [ k ] = 0;

		}

		return false;

	}

	static void describe_into ( const engine& owner, value item, base::list < wchar_t >& out, bool quoted, int32_t depth );

	void engine::describe ( value item, base::writer& out ) {

		base::list < wchar_t > text_out;

		describe_into ( *this, item, text_out, false, 0 );
		text_out.push ( 0 );
		out.add ( text_out.data ( ) );

	}

	static void describe_into ( const engine& owner, value item, base::list < wchar_t >& out, bool quoted, int32_t depth ) {

		switch ( item.type ) {

			case kind::NUMBER:
				append_signed ( out, item.number );
				break;

			case kind::TEXT:

				if ( quoted )
					out.push ( L'"' );

				append ( out, owner.text_of ( item ), owner.text_length ( item ) );

				if ( quoted )
					out.push ( L'"' );

				break;

			case kind::LIST: {

				out.push ( L'[' );

				int32_t count = owner.list_count ( item );

				for ( int32_t i = 0; i < count; i++ ) {

					if ( i )
						append ( out, L", " );

					if ( depth > 8 || i >= 1000 ) {

						append ( out, L"..." );

						break;

					}

					describe_into ( owner, owner.list_item ( item, i ), out, true, depth + 1 );

				}

				out.push ( L']' );

				break;

			}

			default:
				append ( out, L"none" );
				break;

		}

	}

	bool engine::load ( const wchar_t* source, const wchar_t* path, host& with ) {

		texts.clear ( );
		lists.clear ( );
		interned.clear ( );
		tokens.clear ( );
		nodes.clear ( );
		items.clear ( );
		functions.clear ( );
		params.clear ( );
		top_level.clear ( );
		release_jit ( );
		files.clear ( );
		global_names.clear ( );
		globals.clear ( );
		stack.clear ( );
		error = problem ( );
		position = 0;
		text_chars = 0;
		current = &with;
		files.emplace ( ).set ( path ? path : L"" );

		if ( !lex ( source, 0, 0 ) )
			return false;

		return parse_program ( ) && resolve ( );

	}

	bool engine::overran ( const node& at ) {

		base::writer message;

		message.add ( L"The script ran for more than " ).grouped ( static_cast < uint64_t > ( step_limit ) ).add ( L" steps and was stopped." );

		return runtime_error ( at, message.text ( ) );

	}

	bool engine::run ( host& with ) {

		current = &with;
		steps = 0;
		stopped = false;
		depth = 0;
		loops = 0;
		frame_base = 0;
		in_function = false;
		globals.clear ( );
		globals.resize ( static_cast < size_t > ( global_names.count ( ) ) );
		stack.clear ( );
		stack.resize ( static_cast < size_t > ( top_slots ) );
		jit_used = jit_enabled && compile ( );

		if ( jit_used ) {

			reinterpret_cast < int32_t ( * ) ( engine* ) > ( jit_top ) ( this );

			if ( error.set )
				return false;

			if ( stopped )
				return true;

		} else {

			for ( int32_t statement : top_level ) {

				execute ( statement );

				if ( error.set )
					return false;

				if ( stopped )
					return true;

			}

		}

		for ( int32_t i = 0; i < functions.count ( ); i++ ) {

			const function_def& item = functions [ static_cast < size_t > ( i ) ];

			if ( !base::same_text ( texts [ static_cast < size_t > ( item.name ) ].data ( ), L"main" ) )
				continue;

			node at;

			at.line = item.line;
			at.file = item.file;
			call_user ( at, i, nullptr, 0 );

			break;

		}

		return !error.set;

	}

	bool engine::truthy ( value item ) const {

		switch ( item.type ) {

			case kind::NUMBER:
				return item.number != 0;

			case kind::TEXT:
				return text_length ( item ) > 0;

			case kind::LIST:
				return list_count ( item ) > 0;

			default:
				return false;

		}

	}

	int32_t engine::compare_text ( value a, value b ) const {

		const wchar_t* x = text_of ( a );
		const wchar_t* y = text_of ( b );
		int32_t lx = text_length ( a );
		int32_t ly = text_length ( b );

		for ( int32_t i = 0; i < lx && i < ly; i++ ) {

			if ( x [ i ] != y [ i ] )
				return x [ i ] < y [ i ] ? -1 : 1;

		}

		return lx == ly ? 0 : ( lx < ly ? -1 : 1 );

	}

	bool engine::equal ( value a, value b ) const {

		if ( a.type != b.type )
			return false;

		switch ( a.type ) {

			case kind::NUMBER:
				return a.number == b.number;

			case kind::TEXT:
				return compare_text ( a, b ) == 0;

			case kind::LIST: {

				if ( a.ref == b.ref )
					return true;

				int32_t count = list_count ( a );

				if ( count != list_count ( b ) )
					return false;

				for ( int32_t i = 0; i < count; i++ ) {

					if ( !equal ( list_item ( a, i ), list_item ( b, i ) ) )
						return false;

				}

				return true;

			}

			default:
				return true;

		}

	}

	value engine::binary_op ( const node& at, symbol mark, value a, value b ) {

		value result;

		result.type = kind::NUMBER;

		if ( mark == symbol::EQUAL_EQUAL || mark == symbol::BANG_EQUAL ) {

			bool same = equal ( a, b );

			result.number = mark == symbol::EQUAL_EQUAL ? same : !same;

			return result;

		}

		if ( mark == symbol::PLUS && ( a.type == kind::TEXT || b.type == kind::TEXT ) ) {

			base::list < wchar_t > joined;

			describe_into ( *this, a, joined, false, 0 );
			describe_into ( *this, b, joined, false, 0 );

			return text ( joined.data ( ), joined.count ( ) );

		}

		if ( mark == symbol::PLUS && a.type == kind::LIST && b.type == kind::LIST ) {

			value made = list ( );
			int32_t count_a = list_count ( a );
			int32_t count_b = list_count ( b );

			for ( int32_t i = 0; i < count_a; i++ )
				push ( made, list_item ( a, i ) );

			for ( int32_t i = 0; i < count_b; i++ )
				push ( made, list_item ( b, i ) );

			return made;

		}

		if ( ( mark == symbol::LESS || mark == symbol::LESS_EQUAL || mark == symbol::GREATER || mark == symbol::GREATER_EQUAL ) && a.type == kind::TEXT && b.type == kind::TEXT ) {

			int32_t order = compare_text ( a, b );

			result.number = mark == symbol::LESS ? order < 0 : ( mark == symbol::LESS_EQUAL ? order <= 0 : ( mark == symbol::GREATER ? order > 0 : order >= 0 ) );

			return result;

		}

		if ( a.type != kind::NUMBER || b.type != kind::NUMBER ) {

			base::writer message;

			message.add ( L"This operator needs numbers, not " ).add ( kind_name ( a.type == kind::NUMBER ? b.type : a.type ) ).add ( L"." );
			runtime_error ( at, message.text ( ) );

			return value ( );

		}

		int64_t x = a.number;
		int64_t y = b.number;
		uint64_t ux = static_cast < uint64_t > ( x );
		uint64_t uy = static_cast < uint64_t > ( y );

		switch ( mark ) {

			case symbol::PLUS:
				result.number = static_cast < int64_t > ( ux + uy );
				break;

			case symbol::MINUS:
				result.number = static_cast < int64_t > ( ux - uy );
				break;

			case symbol::STAR:
				result.number = static_cast < int64_t > ( ux * uy );
				break;

			case symbol::SLASH:
			case symbol::PERCENT:

				if ( y == 0 ) {

					runtime_error ( at, mark == symbol::SLASH ? L"Division by zero." : L"Remainder of a division by zero." );

					return value ( );

				}

				if ( y == -1 )
					result.number = mark == symbol::SLASH ? static_cast < int64_t > ( 0 - ux ) : 0;
				else
					result.number = mark == symbol::SLASH ? x / y : x % y;

				break;

			case symbol::AMPERSAND:
				result.number = x & y;
				break;

			case symbol::PIPE:
				result.number = x | y;
				break;

			case symbol::CARET:
				result.number = x ^ y;
				break;

			case symbol::SHIFT_LEFT:
				result.number = y >= 64 || y < 0 ? 0 : static_cast < int64_t > ( ux << y );
				break;

			case symbol::SHIFT_RIGHT:
				result.number = y >= 64 || y < 0 ? ( x < 0 ? -1 : 0 ) : x >> y;
				break;

			case symbol::LESS:
				result.number = x < y;
				break;

			case symbol::LESS_EQUAL:
				result.number = x <= y;
				break;

			case symbol::GREATER:
				result.number = x > y;
				break;

			case symbol::GREATER_EQUAL:
				result.number = x >= y;
				break;

			default:
				break;

		}

		return result;

	}

	static inline bool fast_arithmetic ( symbol mark, int64_t x, int64_t y, int64_t& out ) {

		uint64_t ux = static_cast < uint64_t > ( x );
		uint64_t uy = static_cast < uint64_t > ( y );

		switch ( mark ) {

			case symbol::PLUS:
				out = static_cast < int64_t > ( ux + uy );
				return true;

			case symbol::MINUS:
				out = static_cast < int64_t > ( ux - uy );
				return true;

			case symbol::STAR:
				out = static_cast < int64_t > ( ux * uy );
				return true;

			case symbol::AMPERSAND:
				out = x & y;
				return true;

			case symbol::PIPE:
				out = x | y;
				return true;

			case symbol::CARET:
				out = x ^ y;
				return true;

			case symbol::LESS:
				out = x < y;
				return true;

			case symbol::LESS_EQUAL:
				out = x <= y;
				return true;

			case symbol::GREATER:
				out = x > y;
				return true;

			case symbol::GREATER_EQUAL:
				out = x >= y;
				return true;

			case symbol::EQUAL_EQUAL:
				out = x == y;
				return true;

			case symbol::BANG_EQUAL:
				out = x != y;
				return true;

			default:
				return false;

		}

	}

	symbol compound_operator ( symbol mark ) {

		switch ( mark ) {

			case symbol::PLUS_ASSIGN:
				return symbol::PLUS;

			case symbol::MINUS_ASSIGN:
				return symbol::MINUS;

			case symbol::STAR_ASSIGN:
				return symbol::STAR;

			case symbol::SLASH_ASSIGN:
				return symbol::SLASH;

			case symbol::PERCENT_ASSIGN:
				return symbol::PERCENT;

			case symbol::AND_ASSIGN:
				return symbol::AMPERSAND;

			case symbol::OR_ASSIGN:
				return symbol::PIPE;

			case symbol::XOR_ASSIGN:
				return symbol::CARET;

			case symbol::SHIFT_LEFT_ASSIGN:
				return symbol::SHIFT_LEFT;

			case symbol::SHIFT_RIGHT_ASSIGN:
				return symbol::SHIFT_RIGHT;

			default:
				return symbol::NONE;

		}

	}

	bool engine::assign ( const node& target, value item ) {

		if ( target.type == node_kind::NAME ) {

			if ( target.b == 1 )
				globals [ static_cast < size_t > ( target.c ) ] = item;
			else
				stack [ static_cast < size_t > ( frame_base + target.c ) ] = item;

			return true;

		}

		value container = evaluate ( target.a );

		if ( error.set )
			return false;

		value index = evaluate ( target.b );

		if ( error.set )
			return false;

		if ( container.type == kind::TEXT )
			return runtime_error ( target, L"Text can't be changed in place. Build a new string instead." );

		if ( container.type != kind::LIST )
			return runtime_error ( target, L"Only lists can be indexed and assigned to." );

		if ( index.type != kind::NUMBER )
			return runtime_error ( target, L"A list index must be a number." );

		int32_t count = list_count ( container );
		int64_t at = index.number < 0 ? index.number + count : index.number;

		if ( at < 0 || at >= count )
			return runtime_error ( target, L"The list index is out of range." );

		lists [ static_cast < size_t > ( container.ref ) ] [ static_cast < size_t > ( at ) ] = item;

		return true;

	}

	engine::flow engine::execute ( int32_t index ) {

		const node& at = nodes [ static_cast < size_t > ( index ) ];

		if ( !tick ( at ) || error.set || stopped )
			return flow::RETURN;

		switch ( at.type ) {

			case node_kind::BLOCK: {

				for ( int32_t i = 0; i < at.count; i++ ) {

					flow result = execute ( items [ static_cast < size_t > ( at.first + i ) ] );

					if ( result != flow::NEXT )
						return result;

				}

				return flow::NEXT;

			}

			case node_kind::VAR:

				for ( int32_t i = 0; i < at.count; i++ ) {

					const node& item = nodes [ static_cast < size_t > ( items [ static_cast < size_t > ( at.first + i ) ] ) ];
					value start;

					if ( item.a >= 0 ) {

						start = evaluate ( item.a );

						if ( error.set )
							return flow::RETURN;

					}

					assign ( item, start );

				}

				return flow::NEXT;

			case node_kind::EXPR:
				evaluate ( at.a );
				return error.set ? flow::RETURN : flow::NEXT;

			case node_kind::IF: {

				value test = evaluate ( at.a );

				if ( error.set )
					return flow::RETURN;

				if ( truthy ( test ) )
					return execute ( at.b );

				if ( at.c >= 0 )
					return execute ( at.c );

				return flow::NEXT;

			}

			case node_kind::WHILE:
			case node_kind::DO: {

				loops++;

				bool first = at.type == node_kind::DO;

				while ( true ) {

					if ( !first ) {

						value test = evaluate ( at.a );

						if ( error.set ) {

							loops--;

							return flow::RETURN;

						}

						if ( !truthy ( test ) )
							break;

					}

					first = false;

					flow result = execute ( at.b );

					if ( result == flow::BREAK )
						break;

					if ( result == flow::RETURN ) {

						loops--;

						return flow::RETURN;

					}

					if ( at.type == node_kind::DO ) {

						value test = evaluate ( at.a );

						if ( error.set ) {

							loops--;

							return flow::RETURN;

						}

						if ( !truthy ( test ) )
							break;

						first = true;

					}

				}

				loops--;

				return flow::NEXT;

			}

			case node_kind::FOR: {

				flow outcome = flow::NEXT;

				loops++;

				if ( at.a >= 0 && execute ( at.a ) == flow::RETURN )
					outcome = flow::RETURN;

				while ( outcome == flow::NEXT ) {

					if ( at.b >= 0 ) {

						value test = evaluate ( at.b );

						if ( error.set ) {

							outcome = flow::RETURN;

							break;

						}

						if ( !truthy ( test ) )
							break;

					}

					flow result = execute ( at.d );

					if ( result == flow::BREAK )
						break;

					if ( result == flow::RETURN ) {

						outcome = flow::RETURN;

						break;

					}

					if ( at.c >= 0 ) {

						evaluate ( at.c );

						if ( error.set ) {

							outcome = flow::RETURN;

							break;

						}

					}

				}

				loops--;

				return outcome;

			}

			case node_kind::BREAK:
			case node_kind::CONTINUE:

				if ( !loops ) {

					runtime_error ( at, at.type == node_kind::BREAK ? L"break is only allowed inside a loop." : L"continue is only allowed inside a loop." );

					return flow::RETURN;

				}

				return at.type == node_kind::BREAK ? flow::BREAK : flow::CONTINUE;

			case node_kind::RETURN:

				returned = value ( );

				if ( at.a >= 0 ) {

					returned = evaluate ( at.a );

					if ( error.set )
						return flow::RETURN;

				}

				if ( !in_function )
					stopped = true;

				return flow::RETURN;

			default:
				return flow::NEXT;

		}

	}

	value engine::evaluate ( int32_t index ) {

		const node& at = nodes [ static_cast < size_t > ( index ) ];
		value result;

		if ( !tick ( at ) )
			return result;

		switch ( at.type ) {

			case node_kind::NUMBER:
				result.type = kind::NUMBER;
				result.number = at.number;
				return result;

			case node_kind::TEXT:
				result.type = kind::TEXT;
				result.ref = at.text;
				result.number = at.text;
				return result;

			case node_kind::NOTHING:
				return result;

			case node_kind::NAME:
				return at.b == 1 ? globals [ static_cast < size_t > ( at.c ) ] : stack [ static_cast < size_t > ( frame_base + at.c ) ];

			case node_kind::LIST: {

				value made = list ( );

				for ( int32_t i = 0; i < at.count; i++ ) {

					value item = evaluate ( items [ static_cast < size_t > ( at.first + i ) ] );

					if ( error.set )
						return result;

					push ( made, item );

				}

				return made;

			}

			case node_kind::INDEX: {

				value container = evaluate ( at.a );

				if ( error.set )
					return result;

				value position_value = evaluate ( at.b );

				if ( error.set )
					return result;

				if ( position_value.type != kind::NUMBER ) {

					runtime_error ( at, L"An index must be a number." );

					return result;

				}

				int32_t count = container.type == kind::TEXT ? text_length ( container ) : list_count ( container );
				int64_t where = position_value.number < 0 ? position_value.number + count : position_value.number;

				if ( container.type != kind::TEXT && container.type != kind::LIST ) {

					runtime_error ( at, L"Only text and lists can be indexed." );

					return result;

				}

				if ( where < 0 || where >= count ) {

					runtime_error ( at, L"The index is out of range." );

					return result;

				}

				if ( container.type == kind::TEXT )
					return text ( text_of ( container ) + where, 1 );

				return list_item ( container, static_cast < int32_t > ( where ) );

			}

			case node_kind::CALL:
				return evaluate_call ( at );

			case node_kind::UNARY: {

				value operand = evaluate ( at.a );

				if ( error.set )
					return result;

				if ( at.mark == symbol::BANG ) {

					result.type = kind::NUMBER;
					result.number = !truthy ( operand );

					return result;

				}

				if ( operand.type != kind::NUMBER ) {

					runtime_error ( at, L"This operator needs a number." );

					return result;

				}

				result.type = kind::NUMBER;
				result.number = at.mark == symbol::MINUS ? static_cast < int64_t > ( 0 - static_cast < uint64_t > ( operand.number ) ) : ( at.mark == symbol::TILDE ? ~operand.number : operand.number );

				return result;

			}

			case node_kind::BINARY: {

				value left = evaluate ( at.a );

				if ( error.set )
					return result;

				value right = evaluate ( at.b );

				if ( error.set )
					return result;

				if ( left.type == kind::NUMBER && right.type == kind::NUMBER && fast_arithmetic ( at.mark, left.number, right.number, result.number ) ) {

					result.type = kind::NUMBER;

					return result;

				}

				return binary_op ( at, at.mark, left, right );

			}

			case node_kind::AND:
			case node_kind::OR: {

				value left = evaluate ( at.a );

				if ( error.set )
					return result;

				result.type = kind::NUMBER;

				bool decided = at.type == node_kind::AND ? !truthy ( left ) : truthy ( left );

				if ( decided ) {

					result.number = at.type == node_kind::OR;

					return result;

				}

				value right = evaluate ( at.b );

				if ( error.set )
					return value ( );

				result.number = truthy ( right );

				return result;

			}

			case node_kind::TERNARY: {

				value test = evaluate ( at.a );

				if ( error.set )
					return result;

				return evaluate ( truthy ( test ) ? at.b : at.c );

			}

			case node_kind::ASSIGN: {

				const node& target = nodes [ static_cast < size_t > ( at.a ) ];
				value right = evaluate ( at.b );

				if ( error.set )
					return result;

				if ( target.type == node_kind::NAME ) {

					value& slot = target.b == 1 ? globals [ static_cast < size_t > ( target.c ) ] : stack [ static_cast < size_t > ( frame_base + target.c ) ];

					if ( at.mark == symbol::ASSIGN ) {

						slot = right;

						return right;

					}

					if ( slot.type == kind::NUMBER && right.type == kind::NUMBER && fast_arithmetic ( compound_operator ( at.mark ), slot.number, right.number, slot.number ) )
						return slot;

				}

				if ( at.mark != symbol::ASSIGN ) {

					value left = evaluate ( at.a );

					if ( error.set )
						return result;

					right = binary_op ( at, compound_operator ( at.mark ), left, right );

					if ( error.set )
						return result;

				}

				assign ( target, right );

				return right;

			}

			case node_kind::PREFIX:
			case node_kind::POSTFIX: {

				const node& target = nodes [ static_cast < size_t > ( at.a ) ];

				if ( target.type == node_kind::NAME ) {

					value& slot = target.b == 1 ? globals [ static_cast < size_t > ( target.c ) ] : stack [ static_cast < size_t > ( frame_base + target.c ) ];

					if ( slot.type == kind::NUMBER ) {

						value before = slot;

						slot.number = static_cast < int64_t > ( static_cast < uint64_t > ( slot.number ) + ( at.mark == symbol::PLUS_PLUS ? 1ull : ~0ull ) );

						return at.type == node_kind::PREFIX ? slot : before;

					}

				}

				value old = evaluate ( at.a );

				if ( error.set )
					return result;

				if ( old.type != kind::NUMBER ) {

					runtime_error ( at, L"++ and -- need a number." );

					return result;

				}

				value updated = old;

				updated.number = static_cast < int64_t > ( static_cast < uint64_t > ( old.number ) + ( at.mark == symbol::PLUS_PLUS ? 1ull : ~0ull ) );
				assign ( target, updated );

				return at.type == node_kind::PREFIX ? updated : old;

			}

			default:
				return result;

		}

	}

	value engine::evaluate_call ( const node& at ) {

		value inline_args [ 8 ];
		base::list < value > spilled;
		value* args = inline_args;

		if ( at.count > 8 ) {

			spilled.resize ( static_cast < size_t > ( at.count ) );
			args = spilled.data ( );

		}

		for ( int32_t i = 0; i < at.count; i++ ) {

			args [ i ] = evaluate ( items [ static_cast < size_t > ( at.first + i ) ] );

			if ( error.set )
				return value ( );

		}

		if ( at.b == 0 )
			return call_user ( at, at.c, args, at.count );

		return call_function ( at, args, at.count );

	}

	value engine::call_user ( const node& at, int32_t index, const value* args, int32_t count ) {

		value result;
		const function_def& item = functions [ static_cast < size_t > ( index ) ];

		if ( count > item.params ) {

			base::writer message;

			message.add ( L"'" ).add ( texts [ static_cast < size_t > ( item.name ) ].data ( ) ).add ( L"' takes " ).number ( item.params ).add ( item.params == 1 ? L" argument, not " : L" arguments, not " ).number ( count ).add ( L"." );
			runtime_error ( at, message.text ( ) );

			return result;

		}

		if ( depth >= 200 ) {

			runtime_error ( at, L"Functions are nested more than 200 calls deep." );

			return result;

		}

		int32_t saved_base = frame_base;
		int32_t saved_loops = loops;
		bool saved_function = in_function;
		int32_t base_index = stack.count ( );

		stack.resize ( static_cast < size_t > ( base_index + item.slots ) );

		for ( int32_t i = 0; i < count; i++ )
			stack [ static_cast < size_t > ( base_index + i ) ] = args [ i ];

		frame_base = base_index;
		loops = 0;
		in_function = true;
		depth++;
		returned = value ( );

		flow outcome = jit_used && jit_entries [ static_cast < size_t > ( index ) ] ? static_cast < flow > ( reinterpret_cast < int32_t ( * ) ( engine* ) > ( jit_entries [ static_cast < size_t > ( index ) ] ) ( this ) ) : execute ( item.body );

		depth--;
		result = outcome == flow::RETURN && !error.set ? returned : value ( );
		returned = value ( );
		stack.resize ( static_cast < size_t > ( base_index ) );
		frame_base = saved_base;
		loops = saved_loops;
		in_function = saved_function;

		return result;

	}

	value engine::call_function ( const node& at, const value* args, int32_t count ) {

		value result;
		const wchar_t* label = texts [ static_cast < size_t > ( at.text ) ].data ( );

		if ( builtin ( at, label, args, count, result ) ) {

			if ( error.set )
				runtime_error ( at, error.message );

			return result;

		}

		if ( current && current->call ( *this, label, args, count, result ) ) {

			if ( error.set )
				runtime_error ( at, error.message );

			return result;

		}

		base::writer message;

		message.add ( L"There's no function called '" ).add ( label ).add ( L"'." );
		runtime_error ( at, message.text ( ) );

		return result;

	}

}
