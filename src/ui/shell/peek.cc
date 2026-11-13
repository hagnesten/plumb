#include "ui/app.hh"
#include "ui/icons.hh"
#include "ui/tokens.hh"

namespace ui {

	static constexpr float peek_width = 470.0f;
	static constexpr float peek_head = 26.0f;
	static constexpr float peek_row = 16.0f;
	static constexpr int32_t peek_limit = 14;

	void app::peek ( uint64_t target, math::rect anchor ) {

		if ( !target || menu_open >= 0 || palette_open || prompt_open )
			return;

		peek_candidate = target;
		peek_candidate_anchor = anchor;

	}

	void app::build_peek ( uint64_t target, base::list < row_line >& out ) const {

		out.clear ( );

		bool wide = file.wide ( );
		int32_t text_index = string_at ( target );

		for ( const loader::import& item : file.imports ) {

			if ( item.slot != target )
				continue;

			row_line& line = out.emplace ( );
			base::writer label;

			label.add ( file.modules [ static_cast < size_t > ( item.owner ) ].name.get ( ) ).add ( L"!" );

			if ( item.by_ordinal || item.name.empty ( ) )
				label.add ( L"#" ).number ( item.ordinal );
			else
				label.add ( item.name.get ( ) );

			line.clear ( );
			line.put ( L"import  ", tone::INK_FAINT );
			line.put ( label.text ( ), tone::IMPORT );

			return;

		}

		if ( text_index >= 0 ) {

			const loader::found_string& item = file.strings [ static_cast < size_t > ( text_index ) ];
			int32_t length = static_cast < int32_t > ( item.length );
			int32_t at = 0;

			while ( at < length && out.count ( ) < 4 ) {

				row_line& line = out.emplace ( );
				wchar_t chunk [ 64 ];
				int32_t take = base::min ( length - at, 60 );

				for ( int32_t i = 0; i < take; i++ ) {

					wchar_t value = static_cast < wchar_t > ( file.byte ( item.offset + static_cast < uint64_t > ( at + i ) * ( item.wide ? 2 : 1 ) ) );

					chunk [ i ] = value < 32 ? L'\x00B7' : value;

				}

				chunk [ take ] = 0;
				line.clear ( );
				line.put ( chunk, tone::STRING );
				at += take;

			}

			return;

		}

		if ( db && analysis_applied && db->is_start ( target ) ) {

			uint64_t address = target;
			listing_names namer;

			namer.owner = this;

			while ( out.count ( ) < peek_limit ) {

				x86::instruction ins;

				if ( !db->decode ( address, ins ) )
					break;

				token_line printed;
				row_line& line = out.emplace ( );
				base::writer where;

				printed.owner = this;
				printed.db = db;
				x86::format ( ins, printed, &namer, 0 );
				line.clear ( );
				line.address = address;
				where.hex ( address, wide ? 9 : 8 );
				line.put ( where.text ( ), tone::ADDRESS );

				int32_t split = printed.operand_start < 0 ? printed.length : printed.operand_start;
				int32_t operand_column = 19;

				for ( int32_t r = 0; r < printed.count; r++ ) {

					const token_run& run = printed.runs [ r ];
					wchar_t piece [ 200 ];
					int32_t length = base::min ( run.length, 199 );
					bool head = run.start < split;

					for ( int32_t i = 0; i < length; i++ )
						piece [ i ] = printed.text [ run.start + i ];

					piece [ length ] = 0;

					if ( head ) {

						line.put_at ( 11 + run.start, piece, run.color == tone::MNEMONIC ? mnemonic_tone ( ins ) : run.color );
						operand_column = base::max ( 19, line.length + 1 );

					} else
						line.put_at ( operand_column + run.start - split, piece, run.color );

				}

				if ( ins.control == x86::flow::RETURN || ins.control == x86::flow::STOP || ins.control == x86::flow::JUMP )
					break;

				address = ins.next ( );

			}

			return;

		}

		uint64_t offset = 0;

		if ( !file.offset_of ( target, offset ) )
			return;

		for ( int32_t row = 0; row < 4; row++ ) {

			row_line& line = out.emplace ( );
			base::writer where;
			base::writer bytes;
			wchar_t shown [ 9 ];

			line.clear ( );
			where.hex ( target + static_cast < uint64_t > ( row * 8 ), wide ? 9 : 8 );
			line.put ( where.text ( ), tone::ADDRESS );

			for ( int32_t i = 0; i < 8; i++ ) {

				uint64_t at = offset + static_cast < uint64_t > ( row * 8 + i );
				uint8_t value = at < file.size ? file.byte ( at ) : 0;

				bytes.hex ( value, 2 ).add ( L" " );
				shown [ i ] = value >= 32 && value < 127 ? static_cast < wchar_t > ( value ) : L'.';

			}

			shown [ 8 ] = 0;
			line.put_at ( 11, bytes.text ( ), tone::NUMBER );
			line.put_at ( 37, shown, tone::INK_SOFT );

		}

	}

