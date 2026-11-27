#include "decompiler/lift/yields/yields.h"
#include "decompiler/lift/effects/effects.h"
#include "decompiler/lift/flags/flags.h"
#include "decompiler/apis/apis.h"
#include "decompiler/prototypes/prototypes.h"
#include "analysis/graph/graph.h"
#include "x86/decoder/decoder.h"
#include "base/strings/strings.h"

bool decompiler::string_params = true;

bool decompiler::starts_with ( const wchar_t* text, const wchar_t* head ) {

	for ( ; *head; head++, text++ ) {

		if ( *text != *head )
			return false;

	}

	return *text == L' ';

}

int32_t decompiler::declared_width ( const wchar_t* text ) {

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

int32_t decompiler::known_result ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target ) {

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

uint64_t decompiler::call_target ( const x86::instruction& ins ) {

	if ( ins.direct ( ) )
		return ins.target ( );

	const x86::operand& m = ins.operands [ 0 ];

	if ( m.type == x86::kind::MEM && m.rip_relative ( ) )
		return ins.next ( ) + static_cast < uint64_t > ( m.value );

	return 0;

}

int32_t decompiler::callee_width ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target, bool long_mode, int32_t depth ) {

	if ( !target )
		return 8;

	int32_t known = known_result ( db, file, names, target );

	if ( known >= 0 )
		return known;

	if ( depth >= 3 || db.function_starting ( target ) < 0 )
		return 8;

	return yields_value ( db, file, names, target, long_mode, depth + 1 );

}

bool decompiler::zeroing ( const x86::instruction& ins ) {

	return ins.name == mnemonic::xor_ && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 1 ].reg == ins.operands [ 0 ].reg;

}

int32_t decompiler::def_width ( const x86::instruction& ins, bool zeroed ) {

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

bool decompiler::callee_parameter_type ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, int32_t index, wchar_t* out, int32_t room ) {

	int32_t callee = db.function_starting ( function );

	if ( callee < 0 || ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) || index < 0 || index > 3 || !names )
		return false;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) )
		return false;

	static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
	int32_t holds [ 16 ];

	for ( int32_t& slot : holds )
		slot = -1;

	for ( int32_t i = 0; i < 4; i++ )
		holds [ gpr [ i ] ] = i;

	for ( int32_t b = 0; b < graph.blocks.count ( ) && b < 6; b++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( b ) ];
		uint64_t at = item.start;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return false;

			at = ins.next ( );

			if ( ins.name == mnemonic::call || ( ins.control == x86::flow::JUMP && item.out_count == 0 ) ) {

				uint64_t target = call_target ( ins );
				wchar_t name [ 200 ];
				const wchar_t* api = nullptr;

				if ( target && names->name ( target, true, name, 200 ) )
					api = api_prototype ( name );

				if ( api ) {

					c_prototype shape;
					base::writer problem;

					if ( parse_prototype ( api, true, shape, problem ) ) {

						for ( int32_t j = 0; j < 4 && j < shape.count; j++ ) {

							if ( holds [ gpr [ j ] ] != index || !shape.params [ j ].pointer )
								continue;

							const wchar_t* text = shape.types [ j ];

							if ( base::same_text ( text, L"LPVOID" ) || base::same_text ( text, L"PVOID" ) )
								text = L"void *";
							else if ( base::same_text ( text, L"LPCVOID" ) )
								text = L"const void *";

							int32_t length = 0;

							while ( text [ length ] && length < room - 1 ) {

								out [ length ] = text [ length ];
								length++;

							}

							out [ length ] = 0;

							return length > 0;

						}

					}

				}

				static const int32_t volatile_regs [ ] = { 0, 1, 2, 8, 9, 10, 11 };

				for ( int32_t slot : volatile_regs )
					holds [ slot ] = -1;

				continue;

			}

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );

			bool filling = ins.name == mnemonic::stosb || ins.name == mnemonic::stosw || ins.name == mnemonic::stosd || ins.name == mnemonic::stosq;
			bool copying = ins.name == mnemonic::movsb || ins.name == mnemonic::movsw || ins.name == mnemonic::movsq;

			if ( string_params && ( filling || copying ) ) {

				const wchar_t* text = nullptr;

				if ( holds [ 7 ] == index )
					text = L"void *";
				else if ( copying && holds [ 6 ] == index )
					text = L"const void *";
				else if ( holds [ 1 ] == index )
					text = L"unsigned __int64";

				if ( text ) {

					int32_t length = 0;

					while ( text [ length ] && length < room - 1 ) {

						out [ length ] = text [ length ];
						length++;

					}

					out [ length ] = 0;

					return true;

				}

			}

			if ( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && x86::register_size ( ins.operands [ 0 ].reg ) == 8 ) {

				int32_t to = x86::register_family ( ins.operands [ 0 ].reg );
				int32_t from = x86::register_family ( ins.operands [ 1 ].reg );

				if ( to >= 0 && to < 16 && from >= 0 && from < 16 ) {

					holds [ to ] = holds [ from ];

					continue;

				}

			}

			for ( int32_t slot = 0; slot < 16; slot++ ) {

				if ( def & ( 1u << slot ) )
					holds [ slot ] = -1;

			}

		}

	}

	return false;

}

