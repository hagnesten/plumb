#include "decompiler/emit/program/program.h"
#include "decompiler/emit/emitter/emitter.h"
#include "decompiler/emit/braces/braces.h"
#include "decompiler/emit/calltypes/calltypes.h"
#include "decompiler/emit/arrays/arrays.h"
#include "decompiler/emit/narrowing/narrowing.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/booleans/booleans.h"
#include "decompiler/emit/pointers/pointers.h"
#include "decompiler/emit/callnames/callnames.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

void decompiler::append_result ( result& target, const result& source ) {

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

bool decompiler::emit ( builder& b ) {

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

			if ( back.kind == op::CONST || ( expression_returns && var < 0 ) )
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
	simd_vars ( b, body_emitter.vector, body_emitter.si128_var );
	narrow_values ( b, body_emitter.zero_known );
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

		if ( !declared && !( item.kind == var_kind::PARAMETER && b.has_prototype ) && v < inferred.count ( ) && !inferred [ static_cast < size_t > ( v ) ].empty ( ) ) {

			declared = inferred [ static_cast < size_t > ( v ) ].get ( );

			if ( unsigned_results && ( item.bits & UNSIGNED ) && base::same_text ( declared, L"int" ) )
				declared = L"unsigned int";

			if ( expand_strings && item.kind != var_kind::PARAMETER ) {

				if ( base::same_text ( declared, L"LPCWSTR" ) )
					declared = L"const WCHAR *";
				else if ( base::same_text ( declared, L"LPWSTR" ) )
					declared = L"WCHAR *";
				else if ( base::same_text ( declared, L"LPCSTR" ) )
					declared = L"const CHAR *";
				else if ( base::same_text ( declared, L"LPSTR" ) )
					declared = L"CHAR *";

			}

		}

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

	if ( b.names && b.names->number_notes ( ) && !b.ghost_at.empty ( ) ) {

		base::writer ghosts;

		ghosts.add ( L"// ghosts" );

		for ( uint64_t at : b.ghost_at )
			ghosts.add ( L" " ).hex ( at );

		head.out.begin ( b.function );
		head.out.put ( ghosts.text ( ), syntax::COMMENT );
		head.out.finish ( );

	}

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

			if ( b.names && b.names->number_notes ( ) && v < b.number_at.count ( ) )
				note.add ( L" @" ).hex ( b.number_at [ static_cast < size_t > ( v ) ] );

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