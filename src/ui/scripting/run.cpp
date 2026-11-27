#include "ui/scripting.h"
#include "base/files/files.h"
#include "base/clock/clock.h"
#include "base/strings/strings.h"
#include <windows.h>

namespace ui {

	bool load_text_file ( const wchar_t* path, base::list < wchar_t >& out ) {

		base::list < char > raw;

		out.clear ( );

		if ( !files::read ( path, raw ) )
			return false;

		int32_t skip = raw.count ( ) >= 3 && static_cast < uint8_t > ( raw [ 0 ] ) == 0xEF && static_cast < uint8_t > ( raw [ 1 ] ) == 0xBB && static_cast < uint8_t > ( raw [ 2 ] ) == 0xBF ? 3 : 0;
		int32_t length = raw.count ( ) - skip;

		if ( length <= 0 )
			return true;

		int32_t made = MultiByteToWideChar ( CP_UTF8, 0, raw.data ( ) + skip, length, nullptr, 0 );

		if ( made <= 0 )
			return false;

		out.resize ( static_cast < size_t > ( made ) );
		MultiByteToWideChar ( CP_UTF8, 0, raw.data ( ) + skip, length, out.data ( ), made );

		return true;

	}

	bool script_symbols::name ( uint64_t address, bool code, wchar_t* out, int32_t room ) {

		return owner->symbol_name ( address, code, out, room );

	}

	bool script_bridge::call ( script::engine& engine_ref, const wchar_t* name, const script::value* args, int32_t count, script::value& result ) {

		return owner->script_call ( engine_ref, name, args, count, result );

	}

	void script_bridge::print ( const wchar_t* text ) {

		owner->script_print ( text );

	}

	bool script_bridge::read_file ( const wchar_t* path, const wchar_t* from, base::list < wchar_t >& source, base::writer& resolved ) {

		bool absolute = ( path [ 0 ] && path [ 1 ] == L':' ) || path [ 0 ] == L'\\' || path [ 0 ] == L'/';

		if ( !absolute && from && from [ 0 ] ) {

			int32_t cut = -1;

			for ( int32_t i = 0; from [ i ]; i++ ) {

				if ( from [ i ] == L'\\' || from [ i ] == L'/' )
					cut = i;

			}

			for ( int32_t i = 0; i <= cut; i++ )
				resolved.add ( from [ i ] );

		}

		resolved.add ( path );

		return load_text_file ( resolved.text ( ), source );

	}

	bool script_context::is ( const wchar_t* word ) const {

		return base::same_text ( name, word );

	}

	bool script_context::takes ( int32_t low, int32_t high ) {

		if ( count >= low && count <= high )
			return true;

		base::writer message;

		message.add ( L"'" ).add ( name ).add ( L"' takes " );

		if ( low == high )
			message.number ( low );
		else if ( high > 64 )
			message.number ( low ).add ( L" or more" );
		else
			message.number ( low ).add ( L" to " ).number ( high );

		message.add ( low == 1 && high == 1 ? L" argument." : L" arguments." );
		engine->fail ( message.text ( ) );

		return false;

	}

	bool script_context::fail ( const wchar_t* message ) {

		engine->fail ( message );

		return true;

	}

	bool script_context::has ( int32_t index ) const {

		return index < count && args [ index ].type != script::kind::NONE;

	}

	bool script_context::address ( int32_t index, uint64_t& out ) {

		if ( index >= count ) {

			engine->fail ( L"An address is missing." );

			return false;

		}

		if ( args [ index ].type == script::kind::NUMBER ) {

			out = static_cast < uint64_t > ( args [ index ].number );

			return true;

		}

		if ( args [ index ].type == script::kind::TEXT ) {

			if ( owner->parse_address ( engine->text_of ( args [ index ] ), out ) )
				return true;

			base::writer message;

			message.add ( L"Nothing called '" ).add ( engine->text_of ( args [ index ] ) ).add ( L"' in this image." );
			engine->fail ( message.text ( ) );

			return false;

		}

		engine->fail ( L"An address must be a number or a name." );

		return false;

	}

