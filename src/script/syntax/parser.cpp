#include "script/script.h"

namespace script {

	static const wchar_t* const reserved [ ] = { L"var", L"func", L"static", L"if", L"else", L"while", L"do", L"for", L"break", L"continue", L"return", L"true", L"false", L"none", L"null" };

	const token& engine::peek ( int32_t ahead ) const {

		int32_t at = position + ahead;

		if ( at >= tokens.count ( ) )
			at = tokens.count ( ) - 1;

		return tokens [ static_cast < size_t > ( at ) ];

	}

	bool engine::is ( symbol mark ) const {

		const token& item = peek ( );

		return item.type == token_kind::SYMBOL && item.mark == mark;

	}

	bool engine::is_word ( const wchar_t* word ) const {

		const token& item = peek ( );

		return item.type == token_kind::NAME && base::same_text ( texts [ static_cast < size_t > ( item.text ) ].data ( ), word );

	}

	bool engine::accept ( symbol mark ) {

		if ( !is ( mark ) )
			return false;

		position++;

		return true;

	}

	bool engine::parse_error ( const wchar_t* message ) {

		const token& at = peek ( );

		return lex_error ( at.line, at.column, at.file, message );

	}

	bool engine::expect ( symbol mark, const wchar_t* what ) {

		if ( accept ( mark ) )
			return true;

		if ( mark == symbol::SEMICOLON && peek ( ).type == token_kind::END )
			return true;

		base::writer message;

		message.add ( L"Expected " ).add ( what );

		const token& at = peek ( );

		if ( at.type == token_kind::END )
			message.add ( L" but the script ended." );
		else if ( at.type == token_kind::NAME )
			message.add ( L" before '" ).add ( texts [ static_cast < size_t > ( at.text ) ].data ( ) ).add ( L"'." );
		else
			message.add ( L"." );

		return parse_error ( message.text ( ) );

	}

	int32_t engine::make ( node_kind type ) {

		const token& at = peek ( );
		node& item = nodes.emplace ( );

		item.type = type;
		item.line = at.line;
		item.column = at.column;
		item.file = at.file;

		return nodes.count ( ) - 1;

	}

	static bool reserved_word ( const wchar_t* word ) {

		for ( const wchar_t* item : reserved ) {

			if ( base::same_text ( item, word ) )
				return true;

		}

		return false;

	}

	bool engine::parse_program ( ) {

		while ( peek ( ).type != token_kind::END ) {

			if ( is_word ( L"func" ) || is_word ( L"static" ) ) {

				if ( !parse_function ( ) )
					return false;

				continue;

			}

			int32_t statement = parse_statement ( );

			if ( statement < 0 )
				return false;

			top_level.push ( statement );

		}

		return true;

	}

	bool engine::parse_function ( ) {

		const token& keyword = peek ( );

		position++;

		if ( peek ( ).type != token_kind::NAME )
			return parse_error ( L"Expected a function name after func." );

		int32_t name = peek ( ).text;

		if ( reserved_word ( texts [ static_cast < size_t > ( name ) ].data ( ) ) )
			return parse_error ( L"A reserved word can't be a function name." );

		for ( const function_def& known : functions ) {

			if ( known.name == name )
				return parse_error ( L"This function is already defined." );

		}

		position++;

		if ( !expect ( symbol::OPEN_PAREN, L"( after the function name" ) )
			return false;

		function_def made;

		made.name = name;
		made.first_param = params.count ( );
		made.line = keyword.line;
		made.file = keyword.file;

		if ( !is ( symbol::CLOSE_PAREN ) ) {

			do {

				if ( peek ( ).type != token_kind::NAME || reserved_word ( texts [ static_cast < size_t > ( peek ( ).text ) ].data ( ) ) )
					return parse_error ( L"Expected a parameter name." );

				for ( int32_t i = made.first_param; i < params.count ( ); i++ ) {

					if ( params [ static_cast < size_t > ( i ) ] == peek ( ).text )
						return parse_error ( L"Two parameters have the same name." );

				}

				params.push ( peek ( ).text );
				made.params++;
				position++;

			} while ( accept ( symbol::COMMA ) );

		}

		if ( !expect ( symbol::CLOSE_PAREN, L") after the parameters" ) )
			return false;

		if ( !is ( symbol::OPEN_BRACE ) )
			return parse_error ( L"Expected { to start the function body." );

		made.body = parse_block ( );

		if ( made.body < 0 )
			return false;

		functions.push ( made );

		return true;

	}

