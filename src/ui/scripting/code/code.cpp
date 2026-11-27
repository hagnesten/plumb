#include "ui/scripting/code/code.h"
#include "ui/scripting/bridge/bridge.h"
#include "base/strings/strings.h"
#include "ui/style/metrics/metrics.h"
#include "analysis/graph/graph.h"

const wchar_t* ui::flow_word ( x86::flow value ) {

	switch ( value ) {

		case x86::flow::CALL: return L"call";
		case x86::flow::JUMP: return L"jump";
		case x86::flow::BRANCH: return L"branch";
		case x86::flow::RETURN: return L"return";
		case x86::flow::STOP: return L"stop";
		default: return L"none";

	}

}

const wchar_t* ui::edge_word ( analysis::edge_kind value ) {

	switch ( value ) {

		case analysis::edge_kind::TAKEN: return L"taken";
		case analysis::edge_kind::NOT_TAKEN: return L"not_taken";
		case analysis::edge_kind::JUMP: return L"jump";
		case analysis::edge_kind::CASE: return L"case";
		default: return L"fall";

	}

}

int32_t ui::class_index ( const analysis::database& db, script_context& call, int32_t index ) {

	if ( index >= call.count )
		return -1;

	const script::value& given = call.args [ index ];

	if ( given.type == script::kind::NUMBER )
		return given.number >= 0 && given.number < db.classes.count ( ) ? static_cast < int32_t > ( given.number ) : -1;

	if ( given.type != script::kind::TEXT )
		return -1;

	for ( int32_t i = 0; i < db.classes.count ( ); i++ ) {

		if ( base::same_text ( db.classes [ static_cast < size_t > ( i ) ].name.get ( ), call.engine->text_of ( given ) ) )
			return i;

	}

	return -1;

}

bool ui::declared_type ( const decompiler::result& code, const decompiler::line& item, const decompiler::span& part, wchar_t* out, int32_t room ) {

	const wchar_t* text = code.text.data ( ) + item.text;
	int32_t start = part.start - item.text;
	int32_t end = start + part.length;
	wchar_t after = end < item.length ? text [ end ] : 0;

	if ( after != L';' && after != L',' && after != L')' && after != L'[' )
		return false;

	int32_t begin = start;

	while ( begin > 0 && text [ begin - 1 ] != L'(' && text [ begin - 1 ] != L',' )
		begin--;

	for ( int32_t i = begin; i < start; i++ ) {

		if ( text [ i ] == L'=' )
			return false;

	}

	while ( begin < start && text [ begin ] == L' ' )
		begin++;

	int32_t stop = start;

	while ( stop > begin && text [ stop - 1 ] == L' ' )
		stop--;

	if ( stop <= begin )
		return false;

	int32_t k = 0;

	for ( int32_t i = begin; i < stop && k < room - 1; i++ )
		out [ k++ ] = text [ i ];

	out [ k ] = 0;

	return true;

}

