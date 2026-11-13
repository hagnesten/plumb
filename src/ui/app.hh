#pragma once

#include "debugger/debugger.hh"
#include "render/render.hh"
#include "loader/binary.hh"
#include "analysis/database.hh"
#include "analysis/signatures.hh"
#include "decompiler/decompiler.hh"
#include "ui/metrics.hh"

namespace ui {

	enum class hit : int32_t {

		NONE,
		CLIENT,
		CAPTION,
		MAXIMIZE,

	};

	enum class request : int32_t {

		NONE,
		MINIMIZE,
		MAXIMIZE,
		CLOSE,
		OPEN,

	};

	enum class pointer : int32_t {

		ARROW,
		TEXT,
		HAND,
		SIZE_WE,
		SIZE_NS,

	};

	enum class view : int32_t {

		LISTING,
		GRAPH,
		PSEUDOCODE,
		HEX,
		HEADERS,
		CALLS,
		COUNT,

	};

	enum class panel : int32_t {

		FUNCTIONS,
		IMPORTS,
		EXPORTS,
		STRINGS,
		SECTIONS,
		CLASSES,
		STRUCTS,
		REFERENCES,
		SEARCH,
		INSPECTOR,
		COUNT,

	};

	enum class tone : int32_t {

		BASE,
		PANEL,
		RAISED,
		SUNKEN,
		TITLE,
		LINE,
		LINE_STRONG,
		INK,
		INK_SOFT,
		INK_FAINT,
		ACCENT,
		ACCENT_SOFT,
		ACCENT_INK,
		SELECT,
		SELECT_LINE,
		HOVER,
		PRESS,
		SHADOW,
		ADDRESS,
		BYTES,
		LABEL,
		MNEMONIC,
		NUMBER,
		STRING,
		NAME,
		COMMENT,
		CODE_BAND,
		DATA_BAND,
		CONST_BAND,
		EMPTY_BAND,
		HEADER_BAND,
		DANGER,
		SUCCESS,
		WARNING,
		KEYWORD,
		CONTROL,
		TYPE,
		FUNCTION,
		LOCAL,
		PARAMETER,
		GLOBAL,
		OPERATOR,
		REGISTER,
		IMPORT,
		GUIDE,
		COUNT,

	};

	enum class command : int32_t {

		OPEN,
		SAVE_PROJECT,
		UNDO,
		REDO,
		CLOSE,
		EXIT,
		COPY,
		GO_ADDRESS,
		GO_ENTRY,
		BACK,
		FORWARD,
		NEXT_SECTION,
		PREVIOUS_SECTION,
		FIND,
		PALETTE,
		VIEW_LISTING,
		VIEW_PSEUDOCODE,
		VIEW_HEX,
		VIEW_HEADERS,
		SWAP_VIEW,
		TOGGLE_GRAPH,
		SPLIT,
		CLOSE_TAB,
		NEXT_TAB,
		SHOW_FUNCTIONS,
		SHOW_IMPORTS,
		SHOW_EXPORTS,
		SHOW_STRINGS,
		SHOW_SECTIONS,
		SHOW_CLASSES,
		SHOW_STRUCTS,
		SHOW_REFERENCES,
		SHOW_SEARCH,
		TOGGLE_BROWSER,
		TOGGLE_INSPECTOR,
		TOGGLE_OUTPUT,
		RESET_LAYOUT,
		THEME,
		FOLLOW,
		RENAME,
		COMMENT,
		SET_TYPE,
		STRUCT_FROM_VAR,
		BOOKMARK,
		NUMBER_RADIX,
		NUMBER_CHAR,
		NUMBER_BINARY,
		NUMBER_FLOAT,
		NUMBER_RESET,
		MAKE_SIGNATURE,
		FIND_SIGNATURE,
		COPY_FUNCTION,
		SEARCH_ALL,
		ASSEMBLE,
		PATCH_BYTES,
		REVERT_PATCH,
		EXPORT_PATCHED,
		XOR_DECODE,
		VIEW_CALLS,
		EXPORT_CLASSES,
		TOGGLE_BYTES,
		TOGGLE_AUTOSAVE,
		TOGGLE_SPACING,
		TOGGLE_TERNARY,
		DEBUG_RUN,
		DEBUG_STEP_INTO,
		DEBUG_STEP_OVER,
		DEBUG_PAUSE,
		DEBUG_STOP,
		DEBUG_BREAKPOINT,
		SHORTCUTS,
		ABOUT,
		COUNT,

	};