	bool app::draw_peek_box ( uint64_t target, math::rect box, const base::list < row_line >& rows, bool pinned ) {

		render::canvas& c = canvas;
		uint64_t id = make_id ( pinned ? "peek_pinned" : "peek_box" );
		widget& surface = state ( id );
		wchar_t name [ 200 ];
		base::writer where;
		base::writer detail;
		bool keep = true;
		int32_t function = db && analysis_applied ? db->function_starting ( target ) : -1;

		interact ( id, box, surface );
		c.shadow ( box, metric::radius, 12.0f, ink ( tone::SHADOW ).fade ( 0.6f ), math::vec2 ( 0.0f, 3.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( pinned ? tone::SELECT_LINE : tone::LINE_STRONG ) );
		c.fill ( math::rect ( box.x + 1.0f, box.y + peek_head, box.w - 2.0f, 1.0f ), 0.0f, ink ( tone::LINE ) );

		if ( !symbol_name ( target, true, name, 200 ) ) {

			base::writer fallback;

			fallback.hex ( target, file.wide ( ) ? 9 : 8 );

			for ( int32_t i = 0; i <= fallback.size ( ); i++ )
				name [ i ] = fallback.text ( ) [ i ];

		}

		if ( function >= 0 ) {

			const analysis::function_info& info = db->functions [ static_cast < size_t > ( function ) ];
			const wchar_t* shape = prototype_for ( target );
			const wchar_t* remark = user_text ( notes, target );

			if ( remark )
				detail.add ( remark );
			else if ( shape )
				detail.add ( shape );
			else
				detail.number ( info.instructions ).add ( L" instructions \x00B7 " ).number ( info.callers ).add ( info.callers == 1 ? L" caller \x00B7 0x" : L" callers \x00B7 0x" ).hex ( info.end - info.start ).add ( L" bytes" );

		} else if ( string_at ( target ) >= 0 )
			detail.add ( L"string" );

		where.hex ( target, file.wide ( ) ? 9 : 8 );

		float right = box.right ( ) - 8.0f;

		if ( icon_button ( make_id ( pinned ? "peek_close" : "peek_pin" ), math::vec2 ( right - 9.0f, box.y + peek_head * 0.5f ), 20.0f, pinned ? icons::CANCEL : icons::PIN, pinned ? 8.0f : 10.0f, pinned ? L"Close" : L"Keep this open" ) ) {

			if ( pinned )
				keep = false;
			else {

				pinned_target = target;
				pinned_built = 0;
				peek_target = 0;

			}

		}

		right -= 24.0f;

		float where_w = c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( right, box.y + peek_head * 0.5f ), ink ( tone::ADDRESS ), render::align::RIGHT );
		uint64_t title_id = make_id ( pinned ? "peek_title_pinned" : "peek_title" );
		widget& title = state ( title_id );
		math::rect title_area ( box.x + 8.0f, box.y + 3.0f, right - where_w - box.x - 20.0f, peek_head - 6.0f );

		if ( interact ( title_id, title_area, title ) )
			go ( target, true );

		if ( title.hover > 0.5f )
			wanted_cursor = pointer::HAND;

		c.text ( name, render::font::MONO_STRONG, math::vec2 ( box.x + 10.0f, box.y + peek_head * 0.5f ), ink ( function >= 0 ? tone::FUNCTION : tone::LABEL ), render::align::LEFT, title_area.w );
		tip ( title_id, L"Go there", title_area );

		float y = box.y + peek_head + 4.0f;

		if ( detail.size ( ) ) {

			c.text ( detail.text ( ), render::font::SMALL, math::vec2 ( box.x + 10.0f, y + 8.0f ), ink ( tone::INK_SOFT ), render::align::LEFT, box.w - 20.0f );
			y += 18.0f;

		}

		float adv = c.measure ( L"0000000000", render::font::MONO_SMALL ) * 0.1f;

		c.push_clip ( math::rect ( box.x + 1.0f, box.y + peek_head, box.w - 2.0f, box.h - peek_head - 1.0f ) );

		for ( const row_line& line : rows ) {

			float cy = y + peek_row * 0.5f;

			for ( int32_t r = 0; r < line.count; r++ ) {

				const token_run& run = line.runs [ r ];
				wchar_t piece [ 400 ];
				int32_t length = base::min ( run.length, 399 );

				for ( int32_t i = 0; i < length; i++ )
					piece [ i ] = line.text [ run.start + i ];

				piece [ length ] = 0;
				c.text ( piece, render::font::MONO_SMALL, math::vec2 ( box.x + 10.0f + static_cast < float > ( run.start ) * adv, cy ), ink ( run.color ) );

			}

			y += peek_row;

		}

		if ( rows.empty ( ) )
			c.text ( L"Nothing to preview here.", render::font::SMALL, math::vec2 ( box.x + 10.0f, y + 8.0f ), ink ( tone::INK_FAINT ) );

		c.pop_clip ( );

		return keep;

	}

