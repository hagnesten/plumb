#include "ui/app.hh"
#include "ui/format.hh"
#include "ui/tokens.hh"

namespace ui {

	row_columns app::listing_columns ( float width, float adv ) const {

		row_columns cols;
		int32_t available = static_cast < int32_t > ( width / base::max ( adv, 1.0f ) );

		cols.digits = file.wide ( ) ? 16 : 8;
		cols.bytes = show_bytes;
		cols.code = cols.digits + 2 + ( show_bytes ? 22 : 0 );
		cols.mnemonic = cols.code + 4;
		cols.operands = cols.mnemonic + 8;
		cols.note = base::max ( base::min ( cols.operands + 40, available - 30 ), cols.operands + 18 );

		return cols;

	}

	void app::compose_row ( uint64_t row, const row_columns& cols, row_line& out ) const {

		out.clear ( );

		int32_t index = item_at ( row );

		if ( index < 0 )
			return;

		const line_item& item = items [ static_cast < size_t > ( index ) ];
		uint32_t sub = static_cast < uint32_t > ( row - item.first_row );
		bool analyzed = db && analysis_applied && !db->maps.empty ( );
		bool label_row = false;
		uint64_t address = item.address;
		wchar_t name [ 200 ];

		if ( item.kind == line_kind::CODE && analyzed )
			address = db->row_address ( item.address, item.address + item.size, sub, label_row );
		else if ( item.kind == line_kind::DATA || item.kind == line_kind::TABLE || item.kind == line_kind::VTABLE )
			address = item.address + static_cast < uint64_t > ( sub ) * item.stride;

		out.address = address;
		out.kind = item.kind;

		auto put_address = [ & ] ( ) {

			base::writer where;

			where.hex ( address, cols.digits );
			out.put ( where.text ( ), !patches.empty ( ) && is_patched ( address, item.kind == line_kind::CODE || item.address + item.size <= address ? 1 : static_cast < int32_t > ( base::min < uint64_t > ( item.stride, item.address + item.size - address ) ) ) ? tone::WARNING : tone::ADDRESS );

		};

		auto put_note = [ & ] ( const wchar_t* value, tone color, uint64_t target ) {

			out.put_at ( base::max ( cols.note, out.length + 2 ), value, color, target );

		};

		uint64_t offset_at = 0;
		bool mapped = file.offset_of ( address, offset_at );

		switch ( item.kind ) {

			case line_kind::BANNER: {

				if ( sub > 0 )
					break;

				const loader::section& owner = file.sections [ static_cast < size_t > ( item.section ) ];
				base::writer detail;
				uint64_t span = base::max ( owner.size, owner.raw );

				detail.hex ( owner.address, cols.digits ).add ( L" - " ).hex ( owner.address + span, cols.digits ).add ( L"   " ).add ( loader::kind_name ( owner.type ) ).add ( L"   " ).add ( owner.read ? L"R" : L"-" ).add ( owner.write ? L"W" : L"-" ).add ( owner.execute ? L"X" : L"-" ).add ( L"   " ).grouped ( span ).add ( L" bytes" );
				out.decor = true;
				out.put ( owner.name, tone::LABEL, owner.address, true );
				out.put_at ( 10, detail.text ( ), tone::INK_SOFT );

				break;

			}

			case line_kind::FUNCTION: {

				const analysis::function_info& info = db->functions [ static_cast < size_t > ( item.ref ) ];
				base::writer meta;
				const wchar_t* note = user_text ( notes, info.start );

				if ( !note )
					note = user_text ( comments, info.start );

				put_address ( );
				function_name ( item.ref, name, 200 );
				out.put_at ( cols.code, name, ( info.flags & analysis::THUNK ) ? tone::IMPORT : tone::FUNCTION, info.start, true );

				if ( note ) {

					meta.add ( L"; " ).add ( note );
					put_note ( meta.text ( ), tone::COMMENT, 0 );

					break;

				}

				meta.add ( L"; " ).grouped ( info.instructions ).add ( info.instructions == 1 ? L" instruction" : L" instructions" );

				if ( info.blocks > 1 )
					meta.add ( L", " ).number ( info.blocks ).add ( L" blocks" );

				meta.add ( L", " ).number ( info.callers ).add ( info.callers == 1 ? L" caller" : L" callers" );

				if ( info.flags & analysis::THUNK )
					meta.add ( L", import thunk" );
				else if ( info.flags & analysis::FROM_ENTRY )
					meta.add ( L", entry point" );
				else if ( info.flags & analysis::FROM_EXPORT )
					meta.add ( L", exported" );
				else if ( info.flags & analysis::FROM_TLS )
					meta.add ( L", TLS callback" );

				out.put_at ( base::max ( cols.operands, out.length + 3 ), meta.text ( ), tone::INK_FAINT );

				break;

			}

			case line_kind::END: {

				const analysis::function_info& info = db->functions [ static_cast < size_t > ( item.ref ) ];
				base::writer tag;

				function_name ( item.ref, name, 200 );
				tag.add ( L"end of " ).add ( name ).add ( L"  \x00B7  " ).grouped ( info.end - info.start ).add ( L" bytes" );
				out.decor = true;
				out.put_at ( cols.code, tag.text ( ), tone::INK_FAINT );

				break;

			}

			case line_kind::CHUNK: {

				const analysis::chunk_info& chunk = db->chunks [ static_cast < size_t > ( item.ref ) ];
				base::writer label;

				label.add ( L"; chunk of " );

				if ( symbol_name ( chunk.owner, true, name, 200 ) )
					label.add ( name );
				else
					label.hex ( chunk.owner );

				out.put_at ( cols.code, label.text ( ), tone::LABEL, chunk.owner );

				break;

			}

			case line_kind::CODE: {

				put_address ( );

				if ( !analyzed )
					break;

				if ( label_row ) {

					base::writer label;
					int32_t first_ref = 0;
					int32_t ref_count = 0;

					if ( symbol_name ( address, true, name, 200 ) )
						label.add ( name );
					else
						label.add ( L"loc_" ).hex ( address );

					out.put_at ( cols.code, label.text ( ), tone::LABEL, address, true );
					out.put ( L":", tone::OPERATOR );
					db->refs_to ( address, first_ref, ref_count );

					if ( ref_count ) {

						base::writer refs;
						const analysis::xref& from = db->refs [ static_cast < size_t > ( first_ref ) ];
						uint64_t source = file.base + from.source;
						int32_t from_function = db->function_at ( source );
						int32_t function = db->function_at ( address );

						refs.add ( L"; \x2190 " );

						if ( from_function >= 0 && from_function == function ) {

							x86::instruction from_ins;

							if ( db->decode ( source, from_ins ) ) {

								wchar_t word [ 32 ];

								x86::mnemonic_text ( from_ins, word, 32 );
								refs.add ( word ).add ( L" " );

							}

							refs.add ( L"at +" ).hex ( source - db->functions [ static_cast < size_t > ( from_function ) ].start );

						} else if ( from_function >= 0 ) {

							function_name ( from_function, name, 200 );
							refs.add ( name );

						} else
							refs.hex ( source );

						if ( ref_count > 1 )
							refs.add ( L"  +" ).number ( ref_count - 1 );

						put_note ( refs.text ( ), tone::INK_FAINT, source );

					}

					break;

				}

				x86::instruction ins;

				if ( !db->decode ( address, ins ) )
					break;

				out.instruction = true;

				if ( !patches.empty ( ) && out.count && is_patched ( address, ins.length ) )
					out.runs [ 0 ].color = tone::WARNING;

				if ( cols.bytes && mapped ) {

					base::writer bytes;
					int32_t shown_bytes = base::min ( static_cast < int32_t > ( ins.length ), 7 );

					for ( int32_t b = 0; b < shown_bytes; b++ ) {

						if ( b )
							bytes.add ( L' ' );

						bytes.hex ( file.byte ( offset_at + static_cast < uint64_t > ( b ) ), 2 );

					}

					if ( ins.length > 7 )
						bytes.add ( L"+" );

					out.put_at ( cols.digits + 2, bytes.text ( ), tone::BYTES );

				}

				listing_names namer;
				token_line line;

				namer.owner = this;
				line.owner = this;
				line.db = db;
				x86::format ( ins, line, &namer, 0 );

				int32_t split = line.operand_start < 0 ? line.length : line.operand_start;
				int32_t operand_column = cols.operands;

				for ( int32_t r = 0; r < line.count; r++ ) {

					const token_run& run = line.runs [ r ];
					wchar_t piece [ 200 ];
					int32_t length = base::min ( run.length, 199 );
					bool head = run.start < split;

					for ( int32_t i = 0; i < length; i++ )
						piece [ i ] = line.text [ run.start + i ];

					piece [ length ] = 0;

					if ( head ) {

						out.put_at ( cols.mnemonic + run.start, piece, run.color == tone::MNEMONIC ? mnemonic_tone ( ins ) : run.color, run.target );
						operand_column = base::max ( cols.operands, out.length + 1 );

					} else
						out.put_at ( operand_column + run.start - split, piece, run.color, run.target );

				}

				const wchar_t* note = user_text ( comments, address );
				const wchar_t* stacked = nullptr;
				base::writer annotation;
				tone note_tone = tone::COMMENT;
				uint64_t note_target = 0;

				if ( note ) {

					annotation.add ( L"; " ).add ( note );

				} else if ( ins.name == x86::mnemonic::mov && ins.operands [ 0 ].type == x86::kind::MEM && ( stacked = stack_string ( address ) ) != nullptr ) {

					annotation.add ( stacked );
					note_tone = tone::STRING;

				} else {

					uint64_t target = 0;

					if ( !ins.memory_target ( target ) ) {

						for ( int32_t i = 0; i < ins.count; i++ ) {

							if ( ins.operands [ i ].type == x86::kind::IMM && ins.operands [ i ].size >= 4 )
								target = static_cast < uint64_t > ( ins.operands [ i ].value );

						}

					}

					int32_t text_index = target ? string_at ( target ) : -1;

					if ( text_index >= 0 ) {

						const loader::found_string& found = file.strings [ static_cast < size_t > ( text_index ) ];

						annotation.add ( found.wide ? L"L\"" : L"\"" );

						for ( uint32_t i = 0; i < found.length && i < 80; i++ ) {

							wchar_t ch = static_cast < wchar_t > ( file.byte ( found.offset + static_cast < uint64_t > ( i ) * ( found.wide ? 2 : 1 ) ) );

							annotation.add ( ch == L'\t' || ch == L'\n' || ch == L'\r' ? L' ' : ch );

						}

						annotation.add ( found.length > 80 ? L"...\"" : L"\"" );
						note_tone = tone::STRING;
						note_target = target;

					} else if ( ins.control == x86::flow::JUMP && !ins.direct ( ) && db->table_for_jump ( address ) >= 0 ) {

						annotation.add ( L"; switch, " ).number ( db->tables [ static_cast < size_t > ( db->table_for_jump ( address ) ) ].count ).add ( L" cases" );

					} else if ( ( ins.control == x86::flow::CALL || ins.control == x86::flow::JUMP ) && !ins.direct ( ) ) {

						base::writer hint;

						if ( indirect_note ( ins, hint ) ) {

							annotation.add ( L"; " ).add ( hint.text ( ) );
							note_tone = tone::FUNCTION;

						}

					} else if ( ins.control == x86::flow::NONE && ins.immediate_size >= 4 && !user_text ( number_forms, address ) ) {

						for ( int32_t i = 0; i < ins.count; i++ ) {

							const x86::operand& operand = ins.operands [ i ];
							base::writer hint;

							if ( operand.type == x86::kind::IMM && operand.size == ins.immediate_size && float_hint ( hint, static_cast < uint64_t > ( operand.value ), operand.size ) ) {

								annotation.add ( L"; " ).add ( hint.text ( ) );
								note_tone = tone::NUMBER;

								break;

							}

						}

					}

				}

				if ( annotation.size ( ) )
					put_note ( annotation.text ( ), note_tone, note_target );

				break;

			}

			case line_kind::ALIGN: {

				base::writer value;

				put_address ( );
				value.number ( static_cast < int64_t > ( item.size ) ).add ( item.size == 1 ? L" byte of " : L" bytes of " );

				if ( item.ref == 0x90 )
					value.add ( L"nop" );
				else
					value.hex ( static_cast < uint64_t > ( item.ref ), 2 );

				out.put_at ( cols.mnemonic, L"align", tone::INK_FAINT );
				out.put_at ( cols.operands, value.text ( ), tone::INK_FAINT );

				break;

			}

			case line_kind::TABLE: {

				put_address ( );

				if ( item.ref <= -2 ) {

					int32_t table_index = -item.ref - 2;
					base::writer bytes;
					uint64_t take = base::min ( static_cast < uint64_t > ( 8 ), item.address + item.size - address );

					for ( uint64_t b = 0; b < take && mapped; b++ ) {

						if ( b )
							bytes.add ( L", " );

						bytes.number ( file.byte ( offset_at + b ) );

					}

					out.put_at ( cols.mnemonic, L"db", tone::MNEMONIC );
					out.put_at ( cols.operands, bytes.text ( ), tone::NUMBER );

					if ( sub == 0 )
						put_note ( base::writer ( ).add ( L"; case index for switch " ).number ( table_index ).text ( ), tone::COMMENT, 0 );

					break;

				}

				const analysis::jump_table& table = db->tables [ static_cast < size_t > ( item.ref ) ];
				uint64_t raw = table.entry_size == 8 ? file.read64 ( offset_at ) : file.read32 ( offset_at );
				uint64_t target = table.type == analysis::table_kind::IMAGE_BASED ? file.base + static_cast < uint32_t > ( raw ) : ( table.type == analysis::table_kind::TABLE_BASED ? table.anchor + static_cast < uint64_t > ( static_cast < int64_t > ( static_cast < int32_t > ( raw ) ) ) : raw );
				base::writer value;

				if ( symbol_name ( target, true, name, 200 ) )
					value.add ( name );
				else
					value.hex ( target );

				out.put_at ( cols.mnemonic, table.entry_size == 8 ? L"dq" : L"dd", tone::MNEMONIC );
				out.put_at ( cols.operands, value.text ( ), tone::LABEL, target );
				put_note ( base::writer ( ).add ( L"; case " ).number ( sub ).text ( ), tone::COMMENT, 0 );

				break;

			}

			case line_kind::VTABLE: {

				uint64_t target = mapped ? ( item.stride == 8 ? file.read64 ( offset_at ) : file.read32 ( offset_at ) ) : 0;
				base::writer value;

				put_address ( );

				if ( symbol_name ( target, true, name, 200 ) )
					value.add ( name );
				else
					value.hex ( target );

				out.put_at ( cols.mnemonic, item.stride == 8 ? L"dq" : L"dd", tone::MNEMONIC );
				out.put_at ( cols.operands, value.text ( ), tone::FUNCTION, target );
				put_note ( base::writer ( ).add ( L"; slot " ).number ( sub ).text ( ), tone::COMMENT, 0 );

				break;

			}

			case line_kind::LABEL: {

				base::writer label;
				const wchar_t* note = nullptr;
				base::writer refs;

				put_address ( );

				if ( item.ref >= 0 ) {

					label.add ( names [ static_cast < size_t > ( item.ref ) ].get ( ) );
					note = item.function ? L"; function" : L"; exported data";

				} else if ( symbol_name ( item.address, false, name, 200 ) )
					label.add ( name );
				else
					label.add ( L"g_" ).hex ( item.address );

				out.put_at ( cols.code, label.text ( ), tone::GLOBAL, item.address, true );
				out.put ( L":", tone::OPERATOR );

				int32_t first_ref = 0;
				int32_t ref_count = 0;

				if ( analyzed )
					db->refs_to ( item.address, first_ref, ref_count );

				if ( ref_count ) {

					int32_t writes = 0;

					for ( int32_t i = 0; i < ref_count; i++ ) {

						if ( db->refs [ static_cast < size_t > ( first_ref + i ) ].type == analysis::ref::WRITE )
							writes++;

					}

					refs.add ( L"; \x2190 " ).number ( ref_count ).add ( ref_count == 1 ? L" ref" : L" refs" );

					if ( writes )
						refs.add ( L", " ).number ( writes ).add ( writes == 1 ? L" write" : L" writes" );

					note = refs.text ( );

				}

				const wchar_t* user = user_text ( comments, item.address );
				base::writer user_note;

				if ( user ) {

					user_note.add ( L"; " ).add ( user );
					note = user_note.text ( );

				}

				if ( note )
					put_note ( note, user ? tone::COMMENT : tone::INK_FAINT, 0 );

				break;

			}

			case line_kind::DATA: {

				base::writer bytes;
				uint64_t end = item.address + item.size;
				uint32_t take = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( item.stride ), end > address ? end - address : 0 ) );

				put_address ( );

				for ( uint32_t b = 0; b < take && mapped; b++ ) {

					if ( b )
						bytes.add ( L' ' );

					bytes.hex ( file.byte ( offset_at + b ), 2 );

				}

				out.put_at ( cols.code, bytes.text ( ), tone::BYTES );

				break;

			}

