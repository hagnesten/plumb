#include "decompiler/emit/callnames/callnames.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

bool decompiler::unique_name ( const base::list < base::text >& taken, const wchar_t* base_name, wchar_t* out ) {

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

void decompiler::name_from_calls ( builder& b, int32_t result_var ) {

	b.call_names.clear ( );
	b.call_names.resize ( b.vars.size ( ) );
	b.call_types.clear ( );
	b.call_types.resize ( b.vars.size ( ) );
	b.call_counts.clear ( );
	b.call_counts.fill ( b.vars.size ( ), 0 );

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

		if ( !result_suffixes && !base::same_text ( chosen, rest ) )
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

		if ( !text && thread_names && b.db && b.long_mode ) {

			for ( int32_t i = 0; i < node.arg_count && i < 4; i++ ) {

				int32_t arg = b.args [ static_cast < size_t > ( node.first_arg + i ) ];

				while ( arg >= 0 && b.exprs [ static_cast < size_t > ( arg ) ].kind == op::CAST )
					arg = b.exprs [ static_cast < size_t > ( arg ) ].a;

				if ( arg < 0 || b.exprs [ static_cast < size_t > ( arg ) ].kind != op::VAR )
					continue;

				int32_t v = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( arg ) ].value );

				if ( v < 0 || v >= b.vars.count ( ) || b.vars [ static_cast < size_t > ( v ) ].kind != var_kind::PARAMETER || !b.call_names [ static_cast < size_t > ( v ) ].empty ( ) )
					continue;

				wchar_t chosen [ 80 ];

				if ( callee_thread_parameter ( *b.db, b.names, static_cast < uint64_t > ( node.value ), i, 0 ) && unique_name ( taken, L"lpParameter", chosen ) ) {

					taken.push ( base::text ( chosen ) );
					b.call_names [ static_cast < size_t > ( v ) ].set ( chosen );

				}

			}

		}

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
			bool copied_in = false;

			for ( const statement& line : b.statements ) {

				if ( line.kind != stmt::ASSIGN || line.left < 0 || line.right < 0 || b.exprs [ static_cast < size_t > ( line.left ) ].kind != op::VAR || b.exprs [ static_cast < size_t > ( line.left ) ].value != v )
					continue;

				any_write = true;

				if ( b.exprs [ static_cast < size_t > ( line.right ) ].kind != op::CONST )
					only_constants = false;

				if ( plain_sources && ( b.exprs [ static_cast < size_t > ( line.right ) ].kind == op::CONST || b.exprs [ static_cast < size_t > ( line.right ) ].kind == op::VAR ) )
					copied_in = true;

				int32_t source = line.right;

				while ( source >= 0 && b.exprs [ static_cast < size_t > ( source ) ].kind == op::CAST )
					source = b.exprs [ static_cast < size_t > ( source ) ].a;

				if ( source >= 0 && b.exprs [ static_cast < size_t > ( source ) ].kind == op::CALL )
					call_result = true;

			}

			if ( only_constants && any_write && !address )
				continue;

			if ( copied_in && !address && b.vars [ static_cast < size_t > ( v ) ].kind != var_kind::PARAMETER )
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

			if ( !local_suffixes && !base::same_text ( chosen, rest ) )
				continue;

			taken.push ( base::text ( chosen ) );
			b.call_names [ static_cast < size_t > ( v ) ].set ( chosen );

			if ( address && v < b.call_types.count ( ) ) {

				b.call_types [ static_cast < size_t > ( v ) ].set ( shape.types [ i ] );

				for ( int32_t k = 0; k < node.arg_count && k < shape.count; k++ ) {

					const wchar_t* sibling = shape.names [ k ];
					int32_t given = b.args [ static_cast < size_t > ( node.first_arg + k ) ];

					while ( given >= 0 && b.exprs [ static_cast < size_t > ( given ) ].kind == op::CAST )
						given = b.exprs [ static_cast < size_t > ( given ) ].a;

					auto prefix = [ ] ( const wchar_t* text, const wchar_t* head ) {

						for ( ; *head; head++, text++ ) {

							if ( *text != *head )
								return false;

						}

						return true;

					};

					bool counting = prefix ( sibling, L"nSize" ) || prefix ( sibling, L"cch" ) || prefix ( sibling, L"nMaxCount" ) || prefix ( sibling, L"nBufferLength" ) || prefix ( sibling, L"BufferCount" ) || prefix ( sibling, L"SizeInWords" ) || prefix ( sibling, L"MaxCount" );

					if ( k != i && counting && given >= 0 && b.exprs [ static_cast < size_t > ( given ) ].kind == op::CONST && b.exprs [ static_cast < size_t > ( given ) ].value > 0 && b.exprs [ static_cast < size_t > ( given ) ].value < 0x10000 )
						b.call_counts [ static_cast < size_t > ( v ) ] = static_cast < int32_t > ( b.exprs [ static_cast < size_t > ( given ) ].value );

				}

			}

		}

	}

}