bool decompiler::callee_thread_parameter ( const analysis::database& db, namer* names, uint64_t function, int32_t index, int32_t depth ) {

	int32_t callee = db.function_starting ( function );

	if ( callee < 0 || ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) || index < 0 || index > 3 || !names || depth > 3 )
		return false;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) )
		return false;

	static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
	int32_t holds [ 16 ];

	for ( int32_t& slot : holds )
		slot = -1;

	holds [ gpr [ index ] ] = index;

	for ( int32_t b = 0; b < graph.blocks.count ( ) && b < 400; b++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( b ) ];
		uint64_t at = item.start;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return false;

			at = ins.next ( );

			if ( ins.name == mnemonic::call || ( ins.control == x86::flow::JUMP && item.out_count == 0 ) ) {

				uint64_t target = call_target ( ins );
				wchar_t name [ 200 ];
				const wchar_t* api = nullptr;

				if ( target && names->name ( target, true, name, 200 ) )
					api = api_prototype ( name );

				for ( int32_t j = 0; j < 4; j++ ) {

					if ( holds [ gpr [ j ] ] != index )
						continue;

					if ( api ) {

						c_prototype shape;
						base::writer problem;

						if ( parse_prototype ( api, true, shape, problem ) && j < shape.count && base::same_text ( shape.names [ j ], L"lpParameter" ) )
							return true;

					} else if ( target && !names->prototype ( target ) && callee_thread_parameter ( db, names, target, j, depth + 1 ) )
						return true;

				}

				static const int32_t volatile_regs [ ] = { 0, 1, 2, 8, 9, 10, 11 };

				for ( int32_t slot : volatile_regs )
					holds [ slot ] = -1;

				continue;

			}

			if ( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && x86::register_size ( ins.operands [ 0 ].reg ) == 8 ) {

				int32_t to = x86::register_family ( ins.operands [ 0 ].reg );
				int32_t from = x86::register_family ( ins.operands [ 1 ].reg );

				if ( to >= 0 && to < 16 && from >= 0 && from < 16 ) {

					holds [ to ] = holds [ from ];

					continue;

				}

			}

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );

			for ( int32_t slot = 0; slot < 16; slot++ ) {

				if ( def & ( 1u << slot ) )
					holds [ slot ] = -1;

			}

		}

	}

	return false;

}