	enum class dock_side : int32_t {

		LEFT,
		RIGHT,
		BOTTOM,

	};

	enum class line_kind : int32_t {

		BANNER,
		BLANK,
		LABEL,
		CODE,
		DATA,
		STRING,
		POINTER,
		EMPTY,
		FUNCTION,
		END,
		ALIGN,
		TABLE,
		CHUNK,
		VTABLE,

	};

	enum class log_kind : int32_t {

		INFO,
		GOOD,
		WARN,
		FAIL,

	};

	struct widget {

		uint64_t id = 0;
		float hover = 0.0f;
		float press = 0.0f;
		float value = 0.0f;
		uint32_t seen = 0;

	};

	struct hit_rect {

		uint64_t id;
		math::rect area;
		hit kind;

	};

	struct field {

		wchar_t text [ 256 ] = { };
		int32_t length = 0;
		int32_t caret = 0;
		int32_t anchor = 0;
		float scroll = 0.0f;
		uint32_t revision = 0;

	};

	struct scroller {

		float offset = 0.0f;
		float target = 0.0f;
		float limit = 0.0f;
		float shown = 0.0f;

	};

	struct line_item {

		uint64_t first_row = 0;
		uint64_t address = 0;
		uint64_t size = 0;
		uint32_t rows = 1;
		int32_t ref = -1;
		line_kind kind = line_kind::BLANK;
		uint8_t stride = 8;
		uint8_t section = 0;
		bool function = false;

	};

	struct log_line {

		log_kind kind = log_kind::INFO;
		base::text text;
		double time = 0.0;

	};

	struct palette_entry {

		command action = command::COUNT;
		int32_t score = 0;
		int32_t function = -1;

	};

	struct view_state {

		scroller listing_scroll;
		uint64_t cursor_row = 0;
		float cursor_y = 0.0f;
		uint64_t cursor_address = 0;
		bool cursor_ready = false;
		uint64_t select_anchor = 0;
		int32_t caret_col = 0;
		int32_t anchor_col = 0;
		bool selecting = false;
		bool ranged = false;
		uint64_t row_for = ~0ull;
		uint32_t row_build = 0;
		int32_t code_anchor = 0;
		int32_t code_col = 0;
		int32_t code_anchor_col = 0;
		bool code_selecting = false;
		bool code_ranged = false;
		int32_t code_token = -1;
		uint64_t code_seen = 0;
		uint32_t code_seen_revision = 0;
		scroller code_side;
		scroller hex_scroll;
		uint64_t hex_cursor = 0;
		uint64_t hex_anchor = 0;
		bool hex_selecting = false;
		scroller headers_scroll;
		scroller code_scroll;
		int32_t code_line = 0;
		float code_y = 0.0f;
		bool code_ready = false;
		uint64_t function = 0;
		float graph_x = 0.0f;
		float graph_y = 0.0f;
		float graph_zoom = 1.0f;
		float graph_tx = 0.0f;
		float graph_ty = 0.0f;
		float graph_tzoom = 1.0f;
		uint64_t graph_function = 0;
		int32_t graph_block = 0;
		bool graph_ready = false;
		bool dragging = false;
		math::vec2 drag_from;
		math::vec2 drag_origin;
		base::list < uint64_t > back_stack;
		base::list < uint64_t > forward_stack;

	};

	struct document {

		bool used = false;
		view kind = view::LISTING;
		uint32_t serial = 0;
		view_state state;

	};

	struct pane {

		int32_t tabs [ 16 ] = { };
		int32_t count = 0;
		int32_t active = 0;
		math::rect area;
		math::rect strip;
		math::rect body;

	};

