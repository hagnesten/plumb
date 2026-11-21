#include "ui/app.hh"
#include <windows.h>

namespace ui {

	static void split_lines ( const char* text, base::list < base::list < char > >& out ) {

		out.clear ( );

		while ( text && *text ) {

			base::list < char >& line = out.emplace ( );

			while ( *text && *text != '\n' )
				line.push ( *text++ );

			line.push ( 0 );

			if ( *text == '\n' )
				text++;

		}

	}

	static bool same_line ( const base::list < char >& a, const base::list < char >& b ) {

		return a.count ( ) == b.count ( ) && base::same_text ( a.data ( ), b.data ( ) );

	}

	static bool contains_line ( const base::list < base::list < char > >& lines, const base::list < char >& line ) {

		for ( const base::list < char >& item : lines ) {

			if ( same_line ( item, line ) )
				return true;

		}

		return false;

	}

	static void field_of ( const char* line, int32_t index, wchar_t* out, int32_t room, bool rest ) {

		const char* at = line;

		for ( int32_t i = 0; i < index && *at; i++ ) {

			while ( *at && *at != ' ' )
				at++;

			while ( *at == ' ' )
				at++;

		}

		const char* end = at;

		while ( *end && ( rest || *end != ' ' ) )
			end++;

		int32_t made = end > at ? MultiByteToWideChar ( CP_UTF8, 0, at, static_cast < int32_t > ( end - at ), out, room - 1 ) : 0;

		out [ made > 0 ? made : 0 ] = 0;

	}

	void app::describe_change ( const char* before, const char* after, base::writer& out ) const {

		static const struct { const char* key; const wchar_t* label; } labels [ ] = {
			{ "name", L"Rename" }, { "comment", L"Comment" }, { "bookmark", L"Bookmark" }, { "note", L"Note" },
			{ "condition", L"Breakpoint condition" }, { "number", L"Number form" }, { "cnumber", L"C number form" },
			{ "proto", L"Prototype" }, { "bp", L"Breakpoint" }, { "pb", L"Patch" }, { "local", L"Variable" },
			{ "struct", L"Struct" }, { "field", L"Struct field" },
		};

		base::list < base::list < char > > old_lines;
		base::list < base::list < char > > new_lines;
		const base::list < char >* added = nullptr;
		const base::list < char >* removed = nullptr;
		int32_t changes = 0;

		split_lines ( before, old_lines );
		split_lines ( after, new_lines );

		for ( const base::list < char >& line : new_lines ) {

			if ( !contains_line ( old_lines, line ) ) {

				if ( !added )
					added = &line;

				changes++;

			}

		}

		for ( const base::list < char >& line : old_lines ) {

			if ( !contains_line ( new_lines, line ) ) {

				if ( !removed )
					removed = &line;

				changes++;

			}

		}

		if ( !added && !removed ) {

			out.add ( L"No visible change" );

			return;

		}

		const char* line = added ? added->data ( ) : removed->data ( );
		const wchar_t* label = L"Change";
		wchar_t key [ 32 ];
		wchar_t where [ 40 ];
		wchar_t value [ 200 ];

		field_of ( line, 0, key, 32, false );

		for ( const auto& item : labels ) {

			int32_t k = 0;

			while ( item.key [ k ] && static_cast < wchar_t > ( item.key [ k ] ) == key [ k ] )
				k++;

			if ( !item.key [ k ] && !key [ k ] )
				label = item.label;

		}

		field_of ( line, 1, where, 40, false );
		field_of ( line, 2, value, 200, true );

		bool listed = base::same_text ( key, L"struct" ) || base::same_text ( key, L"field" );
		uint64_t address = 0;

		for ( const wchar_t* at = where; *at; at++ ) {

			wchar_t c = *at;
			uint32_t digit = c >= L'0' && c <= L'9' ? static_cast < uint32_t > ( c - L'0' ) : ( c >= L'a' && c <= L'f' ? static_cast < uint32_t > ( c - L'a' + 10 ) : ( c >= L'A' && c <= L'F' ? static_cast < uint32_t > ( c - L'A' + 10 ) : 16u ) );

			if ( digit > 15 )
				break;

			address = address * 16 + digit;

		}

		out.add ( label );

		if ( !added )
			out.add ( L" removed" );

		if ( base::same_text ( key, L"pb" ) )
			out.add ( L" at file offset " ).add ( where );
		else if ( !listed ) {

			wchar_t name [ 200 ];

			out.add ( L" at " ).hex ( address );

			if ( symbol_name ( address, true, name, 200 ) && !base::same_text ( key, L"name" ) )
				out.add ( L" (" ).add ( name ).add ( L")" );

		}

		if ( added && value [ 0 ] && !base::same_text ( key, L"bp" ) && !base::same_text ( key, L"pb" ) )
			out.add ( L": " ).add ( value );

		int32_t others = changes - 1 - ( added && removed ? 1 : 0 );

		if ( others > 0 )
			out.add ( L"  +" ).number ( others ).add ( L" more" );

	}