	void app::draw_peeks ( ) {

		auto box_height = [ & ] ( uint64_t target, const base::list < row_line >& rows ) {

			bool detailed = ( db && analysis_applied && db->function_starting ( target ) >= 0 ) || string_at ( target ) >= 0;

			return peek_head + 8.0f + ( detailed ? 18.0f : 0.0f ) + static_cast < float > ( base::max ( rows.count ( ), 1 ) ) * peek_row + 4.0f;

		};

		if ( pinned_target ) {

			if ( pinned_built != pinned_target ) {

				build_peek ( pinned_target, pinned_rows );
				pinned_built = pinned_target;

			}

			float box_h = box_height ( pinned_target, pinned_rows );
			math::rect box ( view_area.right ( ) - peek_width - 14.0f, view_area.bottom ( ) - box_h - 12.0f, peek_width, box_h );

			if ( !draw_peek_box ( pinned_target, box, pinned_rows, true ) )
				pinned_target = 0;

		}

		if ( !peek_target || peek_time < 0.45f || peek_target == pinned_target )
			return;

		if ( peek_built != peek_target ) {

			build_peek ( peek_target, peek_rows );
			peek_built = peek_target;

		}

		float box_h = box_height ( peek_target, peek_rows );
		float x = base::clamp ( peek_anchor.x - 10.0f, 6.0f, w - peek_width - 6.0f );
		float y = peek_anchor.bottom ( ) + 4.0f;

		if ( y + box_h > status_area.y - 4.0f )
			y = peek_anchor.y - box_h - 4.0f;

		peek_box = math::rect ( x, y, peek_width, box_h );
		draw_peek_box ( peek_target, peek_box, peek_rows, false );

	}

}
