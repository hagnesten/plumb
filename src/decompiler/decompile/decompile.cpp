#include "decompiler/decompile/decompile.h"
#include "decompiler/builder/builder.h"
#include "decompiler/emit/program/program.h"
#include "decompiler/emit/layout/layout.h"
#include "base/clock/clock.h"

bool decompiler::decompile ( const analysis::database& db, uint64_t function, namer& names, result& out ) {

	double started = base::seconds ( );
	builder* b = new builder ( );

	out.text.clear ( );
	out.spans.clear ( );
	out.lines.clear ( );
	out.hints.clear ( );
	out.aliases.clear ( );
	out.function = function;
	b->db = &db;
	b->file = db.file;
	b->names = &names;
	b->out = &out;
	b->long_mode = db.long_mode;
	b->function = function;

	bool ok = b->lift ( );

	if ( ok ) {

		const wchar_t* declared = names.prototype ( function );

		if ( declared ) {

			base::writer problem;

			if ( parse_prototype ( declared, b->long_mode, b->prototype, problem ) ) {

				b->has_prototype = true;
				b->params = b->prototype.count;
				b->returns_value = !b->prototype.result_type.empty;
				b->returns_float = ( b->prototype.result_type.bits & FLOATING ) != 0;

				if ( b->returns_value )
					b->return_size = b->prototype.result_type.size;

			}

		}

		if ( !b->has_prototype && b->long_mode ) {

			for ( const block_code& item : b->code ) {

				if ( item.reachable && item.end == term::TAIL && item.returned >= 0 && !b->bare_return )
					b->returns_value = true;

			}

		}

		for ( const analysis::block& item : b->graph.blocks )
			out.instructions += item.instructions;

		out.blocks = b->graph.blocks.count ( );
		ok = emit ( *b );

		if ( ok ) {

			drop_unreachable ( out );
			space_lines ( out );

		}

	}

	out.ok = ok;
	out.seconds = base::seconds ( ) - started;
	delete b;

	return ok;

}