	int32_t engine::parse_block ( ) {

		int32_t block = make ( node_kind::BLOCK );
		base::list < int32_t > children;

		if ( !expect ( symbol::OPEN_BRACE, L"{" ) )
			return -1;

		while ( !is ( symbol::CLOSE_BRACE ) ) {

			if ( peek ( ).type == token_kind::END ) {

				parse_error ( L"Expected } before the script ended." );

				return -1;

			}

			if ( is_word ( L"func" ) || is_word ( L"static" ) ) {

				parse_error ( L"Functions can only be defined at the top level." );

				return -1;

			}

			int32_t statement = parse_statement ( );

			if ( statement < 0 )
				return -1;

			children.push ( statement );

		}

		position++;
		nodes [ static_cast < size_t > ( block ) ].first = items.count ( );
		nodes [ static_cast < size_t > ( block ) ].count = children.count ( );

		for ( int32_t child : children )
			items.push ( child );

		return block;

	}

	int32_t engine::parse_var ( ) {

		int32_t group = make ( node_kind::VAR );
		base::list < int32_t > children;

		position++;

		do {

			if ( peek ( ).type != token_kind::NAME || reserved_word ( texts [ static_cast < size_t > ( peek ( ).text ) ].data ( ) ) ) {

				parse_error ( L"Expected a variable name after var." );

				return -1;

			}

			int32_t item = make ( node_kind::NAME );

			nodes [ static_cast < size_t > ( item ) ].text = peek ( ).text;
			position++;

			if ( accept ( symbol::ASSIGN ) ) {

				int32_t start = parse_assignment ( );

				if ( start < 0 )
					return -1;

				nodes [ static_cast < size_t > ( item ) ].a = start;

			}

			children.push ( item );

		} while ( accept ( symbol::COMMA ) );

		nodes [ static_cast < size_t > ( group ) ].first = items.count ( );
		nodes [ static_cast < size_t > ( group ) ].count = children.count ( );

		for ( int32_t child : children )
			items.push ( child );

		return group;

	}