			case line_kind::STRING: {

				const loader::found_string& found = file.strings [ static_cast < size_t > ( item.ref ) ];
				base::writer value;
				int32_t shown_chars = base::min ( static_cast < int32_t > ( found.length ), 240 );

				put_address ( );
				value.add ( L'"' );

				for ( int32_t i = 0; i < shown_chars; i++ ) {

					wchar_t ch = static_cast < wchar_t > ( file.byte ( found.offset + static_cast < uint64_t > ( i ) * ( found.wide ? 2 : 1 ) ) );

					if ( ch == L'\t' )
						value.add ( L"\\t" );
					else if ( ch == L'\n' )
						value.add ( L"\\n" );
					else if ( ch == L'\r' )
						value.add ( L"\\r" );
					else
						value.add ( ch );

				}

				if ( shown_chars < static_cast < int32_t > ( found.length ) )
					value.add ( L"..." );

				value.add ( L"\", 0" );
				out.put_at ( cols.mnemonic, found.wide ? L"du" : L"db", tone::MNEMONIC );
				out.put_at ( cols.operands, value.text ( ), tone::STRING );

				break;

			}

			case line_kind::POINTER: {

				const loader::import& entry = file.imports [ static_cast < size_t > ( item.ref ) ];
				const loader::module& from = file.modules [ static_cast < size_t > ( entry.owner ) ];
				base::writer label;
				base::writer note;

				put_address ( );

				if ( entry.by_ordinal || entry.name.empty ( ) )
					label.add ( L"#" ).number ( entry.ordinal );
				else
					label.add ( entry.name.get ( ) );

				note.add ( L"; import from " ).add ( from.name.get ( ) );
				out.put_at ( cols.mnemonic, file.wide ( ) ? L"dq" : L"dd", tone::MNEMONIC );
				out.put_at ( cols.operands, label.text ( ), tone::IMPORT );
				put_note ( note.text ( ), tone::COMMENT, 0 );

				break;

			}