bool decompiler::callee_void_elements ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, int32_t index, int32_t depth ) {

	int32_t callee = db.function_starting ( function );

	if ( callee < 0 || ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) || index < 0 || index > 3 || !names || depth > 2 || names->prototype ( function ) )
		return false;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) || graph.blocks.count ( ) > 200 )
		return false;

	static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
	uint8_t pointer [ 16 ] = { };
	uint8_t element [ 16 ] = { };

	pointer [ gpr [ index ] ] = 1;

	for ( int32_t b = 0; b < graph.blocks.count ( ); b++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( b ) ];
		uint64_t at = item.start;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return false;

			at = ins.next ( );

			if ( ins.name == mnemonic::call || ( ins.control == x86::flow::JUMP && item.out_count == 0 ) ) {

				uint64_t target = call_target ( ins );
				wchar_t name [ 200 ];
				const wchar_t* api = nullptr;

				if ( target && names->name ( target, true, name, 200 ) )
					api = api_prototype ( name );

				for ( int32_t j = 0; j < 4 && target; j++ ) {

					if ( element [ gpr [ j ] ] ) {

						if ( api ) {

							c_prototype shape;
							base::writer problem;

							if ( parse_prototype ( api, true, shape, problem ) && j < shape.count && void_pointer_text ( shape.types [ j ] ) )
								return true;

						} else {

							wchar_t type [ 64 ];

							if ( callee_parameter_type ( db, file, names, target, j, type, 64 ) && ( base::same_text ( type, L"void *" ) || base::same_text ( type, L"const void *" ) ) )
								return true;

						}

					}

					if ( pointer [ gpr [ j ] ] && !api && callee_void_elements ( db, file, names, target, j, depth + 1 ) )
						return true;

				}

				static const int32_t volatile_regs [ ] = { 0, 1, 2, 8, 9, 10, 11 };

				for ( int32_t slot : volatile_regs ) {

					pointer [ slot ] = 0;
					element [ slot ] = 0;

				}

				continue;

			}

			if ( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && x86::register_size ( ins.operands [ 0 ].reg ) == 8 ) {

				int32_t to = x86::register_family ( ins.operands [ 0 ].reg );
				const x86::operand& from_operand = ins.operands [ 1 ];

				if ( to >= 0 && to < 16 && from_operand.type == x86::kind::REG && x86::register_size ( from_operand.reg ) == 8 ) {

					int32_t from = x86::register_family ( from_operand.reg );

					if ( from >= 0 && from < 16 ) {

						pointer [ to ] = pointer [ from ];
						element [ to ] = element [ from ];

						continue;

					}

				}

				if ( to >= 0 && to < 16 && from_operand.type == x86::kind::MEM && from_operand.size == 8 && !from_operand.rip_relative ( ) ) {

					int32_t base_family = x86::register_family ( from_operand.base );
					bool through = base_family >= 0 && base_family < 16 && pointer [ base_family ] && from_operand.index == 0;

					pointer [ to ] = 0;
					element [ to ] = through ? 1 : 0;

					continue;

				}

			}

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );

			for ( int32_t slot = 0; slot < 16; slot++ ) {

				if ( def & ( 1u << slot ) ) {

					pointer [ slot ] = 0;
					element [ slot ] = 0;

				}

			}

		}

	}

	return false;

}

bool decompiler::void_pointer_text ( const wchar_t* text ) {

	return base::same_text ( text, L"LPVOID" ) || base::same_text ( text, L"PVOID" ) || base::same_text ( text, L"void *" ) || base::same_text ( text, L"HLOCAL" ) || base::same_text ( text, L"HGLOBAL" );

}

bool decompiler::callee_void_result ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t target, int32_t depth ) {

	if ( !target || depth > 2 || !names )
		return false;

	wchar_t name [ 200 ];
	const wchar_t* api = nullptr;

	if ( const wchar_t* declared = names->prototype ( target ) )
		api = declared;
	else if ( names->name ( target, true, name, 200 ) )
		api = api_prototype ( name );

	int32_t callee = db.function_starting ( target );

	if ( !api && callee >= 0 && ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) && db.functions [ static_cast < size_t > ( callee ) ].import >= 0 && db.functions [ static_cast < size_t > ( callee ) ].import < file.imports.count ( ) )
		api = api_prototype ( file.imports [ static_cast < size_t > ( db.functions [ static_cast < size_t > ( callee ) ].import ) ].name.get ( ) );

	if ( api ) {

		c_prototype shape;
		base::writer problem;

		return parse_prototype ( api, true, shape, problem ) && void_pointer_text ( shape.result );

	}

	if ( callee < 0 )
		return false;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, target, graph ) || graph.blocks.count ( ) > 40 )
		return false;

	bool any = false;

	for ( int32_t i = 0; i < graph.blocks.count ( ); i++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
		uint64_t at = item.start;
		x86::instruction last;
		x86::instruction producer;
		bool produced = false;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return false;

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );

			if ( def & 1u ) {

				producer = ins;
				produced = true;

			}

			last = ins;
			at = ins.next ( );

		}

		bool tail = last.control == x86::flow::JUMP && item.out_count == 0;

		if ( last.name != mnemonic::ret && !tail )
			continue;

		uint64_t source = 0;

		if ( tail )
			source = call_target ( last );
		else if ( produced && producer.name == mnemonic::call )
			source = call_target ( producer );
		else
			return false;

		if ( !callee_void_result ( db, file, names, source, depth + 1 ) )
			return false;

		any = true;

	}

	return any;

}