	bool script_context::number ( int32_t index, int64_t& out ) {

		if ( index < count && args [ index ].type == script::kind::NUMBER ) {

			out = args [ index ].number;

			return true;

		}

		base::writer message;

		message.add ( L"Argument " ).number ( index + 1 ).add ( L" of '" ).add ( name ).add ( L"' must be a number." );
		engine->fail ( message.text ( ) );

		return false;

	}

	bool script_context::flag ( int32_t index, bool& out ) {

		if ( index < count && ( args [ index ].type == script::kind::NUMBER || args [ index ].type == script::kind::NONE ) ) {

			out = args [ index ].type == script::kind::NUMBER && args [ index ].number != 0;

			return true;

		}

		base::writer message;

		message.add ( L"Argument " ).number ( index + 1 ).add ( L" of '" ).add ( name ).add ( L"' must be true or false." );
		engine->fail ( message.text ( ) );

		return false;

	}

	const wchar_t* script_context::text ( int32_t index ) {

		if ( index < count && args [ index ].type == script::kind::TEXT )
			return engine->text_of ( args [ index ] );

		base::writer message;

		message.add ( L"Argument " ).number ( index + 1 ).add ( L" of '" ).add ( name ).add ( L"' must be text." );
		engine->fail ( message.text ( ) );

		return nullptr;

	}

	bool script_context::need_file ( ) {

		if ( owner->file.loaded ( ) )
			return true;

		engine->fail ( L"No file is open." );

		return false;

	}

	bool script_context::need_analysis ( ) {

		if ( owner->db && owner->analysis_applied && !owner->db->functions.empty ( ) )
			return true;

		engine->fail ( L"There's no analysis yet. Open a file and call wait() for it to finish." );

		return false;

	}

	bool script_context::function_start ( int32_t index, uint64_t& out ) {

		uint64_t where = 0;

		if ( !address ( index, where ) || !need_analysis ( ) )
			return false;

		int32_t found = owner->db->function_at ( where );

		if ( found < 0 ) {

			engine->fail ( base::writer ( ).hex ( where ).add ( L" isn't inside a function." ).text ( ) );

			return false;

		}

		out = owner->db->functions [ static_cast < size_t > ( found ) ].start;

		return true;

	}

	bool script_context::read ( uint64_t where, int32_t size, uint64_t& out ) const {

		uint64_t offset = 0;
		const loader::binary& image = owner->file;

		if ( !image.offset_of ( where, offset ) || offset + static_cast < uint64_t > ( size ) > image.size )
			return false;

		out = 0;

		for ( int32_t i = size - 1; i >= 0; i-- )
			out = ( out << 8 ) | image.byte ( offset + static_cast < uint64_t > ( i ) );

		return true;

	}

	bool script_context::decode ( uint64_t where, x86::instruction& out ) const {

		return owner->db && owner->db->decode ( where, out ) && out.valid ( );

	}

	void script_context::give_number ( int64_t value ) {

		result->type = script::kind::NUMBER;
		result->number = value;

	}

	void script_context::give_text ( const wchar_t* value, int32_t length ) {

		*result = engine->text ( value, length );

	}

	void script_context::give_list ( script::value made ) {

		*result = made;

	}

	script::value script_context::list ( ) {

		return engine->list ( );

	}

	void script_context::push_number ( script::value made, int64_t value ) {

		script::value item;

		item.type = script::kind::NUMBER;
		item.number = value;
		engine->push ( made, item );

	}

	void script_context::push_text ( script::value made, const wchar_t* value, int32_t length ) {

		engine->push ( made, engine->text ( value ? value : L"", length ) );

	}

	void script_context::push_none ( script::value made ) {

		engine->push ( made, script::value ( ) );

	}

	void script_context::push_list ( script::value made, script::value item ) {

		engine->push ( made, item );

	}