			case line_kind::EMPTY: {

				base::writer value;

				put_address ( );
				value.grouped ( item.size ).add ( L" bytes" );
				out.put_at ( cols.mnemonic, L"reserve", tone::MNEMONIC );
				out.put_at ( cols.operands, value.text ( ), tone::NUMBER );
				put_note ( L"; uninitialized, not in the file", tone::COMMENT, 0 );

				break;

			}

			default:
				break;

		}

	}

	bool app::is_breakpoint ( uint64_t address ) const {

		for ( uint64_t item : breakpoints ) {

			if ( item == address )
				return true;

		}

		return false;

	}

	void app::sync_panes ( uint64_t address ) {

		for ( int32_t p = 0; p < pane_count; p++ ) {

			if ( p == focused_pane )
				continue;

			int32_t doc = pane_document ( p );

			if ( doc >= 0 && documents [ doc ].kind == view::LISTING && address )
				documents [ doc ].state.cursor_address = address;

		}

	}

	void app::sync_breakpoints ( ) {

		base::list < uint64_t > rvas;

		for ( uint64_t item : breakpoints )
			rvas.push ( item - file.base );

		debugger::set_breakpoints ( rvas.data ( ), rvas.count ( ) );

		for ( uint64_t item : breakpoints ) {

			const wchar_t* rule = user_text ( conditions, item );

			debugger::set_condition ( item - file.base, rule ? rule : L"" );

		}

	}

	void app::poll_debugger ( ) {

		debugger::snapshot value = debugger::read ( );
		wchar_t text [ 420 ];

		if ( !coverage_blocks.empty ( ) )
			merge_coverage ( );

		while ( debugger::take_log ( text, 420 ) )
			log ( log_kind::INFO, text );

		if ( value.serial == debug_state.serial )
			return;

		debug_state = value;
		pending = base::max ( pending, 2 );

		if ( value.state == debugger::status::PAUSED ) {

			base::writer line;
			uint64_t span = file.high ( ) - file.base;

			debug_pc = value.pc >= value.image_base && value.pc - value.image_base < span ? value.pc - value.image_base + file.base : 0;
			line.add ( value.note ).add ( L" at " ).hex ( value.pc );

			if ( !debug_pc )
				line.add ( L", outside the image" );

			log ( log_kind::GOOD, line.text ( ) );

			if ( debug_pc )
				go ( debug_pc, true );

		} else if ( value.state == debugger::status::FAILED ) {

			debug_pc = 0;
			log ( log_kind::FAIL, value.note );

		} else if ( value.state != debugger::status::PAUSED ) {

			debug_pc = 0;

			if ( value.state == debugger::status::EXITED && !coverage_blocks.empty ( ) ) {

				merge_coverage ( );
				log ( log_kind::GOOD, base::writer ( ).add ( L"Coverage: " ).grouped ( static_cast < uint64_t > ( coverage.count ( ) ) ).add ( L" instructions ran in " ).grouped ( static_cast < uint64_t > ( coverage_functions ) ).add ( L" of " ).grouped ( db ? static_cast < uint64_t > ( db->functions.count ( ) ) : 0 ).add ( L" functions. Query them with functions where covered." ).text ( ) );
				coverage_blocks.clear ( );

			}

		}

	}

	static bool word_char ( wchar_t value ) {

		return ( value >= L'a' && value <= L'z' ) || ( value >= L'A' && value <= L'Z' ) || ( value >= L'0' && value <= L'9' ) || value == L'_' || value == L'.' || value == L'@' || value == L'$' || value == L'?' || value == L':';

	}

	static void push_slice ( base::list < wchar_t >& out, const wchar_t* text, int32_t length, int32_t from, int32_t to ) {

		from = base::clamp ( from, 0, length );
		to = base::clamp ( to, from, length );

		while ( to > from && text [ to - 1 ] == L' ' )
			to--;

		for ( int32_t i = from; i < to; i++ )
			out.push ( text [ i ] );

	}

	bool app::listing_selection ( base::list < wchar_t >& out ) const {

		row_line line;

		if ( !now->ranged || ( now->select_anchor == now->cursor_row && now->anchor_col == now->caret_col ) ) {

			compose_row ( now->cursor_row, listing_cols, line );
			push_slice ( out, line.text, line.length, 0, line.length );
			out.push ( 0 );

			return false;

		}

		bool forward = now->select_anchor < now->cursor_row || ( now->select_anchor == now->cursor_row && now->anchor_col <= now->caret_col );
		uint64_t low = forward ? now->select_anchor : now->cursor_row;
		uint64_t high = forward ? now->cursor_row : now->select_anchor;
		int32_t low_col = forward ? now->anchor_col : now->caret_col;
		int32_t high_col = forward ? now->caret_col : now->anchor_col;

		high = base::min ( high, low + 50000 );

		for ( uint64_t row = low; row <= high; row++ ) {

			compose_row ( row, listing_cols, line );
			push_slice ( out, line.text, line.length, row == low ? low_col : 0, row == high ? high_col : line.length );

			if ( row < high ) {

				out.push ( L'\r' );
				out.push ( L'\n' );

			}

		}

		out.push ( 0 );

		return true;

	}

	bool app::code_selection ( base::list < wchar_t >& out ) const {

		const decompiler::result* code = const_cast < app* > ( this )->pseudocode_for ( now->function );

		if ( !code || code->lines.empty ( ) )
			return false;

		auto line_text = [ & ] ( int32_t index, wchar_t* text, int32_t& length ) {

			const decompiler::line& item = code->lines [ static_cast < size_t > ( index ) ];

			length = 0;

			for ( int32_t k = 0; k < item.indent * 4 && length < 1000; k++ )
				text [ length++ ] = L' ';

			for ( int32_t k = 0; k < item.length && length < 1000; k++ )
				text [ length++ ] = code->text [ static_cast < size_t > ( item.text + k ) ];

			text [ length ] = 0;

		};

		wchar_t text [ 1024 ];
		int32_t length = 0;
		int32_t total = code->lines.count ( );
		bool ranged = now->code_ranged && ( now->code_anchor != now->code_line || now->code_anchor_col != now->code_col );

		if ( !ranged ) {

			line_text ( base::clamp ( now->code_line, 0, total - 1 ), text, length );

			int32_t start = 0;

			while ( start < length && text [ start ] == L' ' )
				start++;

			push_slice ( out, text, length, start, length );
			out.push ( 0 );

			return false;

		}

		bool forward = now->code_anchor < now->code_line || ( now->code_anchor == now->code_line && now->code_anchor_col <= now->code_col );
		int32_t low = base::clamp ( forward ? now->code_anchor : now->code_line, 0, total - 1 );
		int32_t high = base::clamp ( forward ? now->code_line : now->code_anchor, 0, total - 1 );
		int32_t low_col = forward ? now->code_anchor_col : now->code_col;
		int32_t high_col = forward ? now->code_col : now->code_anchor_col;

		for ( int32_t row = low; row <= high; row++ ) {

			line_text ( row, text, length );
			push_slice ( out, text, length, row == low ? low_col : 0, row == high ? high_col : length );

			if ( row < high ) {

				out.push ( L'\r' );
				out.push ( L'\n' );

			}

		}

		out.push ( 0 );

		return true;

	}

	uint64_t app::reference_subject ( ) const {

		if ( !db || !analysis_applied )
			return now->cursor_address;

		if ( showing == view::PSEUDOCODE ) {

			const decompiler::result* code = const_cast < app* > ( this )->pseudocode_for ( now->function );

			if ( code && now->code_line >= 0 && now->code_line < code->lines.count ( ) ) {

				const decompiler::line& item = code->lines [ static_cast < size_t > ( now->code_line ) ];
				int32_t column = now->code_col - item.indent * 4;

				for ( int32_t s = 0; s < item.span_count; s++ ) {

					const decompiler::span& part = code->spans [ static_cast < size_t > ( item.first_span + s ) ];
					int32_t start = part.start - item.text;

					if ( part.target && ( ( column >= start && column < start + part.length ) || column == start + part.length ) )
						return part.target;

				}

			}

			return now->function;

		}

		if ( showing == view::LISTING ) {

			row_line line;

			compose_row ( now->cursor_row, listing_cols, line );

			int32_t hit = line.run_at ( now->caret_col );

			if ( hit < 0 && now->caret_col > 0 )
				hit = line.run_at ( now->caret_col - 1 );

			if ( hit >= 0 && line.runs [ hit ].target )
				return line.runs [ hit ].target;

			if ( line.kind == line_kind::FUNCTION || line.kind == line_kind::LABEL )
				return line.address;

		}

		uint64_t address = now->cursor_address;
		x86::instruction ins;
		uint64_t target = 0;

		if ( db->is_start ( address ) && db->decode ( address, ins ) ) {

			if ( ins.direct ( ) && ( ins.control == x86::flow::CALL || ins.control == x86::flow::JUMP ) )
				return ins.target ( );

			if ( ins.memory_target ( target ) && file.section_at ( target ) > 0 )
				return target;

		}

		int32_t first = 0;
		int32_t count = 0;

		db->refs_to ( address, first, count );

		if ( count )
			return address;

		int32_t owner = db->function_at ( address );

		return owner >= 0 ? db->functions [ static_cast < size_t > ( owner ) ].start : address;

	}

	int32_t step_caret ( const wchar_t* text, int32_t length, int32_t column, bool right, bool word ) {

		if ( !word )
			return base::clamp ( column + ( right ? 1 : -1 ), 0, length );

		if ( right ) {

			while ( column < length && word_char ( text [ column ] ) )
				column++;

			while ( column < length && !word_char ( text [ column ] ) )
				column++;

			return column;

		}

		while ( column > 0 && !word_char ( text [ column - 1 ] ) )
			column--;

		while ( column > 0 && word_char ( text [ column - 1 ] ) )
			column--;

		return column;

	}

	bool word_bounds ( const wchar_t* text, int32_t length, int32_t column, int32_t& from, int32_t& to ) {

		if ( column >= length || !word_char ( text [ column ] ) ) {

			if ( column > 0 && column - 1 < length && word_char ( text [ column - 1 ] ) )
				column--;
			else
				return false;

		}

		from = column;
		to = column;

		while ( from > 0 && word_char ( text [ from - 1 ] ) && text [ from - 1 ] != L':' )
			from--;

		while ( to < length && word_char ( text [ to ] ) && text [ to ] != L':' )
			to++;

		return to > from;

	}

	static int32_t family_of_text ( const wchar_t* text, int32_t length ) {

		if ( length < 2 || length > 5 )
			return -1;

		for ( int32_t r = 1; r < x86::REG_COUNT; r++ ) {

			const char* name = x86::register_name ( static_cast < uint8_t > ( r ) );
			int32_t i = 0;

			if ( !name )
				continue;

			while ( i < length && name [ i ] && base::fold_case ( text [ i ] ) == static_cast < wchar_t > ( name [ i ] ) )
				i++;

			if ( i == length && !name [ i ] )
				return x86::register_family ( static_cast < uint8_t > ( r ) );

		}

		return -1;

	}

	void app::set_highlight ( const wchar_t* text, int32_t length, int32_t column ) {

		wchar_t fresh [ 64 ] = { };
		int32_t from = 0;
		int32_t to = 0;

		if ( text && word_bounds ( text, length, base::clamp ( column, 0, length ), from, to ) && to - from < 63 ) {

			for ( int32_t i = from; i < to; i++ )
				fresh [ i - from ] = text [ i ];

		}

		if ( base::same_text ( fresh, highlight ) )
			return;

		for ( int32_t i = 0; i < 64; i++ )
			highlight [ i ] = fresh [ i ];

		highlight_family = family_of_text ( highlight, base::text_length ( highlight ) );
		pending = base::max ( pending, 2 );

	}

	bool app::highlighted ( const wchar_t* piece, int32_t length ) const {

		if ( !highlight [ 0 ] || length <= 0 )
			return false;

		int32_t i = 0;

		while ( i < length && highlight [ i ] && piece [ i ] == highlight [ i ] )
			i++;

		if ( i == length && !highlight [ i ] )
			return true;

		return highlight_family >= 0 && family_of_text ( piece, length ) == highlight_family;

	}

}