	int32_t engine::parse_statement ( ) {

		if ( is ( symbol::OPEN_BRACE ) )
			return parse_block ( );

		if ( is ( symbol::SEMICOLON ) ) {

			int32_t empty = make ( node_kind::EMPTY );

			position++;

			return empty;

		}

		if ( is_word ( L"var" ) ) {

			int32_t group = parse_var ( );

			if ( group < 0 || !expect ( symbol::SEMICOLON, L"; after the declaration" ) )
				return -1;

			return group;

		}

		if ( is_word ( L"if" ) ) {

			int32_t made = make ( node_kind::IF );

			position++;

			if ( !expect ( symbol::OPEN_PAREN, L"( after if" ) )
				return -1;

			int32_t test = parse_expression ( );

			if ( test < 0 || !expect ( symbol::CLOSE_PAREN, L") after the condition" ) )
				return -1;

			int32_t yes = parse_statement ( );

			if ( yes < 0 )
				return -1;

			int32_t no = -1;

			if ( is_word ( L"else" ) ) {

				position++;
				no = parse_statement ( );

				if ( no < 0 )
					return -1;

			}

			nodes [ static_cast < size_t > ( made ) ].a = test;
			nodes [ static_cast < size_t > ( made ) ].b = yes;
			nodes [ static_cast < size_t > ( made ) ].c = no;

			return made;

		}

		if ( is_word ( L"while" ) ) {

			int32_t made = make ( node_kind::WHILE );

			position++;

			if ( !expect ( symbol::OPEN_PAREN, L"( after while" ) )
				return -1;

			int32_t test = parse_expression ( );

			if ( test < 0 || !expect ( symbol::CLOSE_PAREN, L") after the condition" ) )
				return -1;

			int32_t body = parse_statement ( );

			if ( body < 0 )
				return -1;

			nodes [ static_cast < size_t > ( made ) ].a = test;
			nodes [ static_cast < size_t > ( made ) ].b = body;

			return made;

		}

		if ( is_word ( L"do" ) ) {

			int32_t made = make ( node_kind::DO );

			position++;

			int32_t body = parse_statement ( );

			if ( body < 0 )
				return -1;

			if ( !is_word ( L"while" ) ) {

				parse_error ( L"Expected while after the do body." );

				return -1;

			}

			position++;

			if ( !expect ( symbol::OPEN_PAREN, L"( after while" ) )
				return -1;

			int32_t test = parse_expression ( );

			if ( test < 0 || !expect ( symbol::CLOSE_PAREN, L") after the condition" ) || !expect ( symbol::SEMICOLON, L"; after do ... while" ) )
				return -1;

			nodes [ static_cast < size_t > ( made ) ].a = test;
			nodes [ static_cast < size_t > ( made ) ].b = body;

			return made;

		}

		if ( is_word ( L"for" ) ) {

			int32_t made = make ( node_kind::FOR );
			int32_t start = -1;
			int32_t test = -1;
			int32_t step = -1;

			position++;

			if ( !expect ( symbol::OPEN_PAREN, L"( after for" ) )
				return -1;

			if ( is_word ( L"var" ) ) {

				start = parse_var ( );

				if ( start < 0 )
					return -1;

			} else if ( !is ( symbol::SEMICOLON ) ) {

				int32_t expression = parse_expression ( );

				if ( expression < 0 )
					return -1;

				start = make ( node_kind::EXPR );
				nodes [ static_cast < size_t > ( start ) ].a = expression;

			}

			if ( !expect ( symbol::SEMICOLON, L"; after the for setup" ) )
				return -1;

			if ( !is ( symbol::SEMICOLON ) ) {

				test = parse_expression ( );

				if ( test < 0 )
					return -1;

			}

			if ( !expect ( symbol::SEMICOLON, L"; after the for condition" ) )
				return -1;

			if ( !is ( symbol::CLOSE_PAREN ) ) {

				step = parse_expression ( );

				if ( step < 0 )
					return -1;

			}

			if ( !expect ( symbol::CLOSE_PAREN, L") after the for step" ) )
				return -1;

			int32_t body = parse_statement ( );

			if ( body < 0 )
				return -1;

			nodes [ static_cast < size_t > ( made ) ].a = start;
			nodes [ static_cast < size_t > ( made ) ].b = test;
			nodes [ static_cast < size_t > ( made ) ].c = step;
			nodes [ static_cast < size_t > ( made ) ].d = body;

			return made;

		}

		if ( is_word ( L"break" ) || is_word ( L"continue" ) ) {

			int32_t made = make ( is_word ( L"break" ) ? node_kind::BREAK : node_kind::CONTINUE );

			position++;

			if ( !expect ( symbol::SEMICOLON, L";" ) )
				return -1;

			return made;

		}

		if ( is_word ( L"return" ) ) {

			int32_t made = make ( node_kind::RETURN );

			position++;

			if ( !is ( symbol::SEMICOLON ) ) {

				int32_t result = parse_expression ( );

				if ( result < 0 )
					return -1;

				nodes [ static_cast < size_t > ( made ) ].a = result;

			}

			if ( !expect ( symbol::SEMICOLON, L"; after return" ) )
				return -1;

			return made;

		}

		if ( is_word ( L"else" ) ) {

			parse_error ( L"This else has no if before it." );

			return -1;

		}

		int32_t made = make ( node_kind::EXPR );
		int32_t expression = parse_expression ( );

		if ( expression < 0 || !expect ( symbol::SEMICOLON, L"; at the end of the statement" ) )
			return -1;

		nodes [ static_cast < size_t > ( made ) ].a = expression;

		return made;

	}

	int32_t engine::parse_expression ( ) {

		return parse_assignment ( );

	}

	static bool assignment_mark ( symbol mark ) {

		return mark >= symbol::ASSIGN && mark <= symbol::SHIFT_RIGHT_ASSIGN;

	}

	int32_t engine::parse_assignment ( ) {

		int32_t left = parse_ternary ( );

		if ( left < 0 )
			return -1;

		const token& next = peek ( );

		if ( next.type != token_kind::SYMBOL || !assignment_mark ( next.mark ) )
			return left;

		node_kind target = nodes [ static_cast < size_t > ( left ) ].type;

		if ( target != node_kind::NAME && target != node_kind::INDEX ) {

			parse_error ( L"Only a variable or an indexed element can be assigned to." );

			return -1;

		}

		int32_t made = make ( node_kind::ASSIGN );

		nodes [ static_cast < size_t > ( made ) ].mark = next.mark;
		position++;

		int32_t right = parse_assignment ( );

		if ( right < 0 )
			return -1;

		nodes [ static_cast < size_t > ( made ) ].a = left;
		nodes [ static_cast < size_t > ( made ) ].b = right;

		return made;

	}

