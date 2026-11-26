#include "script/resolver/resolver.h"

namespace script {

	bool engine::resolve ( ) {

		global_names.clear ( );

		for ( int32_t statement : top_level ) {

			const node& item = nodes [ static_cast < size_t > ( statement ) ];

			if ( item.type != node_kind::VAR )
				continue;

			for ( int32_t i = 0; i < item.count; i++ ) {

				int32_t name = nodes [ static_cast < size_t > ( items [ static_cast < size_t > ( item.first + i ) ] ) ].text;
				bool known = false;

				for ( int32_t existing : global_names )
					known = known || existing == name;

				if ( !known )
					global_names.push ( name );

			}

		}

		base::list < scope_entry > scope;
		int32_t next = 0;
		int32_t most = 0;

		for ( int32_t statement : top_level ) {

			if ( !resolve_node ( statement, scope, next, most, true ) )
				return false;

		}

		top_slots = most;

		for ( function_def& item : functions ) {

			scope.clear ( );
			next = 0;
			most = 0;

			for ( int32_t i = 0; i < item.params; i++ ) {

				scope_entry& entry = scope.emplace ( );

				entry.name = params [ static_cast < size_t > ( item.first_param + i ) ];
				entry.slot = next++;

			}

			most = next;

			if ( !resolve_node ( item.body, scope, next, most, false ) )
				return false;

			item.slots = most;

		}

		return true;

	}

	bool engine::resolve_node ( int32_t index, base::list < scope_entry >& scope, int32_t& next, int32_t& most, bool top ) {

		if ( index < 0 )
			return true;

		node& item = nodes [ static_cast < size_t > ( index ) ];

		auto name_error = [ & ] ( const node& at, const wchar_t* tail ) {

			base::writer message;

			message.add ( L"'" ).add ( texts [ static_cast < size_t > ( at.text ) ].data ( ) ).add ( tail );

			return lex_error ( at.line, at.column, at.file, message.text ( ) );

		};

		switch ( item.type ) {

			case node_kind::NAME: {

				for ( int32_t i = scope.count ( ) - 1; i >= 0; i-- ) {

					if ( scope [ static_cast < size_t > ( i ) ].name == item.text ) {

						item.b = 0;
						item.c = scope [ static_cast < size_t > ( i ) ].slot;

						return true;

					}

				}

				for ( int32_t i = 0; i < global_names.count ( ); i++ ) {

					if ( global_names [ static_cast < size_t > ( i ) ] == item.text ) {

						item.b = 1;
						item.c = i;

						return true;

					}

				}

				for ( const function_def& known : functions ) {

					if ( known.name == item.text )
						return name_error ( item, L"' is a function. Call it with ()." );

				}

				return name_error ( item, L"' isn't defined. Declare it with var." );

			}

			case node_kind::VAR: {

				for ( int32_t i = 0; i < item.count; i++ ) {

					int32_t child_index = items [ static_cast < size_t > ( item.first + i ) ];

					if ( !resolve_node ( nodes [ static_cast < size_t > ( child_index ) ].a, scope, next, most, false ) )
						return false;

					node& child = nodes [ static_cast < size_t > ( child_index ) ];

					if ( top ) {

						for ( int32_t g = 0; g < global_names.count ( ); g++ ) {

							if ( global_names [ static_cast < size_t > ( g ) ] == child.text ) {

								child.b = 1;
								child.c = g;

							}

						}

						continue;

					}

					scope_entry& entry = scope.emplace ( );

					entry.name = child.text;
					entry.slot = next++;
					most = base::max ( most, next );
					child.b = 0;
					child.c = entry.slot;

				}

				return true;

			}

			case node_kind::BLOCK: {

				int32_t mark = scope.count ( );

				for ( int32_t i = 0; i < item.count; i++ ) {

					if ( !resolve_node ( items [ static_cast < size_t > ( item.first + i ) ], scope, next, most, false ) )
						return false;

				}

				while ( scope.count ( ) > mark )
					scope.pop ( );

				return true;

			}

			case node_kind::FOR: {

				int32_t mark = scope.count ( );
				int32_t start = item.a;
				int32_t test = item.b;
				int32_t step = item.c;
				int32_t body = item.d;
				bool ok = resolve_node ( start, scope, next, most, false ) && resolve_node ( test, scope, next, most, false ) && resolve_node ( step, scope, next, most, false ) && resolve_node ( body, scope, next, most, false );

				while ( scope.count ( ) > mark )
					scope.pop ( );

				return ok;

			}

			case node_kind::CALL: {

				item.b = 2;
				item.c = -1;

				for ( int32_t i = 0; i < functions.count ( ); i++ ) {

					if ( functions [ static_cast < size_t > ( i ) ].name == item.text ) {

						item.b = 0;
						item.c = i;

					}

				}

				int32_t first = item.first;
				int32_t count = item.count;

				for ( int32_t i = 0; i < count; i++ ) {

					if ( !resolve_node ( items [ static_cast < size_t > ( first + i ) ], scope, next, most, false ) )
						return false;

				}

				return true;

			}

			case node_kind::LIST: {

				int32_t first = item.first;
				int32_t count = item.count;

				for ( int32_t i = 0; i < count; i++ ) {

					if ( !resolve_node ( items [ static_cast < size_t > ( first + i ) ], scope, next, most, false ) )
						return false;

				}

				return true;

			}

			case node_kind::NUMBER:
			case node_kind::TEXT:
			case node_kind::NOTHING:
			case node_kind::BREAK:
			case node_kind::CONTINUE:
			case node_kind::EMPTY:
				return true;

			default: {

				int32_t a = item.a;
				int32_t b = item.b;
				int32_t c = item.c;
				int32_t d = item.d;

				return resolve_node ( a, scope, next, most, false ) && resolve_node ( b, scope, next, most, false ) && resolve_node ( c, scope, next, most, false ) && resolve_node ( d, scope, next, most, false );

			}

		}

	}

}