bool ui::app::script_code ( script_context& call ) {

	if ( call.is ( L"function_flags" ) || call.is ( L"function_info" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
			return true;

		const analysis::function_info& item = db->functions [ static_cast < size_t > ( db->function_starting ( where ) ) ];
		script::value made = call.list ( );

		if ( call.is ( L"function_info" ) ) {

			call.push_number ( made, static_cast < int64_t > ( item.start ) );
			call.push_number ( made, static_cast < int64_t > ( item.end ) );
			call.push_number ( made, static_cast < int64_t > ( item.end - item.start ) );
			call.push_number ( made, item.instructions );
			call.push_number ( made, item.blocks );
			call.push_number ( made, item.callers );
			call.push_number ( made, item.flags );
			call.give_list ( made );

			return true;

		}

		static const struct { uint32_t bit; const wchar_t* word; } words [ ] = {

			{ analysis::FROM_ENTRY, L"entry" },
			{ analysis::FROM_EXPORT, L"export" },
			{ analysis::FROM_TLS, L"tls" },
			{ analysis::FROM_PDATA, L"pdata" },
			{ analysis::FROM_CALL, L"called" },
			{ analysis::FROM_POINTER, L"pointer" },
			{ analysis::FROM_PROLOGUE, L"prologue" },
			{ analysis::THUNK, L"thunk" },
			{ analysis::NORETURN, L"noreturn" },
			{ analysis::LEAF, L"leaf" },
			{ analysis::NAMED, L"named" },
			{ analysis::FROM_JUMP, L"jumped" },
			{ analysis::FROM_SYMBOLS, L"symbols" },

		};

		for ( const auto& entry : words ) {

			if ( item.flags & entry.bit )
				call.push_text ( made, entry.word );

		}

		if ( db->virtual_of ( item.start ) >= 0 )
			call.push_text ( made, L"virtual" );

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"instructions" ) || call.is ( L"blocks" ) || call.is ( L"edges" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
			return true;

		analysis::function_graph graph;

		if ( !analysis::build_graph ( *db, where, graph ) )
			return true;

		script::value made = call.list ( );

		if ( call.is ( L"edges" ) ) {

			for ( const analysis::edge& item : graph.edges ) {

				script::value row = call.list ( );

				call.push_number ( row, static_cast < int64_t > ( graph.blocks [ static_cast < size_t > ( item.from ) ].start ) );
				call.push_number ( row, static_cast < int64_t > ( graph.blocks [ static_cast < size_t > ( item.to ) ].start ) );
				call.push_text ( row, edge_word ( item.kind ) );

				if ( item.kind == analysis::edge_kind::CASE )
					call.push_number ( row, item.value );

				call.push_list ( made, row );

			}

		} else if ( call.is ( L"blocks" ) ) {

			for ( const analysis::block& item : graph.blocks ) {

				script::value row = call.list ( );

				call.push_number ( row, static_cast < int64_t > ( item.start ) );
				call.push_number ( row, static_cast < int64_t > ( item.end ) );
				call.push_number ( row, item.instructions );
				call.push_number ( row, item.out_count );
				call.push_number ( row, item.returns );
				call.push_list ( made, row );

			}

		} else {

			base::list < uint64_t > starts;

			for ( const analysis::block& item : graph.blocks ) {

				x86::instruction ins;

				for ( uint64_t at = item.start; at < item.end && call.decode ( at, ins ); at = ins.next ( ) )
					starts.push ( at );

			}

			for ( int32_t i = 1; i < starts.count ( ); i++ ) {

				uint64_t held = starts [ static_cast < size_t > ( i ) ];
				int32_t k = i;

				while ( k > 0 && starts [ static_cast < size_t > ( k - 1 ) ] > held ) {

					starts [ static_cast < size_t > ( k ) ] = starts [ static_cast < size_t > ( k - 1 ) ];
					k--;

				}

				starts [ static_cast < size_t > ( k ) ] = held;

			}

			for ( uint64_t at : starts )
				call.push_number ( made, static_cast < int64_t > ( at ) );

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"callees" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
			return true;

		int32_t owner_index = db->function_starting ( where );
		script::value made = call.list ( );
		base::list < uint64_t > seen;

		for ( const analysis::xref& item : db->refs ) {

			if ( item.type != analysis::ref::CALL || db->function_at ( file.base + item.source ) != owner_index )
				continue;

			uint64_t target = file.base + item.target;
			bool known = false;

			for ( uint64_t other : seen )
				known = known || other == target;

			if ( known )
				continue;

			seen.push ( target );
			call.push_number ( made, static_cast < int64_t > ( target ) );

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"head" ) || call.is ( L"prev" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
			return true;

		uint64_t start = db->instruction_at ( where );

		if ( !start || !db->is_start ( start ) )
			return true;

		if ( call.is ( L"prev" ) ) {

			if ( start <= file.base )
				return true;

			start = db->instruction_at ( start - 1 );

			if ( !start || !db->is_start ( start ) )
				return true;

		}

		call.give_number ( static_cast < int64_t > ( start ) );

		return true;

	}

	if ( call.is ( L"is_label" ) ) {

		uint64_t where = 0;

		if ( call.takes ( 1, 1 ) && call.address ( 0, where ) && call.need_analysis ( ) )
			call.give_number ( db->is_label ( where ) );

		return true;

	}

	if ( call.is ( L"flow" ) || call.is ( L"target" ) ) {

		uint64_t where = 0;
		x86::instruction ins;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) || !call.decode ( where, ins ) )
			return true;

		if ( call.is ( L"flow" ) )
			call.give_text ( flow_word ( ins.control ) );
		else if ( ins.direct ( ) )
			call.give_number ( static_cast < int64_t > ( ins.target ( ) ) );
		else {

			uint64_t target = 0;

			if ( ins.memory_target ( target ) )
				call.give_number ( static_cast < int64_t > ( target ) );

		}

		return true;

	}

	if ( call.is ( L"refs_to_info" ) || call.is ( L"refs_from_info" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
			return true;

		script::value made = call.list ( );

		auto push_ref = [ & ] ( uint64_t at, analysis::ref type ) {

			script::value row = call.list ( );

			call.push_number ( row, static_cast < int64_t > ( at ) );
			call.push_text ( row, analysis::ref_name ( type ) );
			call.push_list ( made, row );

		};

		if ( call.is ( L"refs_from_info" ) ) {

			uint32_t source = static_cast < uint32_t > ( where - file.base );

			for ( const analysis::xref& item : db->refs ) {

				if ( item.source == source )
					push_ref ( file.base + item.target, item.type );

			}

		} else {

			int32_t first = 0;
			int32_t total = 0;

			db->refs_to ( where, first, total );

			for ( int32_t r = first; r < first + total; r++ ) {

				const analysis::xref& item = db->refs [ static_cast < size_t > ( r ) ];

				push_ref ( file.base + item.source, item.type );

			}

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"c_lines" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
			return true;

		const decompiler::result* code = pseudocode_for ( where );

		if ( !code || !code->ok )
			return true;

		script::value made = call.list ( );

		for ( const decompiler::line& item : code->lines ) {

			script::value row = call.list ( );

			call.push_number ( row, static_cast < int64_t > ( item.address ) );
			call.push_number ( row, item.indent );
			call.push_text ( row, code->text.data ( ) + item.text, item.length );
			call.push_list ( made, row );

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"locals" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
			return true;

		const decompiler::result* code = pseudocode_for ( where );

		if ( !code || !code->ok )
			return true;

		base::list < base::text > seen;
		script::value made = call.list ( );

		for ( const decompiler::line& item : code->lines ) {

			for ( int32_t s = 0; s < item.span_count; s++ ) {

				const decompiler::span& part = code->spans [ static_cast < size_t > ( item.first_span + s ) ];

				if ( part.kind != decompiler::syntax::LOCAL && part.kind != decompiler::syntax::PARAMETER )
					continue;

				int32_t length = base::min ( part.length, 62 );
				wchar_t shown [ 64 ];

				for ( int32_t k = 0; k < length; k++ )
					shown [ k ] = code->text [ static_cast < size_t > ( part.start + k ) ];

				shown [ length ] = 0;

				bool known = false;

				for ( const base::text& other : seen )
					known = known || base::same_text ( other.get ( ), shown );

				if ( known )
					continue;

				seen.push ( base::text ( shown ) );

				const wchar_t* key = shown;

				for ( const decompiler::name_alias& alias : code->aliases ) {

					if ( base::same_text ( alias.shown, shown ) )
						key = alias.key;

				}

				for ( const local_entry& entry : locals ) {

					if ( entry.function == where && !entry.name.empty ( ) && base::same_text ( entry.name.get ( ), shown ) )
						key = entry.key.get ( );

				}

				wchar_t type [ 128 ] = { };
				const local_entry* set = local_for ( where, key );

				if ( set && !set->type.empty ( ) ) {

					int32_t k = 0;

					for ( const wchar_t* at = set->type.get ( ); *at && k < 127; at++ )
						type [ k++ ] = *at;

					type [ k ] = 0;

				} else {

					for ( const decompiler::line& other : code->lines ) {

						if ( type [ 0 ] || other.indent > 1 )
							continue;

						for ( int32_t t = 0; t < other.span_count && !type [ 0 ]; t++ ) {

							const decompiler::span& candidate = code->spans [ static_cast < size_t > ( other.first_span + t ) ];

							if ( candidate.kind != part.kind || candidate.length != part.length )
								continue;

							bool same = true;

							for ( int32_t k = 0; k < part.length && same; k++ )
								same = code->text [ static_cast < size_t > ( candidate.start + k ) ] == code->text [ static_cast < size_t > ( part.start + k ) ];

							if ( same )
								declared_type ( *code, other, candidate, type, 128 );

						}

					}

				}

				script::value row = call.list ( );

				call.push_text ( row, key );
				call.push_text ( row, shown );

				if ( type [ 0 ] )
					call.push_text ( row, type );
				else
					call.push_none ( row );

				call.push_text ( row, part.kind == decompiler::syntax::PARAMETER ? L"parameter" : L"local" );
				call.push_list ( made, row );

			}

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"local_name" ) || call.is ( L"local_type" ) || call.is ( L"set_local_name" ) || call.is ( L"set_local_type" ) ) {

		uint64_t where = 0;
		const wchar_t* key = nullptr;
		bool setting = call.is ( L"set_local_name" ) || call.is ( L"set_local_type" );
		bool naming = call.is ( L"local_name" ) || call.is ( L"set_local_name" );

		if ( !call.takes ( setting ? 3 : 2, setting ? 3 : 2 ) || !call.function_start ( 0, where ) || !( key = call.text ( 1 ) ) )
			return true;

		for ( const local_entry& entry : locals ) {

			if ( entry.function == where && !entry.name.empty ( ) && base::same_text ( entry.name.get ( ), key ) )
				key = entry.key.get ( );

		}

		const decompiler::result* named = pseudocode_for ( where );

		if ( named ) {

			for ( const decompiler::name_alias& alias : named->aliases ) {

				if ( base::same_text ( alias.shown, key ) )
					key = alias.key;

			}

		}

		base::text held;

		held.set ( key );

		if ( !setting ) {

			const local_entry* found = local_for ( where, held.get ( ) );
			const base::text* part = !found ? nullptr : ( naming ? &found->name : &found->type );

			if ( part && !part->empty ( ) )
				call.give_text ( part->get ( ) );

			return true;

		}

		const wchar_t* given = L"";

		if ( call.has ( 2 ) && !( given = call.text ( 2 ) ) )
			return true;

		if ( naming ) {

			if ( given [ 0 ] && !base::is_identifier ( given ) )
				return call.fail ( L"Variable names are letters, digits and underscores, and can't start with a digit." );

			set_local ( where, held.get ( ), given, nullptr );

		} else if ( !given [ 0 ] )
			set_local ( where, held.get ( ), nullptr, L"" );
		else {

			decompiler::c_type shape;
			wchar_t clean [ 64 ];
			base::writer problem;

			if ( !decompiler::parse_type ( given, file.wide ( ), shape, clean, 64 ) || shape.empty )
				return call.fail ( base::writer ( ).add ( L"Couldn't read the type \"" ).add ( given ).add ( L"\". Try int32_t, uint8_t*, DWORD, double or a struct pointer." ).text ( ) );

			if ( !known_type ( shape, problem ) )
				return call.fail ( problem.text ( ) );

			set_local ( where, held.get ( ), nullptr, clean );

		}

		script_touch ( );
		call.give_number ( 1 );

		return true;

	}

	if ( call.is ( L"disassemble" ) ) {

		uint64_t where = 0;
		int64_t lines = 20;

		if ( !call.takes ( 1, 2 ) || !call.address ( 0, where ) || ( call.has ( 1 ) && !call.number ( 1, lines ) ) || !call.need_analysis ( ) )
			return true;

		base::list < wchar_t > out;

		mcp_disassemble ( where, static_cast < int32_t > ( base::clamp ( lines, 1ll, 100000ll ) ), file.high ( ), out );

		int32_t length = out.count ( );

		while ( length > 0 && ( out [ static_cast < size_t > ( length - 1 ) ] == 0 ) )
			length--;

		call.give_text ( out.data ( ), length );

		return true;

	}

	if ( call.is ( L"classes" ) ) {

		if ( !call.takes ( 0, 0 ) || !call.need_analysis ( ) )
			return true;

		script::value made = call.list ( );

		for ( const analysis::class_info& item : db->classes ) {

			script::value row = call.list ( );

			call.push_text ( row, item.name.get ( ) );
			call.push_number ( row, static_cast < int64_t > ( item.vtable ) );
			call.push_number ( row, item.methods );
			call.push_number ( row, static_cast < int64_t > ( item.locator ) );
			call.push_number ( row, item.offset );
			call.push_list ( made, row );

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"class_methods" ) || call.is ( L"class_bases" ) ) {

		if ( !call.takes ( 1, 1 ) || !call.need_analysis ( ) )
			return true;

		int32_t index = class_index ( *db, call, 0 );

		if ( index < 0 )
			return call.fail ( L"There's no such class. classes() lists them." );

		const analysis::class_info& item = db->classes [ static_cast < size_t > ( index ) ];
		script::value made = call.list ( );

		if ( call.is ( L"class_bases" ) ) {

			for ( int32_t i = 0; i < item.base_count; i++ )
				call.push_text ( made, db->class_bases [ static_cast < size_t > ( item.first_base + i ) ].get ( ) );

		} else {

			for ( int32_t slot = 0; slot < item.methods; slot++ ) {

				uint64_t read = 0;
				int32_t width = file.wide ( ) ? 8 : 4;

				if ( call.read ( item.vtable + static_cast < uint64_t > ( slot * width ), width, read ) )
					call.push_number ( made, static_cast < int64_t > ( read ) );
				else
					call.push_none ( made );

			}

		}

		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"vfunc" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.function_start ( 0, where ) )
			return true;

		int32_t found = db->virtual_of ( where );

		if ( found < 0 )
			return true;

		const analysis::virtual_slot& item = db->virtuals [ static_cast < size_t > ( found ) ];
		script::value made = call.list ( );

		call.push_text ( made, db->classes [ static_cast < size_t > ( item.owner ) ].name.get ( ) );
		call.push_number ( made, item.slot );
		call.give_list ( made );

		return true;

	}

	if ( call.is ( L"jump_table" ) ) {

		uint64_t where = 0;

		if ( !call.takes ( 1, 1 ) || !call.address ( 0, where ) || !call.need_analysis ( ) )
			return true;

		int32_t found = db->table_for_jump ( where );

		if ( found < 0 )
			found = db->table_at ( where );

		if ( found < 0 )
			return true;

		const analysis::jump_table& item = db->tables [ static_cast < size_t > ( found ) ];
		script::value made = call.list ( );
		script::value targets = call.list ( );
		analysis::function_graph graph;
		int32_t owner_index = db->function_at ( item.jump );

		if ( owner_index >= 0 && analysis::build_graph ( *db, db->functions [ static_cast < size_t > ( owner_index ) ].start, graph ) ) {

			int32_t from = graph.block_at ( item.jump );

			for ( const analysis::edge& link : graph.edges ) {

				if ( link.from == from && link.kind == analysis::edge_kind::CASE )
					call.push_number ( targets, static_cast < int64_t > ( graph.blocks [ static_cast < size_t > ( link.to ) ].start ) );

			}

		}

		call.push_number ( made, static_cast < int64_t > ( item.jump ) );
		call.push_number ( made, static_cast < int64_t > ( item.table ) );
		call.push_number ( made, item.count );
		call.push_list ( made, targets );
		call.give_list ( made );

		return true;

	}

	return false;

}