	int32_t engine::parse_ternary ( ) {

		int32_t test = parse_binary ( 0 );

		if ( test < 0 || !is ( symbol::QUESTION ) )
			return test;

		int32_t made = make ( node_kind::TERNARY );

		position++;

		int32_t yes = parse_assignment ( );

		if ( yes < 0 || !expect ( symbol::COLON, L": in the ?: expression" ) )
			return -1;

		int32_t no = parse_assignment ( );

		if ( no < 0 )
			return -1;

		nodes [ static_cast < size_t > ( made ) ].a = test;
		nodes [ static_cast < size_t > ( made ) ].b = yes;
		nodes [ static_cast < size_t > ( made ) ].c = no;

		return made;

	}

	static bool level_has ( int32_t level, symbol mark ) {

		switch ( level ) {

			case 0:
				return mark == symbol::PIPE_PIPE;

			case 1:
				return mark == symbol::AND_AND;

			case 2:
				return mark == symbol::PIPE;

			case 3:
				return mark == symbol::CARET;

			case 4:
				return mark == symbol::AMPERSAND;

			case 5:
				return mark == symbol::EQUAL_EQUAL || mark == symbol::BANG_EQUAL;

			case 6:
				return mark == symbol::LESS || mark == symbol::LESS_EQUAL || mark == symbol::GREATER || mark == symbol::GREATER_EQUAL;

			case 7:
				return mark == symbol::SHIFT_LEFT || mark == symbol::SHIFT_RIGHT;

			case 8:
				return mark == symbol::PLUS || mark == symbol::MINUS;

			case 9:
				return mark == symbol::STAR || mark == symbol::SLASH || mark == symbol::PERCENT;

			default:
				return false;

		}

	}

	int32_t engine::parse_binary ( int32_t level ) {

		if ( level > 9 )
			return parse_unary ( );

		int32_t left = parse_binary ( level + 1 );

		if ( left < 0 )
			return -1;

		while ( peek ( ).type == token_kind::SYMBOL && level_has ( level, peek ( ).mark ) ) {

			symbol mark = peek ( ).mark;
			int32_t made = make ( level == 0 ? node_kind::OR : ( level == 1 ? node_kind::AND : node_kind::BINARY ) );

			position++;

			int32_t right = parse_binary ( level + 1 );

			if ( right < 0 )
				return -1;

			nodes [ static_cast < size_t > ( made ) ].mark = mark;
			nodes [ static_cast < size_t > ( made ) ].a = left;
			nodes [ static_cast < size_t > ( made ) ].b = right;
			left = made;

		}

		return left;

	}

	int32_t engine::parse_unary ( ) {

		const token& next = peek ( );

		if ( next.type == token_kind::SYMBOL && ( next.mark == symbol::MINUS || next.mark == symbol::PLUS || next.mark == symbol::BANG || next.mark == symbol::TILDE ) ) {

			int32_t made = make ( node_kind::UNARY );

			nodes [ static_cast < size_t > ( made ) ].mark = next.mark;
			position++;

			int32_t operand = parse_unary ( );

			if ( operand < 0 )
				return -1;

			nodes [ static_cast < size_t > ( made ) ].a = operand;

			return made;

		}

		if ( next.type == token_kind::SYMBOL && ( next.mark == symbol::PLUS_PLUS || next.mark == symbol::MINUS_MINUS ) ) {

			int32_t made = make ( node_kind::PREFIX );

			nodes [ static_cast < size_t > ( made ) ].mark = next.mark;
			position++;

			int32_t operand = parse_unary ( );

			if ( operand < 0 )
				return -1;

			node_kind target = nodes [ static_cast < size_t > ( operand ) ].type;

			if ( target != node_kind::NAME && target != node_kind::INDEX ) {

				parse_error ( L"++ and -- need a variable or an indexed element." );

				return -1;

			}

			nodes [ static_cast < size_t > ( made ) ].a = operand;

			return made;

		}

		return parse_postfix ( );

	}

