#include "decompiler/internal.hh"

namespace decompiler {

	using x86::mnemonic;

	static bool declared_void ( const wchar_t* text ) {

		if ( !text || text [ 0 ] != L'v' || text [ 1 ] != L'o' || text [ 2 ] != L'i' || text [ 3 ] != L'd' )
			return false;

		const wchar_t* after = text + 4;

		if ( *after != L' ' && *after != L'*' )
			return false;

		while ( *after == L' ' )
			after++;

		return *after != L'*';

	}

	static int32_t known_result ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target ) {

		if ( names ) {

			if ( const wchar_t* declared = names->prototype ( target ) )
				return declared_void ( declared ) ? 0 : 1;

			wchar_t name [ 200 ];

			if ( names->name ( target, true, name, 200 ) ) {

				if ( const wchar_t* api = api_prototype ( name ) )
					return declared_void ( api ) ? 0 : 1;

			}

		}

		int32_t callee = db.function_starting ( target );

		if ( callee >= 0 ) {

			const analysis::function_info& info = db.functions [ static_cast < size_t > ( callee ) ];

			if ( ( info.flags & analysis::THUNK ) && info.import >= 0 && info.import < file.imports.count ( ) ) {

				const wchar_t* api = api_prototype ( file.imports [ static_cast < size_t > ( info.import ) ].name.get ( ) );

				return api && declared_void ( api ) ? 0 : 1;

			}

		}

		return -1;

	}

	static uint64_t call_target ( const x86::instruction& ins ) {

		if ( ins.direct ( ) )
			return ins.target ( );

		const x86::operand& m = ins.operands [ 0 ];

		if ( m.type == x86::kind::MEM && m.rip_relative ( ) )
			return ins.next ( ) + static_cast < uint64_t > ( m.value );

		return 0;

	}

	static bool callee_yields ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target, bool long_mode, int32_t depth ) {

		if ( !target )
			return true;

		int32_t known = known_result ( db, file, names, target );

		if ( known >= 0 )
			return known == 1;

		if ( depth >= 3 || db.function_starting ( target ) < 0 )
			return true;

		return yields_value ( db, file, names, target, long_mode, depth + 1 );

	}

	bool yields_value ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, bool long_mode, int32_t depth ) {

		analysis::function_graph graph;

		if ( !analysis::build_graph ( db, function, graph ) )
			return true;

		int32_t count = graph.blocks.count ( );
		base::list < int8_t > state;
		base::list < uint8_t > exits;
		base::list < x86::instruction > last_def;
		base::list < x86::instruction > last_ins;

		state.fill ( static_cast < size_t > ( count ), 0 );
		exits.fill ( static_cast < size_t > ( count ), 0 );
		last_def.resize ( static_cast < size_t > ( count ) );
		last_ins.resize ( static_cast < size_t > ( count ) );

		for ( int32_t i = 0; i < count; i++ ) {

			const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
			uint64_t at = item.start;
			bool any = false;

			while ( at < item.end ) {

				x86::instruction ins;

				if ( !db.decode ( at, ins ) )
					break;

				uint32_t use = 0;
				uint32_t def = 0;

				effects ( ins, long_mode, use, def );

				if ( def & 1u ) {

					last_def [ static_cast < size_t > ( i ) ] = ins;
					state [ static_cast < size_t > ( i ) ] = 1;

				}

				last_ins [ static_cast < size_t > ( i ) ] = ins;
				any = true;
				at = ins.next ( );

			}

			if ( !any )
				continue;

			const x86::instruction& end = last_ins [ static_cast < size_t > ( i ) ];

			if ( end.name == mnemonic::ret )
				exits [ static_cast < size_t > ( i ) ] = 1;
			else if ( end.control == x86::flow::JUMP && item.out_count == 0 )
				exits [ static_cast < size_t > ( i ) ] = 2;

		}

		base::list < int8_t > verdict;

		verdict.fill ( static_cast < size_t > ( count ), -1 );

		auto judge = [ & ] ( int32_t block ) {

			if ( verdict [ static_cast < size_t > ( block ) ] >= 0 )
				return static_cast < int32_t > ( verdict [ static_cast < size_t > ( block ) ] );

			const x86::instruction& ins = last_def [ static_cast < size_t > ( block ) ];
			int32_t kind = ins.name != mnemonic::call ? 1 : ( callee_yields ( db, file, names, call_target ( ins ), long_mode, depth ) ? 2 : 0 );

			verdict [ static_cast < size_t > ( block ) ] = static_cast < int8_t > ( kind );

			return kind;

		};

		base::list < uint8_t > seen;
		base::list < int32_t > work;
		bool found_plain = false;
		bool found_call = false;
		bool found_void = false;
		bool found_undefined = false;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( exits [ static_cast < size_t > ( i ) ] == 2 ) {

				if ( callee_yields ( db, file, names, call_target ( last_ins [ static_cast < size_t > ( i ) ] ), long_mode, depth ) )
					found_call = true;
				else
					found_void = true;

				continue;

			}

			if ( exits [ static_cast < size_t > ( i ) ] != 1 )
				continue;

			seen.clear ( );
			seen.fill ( static_cast < size_t > ( count ), 0 );
			work.clear ( );
			work.push ( i );
			seen [ static_cast < size_t > ( i ) ] = 1;

			while ( work.count ( ) ) {

				int32_t at = work [ static_cast < size_t > ( work.count ( ) - 1 ) ];

				work.pop ( );

				if ( state [ static_cast < size_t > ( at ) ] ) {

					int32_t kind = judge ( at );

					found_plain = found_plain || kind == 1;
					found_call = found_call || kind == 2;
					found_void = found_void || kind == 0;

					continue;

				}

				if ( at == 0 )
					found_undefined = true;

				for ( const analysis::edge& link : graph.edges ) {

					if ( link.to != at || link.from < 0 || link.from >= count || seen [ static_cast < size_t > ( link.from ) ] )
						continue;

					seen [ static_cast < size_t > ( link.from ) ] = 1;
					work.push ( link.from );

				}

			}

		}

		return !found_void && ( found_plain || ( found_call && !found_undefined ) );

	}

}