	void app::script_print ( const wchar_t* text ) {

		if ( script_capture ) {

			for ( const wchar_t* c = text; *c; c++ )
				script_capture->push ( *c );

			script_capture->push ( L'\n' );

		}

		if ( script_lines < 5000 )
			log ( log_kind::INFO, text );
		else if ( script_lines == 5000 )
			log ( log_kind::WARN, L"The script printed more than 5,000 lines. The rest only go to the capture." );

		script_lines++;

	}

	void app::script_touch ( ) {

		if ( !script_touched ) {

			remember_undo ( );
			script_touched = true;

		}

		names_revision++;
		annotation_revision++;

	}

	void app::script_changed ( panel which ) {

		script_touch ( );
		refresh_filter ( which );

	}

	bool app::run_script_file ( const wchar_t* path, base::list < wchar_t >* capture ) {

		base::list < wchar_t > source;

		if ( !load_text_file ( path, source ) ) {

			log ( log_kind::FAIL, base::writer ( ).add ( L"Couldn't read the script " ).add ( path ).add ( L"." ).text ( ) );

			return false;

		}

		source.push ( 0 );

		if ( last_script.get ( ) != path )
			last_script.set ( path );

		return run_script_text ( source.data ( ), path, capture );

	}

	bool app::run_script_text ( const wchar_t* source, const wchar_t* label, base::list < wchar_t >* capture ) {

		script::engine* engine_ref = new script::engine ( );
		script_bridge bridge;
		double started = base::seconds ( );
		const wchar_t* shown = label;

		for ( const wchar_t* c = label; *c; c++ ) {

			if ( *c == L'\\' || *c == L'/' )
				shown = c + 1;

		}

		bridge.owner = this;
		engine_ref->jit_enabled = script_jit;
		script_capture = capture;
		script_touched = false;
		script_lines = 0;

		bool ok = engine_ref->load ( source, label, bridge );

		if ( ok )
			ok = engine_ref->run ( bridge );

		if ( !ok ) {

			const script::problem& error = engine_ref->error;
			const wchar_t* where = error.file [ 0 ] ? error.file : label;
			const wchar_t* short_where = where;

			for ( const wchar_t* c = where; *c; c++ ) {

				if ( *c == L'\\' || *c == L'/' )
					short_where = c + 1;

			}

			base::writer line;

			line.add ( short_where );

			if ( error.line )
				line.add ( L"(" ).number ( error.line ).add ( L"," ).number ( error.column ).add ( L")" );

			line.add ( L": " ).add ( error.message );
			log ( log_kind::FAIL, line.text ( ) );

			if ( capture ) {

				for ( const wchar_t* c = line.text ( ); *c; c++ )
					capture->push ( *c );

				capture->push ( L'\n' );

			}

		} else
			log ( log_kind::GOOD, base::writer ( ).add ( L"Ran " ).add ( shown ).add ( L" in " ).fixed ( ( base::seconds ( ) - started ) * 1000.0, 1 ).add ( L" ms." ).text ( ) );

		if ( script_touched ) {

			names_revision++;
			annotation_revision++;
			code_cache.clear ( );
			refresh_filter ( panel::FUNCTIONS );
			refresh_filter ( panel::NOTES );
			refresh_filter ( panel::STRUCTS );
			changed ( );

		}

		script_capture = nullptr;
		delete engine_ref;

		return ok;

	}

	bool app::script_call ( script::engine& owner, const wchar_t* name, const script::value* args, int32_t count, script::value& result ) {

		script_context call;

		call.owner = this;
		call.engine = &owner;
		call.name = name;
		call.args = args;
		call.count = count;
		call.result = &result;

		if ( script_control ( call ) )
			return true;

		if ( !file.loaded ( ) )
			return call.fail ( L"No file is open. Open one with open(path) first." );

		return script_core ( call ) || script_data ( call ) || script_code ( call ) || script_structs ( call ) || script_tools ( call );

	}

}