	struct user_name {

		uint64_t address = 0;
		base::text text;

	};

	struct local_entry {

		uint64_t function = 0;
		base::text key;
		base::text name;
		base::text type;

	};

	struct user_field {

		int32_t offset = 0;
		int32_t size = 0;
		base::text name;
		base::text type;

	};

	struct user_struct {

		base::text name;
		base::list < user_field > fields;

	};

	struct search_hit {

		uint64_t address = 0;
		uint32_t length = 0;
		base::text note;

	};

	struct patched_byte {

		uint64_t offset = 0;
		uint8_t original = 0;
		uint8_t value = 0;

	};

	struct stack_text {

		uint64_t address = 0;
		base::text text;

	};

	struct call_node {

		uint64_t address = 0;
		int32_t column = 0;
		int32_t parent = -1;
		math::rect box;

	};

	struct token_run {

		int32_t start = 0;
		int32_t length = 0;
		tone color = tone::INK;
		uint64_t target = 0;

	};

	struct row_line {

		wchar_t text [ 400 ];
		int32_t length = 0;
		token_run runs [ 64 ];
		uint8_t strong [ 64 ];
		int32_t count = 0;
		uint64_t address = 0;
		line_kind kind = line_kind::BLANK;
		bool decor = false;
		bool instruction = false;

		void clear ( ) {

			length = 0;
			count = 0;
			text [ 0 ] = 0;
			address = 0;
			kind = line_kind::BLANK;
			decor = false;
			instruction = false;

		}

		void pad ( int32_t column ) {

			while ( length < column && length < 398 )
				text [ length++ ] = L' ';

			text [ length ] = 0;

		}

		void put ( const wchar_t* value, tone color, uint64_t target = 0, bool bold = false ) {

			int32_t start = length;

			for ( const wchar_t* at = value; *at && length < 398; at++ )
				text [ length++ ] = *at;

			text [ length ] = 0;

			if ( length > start && count < 64 ) {

				token_run& run = runs [ count ];

				run.start = start;
				run.length = length - start;
				run.color = color;
				run.target = target;
				strong [ count ] = bold ? 1 : 0;
				count++;

			}

		}

		void put_at ( int32_t column, const wchar_t* value, tone color, uint64_t target = 0, bool bold = false ) {

			pad ( column );
			put ( value, color, target, bold );

		}

		int32_t run_at ( int32_t column ) const {

			for ( int32_t i = 0; i < count; i++ ) {

				if ( column >= runs [ i ].start && column < runs [ i ].start + runs [ i ].length )
					return i;

			}

			return -1;

		}

	};

	struct row_columns {

		int32_t digits = 16;
		int32_t code = 18;
		int32_t mnemonic = 24;
		int32_t operands = 32;
		int32_t note = 76;
		bool bytes = false;

	};

	uint64_t make_id ( const char* name, int64_t index = 0 );

	bool word_bounds ( const wchar_t* text, int32_t length, int32_t column, int32_t& from, int32_t& to );

	int32_t step_caret ( const wchar_t* text, int32_t length, int32_t column, bool right, bool word );

	class app {

	public:

		bool setup ( render::gpu* device, void* window_handle );

		void rebuild_gpu ( render::gpu* device );

		void shutdown ( );

		void resize ( int32_t width_px, int32_t height_px, float dpi_scale );

		void frame ( double seconds );

		bool busy ( ) const;

		double wake_after ( ) const;

		void mouse_move ( float x, float y );

		void mouse_button ( int32_t button, bool down );

		void mouse_leave ( );

		void mouse_wheel ( float steps, bool horizontal );

		void key ( uint32_t code, bool control, bool shift, bool alt );

		void character ( wchar_t value );

		hit hit_test ( float x, float y ) const;

		request take_request ( );

		pointer cursor ( ) const;

		void set_maximized ( bool value );

		void set_focus ( bool value );

		bool open_file ( const wchar_t* path );

		void close_file ( );