int32_t decompiler::callee_argument_width ( const analysis::database& db, uint64_t function, int32_t index, int32_t depth ) {

	int32_t callee = db.function_starting ( function );

	if ( callee < 0 || ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) || index < 0 || index > 3 || depth > 2 )
		return 0;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) || graph.blocks.count ( ) == 0 )
		return 0;

	static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
	uint32_t mask = 1u << gpr [ index ];
	uint64_t at = graph.blocks [ 0 ].start;
	uint64_t end = graph.blocks [ 0 ].end;
	int32_t steps = 0;

	while ( at < end && steps++ < 64 ) {

		x86::instruction ins;

		if ( !db.decode ( at, ins ) )
			return 0;

		at = ins.next ( );

		if ( ins.name == mnemonic::call || ( ins.control == x86::flow::JUMP && ins.next ( ) >= end ) ) {

			uint64_t target = call_target ( ins );

			for ( int32_t j = 0; j < 4; j++ ) {

				if ( mask & ( 1u << gpr [ j ] ) )
					return target ? callee_argument_width ( db, target, j, depth + 1 ) : 0;

			}

			return 0;

		}

		if ( ins.name == mnemonic::push || ins.name == mnemonic::pop )
			continue;

		if ( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::MEM && ins.operands [ 0 ].base && x86::register_family ( ins.operands [ 0 ].base ) == 4 && ins.operands [ 0 ].index == x86::NONE && ins.operands [ 1 ].type == x86::kind::REG )
			continue;

		if ( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && x86::register_size ( ins.operands [ 0 ].reg ) == 8 && x86::register_size ( ins.operands [ 1 ].reg ) == 8 ) {

			int32_t to = x86::register_family ( ins.operands [ 0 ].reg );
			int32_t from = x86::register_family ( ins.operands [ 1 ].reg );

			if ( to >= 0 && to < 16 && from >= 0 && from < 16 ) {

				if ( mask & ( 1u << from ) )
					mask |= 1u << to;
				else
					mask &= ~( 1u << to );

				continue;

			}

		}

		bool zero = ins.name == mnemonic::xor_ && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 0 ].reg == ins.operands [ 1 ].reg;

		for ( int32_t k = 0; k < ins.count && !zero; k++ ) {

			const x86::operand& o = ins.operands [ k ];

			if ( o.type != x86::kind::MEM )
				continue;

			int32_t base = o.base ? x86::register_family ( o.base ) : -1;
			int32_t index_reg = o.index ? x86::register_family ( o.index ) : -1;

			if ( ( base >= 0 && base < 16 && ( mask & ( 1u << base ) ) ) || ( index_reg >= 0 && index_reg < 16 && ( mask & ( 1u << index_reg ) ) ) )
				return 8;

		}

		bool pure_write = ins.name == mnemonic::mov || ins.name == mnemonic::movzx || ins.name == mnemonic::movsx || ins.name == mnemonic::movsxd || ins.name == mnemonic::lea;

		for ( int32_t k = 0; k < ins.count && !zero; k++ ) {

			const x86::operand& o = ins.operands [ k ];

			if ( o.type != x86::kind::REG || ( k == 0 && pure_write ) )
				continue;

			int32_t f = x86::register_family ( o.reg );

			if ( f < 0 || f >= 16 || !( mask & ( 1u << f ) ) )
				continue;

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );

			if ( !( use & ( 1u << f ) ) )
				continue;

			return x86::register_size ( o.reg );

		}

		uint32_t use = 0;
		uint32_t def = 0;

		effects ( ins, true, use, def, false );
		mask &= ~def;

		if ( !mask )
			return 0;

	}

	return 0;

}

