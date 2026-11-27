#include "decompiler/emit/pointers/pointers.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

void decompiler::flow_pointers ( builder& b, base::list < uint8_t >& element ) {

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

void decompiler::flavor_elements ( builder& b, base::list < uint8_t >& element ) {

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

void decompiler::typed_pointers ( builder& b, base::list < uint8_t >& element ) {

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
	base::list < uint8_t > stepped;
	base::list < int32_t > steps;

	direct.fill ( static_cast < size_t > ( total ), 0 );
	passed_on.fill ( static_cast < size_t > ( total ), 0 );
	stepped.fill ( static_cast < size_t > ( total ), 0 );

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

		if ( pointer_compares && node.kind >= op::EQ && node.kind <= op::GE && var_of ( node.a ) >= 0 && var_of ( node.b ) >= 0 )
			return;

		if ( pointer_steps && node.kind == op::ADD && var_of ( node.a ) >= 0 && node.b >= 0 && b.exprs [ static_cast < size_t > ( node.b ) ].kind == op::CONST && !( b.exprs [ static_cast < size_t > ( node.b ) ].bits & POINTER ) ) {

			int32_t v = var_of ( node.a );

			if ( v < total ) {

				stepped [ static_cast < size_t > ( v ) ] = 1;
				steps.push ( v );
				steps.push ( static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( node.b ) ].value ) );

			}

			return;

		}

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

	for ( int32_t k = 0; k + 1 < steps.count ( ); k += 2 ) {

		int32_t v = steps [ static_cast < size_t > ( k ) ];
		int32_t amount = steps [ static_cast < size_t > ( k + 1 ) ];
		int32_t unit = element [ static_cast < size_t > ( v ) ] & 0x1F;

		if ( !unit || amount % unit != 0 )
			bad [ static_cast < size_t > ( v ) ] = 1;

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