	void app::open_history ( ) {

		base::list < char > current;

		serialize_project ( current );
		current.push ( 0 );
		history_rows.clear ( );
		history_steps.clear ( );

		for ( int32_t i = 0; i < redo_stack.count ( ); i++ ) {

			const char* before = i + 1 < redo_stack.count ( ) ? redo_stack [ static_cast < size_t > ( i + 1 ) ].data ( ) : current.data ( );
			base::writer text;

			describe_change ( before, redo_stack [ static_cast < size_t > ( i ) ].data ( ), text );
			history_rows.push ( base::text ( text.text ( ) ) );
			history_steps.push ( redo_stack.count ( ) - i );

		}

		history_rows.push ( base::text ( L"Now" ) );
		history_steps.push ( 0 );
		history_pick = history_rows.count ( ) - 1;

		for ( int32_t i = undo_stack.count ( ) - 1; i >= 0; i-- ) {

			const char* after = i + 1 < undo_stack.count ( ) ? undo_stack [ static_cast < size_t > ( i + 1 ) ].data ( ) : current.data ( );
			base::writer text;

			describe_change ( undo_stack [ static_cast < size_t > ( i ) ].data ( ), after, text );
			history_rows.push ( base::text ( text.text ( ) ) );
			history_steps.push ( -( undo_stack.count ( ) - i ) );

		}

		history_open = true;

	}

	void app::history_jump ( int32_t row ) {

		if ( row < 0 || row >= history_steps.count ( ) )
			return;

		int32_t steps = history_steps [ static_cast < size_t > ( row ) ];
		int32_t backward = steps < 0 ? -steps : 0;
		int32_t forward = steps > 0 ? steps : 0;

		for ( int32_t i = 0; i < backward; i++ )
			undo ( false );

		for ( int32_t i = 0; i < forward; i++ )
			undo ( true );

		open_history ( );

	}

	void app::draw_history ( ) {

		settle ( history_show, history_open ? 1.0f : 0.0f );

		float show = math::saturate ( history_show );

		if ( show <= 0.004f )
			return;

		render::canvas& c = canvas;
		uint64_t blocker = make_id ( "history_blocker" );
		widget& block = state ( blocker );

		if ( interact ( blocker, math::rect ( 0.0f, 0.0f, w, h ), block ) && history_open )
			history_open = false;

		c.fill ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, ink ( tone::SHADOW ).fade ( 0.3f * show ) );

		float row_h = metric::list_row + 4.0f;
		int32_t shown = base::min ( history_rows.count ( ), 18 );
		math::rect box ( w * 0.5f - 300.0f, title_area.bottom ( ) + 50.0f, 600.0f, 70.0f + static_cast < float > ( shown ) * row_h );

		c.push ( box.center ( ), 1.0f, show );
		c.shadow ( box, metric::radius, 14.0f, ink ( tone::SHADOW ).fade ( 0.7f ), math::vec2 ( 0.0f, 4.0f ) );
		c.outline ( box, metric::radius, ink ( tone::RAISED ), 1.0f, ink ( tone::LINE_STRONG ) );

		uint64_t surface = make_id ( "history_surface" );
		widget& surface_state = state ( surface );

		interact ( surface, box, surface_state );
		c.text ( L"Undo history", render::font::UI_STRONG, math::vec2 ( box.x + 20.0f, box.y + 24.0f ), ink ( tone::INK ) );
		c.text ( L"Click a step to go back or forward to it.  Up, Down and Enter work too.", render::font::SMALL, math::vec2 ( box.x + 20.0f, box.y + 44.0f ), ink ( tone::INK_FAINT ) );

		int32_t first = base::clamp ( history_pick - shown / 2, 0, base::max ( history_rows.count ( ) - shown, 0 ) );
		float top = box.y + 60.0f;

		for ( int32_t k = 0; k < shown; k++ ) {

			int32_t i = first + k;
			math::rect row ( box.x + 8.0f, top + static_cast < float > ( k ) * row_h, box.w - 16.0f, row_h );
			uint64_t id = make_id ( "history_row", i );
			widget& item = state ( id );
			int32_t steps = history_steps [ static_cast < size_t > ( i ) ];
			bool now_row = steps == 0;
			bool future = steps > 0;

			if ( interact ( id, row, item ) && history_open ) {

				history_jump ( i );

				break;

			}

			if ( item.hover > 0.5f )
				history_pick = i;

			if ( i == history_pick )
				c.fill ( row, 2.0f, ink ( tone::SELECT ) );

			if ( now_row )
				c.fill ( math::rect ( row.x, row.y + 4.0f, 3.0f, row.h - 8.0f ), 1.5f, ink ( tone::ACCENT ) );

			base::writer step;

			if ( now_row )
				step.add ( L"current" );
			else
				step.add ( future ? L"redo " : L"undo " ).number ( future ? steps : -steps );

			c.text ( history_rows [ static_cast < size_t > ( i ) ].get ( ), now_row ? render::font::UI_STRONG : render::font::UI, math::vec2 ( row.x + 14.0f, row.center ( ).y ), ink ( future ? tone::INK_FAINT : tone::INK ), render::align::LEFT, row.w - 110.0f );
			c.text ( step.text ( ), render::font::SMALL, math::vec2 ( row.right ( ) - 10.0f, row.center ( ).y ), ink ( tone::INK_FAINT ), render::align::RIGHT );

		}

		if ( history_rows.count ( ) <= 1 )
			c.text ( L"Nothing to undo yet.", render::font::SMALL, math::vec2 ( box.x + 20.0f, top + row_h * 1.5f ), ink ( tone::INK_FAINT ) );

		c.pop ( );

	}

}