bool decompiler::stack_probe ( const analysis::database& db, const x86::instruction& ins ) {

	x86::instruction after;

	if ( ins.name != mnemonic::call || !db.decode ( ins.next ( ), after ) )
		return false;

	return after.name == mnemonic::sub && after.count == 2 && after.operands [ 0 ].type == x86::kind::REG && x86::register_family ( after.operands [ 0 ].reg ) == 4 && after.operands [ 1 ].type == x86::kind::REG && x86::register_family ( after.operands [ 1 ].reg ) == 0;

}

uint8_t decompiler::callee_pointer_shape ( const analysis::database& db, namer* names, uint64_t function, int32_t index, int32_t depth ) {

	int32_t callee = db.function_starting ( function );

	if ( callee < 0 || ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) || index < 0 || index > 3 || depth > 2 )
		return 0;

	if ( names && names->prototype ( function ) )
		return 0;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) || graph.blocks.count ( ) == 0 || graph.blocks.count ( ) > 400 )
		return 0;

	static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
	const uint32_t volatile_mask = ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 8 ) | ( 1u << 9 ) | ( 1u << 10 ) | ( 1u << 11 );
	uint32_t outside = 0;

	for ( int32_t i = 1; i < graph.blocks.count ( ); i++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
		uint64_t at = item.start;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return 0;

			at = ins.next ( );

			if ( ins.name == mnemonic::pop )
				continue;

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );
			outside |= def;

		}

	}

	uint8_t slot = 0;
	bool direct = false;
	bool bad = false;
	uint32_t stable = 0;

	auto note = [ & ] ( int64_t offset, uint8_t shape ) {

		uint8_t size = shape & 0x1F;

		if ( size != 1 && size != 2 && size != 4 && size != 8 ) {

			bad = true;

			return;

		}

		if ( offset < 0 || offset % size != 0 || ( slot && ( slot & 0x9F ) != ( shape & 0x9F ) ) ) {

			bad = true;

			return;

		}

		slot = static_cast < uint8_t > ( slot | shape );

		if ( offset == 0 )
			direct = true;

	};

	auto family = [ ] ( uint8_t reg ) -> int32_t {

		int32_t f = x86::register_family ( reg );

		return f >= 0 && f < 16 ? f : -1;

	};

	for ( int32_t i = 0; i < graph.blocks.count ( ) && !bad; i++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
		uint32_t mask = i == 0 ? ( 1u << gpr [ index ] ) : stable;
		uint64_t at = item.start;

		while ( at < item.end && !bad ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return 0;

			at = ins.next ( );

			if ( ins.name == mnemonic::call ) {

				uint64_t target = call_target ( ins );

				for ( int32_t j = 0; j < 4; j++ ) {

					if ( !( mask & ( 1u << gpr [ j ] ) ) )
						continue;

					wchar_t name [ 200 ];

					if ( !target || ( names && names->name ( target, true, name, 200 ) && api_prototype ( name ) ) ) {

						bad = true;

						break;

					}

					uint8_t inner = callee_pointer_shape ( db, names, target, j, depth + 1 );

					if ( inner )
						note ( 0, inner );

				}

				mask &= ~volatile_mask;

				continue;

			}

			if ( ins.name == mnemonic::mov && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && x86::register_size ( ins.operands [ 0 ].reg ) == 8 && x86::register_size ( ins.operands [ 1 ].reg ) == 8 ) {

				int32_t to = family ( ins.operands [ 0 ].reg );
				int32_t from = family ( ins.operands [ 1 ].reg );

				if ( to >= 0 && from >= 0 ) {

					if ( mask & ( 1u << from ) )
						mask |= 1u << to;
					else
						mask &= ~( 1u << to );

					continue;

				}

			}

			bool xmm = false;

			for ( int32_t k = 0; k < ins.count; k++ ) {

				const x86::operand& o = ins.operands [ k ];

				if ( o.type == x86::kind::REG && o.reg >= x86::XMM0 && o.reg < x86::YMM0 )
					xmm = true;

			}

			uint32_t use = 0;
			uint32_t def = 0;

			effects ( ins, true, use, def, false );

			for ( int32_t k = 0; k < ins.count && !bad; k++ ) {

				const x86::operand& o = ins.operands [ k ];

				if ( o.type == x86::kind::MEM ) {

					int32_t base = o.base ? family ( o.base ) : -1;
					int32_t index_reg = o.index ? family ( o.index ) : -1;

					if ( index_reg >= 0 && ( mask & ( 1u << index_reg ) ) )
						bad = true;

					if ( base < 0 || !( mask & ( 1u << base ) ) )
						continue;

					if ( o.index || ins.name == mnemonic::lea ) {

						bad = true;

						continue;

					}

					note ( o.value, static_cast < uint8_t > ( o.size | ( xmm ? 0x80 : 0 ) | ( ins.name == mnemonic::movzx ? 0x40 : 0 ) ) );

					continue;

				}

				if ( o.type != x86::kind::REG )
					continue;

				int32_t f = family ( o.reg );

				if ( f < 0 || !( mask & ( 1u << f ) ) )
					continue;

				bool killed = k == 0 && !( use & ( 1u << f ) );

				if ( killed )
					continue;

				bool zero_test = ( ins.name == mnemonic::test && ins.count == 2 && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 1 ].reg == ins.operands [ 0 ].reg ) || ( ins.name == mnemonic::cmp && ins.count == 2 && ins.operands [ 1 ].type == x86::kind::IMM && ins.operands [ 1 ].value == 0 );

				if ( !zero_test )
					bad = true;

			}

			mask &= ~def;

		}

		if ( i == 0 )
			stable = mask & ~volatile_mask & ~outside;

	}

	return !bad && direct ? slot : 0;

}