		void run ( command action );

		void set_theme ( bool light );

		bool can_close ( );

		void load_settings ( );

		void save_settings ( );

		void log ( log_kind kind, const wchar_t* text );

		void analysis_finished ( );

		bool symbol_name ( uint64_t address, bool code, wchar_t* out, int32_t room ) const;

		bool number_text ( uint64_t at, uint64_t raw, uint8_t size, wchar_t* out, int32_t room ) const;

		const local_entry* local_for ( uint64_t function, const wchar_t* key ) const;

		const wchar_t* prototype_for ( uint64_t function ) const;

		void compose_row ( uint64_t row, const row_columns& cols, row_line& out ) const;

		row_columns listing_columns ( float width, float adv ) const;

		bool listing_selection ( base::list < wchar_t >& out ) const;

		bool code_selection ( base::list < wchar_t >& out ) const;

		uint64_t reference_subject ( ) const;

		bool is_breakpoint ( uint64_t address ) const;

		void sync_breakpoints ( );

		void poll_debugger ( );

		void sync_panes ( uint64_t address );

		void serialize_project ( base::list < char >& out ) const;

		void project_beside ( base::writer& out ) const;

		bool project_ours ( const wchar_t* path ) const;

		void apply_project ( const char* data );

		void remember_undo ( );

		void undo ( bool forward );

		void changed ( );

		bool confirm_discard ( );

		int32_t struct_named ( const wchar_t* name ) const;

		int32_t struct_size ( int32_t index ) const;

		bool add_field ( int32_t index, int32_t offset, const wchar_t* name, const wchar_t* type );

		const wchar_t* comment_at ( uint64_t address ) const;

		bool code_field ( int32_t& index, int32_t& offset ) const;

		bool known_type ( const decompiler::c_type& shape, base::writer& problem ) const;

		void open_struct_prompt ( int32_t mode, int32_t index, int32_t offset, const wchar_t* fill );

		bool apply_struct_prompt ( const wchar_t* text );

		void function_name ( int32_t index, wchar_t* out, int32_t room ) const;

		int32_t string_at ( uint64_t address ) const;

		bool indirect_note ( const x86::instruction& ins, base::writer& out ) const;

		bool parse_address ( const wchar_t* text, uint64_t& result ) const;

		int32_t patch_index ( uint64_t offset ) const;

		bool is_patched ( uint64_t address, int32_t length ) const;

		void set_byte ( uint64_t offset, uint8_t value );

		bool patch_bytes ( uint64_t address, const uint8_t* bytes, int32_t count );

		bool patch_text ( uint64_t address, const wchar_t* text );

		void revert_patch ( uint64_t address );

		void reanalyze ( );

		void export_patched ( );

		bool assemble_at ( uint64_t address, const wchar_t* text );

		uint64_t patch_target ( ) const;

		const wchar_t* stack_string ( uint64_t address ) const;

		void xor_decode ( uint64_t address, const wchar_t* spec );

		void search_everything ( const wchar_t* query );

		void export_classes ( );

		void build_calls ( uint64_t function );


		render::gpu* gpu = nullptr;
		void* window = nullptr;
		render::canvas canvas;
		loader::binary file;
		analysis::database* db = nullptr;
		analysis::progress analysis_state;
		void* analysis_thread = nullptr;
		volatile long analysis_done = 0;
		bool analysis_applied = false;
		bool analysis_sync = false;
		double analysis_clock = 0.0;
		int32_t width = 0;
		int32_t height = 0;
		float dpi = 1.0f;
		float w = 0.0f;
		float h = 0.0f;
		double clock = 0.0;
		double last = -1.0;
		float dt = 0.0f;
		bool fixed_step = false;
		bool persist = true;
		bool settings_frozen = false;
		bool maximized = false;
		bool focused = true;
		bool animating = false;
		mutable int32_t pending = 3;
		bool light = false;
		float theme_mix = 0.0f;
		render::color tones [ static_cast < int32_t > ( tone::COUNT ) ];
		int32_t placement [ 4 ] = { };
		bool placement_valid = false;
		bool placement_maximized = false;