	int32_t engine::parse_postfix ( ) {

		int32_t left = parse_primary ( );

		if ( left < 0 )
			return -1;

		while ( true ) {

			if ( is ( symbol::OPEN_PAREN ) ) {

				if ( nodes [ static_cast < size_t > ( left ) ].type != node_kind::NAME ) {

					parse_error ( L"Only functions can be called, by name." );

					return -1;

				}

				nodes [ static_cast < size_t > ( left ) ].type = node_kind::CALL;
				position++;

				base::list < int32_t > args;

				if ( !is ( symbol::CLOSE_PAREN ) ) {

					do {

						int32_t arg = parse_assignment ( );

						if ( arg < 0 )
							return -1;

						args.push ( arg );

					} while ( accept ( symbol::COMMA ) );

				}

				if ( !expect ( symbol::CLOSE_PAREN, L") after the arguments" ) )
					return -1;

				nodes [ static_cast < size_t > ( left ) ].first = items.count ( );
				nodes [ static_cast < size_t > ( left ) ].count = args.count ( );

				for ( int32_t arg : args )
					items.push ( arg );

				continue;

			}

			if ( is ( symbol::OPEN_BRACKET ) ) {

				int32_t made = make ( node_kind::INDEX );

				position++;

				int32_t index = parse_expression ( );

				if ( index < 0 || !expect ( symbol::CLOSE_BRACKET, L"] after the index" ) )
					return -1;

				nodes [ static_cast < size_t > ( made ) ].a = left;
				nodes [ static_cast < size_t > ( made ) ].b = index;
				left = made;

				continue;

			}

			if ( is ( symbol::PLUS_PLUS ) || is ( symbol::MINUS_MINUS ) ) {

				node_kind target = nodes [ static_cast < size_t > ( left ) ].type;

				if ( target != node_kind::NAME && target != node_kind::INDEX ) {

					parse_error ( L"++ and -- need a variable or an indexed element." );

					return -1;

				}

				int32_t made = make ( node_kind::POSTFIX );

				nodes [ static_cast < size_t > ( made ) ].mark = peek ( ).mark;
				nodes [ static_cast < size_t > ( made ) ].a = left;
				position++;
				left = made;

				continue;

			}

			return left;

		}

	}

	int32_t engine::parse_primary ( ) {

		const token next = peek ( );

		if ( next.type == token_kind::NUMBER ) {

			int32_t made = make ( node_kind::NUMBER );

			nodes [ static_cast < size_t > ( made ) ].number = next.number;
			position++;

			return made;

		}

		if ( next.type == token_kind::TEXT ) {

			int32_t made = make ( node_kind::TEXT );

			nodes [ static_cast < size_t > ( made ) ].text = next.text;
			position++;

			return made;

		}

		if ( next.type == token_kind::NAME ) {

			const wchar_t* word = texts [ static_cast < size_t > ( next.text ) ].data ( );

			if ( base::same_text ( word, L"true" ) || base::same_text ( word, L"false" ) ) {

				int32_t made = make ( node_kind::NUMBER );

				nodes [ static_cast < size_t > ( made ) ].number = word [ 0 ] == L't' ? 1 : 0;
				position++;

				return made;

			}

			if ( base::same_text ( word, L"none" ) || base::same_text ( word, L"null" ) ) {

				int32_t made = make ( node_kind::NOTHING );

				position++;

				return made;

			}

			if ( reserved_word ( word ) ) {

				base::writer message;

				message.add ( L"'" ).add ( word ).add ( L"' can't be used here." );
				parse_error ( message.text ( ) );

				return -1;

			}

			int32_t made = make ( node_kind::NAME );

			nodes [ static_cast < size_t > ( made ) ].text = next.text;
			position++;

			return made;

		}

		if ( accept ( symbol::OPEN_PAREN ) ) {

			int32_t inner = parse_expression ( );

			if ( inner < 0 || !expect ( symbol::CLOSE_PAREN, L")" ) )
				return -1;

			return inner;

		}

		if ( is ( symbol::OPEN_BRACKET ) ) {

			int32_t made = make ( node_kind::LIST );
			base::list < int32_t > children;

			position++;

			if ( !is ( symbol::CLOSE_BRACKET ) ) {

				do {

					if ( is ( symbol::CLOSE_BRACKET ) )
						break;

					int32_t child = parse_assignment ( );

					if ( child < 0 )
						return -1;

					children.push ( child );

				} while ( accept ( symbol::COMMA ) );

			}

			if ( !expect ( symbol::CLOSE_BRACKET, L"] at the end of the list" ) )
				return -1;

			nodes [ static_cast < size_t > ( made ) ].first = items.count ( );
			nodes [ static_cast < size_t > ( made ) ].count = children.count ( );

			for ( int32_t child : children )
				items.push ( child );

			return made;

		}

		if ( next.type == token_kind::END )
			parse_error ( L"Expected a value but the script ended." );
		else
			parse_error ( L"Expected a value here." );

		return -1;

	}

}