int64_t decompiler::entry_arguments ( const analysis::database& db, uint64_t function, bool long_mode, int32_t depth, bool floats ) {

	int32_t callee = db.function_starting ( function );

	if ( !long_mode || callee < 0 || ( db.functions [ static_cast < size_t > ( callee ) ].flags & analysis::THUNK ) || depth > 2 )
		return -1;

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) )
		return -1;

	static const int32_t gpr [ 4 ] = { 1, 2, 8, 9 };
	const uint32_t arg_mask = ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 8 ) | ( 1u << 9 ) | ( floats ? 0xF0000u : 0u );
	int32_t count = graph.blocks.count ( );
	base::list < uint32_t > uses;
	base::list < uint32_t > defs;
	base::list < uint32_t > live;

	uses.fill ( static_cast < size_t > ( count ), 0 );
	defs.fill ( static_cast < size_t > ( count ), 0 );
	live.fill ( static_cast < size_t > ( count ), 0 );

	auto passed = [ & ] ( const x86::instruction& ins ) {

		uint64_t target = call_target ( ins );

		if ( floats ) {

			int64_t inner = target ? entry_arguments ( db, target, long_mode, depth + 1, true ) : -1;

			return inner > 0 ? static_cast < uint32_t > ( inner ) : 0u;

		}

		int64_t inner = target ? entry_arguments ( db, target, long_mode, depth + 1, false ) : -1;
		uint32_t mask = 0;

		for ( int32_t i = 0; i < inner && i < 4; i++ )
			mask |= 1u << gpr [ i ];

		return mask;

	};

	for ( int32_t i = 0; i < count; i++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
		uint64_t at = item.start;
		uint32_t use = 0;
		uint32_t def = 0;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				break;

			uint32_t u = 0;
			uint32_t d = 0;

			effects ( ins, long_mode, u, d, false );

			bool zeroing = ins.name == mnemonic::xor_ && ins.count == 2 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 1 ].type == x86::kind::REG && ins.operands [ 0 ].reg == ins.operands [ 1 ].reg;

			if ( zeroing )
				u = 0;

			if ( floats && ins.name == mnemonic::ret )
				u &= ~( 1u << 16 );

			if ( ins.name == mnemonic::call && stack_probe ( db, ins ) ) {

				at = ins.next ( );

				continue;

			}

			if ( ins.name == mnemonic::call || ( ins.control == x86::flow::JUMP && item.out_count == 0 && ins.next ( ) >= item.end ) )
				u |= passed ( ins );

			use |= u & ~def;
			def |= d;
			at = ins.next ( );

		}

		uses [ static_cast < size_t > ( i ) ] = use;
		defs [ static_cast < size_t > ( i ) ] = def;

	}

	for ( bool moved = true; moved; ) {

		moved = false;

		for ( int32_t i = count - 1; i >= 0; i-- ) {

			uint32_t out = 0;
			const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];

			for ( int32_t e = 0; e < item.out_count; e++ )
				out |= live [ static_cast < size_t > ( graph.edges [ static_cast < size_t > ( item.first_out + e ) ].to ) ];

			uint32_t in = uses [ static_cast < size_t > ( i ) ] | ( out & ~defs [ static_cast < size_t > ( i ) ] );

			if ( in != live [ static_cast < size_t > ( i ) ] ) {

				live [ static_cast < size_t > ( i ) ] = in;
				moved = true;

			}

		}

	}

	uint32_t entry = count ? live [ 0 ] & arg_mask : 0;
	int32_t highest = 0;

	if ( floats )
		return static_cast < int64_t > ( entry );

	for ( int32_t i = 0; i < 4; i++ ) {

		if ( entry & ( 1u << gpr [ i ] ) )
			highest = i + 1;

	}

	return highest;

}