		math::vec2 mouse = math::vec2 ( -1000.0f, -1000.0f );
		bool mouse_inside = false;
		bool mouse_down = false;
		bool press_event = false;
		bool release_event = false;
		bool right_event = false;
		bool double_event = false;
		bool middle_event = false;
		double last_press = -10.0;
		bool double_event_last = false;
		math::vec2 press_at;
		float wheel_steps = 0.0f;
		float wheel_side = 0.0f;
		uint64_t hot = 0;
		uint64_t active_id = 0;
		uint64_t clicked_id = 0;
		uint64_t double_id = 0;
		uint64_t right_id = 0;
		uint64_t middle_id = 0;
		uint64_t scroll_hot = 0;
		base::list < hit_rect > hits;
		base::list < hit_rect > next_hits;
		base::list < hit_rect > scrolls;
		base::list < hit_rect > next_scrolls;
		base::list < uint32_t > keys;
		base::list < wchar_t > typed;
		bool key_control = false;
		bool key_shift = false;
		bool key_alt = false;
		request pending_request = request::NONE;
		pointer wanted_cursor = pointer::ARROW;
		widget* widgets = nullptr;
		static constexpr int32_t widget_room = 4096;
		uint32_t frame_index = 1;
		uint64_t focus_id = 0;
		field* focused_field = nullptr;
		bool focus_drawn = false;
		bool field_submit = false;
		double caret_time = 0.0;
		const wchar_t* tip_text = nullptr;
		uint64_t tip_id = 0;
		float tip_time = 0.0f;
		float tip_show = 0.0f;
		math::rect tip_anchor;
		uint64_t tip_candidate = 0;
		const wchar_t* tip_candidate_text = nullptr;
		math::rect tip_candidate_anchor;

		math::rect title_area;
		math::rect status_area;
		math::rect rail_area;
		math::rect browser_area;
		math::rect output_area;
		math::rect center_area;
		math::rect view_area;

		dock_side docks [ 2 ] = { dock_side::LEFT, dock_side::BOTTOM };
		float dock_size [ 3 ] = { 288.0f, 300.0f, 190.0f };
		math::rect dock_areas [ 3 ];
		float dock_shows [ 3 ] = { };
		int32_t dock_drag = -1;
		int32_t dock_target = -1;
		bool dock_moving = false;
		math::vec2 dock_from;
		float browser_fit = 288.0f;
		bool browser_open = true;
		bool output_open = true;
		float browser_show = 1.0f;
		float output_show = 1.0f;
		int32_t dragging_split = 0;
		float split_grab = 0.0f;
		float pane_split = 0.5f;

		panel browsing = panel::FUNCTIONS;
		view showing = view::LISTING;

		document documents [ 16 ];
		pane panes [ 2 ];
		int32_t pane_count = 1;
		int32_t focused_pane = 0;
		uint32_t next_serial = 1;
		view_state spare_state;
		view_state* now = &spare_state;
		uint32_t drawing_serial = 0;

		int32_t menu_open = -1;
		float menu_show = 0.0f;
		math::rect menu_rect;
		bool menu_keyboard = false;
		int32_t menu_focus = -1;

		bool palette_open = false;
		float palette_show = 0.0f;
		bool palette_address = false;
		field palette_field;
		uint32_t palette_seen = 0xFFFFFFFFu;
		base::list < palette_entry > palette_items;
		int32_t palette_pick = 0;
		float palette_pick_y = 0.0f;

		bool about_open = false;
		float about_show = 0.0f;
		bool keys_open = false;
		float keys_show = 0.0f;

		bool prompt_open = false;
		float prompt_show = 0.0f;
		command prompt_action = command::COUNT;
		uint64_t prompt_address = 0;
		int32_t prompt_mode = 0;
		base::text prompt_key;
		uint64_t prompt_function = 0;
		field prompt_field;

