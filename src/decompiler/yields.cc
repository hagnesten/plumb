#include "decompiler/internal.hh"

namespace decompiler {

	using x86::mnemonic;

	static bool starts_with ( const wchar_t* text, const wchar_t* head ) {

		for ( ; *head; head++, text++ ) {

			if ( *text != *head )
				return false;

		}

		return *text == L' ';

	}

	static int32_t declared_width ( const wchar_t* text ) {

		if ( !text )
			return -1;

		const wchar_t* star = text;

		while ( *star && *star != L'(' && *star != L'*' )
			star++;

		if ( *star == L'*' )
			return 8;

		if ( starts_with ( text, L"void" ) )
			return 0;

		if ( starts_with ( text, L"bool" ) )
			return 2;

		if ( starts_with ( text, L"char" ) || starts_with ( text, L"BOOLEAN" ) || starts_with ( text, L"BYTE" ) || starts_with ( text, L"UCHAR" ) || starts_with ( text, L"_BYTE" ) || starts_with ( text, L"unsigned char" ) )
			return 1;

		return 8;

	}

	static int32_t known_result ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target ) {

		if ( names ) {

			if ( const wchar_t* declared = names->prototype ( target ) )
				return declared_width ( declared );

			wchar_t name [ 200 ];

			if ( names->name ( target, true, name, 200 ) ) {

				if ( const wchar_t* api = api_prototype ( name ) )
					return declared_width ( api );

			}

		}

		int32_t callee = db.function_starting ( target );

		if ( callee >= 0 ) {

			const analysis::function_info& info = db.functions [ static_cast < size_t > ( callee ) ];

			if ( ( info.flags & analysis::THUNK ) && info.import >= 0 && info.import < file.imports.count ( ) ) {

				const wchar_t* api = api_prototype ( file.imports [ static_cast < size_t > ( info.import ) ].name.get ( ) );

				return api ? declared_width ( api ) : 8;

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

	static int32_t callee_width ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target, bool long_mode, int32_t depth ) {

		if ( !target )
			return 8;

		int32_t known = known_result ( db, file, names, target );

		if ( known >= 0 )
			return known;

		if ( depth >= 3 || db.function_starting ( target ) < 0 )
			return 8;

		return yields_value ( db, file, names, target, long_mode, depth + 1 );

	}

	static bool zeroing ( const x86::instruction& ins ) {

		return ins.name == mnemonic::xor_ && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 1 ].reg == ins.operands [ 0 ].reg;

	}

	static int32_t def_width ( const x86::instruction& ins, bool zeroed ) {

		if ( ins.count > 0 && ins.operands [ 0 ].type == x86::kind::REG ) {

			if ( zeroing ( ins ) )
				return 0;

			if ( ins.operands [ 0 ].reg == x86::AL ) {

				if ( static_cast < int32_t > ( ins.name ) >= static_cast < int32_t > ( mnemonic::seto ) && static_cast < int32_t > ( ins.name ) <= static_cast < int32_t > ( mnemonic::setg ) )
					return zeroed ? 3 : 2;

				return 1;

			}

		}

		return 8;

	}

	int32_t yields_value ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, bool long_mode, int32_t depth ) {

		analysis::function_graph graph;

		if ( !analysis::build_graph ( db, function, graph ) )
			return 8;

		int32_t count = graph.blocks.count ( );
		base::list < int8_t > state;
		base::list < uint8_t > exits;
		base::list < uint8_t > zeroed;
		base::list < x86::instruction > last_def;
		base::list < x86::instruction > last_ins;

		state.fill ( static_cast < size_t > ( count ), 0 );
		exits.fill ( static_cast < size_t > ( count ), 0 );
		zeroed.fill ( static_cast < size_t > ( count ), 0 );
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

					bool partial = ins.count > 0 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 0 ].reg == x86::AL;

					if ( !partial )
						zeroed [ static_cast < size_t > ( i ) ] = zeroing ( ins ) ? 1 : 0;

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
			int32_t width = ins.name != mnemonic::call ? def_width ( ins, zeroed [ static_cast < size_t > ( block ) ] != 0 ) + 16 : callee_width ( db, file, names, call_target ( ins ), long_mode, depth );

			verdict [ static_cast < size_t > ( block ) ] = static_cast < int8_t > ( width );

			return width;

		};

		base::list < uint8_t > seen;
		base::list < int32_t > work;
		bool found_plain = false;
		bool found_call = false;
		bool found_void = false;
		bool found_undefined = false;
		uint32_t kinds = 0;

		auto note = [ & ] ( int32_t kind ) {

			if ( kind == 0 ) {

				found_void = true;

				return;

			}

			if ( kind >= 16 ) {

				found_plain = true;
				kind -= 16;

			} else
				found_call = true;

			kinds |= 1u << kind;

		};

		for ( int32_t i = 0; i < count; i++ ) {

			if ( exits [ static_cast < size_t > ( i ) ] == 2 ) {

				note ( callee_width ( db, file, names, call_target ( last_ins [ static_cast < size_t > ( i ) ] ), long_mode, depth ) );

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

					note ( judge ( at ) );

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

		if ( found_void || !( found_plain || ( found_call && !found_undefined ) ) )
			return 0;

		if ( kinds & ( 1u << 8 ) )
			return 8;

		if ( kinds & ( 1u << 1 ) )
			return 1;

		if ( kinds & ( 1u << 3 ) )
			return 3;

		if ( kinds & ( 1u << 2 ) )
			return 2;

		return 8;

	}

}