int32_t decompiler::internal_arguments ( const analysis::database& db, uint64_t function, bool long_mode, int32_t depth ) {

	return static_cast < int32_t > ( entry_arguments ( db, function, long_mode, depth, false ) );

}

int64_t decompiler::argument_registers ( const analysis::database& db, uint64_t function ) {

	return entry_arguments ( db, function, true, 0, true );

}

int32_t decompiler::stack_arguments ( const analysis::database& db, uint64_t function ) {

	analysis::function_graph graph;

	if ( !analysis::build_graph ( db, function, graph ) || graph.blocks.empty ( ) )
		return -1;

	int64_t frame = 0;
	int32_t found = 0;
	bool prologue = true;

	for ( int32_t i = 0; i < graph.blocks.count ( ); i++ ) {

		const analysis::block& item = graph.blocks [ static_cast < size_t > ( i ) ];
		uint64_t at = item.start;

		while ( at < item.end ) {

			x86::instruction ins;

			if ( !db.decode ( at, ins ) )
				return -1;

			at = ins.next ( );

			if ( ins.name == mnemonic::pop )
				continue;

			if ( ins.name == mnemonic::push ) {

				if ( !prologue )
					return -1;

				frame += 8;

				continue;

			}

			if ( ins.count >= 1 && ins.operands [ 0 ].type == x86::kind::REG && ins.operands [ 0 ].reg == x86::RSP ) {

				if ( ins.name == mnemonic::sub && prologue && ins.count == 2 && ins.operands [ 1 ].type == x86::kind::IMM ) {

					frame += ins.operands [ 1 ].value;
					prologue = false;

					continue;

				}

				if ( ins.name == mnemonic::add && ins.count == 2 && ins.operands [ 1 ].type == x86::kind::IMM )
					continue;

				return -1;

			}

			for ( int32_t k = 0; k < ins.count; k++ ) {

				const x86::operand& part = ins.operands [ static_cast < size_t > ( k ) ];

				if ( part.type == x86::kind::REG && part.reg == x86::RBP && k == 0 && ( part.access & x86::WRITE ) )
					return -1;

				if ( part.type != x86::kind::MEM || part.base != x86::RSP || part.index != x86::NONE )
					continue;

				int64_t offset = part.value - frame - 8;

				if ( offset >= 0x20 && ( offset & 7 ) == 0 )
					found = base::max ( found, static_cast < int32_t > ( ( offset - 0x20 ) / 8 ) + 1 );

			}

			if ( ins.name != mnemonic::mov && ins.name != mnemonic::lea && ins.name != mnemonic::movaps && ins.name != mnemonic::movups && ins.name != mnemonic::movdqa && ins.name != mnemonic::movapd && ins.name != mnemonic::nop )
				prologue = false;

		}

	}

	return found;

}

int32_t decompiler::yields_value ( const analysis::database& db, const loader::binary& file, namer* names, uint64_t function, bool long_mode, int32_t depth ) {

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
	bool plain_byte = false;
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

			if ( kind == 1 )
				plain_byte = true;

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

	if ( byte_returns && ( kinds & ( ( 1u << 2 ) | ( 1u << 3 ) ) ) && !( kinds & ( 1u << 8 ) ) )
		return ( kinds & ( 1u << 3 ) ) ? 3 : 2;

	if ( byte_returns && plain_byte )
		return 1;

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