		bool signature_open = false;
		float signature_show = 0.0f;
		analysis::signature signature_result;
		bool signature_ok = false;
		double signature_seconds = 0.0;
		int32_t signature_copied = -1;
		double signature_copied_at = -10.0;

		field filters [ static_cast < int32_t > ( panel::COUNT ) ];
		uint32_t filter_seen [ static_cast < int32_t > ( panel::COUNT ) ] = { };
		base::list < int32_t > shown [ static_cast < int32_t > ( panel::COUNT ) ];
		scroller browser_scroll [ static_cast < int32_t > ( panel::COUNT ) ];
		int32_t browser_pick [ static_cast < int32_t > ( panel::COUNT ) ] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
		int8_t sort_by [ static_cast < int32_t > ( panel::COUNT ) ] = { };
		bool sort_down [ static_cast < int32_t > ( panel::COUNT ) ] = { };
		int32_t expanded_struct = -1;
		int32_t prompt_struct = -1;
		int32_t prompt_offset = 0;
		bool prompt_from_var = false;
		base::text prompt_shown;
		int32_t expanded_class = -1;
		uint64_t references_for = ~0ull;
		uint32_t references_revision = 0;
		base::list < analysis::xref > reference_list;
		base::list < search_hit > search_hits;
		base::text search_query;
		double search_seconds = 0.0;
		bool search_truncated = false;

		base::list < line_item > items;
		base::list < base::text > names;
		uint64_t total_rows = 0;
		row_columns listing_cols;
		base::list < row_line > row_cache;
		bool show_bytes = false;
		base::text project_file;
		bool autosave = false;
		bool dirty = false;
		base::list < uint64_t > breakpoints;
		debugger::snapshot debug_state;
		uint64_t debug_pc = 0;
		bool c_spaced = true;
		bool c_no_ternary = false;
		uint64_t reference_target = ~0ull;
		double blink_from = 0.0;
		bool caret_live = false;
		uint32_t listing_build = 1;
		base::list < uint64_t > string_addresses;
		base::list < int32_t > string_order;
		base::list < user_name > renamed;
		base::list < user_name > comments;
		base::list < user_name > bookmarks;
		base::list < user_name > number_forms;
		base::list < user_name > prototypes;
		base::list < local_entry > locals;
		base::list < user_struct > structs;
		base::list < base::list < char > > undo_stack;
		base::list < base::list < char > > redo_stack;
		uint32_t names_revision = 0;

		decompiler::cache code_cache;
		base::list < patched_byte > patches;
		mutable base::list < uint64_t > stack_seen;
		mutable base::list < stack_text > stack_texts;
		analysis::database* building = nullptr;
		bool rebuild_pending = false;
		base::list < call_node > call_nodes;
		uint64_t calls_for = ~0ull;
		uint32_t calls_revision = 0;
		const analysis::database* calls_db = nullptr;
		int32_t call_totals [ 2 ] = { };

		scroller inspector_scroll;
		scroller output_scroll;

		base::list < log_line > output;
		field command_field;
		base::list < base::text > recent;
		base::text load_problem;
		double load_time = -10.0;
		float welcome_show = 1.0f;
		float band_hover = -1.0f;

	private:

		widget& state ( uint64_t id );

		bool over ( math::rect area ) const;

		bool interact ( uint64_t id, math::rect area, widget& item, hit kind = hit::CLIENT );

		void scroll_region ( uint64_t id, math::rect area );

		float take_wheel ( uint64_t id );

		void tip ( uint64_t id, const wchar_t* text, math::rect anchor );

		void settle ( float& value, float target );

		void glide ( scroller& item, float limit, float unit = 1.0f );

		bool begin_frame ( double seconds );

		void begin_input ( );

		void end_input ( );

		void handle_keys ( );

		bool edit_keys ( field& value, uint32_t code );

		void update_theme ( );

		render::color ink ( tone value ) const;

		void layout ( );

		bool enabled ( command action ) const;

		bool checked ( command action ) const;

		void start_analysis ( );

		void wait_analysis ( );

		void build_listing ( );

		void build_code_items ( int32_t section, uint64_t& rows );

