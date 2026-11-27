#include "ui/shell/frame/status/status.h"
#include "ui/core/app/app.h"
#include "ui/style/icons/icons.h"
#include <windows.h>

void ui::app::draw_status ( ) {

	render::canvas& c = canvas;
	math::rect area = status_area;
	float cy = area.center ( ).y;

	c.fill ( area, 0.0f, ink ( tone::TITLE ) );
	c.fill ( math::rect ( 0.0f, area.y, w, 1.0f ), 0.0f, ink ( tone::LINE ) );

	bool has = file.loaded ( );
	bool working = has && !analysis_applied;
	render::color dot = working ? ink ( tone::WARNING ) : ( has ? ink ( tone::SUCCESS ) : ink ( tone::INK_FAINT ) );

	c.circle ( math::vec2 ( 14.0f, cy ), 3.5f, dot );

	float x = 26.0f;

	x += c.text ( working ? L"Analyzing" : ( has ? L"Ready" : L"No file open" ), render::font::SMALL, math::vec2 ( x, cy ), ink ( tone::INK_SOFT ) ) + 16.0f;

	if ( has ) {

		base::writer state_text;

		if ( working ) {

			long done = __atomic_load_n ( &analysis_state.done, __ATOMIC_RELAXED );
			long total = __atomic_load_n ( &analysis_state.total, __ATOMIC_RELAXED );

			state_text.add ( L"Finding functions" );

			if ( total > 0 )
				state_text.add ( L" \x00B7 " ).grouped ( static_cast < uint64_t > ( done ) ).add ( L" of " ).grouped ( static_cast < uint64_t > ( total ) );

			animating = true;

		} else if ( db && !db->maps.empty ( ) ) {

			state_text.grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L" functions \x00B7 " ).grouped ( db->decoded ).add ( L" instructions \x00B7 analyzed in " ).fixed ( db->seconds * 1000.0, 1 ).add ( L" ms" );

			int32_t owner = db->function_at ( now->cursor_address );

			if ( owner >= 0 ) {

				wchar_t name [ 160 ];

				function_name ( owner, name, 160 );
				state_text.add ( L"   \x00B7   in " ).add ( name ).add ( L"+" ).hex ( now->cursor_address - db->functions [ static_cast < size_t > ( owner ) ].start );

			}

		} else
			state_text.add ( L"No x86 code to analyze" );

		c.fill ( math::rect ( x - 8.0f, area.y + 6.0f, 1.0f, area.h - 12.0f ), 0.0f, ink ( tone::LINE ) );
		x += c.text ( state_text.text ( ), render::font::SMALL, math::vec2 ( x, cy ), ink ( tone::INK_FAINT ), render::align::LEFT, w * 0.5f ) + 16.0f;

		base::writer where;
		uint64_t offset = 0;
		int32_t owner = file.section_at ( now->cursor_address );

		if ( owner >= 0 )
			where.add ( file.sections [ static_cast < size_t > ( owner ) ].name ).add ( L":" );

		where.hex ( now->cursor_address, file.wide ( ) ? 16 : 8 );

		if ( file.type != loader::format::RAW )
			where.add ( L"   RVA " ).hex ( now->cursor_address - file.base );

		if ( file.offset_of ( now->cursor_address, offset ) )
			where.add ( L"   File " ).hex ( offset );

		float right = w - 12.0f;
		base::writer info;

		info.add ( file.format_name ( ) ).add ( L" \x00B7 " ).add ( file.machine_name ( ) ).add ( L" \x00B7 " ).grouped ( file.size ).add ( L" bytes" );
		right -= c.text ( info.text ( ), render::font::SMALL, math::vec2 ( right, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT ) + 18.0f;
		c.fill ( math::rect ( right + 8.0f, area.y + 6.0f, 1.0f, area.h - 12.0f ), 0.0f, ink ( tone::LINE ) );
		c.text ( where.text ( ), render::font::MONO_SMALL, math::vec2 ( right, cy ), ink ( tone::INK_SOFT ), render::align::RIGHT );

	} else {

		c.text ( light ? L"Light theme" : L"Dark theme", render::font::SMALL, math::vec2 ( w - 12.0f, cy ), ink ( tone::INK_FAINT ), render::align::RIGHT );

	}

}