		void push_item ( line_kind kind, int32_t section, uint64_t address, uint64_t size, uint32_t rows, uint8_t stride, int32_t ref, uint64_t& total );

		uint64_t row_of ( uint64_t address ) const;

		int32_t item_at ( uint64_t row ) const;

		uint64_t address_at_row ( uint64_t row ) const;

		void go ( uint64_t address, bool remember );

		void go_offset ( uint64_t offset );

		void set_cursor_row ( uint64_t row, bool center );

		void refresh_filter ( panel which );

		void sort_rows ( panel which );

		void refresh_references ( );

		void run_command_line ( );

		void copy_text ( const wchar_t* text );

		void remember_recent ( const wchar_t* path );

		void forget_recent ( int32_t index );

		void string_label ( int32_t index, wchar_t* out, int32_t room ) const;

		const wchar_t* user_text ( const base::list < user_name >& list, uint64_t address ) const;

		void set_user_text ( base::list < user_name >& list, uint64_t address, const wchar_t* text );

		void load_project ( );

		void save_project ( );

		int32_t open_document ( view kind, int32_t target_pane, uint64_t address );

		void close_document ( int32_t target_pane, int32_t tab );

		void focus_document ( int32_t target_pane, int32_t tab );

		int32_t current_document ( ) const;

		int32_t pane_document ( int32_t index ) const;

		void select_state ( int32_t document_index );

		void open_pseudocode ( uint64_t address, bool other_pane );

		const decompiler::result* pseudocode_for ( uint64_t function );

		void make_signature ( uint64_t address );

		void find_signature ( const wchar_t* text );

		void open_prompt ( command action, uint64_t address );

		void open_code_prompt ( command action, int32_t mode, const wchar_t* key, const wchar_t* fill );

		bool code_variable ( wchar_t* key, wchar_t* shown ) const;

		void set_local ( uint64_t function, const wchar_t* key, const wchar_t* name, const wchar_t* type );

		void apply_prompt ( );

		void draw_brand ( math::vec2 center, float height );

		void draw_title ( );

		void draw_menu_bar ( math::rect area );

		void draw_menu ( );


		void draw_status ( );

		void draw_rail ( );

		void draw_browser ( );

		void draw_browser_rows ( panel which, math::rect area );

		void draw_panes ( );

		void draw_tabs ( int32_t index );

		void draw_document ( int32_t index );

		void draw_listing ( math::rect area );

		void draw_listing_map ( math::rect area );

		void draw_graph ( math::rect area );

		void draw_pseudocode ( math::rect area );

		void draw_hex ( math::rect area );

		void draw_headers ( math::rect area );

		void draw_calls ( math::rect area );

		void draw_inspector ( math::rect area, float inner );

		void draw_output ( );

		void draw_welcome ( math::rect area );

		void draw_palette ( );

		void draw_dialogs ( );

		void draw_prompt ( );

		void draw_signature ( );

		void draw_tip ( );

		void draw_scrollbar ( uint64_t id, math::rect track, scroller& item, float content, float page );

		void dock_handles ( );

		void draw_docks ( );

		void reset_layout ( );

		void draw_side_scrollbar ( uint64_t id, math::rect track, scroller& item, float content, float page );

		bool splitter ( uint64_t id, math::rect area, bool vertical );

		bool button ( uint64_t id, math::rect area, const wchar_t* label, uint32_t glyph, bool primary );

		bool icon_button ( uint64_t id, math::vec2 center, float size, uint32_t glyph, float glyph_size, const wchar_t* text, hit kind = hit::CLIENT );

		bool text_field ( uint64_t id, field& value, math::rect area, const wchar_t* placeholder, uint32_t glyph, render::font role );

		void focus_field ( uint64_t id, field& value, bool select );

		void section_heading ( math::rect area, const wchar_t* text, const wchar_t* detail );

		void key_cap ( math::vec2 at, const wchar_t* keys, render::color tint, bool right );

	};

	const wchar_t* command_label ( command action );

	const wchar_t* command_keys ( command action );

}