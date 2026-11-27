#include "ui/tools/mcp/mcp.h"
#include "ui/core/app/app.h"
#include "ui/listing/tokens/tokens.h"
#include "base/json/json.h"
#include "base/files/files.h"
#include "base/strings/strings.h"
#include <windows.h>

const ui::mcp_tool ui::mcp_tools [ ] = {
	{ "open_binary", "Open a Windows executable or DLL (or a .plumb project) and analyze it. Everything else works on the open file.", "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Full path to the file\"}},\"required\":[\"path\"]}" },
	{ "binary_info", "Summary of the open file: format, machine, image base, entry point, sections, symbol source and analysis counts.", "{\"type\":\"object\",\"properties\":{}}" },
	{ "list_functions", "List functions as address, size, callers and name. Filter matches part of the name.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"},\"offset\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\",\"description\":\"Default 200\"}}}" },
	{ "disassemble", "Disassemble a whole function (when given its start or name) or a number of instructions from any address. Addresses are hex like 140001000, names, or +RVA.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\",\"description\":\"Instructions to show; omit for the whole function\"}},\"required\":[\"address\"]}" },
	{ "decompile", "Decompile the function containing an address to C, using the names, types and prototypes set so far.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\",\"description\":\"Address or name\"}},\"required\":[\"function\"]}" },
	{ "xrefs_to", "List every reference to an address or name: the referencing function, offset and kind (call, jump, read, write, address).", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"}},\"required\":[\"address\"]}" },
	{ "calls_from", "List the functions and imports a function calls.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"}},\"required\":[\"function\"]}" },
	{ "list_strings", "List strings found in the file with their addresses. Filter matches part of the text.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"},\"offset\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\",\"description\":\"Default 200\"}}}" },
	{ "list_imports", "List imported functions by module with their slot addresses.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"}}}" },
	{ "list_exports", "List exported functions with addresses or forwarders.", "{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"}}}" },
	{ "search_code", "Search the text of every instruction. #0x40 finds immediates and displacements, c:text searches the decompiled C.", "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}" },
	{ "query_functions", "Find functions by conditions joined with and: calls X, calledby X, refs text, name X, size > N, callers > N, blocks > N, instructions > N, thunk, leaf, noreturn, exported, entry, named, noted, typed. Prefix not to negate.", "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"For example: calls VirtualAlloc and size > 0x100 and not thunk\"}},\"required\":[\"query\"]}" },
	{ "read_bytes", "Read raw bytes at an address as hex and ASCII.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\",\"description\":\"Default 64, at most 4096\"}},\"required\":[\"address\"]}" },
	{ "rename", "Rename a function, label or global. An empty name resets it.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}},\"required\":[\"address\",\"name\"]}" },
	{ "set_comment", "Set the comment on an instruction or data address. Empty text removes it.", "{\"type\":\"object\",\"properties\":{\"address\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"address\",\"text\"]}" },
	{ "set_note", "Set the note describing what a function does. It shows in the function header, the Notes panel and reports.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"function\",\"text\"]}" },
	{ "set_prototype", "Set a function's C prototype, like int __fastcall parse(char* text, int length). The C view and callers use it.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"prototype\":{\"type\":\"string\"}},\"required\":[\"function\",\"prototype\"]}" },
	{ "compare_with", "Compare the open file with another build of it. Lists functions that changed, exist only here, or exist only in the other file.", "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Full path to the other build\"},\"show_same\":{\"type\":\"boolean\"}},\"required\":[\"path\"]}" },
	{ "emulate", "Run a function in Plumb's x86 emulator without starting the program. Reports the result, the imports it calls and any readable strings it writes, which recovers decrypted strings. Decoded strings become comments.", "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"registers\":{\"type\":\"string\",\"description\":\"Optional starting registers like rcx=0x40 rdx=5\"}},\"required\":[\"function\"]}" },
	{ "save_project", "Save names, comments, notes, types and patches to the .plumb project beside the binary.", "{\"type\":\"object\",\"properties\":{}}" },
	{ "run_script", "Run Plumb Script (.psc), a C-like scripting language with Plumb's API: functions(), name(a), set_name(a, n), refs_to(a), instruction(a), decompile(f), bytes(a, n), find_bytes(p) and more. Returns everything the script prints, or the error with its line.", "{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\",\"description\":\"Script text\"},\"path\":{\"type\":\"string\",\"description\":\"Or the full path of a .psc file\"}}}" }
};

const ui::mcp_bridge ui::mcp_bridges [ ] = {
	{ "function_info", "Everything about one function: bounds, size, instruction and block counts, callers, how it was found, its prototype and note.", "function!:Address or name", L"var i = function_info($function); print(\"name \" + function_name($function)); print(\"start \" + hex(i[0]) + \"  end \" + hex(i[1]) + \"  size \" + hex(i[2])); print(\"instructions \" + i[3] + \"  blocks \" + i[4] + \"  callers \" + i[5]); print(\"flags \" + join(function_flags($function), \" \")); var p = prototype($function); if (p != none && p != \"\") print(\"prototype \" + p); var n = note($function); if (n != none && n != \"\") print(\"note \" + n);" },
	{ "function_summary", "A compact digest of a function for reading at a glance: name, size, callers, every function and import it calls, and every string it uses.", "function!:Address or name", L"var i = function_info($function); print(function_name($function) + \"  \" + hex(i[0]) + \"  size \" + hex(i[2]) + \"  callers \" + i[5]); print(\"calls:\"); named(callees($function)); print(\"strings:\"); texts($function);" },
	{ "function_strings", "Every string a function references, with the instruction that uses it.", "function!:Address or name", L"texts($function);" },
	{ "function_cfg", "The function's control flow graph: basic blocks as [start, end, instructions, successors, returns] and edges as [from, to, kind] with kinds fall, taken, not_taken, jump and case.", "function!:Address or name", L"print(\"blocks:\"); rows(blocks($function)); print(\"edges:\"); rows(edges($function));" },
	{ "callers", "Functions that call a function, with their names.", "function!:Address or name", L"named(callers($function));" },
	{ "call_graph", "A call tree around a function, a few levels deep. Direction is callees (default) or callers.", "function!:Address or name|depth#:Levels, default 2, at most 6|direction:callees or callers", L"var d = $depth == none ? 2 : min($depth, 6); var f = function_at($function); tree(f, 0, d, \"\", [f], $direction == \"callers\");" },
	{ "decompile_many", "Decompile several functions in one call. Give addresses or names separated by commas or new lines.", "functions!:Addresses or names", L"var list = split(replace($functions, \"\\n\", \",\"), \",\"); for (var i = 0; i < len(list); i++) { var f = trim(list[i]); if (f == \"\") continue; print(\"// \" + f); print(decompile(f)); }" },
	{ "list_locals", "Every variable in a function's C as [key, name, type, local or parameter]. The key (v3, a1) never changes.", "function!:Address or name", L"rows(locals($function));" },
	{ "rename_local", "Rename a variable in a function's C. variable is its key (v3, a1) or current name. An empty name resets it.", "function!:Address or name|variable!:Key or current name|name!:New name", L"set_local_name($function, $variable, $name); rows(locals($function));" },
	{ "set_local_type", "Set a variable's type in a function's C, like uint8_t* or player_t*. An empty type goes back to the inferred one.", "function!:Address or name|variable!:Key or current name|type!:C type", L"set_local_type($function, $variable, $type); rows(locals($function));" },
	{ "list_structs", "Every user-defined struct.", "", L"rows(structs());" },
	{ "get_struct", "A struct as a C declaration with offsets.", "struct!:Struct name", L"print(struct_text($struct));" },
	{ "declare_struct", "Create a struct, or add fields to an existing one. Give one field per line as offset name type, like 0x8 health int32_t. Offset -1 appends. A name like buf[32] makes an array.", "name!:Struct name|fields!:Field lines", L"if (!struct_exists($name)) struct_create($name); var lines = split($fields, \"\\n\"); for (var i = 0; i < len(lines); i++) { var w = words(lines[i]); if (len(w) == 0) continue; if (len(w) < 3) { print(\"skipped: \" + lines[i]); continue; } var t = w[2]; for (var k = 3; k < len(w); k++) t = t + \" \" + w[k]; struct_add_field($name, int(w[0]), w[1], t); } print(struct_text($name));" },
	{ "edit_struct_field", "Rename or retype a struct field, given by offset or name.", "struct!:Struct name|field!:Offset or name|name:New name|type:New type", L"var f = $field; if (starts(f, \"0x\") || (f >= \"0\" && f <= \"9\")) f = int(f); if ($type != none && $type != \"\") struct_set_field_type($struct, f, $type); if ($name != none && $name != \"\") struct_rename_field($struct, f, $name); print(struct_text($struct));" },
	{ "delete_struct", "Delete a struct.", "struct!:Struct name", L"print(struct_delete($struct) ? \"Deleted.\" : \"No such struct.\");" },
	{ "list_classes", "Classes recovered from RTTI as [name, vtable, methods, locator, offset].", "", L"rows(classes());" },
	{ "class_info", "A class's base classes and the function in every vtable slot.", "class!:Class name or index", L"print(\"bases: \" + join(class_bases($class), \", \")); var m = class_methods($class); for (var i = 0; i < len(m); i++) print(\"slot \" + i + \"  \" + hex(m[i]) + \"  \" + name(m[i]));" },
	{ "list_globals", "Named addresses that are not functions: globals, labels and data named by hand or by scripts.", "", L"var n = names(); var shown = 0; for (var i = 0; i < len(n); i++) if (!is_function(n[i][0])) { print(hex(n[i][0]) + \"  \" + n[i][1]); shown++; } if (!shown) print(\"(none)\");" },
	{ "entry_points", "The entry point, exports and TLS callbacks.", "", L"print(hex(entry()) + \"  \" + name(entry()) + \"  entry\"); var e = exports(); for (var i = 0; i < len(e); i++) print(cell(e[i][1]) + \"  \" + e[i][0] + \"  export\"); var f = functions(); for (var i = 0; i < len(f); i++) if (contains(function_flags(f[i]), \"tls\")) print(hex(f[i]) + \"  \" + name(f[i]) + \"  tls\");" },
	{ "list_comments", "Every comment as [address, text].", "", L"rows(comments());" },
	{ "list_notes", "Every function note as [function, text].", "", L"rows(notes());" },
	{ "list_bookmarks", "Every bookmark as [address, label].", "", L"rows(bookmarks());" },
	{ "set_bookmark", "Bookmark an address, optionally with a label. An empty label removes it.", "address!:Address or name|label:Label", L"if ($label == \"\") del_bookmark($address); else if ($label == none) set_bookmark($address); else set_bookmark($address, $label); rows(bookmarks());" },
	{ "read_value", "Read a typed value: byte, word, dword, qword (default dword) or string.", "address!:Address or name|kind:byte, word, dword, qword or string", L"var k = $kind == none ? \"dword\" : $kind; if (k == \"byte\") print(hex(byte($address))); else if (k == \"word\") print(hex(word($address))); else if (k == \"qword\") print(hex(qword($address))); else if (k == \"string\") print(string($address)); else print(hex(dword($address)));" },
	{ "convert_number", "Show a number as decimal, hex, binary and the characters its bytes spell.", "value!:Number like 0x41424344 or 1094861636", L"var v = int($value); print(\"decimal  \" + format(\"%d\", v)); print(\"hex      \" + hex(v)); var b = \"\"; var x = v; if (x == 0) b = \"0\"; while (x != 0 && len(b) < 64) { b = ((x & 1) ? \"1\" : \"0\") + b; x = (x >> 1) & 0x7FFFFFFFFFFFFFFF; } print(\"binary   \" + b); var s = \"\"; var y = v; for (var i = 0; i < 8 && y != 0; i++) { var c = y & 0xFF; s = s + ((c >= 32 && c < 127) ? format(\"%c\", c) : \".\"); y = (y >> 8) & 0x00FFFFFFFFFFFFFF; } print(\"chars    \" + s);" },
	{ "patch_assemble", "Assemble an instruction over the code at an address, padding with nop, then show the result. Saved with the project; the file itself is never changed.", "address!:Address or name|instruction!:Like xor eax, eax", L"assemble($address, $instruction); wait(); print(disassemble($address, 4));" },
	{ "patch_bytes", "Write bytes at an address, as hex text like 90 90 or a list of numbers.", "address!:Address or name|bytes!:Hex bytes", L"patch_bytes($address, $bytes); wait(); print(disassemble($address, 4));" },
	{ "revert_patch", "Undo the patch run touching an address.", "address!:Address or name", L"revert($address); wait(); print(disassemble($address, 4));" },
	{ "list_patches", "Every patched byte as [address, original, value].", "", L"rows(patches());" },
	{ "write_patched", "Write the patched file next to the original as name.patched.exe.", "", L"print(write_patched());" },
	{ "find_bytes", "Find every match of a byte pattern like 48 8B ?? 05, where ?? matches anything.", "pattern!:Byte pattern|limit#:Most matches, default 200", L"var r = find_all_bytes($pattern, image_base(), $limit == none ? 200 : $limit); for (var i = 0; i < len(r); i++) { var f = function_at(r[i]); print(hex(r[i]) + (f == none ? \"\" : \"  \" + name(f))); } if (len(r) == 0) print(\"(none)\");" },
	{ "find_signature", "Find matches of an IDA-style signature like 48 8B 05 ? ? ? ?.", "pattern!:Signature|limit#:Most matches, default 200", L"rows(find_signature($pattern, $limit == none ? 200 : $limit));" },
	{ "make_signature", "Make the shortest unique signature for an address, in ida, x64dbg, code or array style.", "address!:Address or name|style:ida, x64dbg, code or array", L"var s = signature_info($address, $style == none ? \"ida\" : $style); print(s[0]); print(\"unique \" + s[1] + \"  matches \" + s[2] + \"  start \" + hex(s[3]) + (s[4] ? \"  through a reference\" : \"\"));" },
	{ "xor_decode", "Decode XOR-encoded bytes at an address. Key is hex like 5A or 12 34, optionally followed by a length, or auto to guess.", "address!:Address or name|key:Hex key or auto", L"print(xor_decode($address, $key == none ? \"auto\" : $key));" },
	{ "stack_string", "A string built on the stack by the instructions at an address, if Plumb recognized one.", "address!:Address or name", L"print(stack_string($address));" },
	{ "jump_table", "A switch jump or its table as [jump, table, count, [targets]].", "address!:Address or name", L"rows(jump_table($address));" },
	{ "set_number_form", "Show the immediate at an address as hex, dec, char, bin or float in the listing.", "address!:Address or name|form!:hex, dec, char, bin or float", L"set_number_form($address, $form); print(disassemble($address, 1));" },
	{ "learn_names", "Teach the name library this file's function names, so they are applied to related binaries.", "", L"print(learn_names());" },
	{ "apply_names", "Name functions from the name library.", "", L"print(apply_names());" },
	{ "undo", "Undo the last change to names, comments, types or patches.", "", L"print(undo());" },
	{ "redo", "Redo the last undone change.", "", L"print(redo());" },
	{ "reanalyze", "Run the whole analysis again and wait for it.", "", L"reanalyze(); wait(); print(len(functions()) + \" functions.\");" },
	{ "export_report", "Write name.report.md next to the file: functions, notes, names, strings and imports.", "", L"print(export_report());" },
	{ "export_classes", "Write name.classes.h next to the file with every recovered class.", "", L"print(export_classes());" },
	{ "debug_start", "Start the open file under Plumb's debugger and stop at its entry point. A DLL runs in its host.", "", L"debug_start(); report(debug_wait(10000));" },
	{ "debug_attach", "Attach the debugger to a running process by ID or name, or to the one running the open file.", "process:Process ID or name", L"if ($process == none) debug_attach(); else debug_attach($process); report(debug_wait(5000));" },
	{ "debug_detach", "Detach and let the process keep running.", "", L"debug_detach(); print(debug_state());" },
	{ "debug_state", "The debugger state, why it last stopped and the instruction pointer.", "", L"report(debug_state());" },
	{ "debug_continue", "Resume the paused process and wait until it pauses or ends.", "wait_ms#:How long to wait, default 10000", L"debug_continue(); report(debug_wait($wait_ms == none ? 10000 : $wait_ms));" },
	{ "debug_step_into", "Step one instruction, into calls.", "", L"debug_step_into(); report(debug_state());" },
	{ "debug_step_over", "Step one instruction, over calls.", "", L"debug_step_over(); report(debug_state());" },
	{ "debug_step_out", "Run until the current function returns.", "", L"debug_step_out(); report(debug_state());" },
	{ "debug_run_to", "Run until an address is reached.", "address!:Address or name", L"debug_run_to($address); report(debug_wait(10000));" },
	{ "debug_pause", "Break into the running process.", "", L"debug_pause(); report(debug_wait(5000));" },
	{ "debug_stop", "End the debugged process.", "", L"debug_stop(); print(debug_state());" },
	{ "debug_registers", "Every register while paused.", "", L"rows(registers());" },
	{ "debug_read", "Read live memory while paused, as a hex dump. Image addresses follow the image to where it loaded.", "address!:Address or name|count#:Bytes, default 64", L"dump($address, debug_read($address, $count == none ? 64 : $count));" },
	{ "debug_write", "Write live memory while paused. Bytes are hex text like 90 90.", "address!:Address or name|bytes!:Hex bytes", L"debug_write($address, $bytes); dump($address, debug_read($address, 16));" },
	{ "debug_evaluate", "Evaluate a watch expression like [rsp+8] or rcx+0x10 while paused.", "expression!:Expression", L"print(cell(debug_evaluate($expression)));" },
	{ "debug_modules", "Every module loaded in the debugged process as [path, base, size].", "", L"rows(debug_modules());" },
	{ "list_processes", "Running processes as [id, name].", "", L"rows(processes());" },
	{ "list_breakpoints", "Software and hardware breakpoints with their conditions.", "", L"var b = breakpoints(); for (var i = 0; i < len(b); i++) { var c = condition(b[i]); print(hex(b[i]) + \"  \" + name(b[i]) + (c != none && c != \"\" ? \"  if \" + c : \"\")); } print(\"hardware:\"); rows(hardware_breakpoints());" },
	{ "set_breakpoint", "Set a breakpoint, optionally with a condition like rax == 0.", "address!:Address or name|condition:Condition", L"if ($condition == none || $condition == \"\") add_breakpoint($address); else add_breakpoint($address, $condition); rows(breakpoints());" },
	{ "delete_breakpoint", "Remove a breakpoint.", "address!:Address or name", L"del_breakpoint($address); print(\"Removed.\");" },
	{ "set_hardware_breakpoint", "Set one of four hardware breakpoints: x on execution, w 4 on 4-byte writes, rw 8 on any 8-byte access.", "address!:Address or name|kind:x, w 1/2/4/8 or rw 1/2/4/8", L"if ($kind == none) set_hardware_breakpoint($address); else set_hardware_breakpoint($address, $kind); rows(hardware_breakpoints());" },
	{ "delete_hardware_breakpoint", "Remove a hardware breakpoint.", "address!:Address or name", L"del_hardware_breakpoint($address); rows(hardware_breakpoints());" },
	{ "set_debug_command", "Arguments for the debugged executable, or host.exe arguments for a DLL.", "command!:Command line", L"set_debug_command($command); print(debug_command());" }
};

const int32_t ui::mcp_bridge_count = static_cast < int32_t > ( sizeof ( mcp_bridges ) / sizeof ( mcp_bridges [ 0 ] ) );

void ui::write_schema ( const char* params, json::writer& out ) {

	out.open_object ( ).key ( "type" ).string ( "object" ).key ( "properties" ).open_object ( );

	base::list < base::list < char > > required;
	const char* at = params;

	while ( *at ) {

		char name [ 64 ] = { };
		char description [ 256 ] = { };
		int32_t n = 0;
		bool needed = false;
		bool integer = false;

		while ( *at && *at != '!' && *at != '#' && *at != ':' && *at != '|' && n < 63 )
			name [ n++ ] = *at++;

		while ( *at == '!' || *at == '#' ) {

			needed = needed || *at == '!';
			integer = integer || *at == '#';
			at++;

		}

		n = 0;

		if ( *at == ':' ) {

			at++;

			while ( *at && *at != '|' && n < 255 )
				description [ n++ ] = *at++;

		}

		if ( *at == '|' )
			at++;

		if ( !name [ 0 ] )
			continue;

		out.key ( name ).open_object ( ).key ( "type" ).string ( integer ? "integer" : "string" );

		if ( description [ 0 ] )
			out.key ( "description" ).string ( description );

		out.close_object ( );

		if ( needed ) {

			base::list < char >& kept = required.emplace ( );

			for ( const char* c = name; *c; c++ )
				kept.push ( *c );

			kept.push ( 0 );

		}

	}

	out.close_object ( );

	if ( !required.empty ( ) ) {

		out.key ( "required" ).open_array ( );

		for ( const base::list < char >& item : required )
			out.string ( item.data ( ) );

		out.close_array ( );

	}

	out.close_object ( );

}

void ui::bridge_source ( const wchar_t* script, const json::value* args, base::list < wchar_t >& out ) {

	auto put = [ & ] ( const wchar_t* text ) {

		for ( ; *text; text++ )
			out.push ( *text );

	};

	auto literal = [ & ] ( auto& self, const json::value* item ) -> void {

		if ( !item || item->kind == json::type::NONE ) {

			put ( L"none" );

			return;

		}

		if ( item->kind == json::type::BOOLEAN ) {

			put ( item->flag ? L"1" : L"0" );

			return;

		}

		if ( item->kind == json::type::NUMBER ) {

			put ( base::writer ( ).number ( static_cast < int64_t > ( item->number ) ).text ( ) );

			return;

		}

		if ( item->kind == json::type::ARRAY ) {

			out.push ( L'[' );

			for ( int32_t i = 0; i < item->items.count ( ); i++ ) {

				if ( i )
					put ( L", " );

				self ( self, &item->items [ static_cast < size_t > ( i ) ] );

			}

			out.push ( L']' );

			return;

		}

		wchar_t wide [ 8192 ];

		json::to_wide ( item->string ( ), wide, 8192 );
		out.push ( L'"' );

		for ( const wchar_t* c = wide; *c; c++ ) {

			if ( *c == L'"' || *c == L'\\' ) {

				out.push ( L'\\' );
				out.push ( *c );

			} else if ( *c == L'\n' )
				put ( L"\\n" );
			else if ( *c == L'\r' )
				put ( L"\\r" );
			else if ( *c == L'\t' )
				put ( L"\\t" );
			else
				out.push ( *c );

		}

		out.push ( L'"' );

	};

	put ( L"func cell(x) { if (typeof(x) == \"number\") return (x >= 0x10000 || x <= -0x10000) ? hex(x) : \"\" + x; if (typeof(x) == \"list\") { var p = []; for (var i = 0; i < len(x); i++) append(p, cell(x[i])); return \"[\" + join(p, \", \") + \"]\"; } if (typeof(x) == \"none\") return \"none\"; return x; }\n" );
	put ( L"func row(x) { if (typeof(x) != \"list\") return cell(x); var p = []; for (var i = 0; i < len(x); i++) append(p, cell(x[i])); return join(p, \"  \"); }\n" );
	put ( L"func rows(v) { if (typeof(v) != \"list\") { print(cell(v)); return; } if (len(v) == 0) print(\"(none)\"); for (var i = 0; i < len(v); i++) print(row(v[i])); }\n" );
	put ( L"func named(v) { if (len(v) == 0) print(\"(none)\"); for (var i = 0; i < len(v); i++) print(hex(v[i]) + \"  \" + name(v[i])); }\n" );
	put ( L"func words(t) { var p = split(trim(t), \" \"); var w = []; for (var i = 0; i < len(p); i++) if (p[i] != \"\") append(w, p[i]); return w; }\n" );
	put ( L"func texts(f) { var ins = instructions(f); var shown = 0; for (var i = 0; i < len(ins); i++) { var r = refs_from(ins[i]); for (var k = 0; k < len(r); k++) { if (is_code(r[k])) continue; var s = string(r[k]); if (s != none && len(s) >= 4) { print(hex(ins[i]) + \"  \" + s); shown++; } } } if (!shown) print(\"(none)\"); }\n" );
	put ( L"func tree(f, depth, max, pad, seen, up) { print(pad + hex(f) + \"  \" + name(f)); if (depth >= max || !is_function(f)) return; var c = up ? callers(f) : callees(f); for (var i = 0; i < len(c); i++) { if (contains(seen, c[i])) { print(pad + \"  \" + hex(c[i]) + \"  \" + name(c[i]) + \"  (above)\"); continue; } append(seen, c[i]); tree(c[i], depth + 1, max, pad + \"  \", seen, up); } }\n" );
	put ( L"func report(state) { print(state); var n = debug_note(); if (n != none && n != \"\") print(n); if (state == \"paused\") print(\"rip \" + hex(pc())); }\n" );
	put ( L"func dump(a, b) { for (var i = 0; i < len(b); i += 16) { var line = format(\"+%04X \", i); for (var k = i; k < i + 16 && k < len(b); k++) line = line + \" \" + format(\"%02X\", b[k]); print(line); } }\n" );
	put ( L"func main() {\n" );

	for ( const wchar_t* at = script; *at; ) {

		if ( *at != L'$' ) {

			out.push ( *at++ );

			continue;

		}

		at++;

		char key [ 64 ] = { };
		int32_t n = 0;

		while ( ( ( *at >= L'a' && *at <= L'z' ) || *at == L'_' ) && n < 63 )
			key [ n++ ] = static_cast < char > ( *at++ );

		literal ( literal, args ? args->get ( key ) : nullptr );

	}

	put ( L"\n}\n" );
	out.push ( 0 );

}

int64_t ui::integer_arg ( const json::value* args, const char* key, int64_t fallback ) {

	const json::value* item = args ? args->get ( key ) : nullptr;

	if ( !item )
		return fallback;

	if ( item->kind == json::type::NUMBER )
		return static_cast < int64_t > ( item->number );

	if ( item->kind == json::type::STRING ) {

		int64_t value = 0;

		for ( const char* at = item->string ( ); *at >= '0' && *at <= '9'; at++ )
			value = value * 10 + ( *at - '0' );

		return value;

	}

	return fallback;

}

void ui::text_arg ( const json::value* args, const char* key, wchar_t* out, int32_t room ) {

	const json::value* item = args ? args->get ( key ) : nullptr;

	out [ 0 ] = 0;

	if ( item && item->kind == json::type::STRING )
		json::to_wide ( item->string ( ), out, room );
	else if ( item && item->kind == json::type::NUMBER ) {

		base::writer text;

		text.hex ( static_cast < uint64_t > ( item->number ) );

		for ( int32_t i = 0; i <= text.size ( ) && i < room - 1; i++ )
			out [ i ] = text.text ( ) [ i ];

	}

}

bool ui::app::mcp_address ( const json::value* args, const char* key, uint64_t& address, base::writer& problem ) const {

	wchar_t text [ 512 ];

	text_arg ( args, key, text, 512 );

	const wchar_t* body = text;

	if ( body [ 0 ] == L'0' && ( body [ 1 ] == L'x' || body [ 1 ] == L'X' ) )
		body += 2;

	if ( !body [ 0 ] ) {

		problem.add ( L"Missing \"" );

		for ( const char* at = key; *at; at++ )
			problem.add ( static_cast < wchar_t > ( *at ) );

		problem.add ( L"\"." );

		return false;

	}

	if ( parse_address ( body, address ) || parse_address ( text, address ) )
		return true;

	problem.add ( L"\"" ).add ( text ).add ( L"\" isn't an address or a known name in this file." );

	return false;

}

void ui::app::mcp_disassemble ( uint64_t address, int32_t count, uint64_t limit, base::list < wchar_t >& out ) const {

	listing_names namer;
	mcp_text text;
	wchar_t line [ 400 ];
	wchar_t name [ 200 ];
	bool wide = file.wide ( );
	int32_t shown = 0;
	int32_t skipped = 0;
	uint64_t at = address;

	namer.owner = this;

	while ( shown < count && at < limit ) {

		x86::instruction ins;

		if ( !db->is_start ( at ) || !db->decode ( at, ins ) ) {

			if ( ++skipped > 65536 )
				break;

			at++;

			continue;

		}

		if ( ( db->is_label ( at ) || db->function_starting ( at ) >= 0 ) && symbol_name ( at, true, name, 200 ) )
			text.line ( base::writer ( ).add ( name ).add ( L":" ).text ( ) );

		x86::format_text ( ins, line, 400, &namer, 0 );

		base::writer row;
		const wchar_t* remark = user_text ( comments, at );

		row.add ( L"  " ).hex ( at, wide ? 9 : 8 ).add ( L"  " ).add ( line );

		if ( remark )
			row.add ( L"    ; " ).add ( remark );
		else if ( const wchar_t* stacked = stack_string ( at ) )
			row.add ( L"    ; " ).add ( stacked );

		text.line ( row.text ( ) );
		shown++;
		at = ins.next ( );

	}

	if ( shown == count && at < limit )
		text.line ( L"  ..." );

	out = base::move ( text.out );

}

bool ui::app::mcp_call ( const char* name, const json::value* args, base::list < wchar_t >& out ) {

	mcp_text text;
	base::writer problem;
	bool analyzed = db && analysis_applied && !db->functions.empty ( );
	bool wide = file.wide ( );
	wchar_t buffer [ 1024 ];
	wchar_t label [ 200 ];

	auto finish = [ & ] ( bool ok ) {

		if ( !ok )
			text.line ( problem.text ( ) );

		out = base::move ( text.out );

		return ok;

	};

	if ( base::same_text ( name, "open_binary" ) ) {

		text_arg ( args, "path", buffer, 1024 );

		if ( !buffer [ 0 ] ) {

			problem.add ( L"Give the full path of the file to open." );

			return finish ( false );

		}

		if ( !open_file ( buffer ) ) {

			problem.add ( L"Couldn't open " ).add ( buffer ).add ( L". " ).add ( load_problem.get ( ) );

			return finish ( false );

		}

		wait_analysis ( );

		if ( analysis_done && !analysis_applied )
			analysis_finished ( );

		return mcp_call ( "binary_info", nullptr, out );

	}

	if ( base::same_text ( name, "run_script" ) ) {

		const json::value* given = args ? args->get ( "source" ) : nullptr;
		base::list < wchar_t > captured;
		bool ok = false;

		if ( given && given->kind == json::type::STRING ) {

			const char* utf8 = given->string ( );
			int32_t length = 0;

			while ( utf8 [ length ] )
				length++;

			base::list < wchar_t > source;
			int32_t made = length ? MultiByteToWideChar ( CP_UTF8, 0, utf8, length, nullptr, 0 ) : 0;

			source.resize ( static_cast < size_t > ( made + 1 ) );

			if ( made )
				MultiByteToWideChar ( CP_UTF8, 0, utf8, length, source.data ( ), made );

			source [ static_cast < size_t > ( made ) ] = 0;
			ok = run_script_text ( source.data ( ), L"mcp", &captured );

		} else {

			text_arg ( args, "path", buffer, 1024 );

			if ( !buffer [ 0 ] ) {

				problem.add ( L"Give the script as source, or the path of a .psc file." );

				return finish ( false );

			}

			ok = run_script_file ( buffer, &captured );

		}

		text.out = base::move ( captured );

		if ( text.out.empty ( ) )
			text.line ( ok ? L"The script ran and printed nothing." : L"The script failed." );

		out = base::move ( text.out );

		return ok;

	}

	if ( !file.loaded ( ) ) {

		problem.add ( L"No file is open. Call open_binary first." );

		return finish ( false );

	}

	if ( base::same_text ( name, "binary_info" ) ) {

		text.line ( base::writer ( ).add ( L"File: " ).add ( file.path.get ( ) ).text ( ) );
		text.line ( base::writer ( ).add ( L"Format: " ).add ( file.format_name ( ) ).add ( L", " ).add ( file.machine_name ( ) ).add ( file.dll ? L", DLL" : L"" ).add ( L", " ).grouped ( file.size ).add ( L" bytes" ).text ( ) );
		text.line ( base::writer ( ).add ( L"Image base: " ).hex ( file.base, wide ? 9 : 8 ).add ( L"   Entry point: " ).hex ( file.entry, wide ? 9 : 8 ).text ( ) );

		if ( !file.symbol_file.empty ( ) )
			text.line ( base::writer ( ).add ( L"Symbols: " ).add ( file.symbol_file.get ( ) ).text ( ) );
		else if ( !file.debug_path.empty ( ) )
			text.line ( base::writer ( ).add ( L"Symbols: none loaded, debug info points to " ).add ( file.debug_path.get ( ) ).text ( ) );

		text.line ( base::writer ( ).add ( L"Imports: " ).grouped ( static_cast < uint64_t > ( file.imports.count ( ) ) ).add ( L" from " ).number ( file.modules.count ( ) ).add ( L" modules   Exports: " ).grouped ( static_cast < uint64_t > ( file.exports.count ( ) ) ).add ( L"   Strings: " ).grouped ( static_cast < uint64_t > ( file.strings.count ( ) ) ).text ( ) );

		if ( analyzed )
			text.line ( base::writer ( ).add ( L"Analysis: " ).grouped ( static_cast < uint64_t > ( db->functions.count ( ) ) ).add ( L" functions, " ).grouped ( db->decoded ).add ( L" instructions, " ).grouped ( static_cast < uint64_t > ( db->refs.count ( ) ) ).add ( L" references, " ).number ( db->classes.count ( ) ).add ( L" RTTI classes" ).text ( ) );

		text.line ( L"Sections:" );

		for ( const loader::section& item : file.sections )
			text.line ( base::writer ( ).add ( L"  " ).add ( item.name ).add ( L"  " ).hex ( item.address, wide ? 9 : 8 ).add ( L"  size 0x" ).hex ( item.size ).add ( L"  " ).add ( item.read ? L"R" : L"-" ).add ( item.write ? L"W" : L"-" ).add ( item.execute ? L"X" : L"-" ).add ( L"  " ).add ( loader::kind_name ( item.type ) ).text ( ) );

		return finish ( true );

	}

	if ( base::same_text ( name, "list_imports" ) || base::same_text ( name, "list_exports" ) ) {

		text_arg ( args, "filter", buffer, 1024 );

		bool imports = base::same_text ( name, "list_imports" );
		int32_t count = 0;

		if ( imports ) {

			for ( const loader::import& item : file.imports ) {

				const wchar_t* module = file.modules [ static_cast < size_t > ( item.owner ) ].name.get ( );

				if ( buffer [ 0 ] && !base::contains_folded ( item.name.get ( ), buffer ) && !base::contains_folded ( module, buffer ) )
					continue;

				base::writer row;

				row.hex ( item.slot, wide ? 9 : 8 ).add ( L"  " ).add ( module ).add ( L"!" );

				if ( item.by_ordinal || item.name.empty ( ) )
					row.add ( L"#" ).number ( item.ordinal );
				else
					row.add ( item.name.get ( ) );

				text.line ( row.text ( ) );
				count++;

			}

		} else {

			for ( const loader::exported& item : file.exports ) {

				if ( buffer [ 0 ] && !base::contains_folded ( item.name.get ( ), buffer ) )
					continue;

				base::writer row;

				if ( item.forward.empty ( ) )
					row.hex ( item.address, wide ? 9 : 8 );
				else
					row.add ( L"forward" );

				row.add ( L"  #" ).number ( item.ordinal ).add ( L"  " ).add ( item.name.empty ( ) ? L"(no name)" : item.name.get ( ) );

				if ( !item.forward.empty ( ) )
					row.add ( L" -> " ).add ( item.forward.get ( ) );

				text.line ( row.text ( ) );
				count++;

			}

		}

		if ( !count )
			text.line ( L"Nothing matches." );

		return finish ( true );

	}

	if ( base::same_text ( name, "list_strings" ) ) {

		text_arg ( args, "filter", buffer, 1024 );

		int64_t skip = integer_arg ( args, "offset", 0 );
		int64_t limit = base::clamp < int64_t > ( integer_arg ( args, "limit", 200 ), 1, 5000 );
		int64_t matched = 0;
		int64_t shown = 0;
		wchar_t preview [ 400 ];

		for ( const loader::found_string& item : file.strings ) {

			file.string_text ( item, preview, 400 );

			if ( buffer [ 0 ] && !base::contains_folded ( preview, buffer ) )
				continue;

			if ( matched++ < skip || shown >= limit )
				continue;

			uint64_t address = 0;
			base::writer row;

			for ( wchar_t* c = preview; *c; c++ ) {

				if ( *c < 32 )
					*c = L' ';

			}

			if ( file.address_of ( item.offset, address ) )
				row.hex ( address, wide ? 9 : 8 );
			else
				row.add ( L"file+" ).hex ( item.offset );

			row.add ( item.wide ? L"  W  \"" : L"  A  \"" ).add ( preview ).add ( L"\"" );
			text.line ( row.text ( ) );
			shown++;

		}

		text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( matched ) ).add ( L" strings match, showing " ).grouped ( static_cast < uint64_t > ( shown ) ).add ( L" from offset " ).number ( skip ).add ( L"." ).text ( ) );

		return finish ( true );

	}

	if ( base::same_text ( name, "read_bytes" ) ) {

		uint64_t address = 0;
		uint64_t offset = 0;

		if ( !mcp_address ( args, "address", address, problem ) )
			return finish ( false );

		if ( !file.offset_of ( address, offset ) ) {

			problem.add ( L"That address isn't backed by bytes in the file." );

			return finish ( false );

		}

		int64_t count = base::clamp < int64_t > ( integer_arg ( args, "count", 64 ), 1, 4096 );

		for ( int64_t row = 0; row < count; row += 16 ) {

			base::writer line;
			wchar_t shown [ 17 ];
			int32_t n = 0;

			line.hex ( address + static_cast < uint64_t > ( row ), wide ? 9 : 8 ).add ( L"  " );

			for ( int64_t i = row; i < row + 16 && i < count; i++ ) {

				uint64_t at = offset + static_cast < uint64_t > ( i );
				uint8_t value = at < file.size ? file.byte ( at ) : 0;

				line.hex ( value, 2 ).add ( L" " );
				shown [ n++ ] = value >= 32 && value < 127 ? static_cast < wchar_t > ( value ) : L'.';

			}

			shown [ n ] = 0;
			line.add ( L" " ).add ( shown );
			text.line ( line.text ( ) );

		}

		return finish ( true );

	}

	if ( !analyzed ) {

		problem.add ( L"The analysis hasn't produced any functions for this file." );

		return finish ( false );

	}

	if ( base::same_text ( name, "list_functions" ) ) {

		text_arg ( args, "filter", buffer, 1024 );

		int64_t skip = integer_arg ( args, "offset", 0 );
		int64_t limit = base::clamp < int64_t > ( integer_arg ( args, "limit", 200 ), 1, 5000 );
		int64_t matched = 0;
		int64_t shown = 0;

		for ( int32_t i = 0; i < db->functions.count ( ); i++ ) {

			function_name ( i, label, 200 );

			if ( buffer [ 0 ] && !base::contains_folded ( label, buffer ) )
				continue;

			if ( matched++ < skip || shown >= limit )
				continue;

			const analysis::function_info& info = db->functions [ static_cast < size_t > ( i ) ];
			const wchar_t* remark = user_text ( notes, info.start );
			base::writer row;

			row.hex ( info.start, wide ? 9 : 8 ).add ( L"  size 0x" ).hex ( info.end - info.start ).add ( L"  callers " ).number ( info.callers ).add ( L"  " ).add ( label );

			if ( remark )
				row.add ( L"  // " ).add ( remark );

			text.line ( row.text ( ) );
			shown++;

		}

		text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( matched ) ).add ( L" functions match, showing " ).grouped ( static_cast < uint64_t > ( shown ) ).add ( L" from offset " ).number ( skip ).add ( L"." ).text ( ) );

		return finish ( true );

	}

	if ( base::same_text ( name, "disassemble" ) ) {

		uint64_t address = 0;

		if ( !mcp_address ( args, "address", address, problem ) )
			return finish ( false );

		int32_t owner = db->function_starting ( address );
		int64_t count = integer_arg ( args, "count", -1 );
		uint64_t limit = file.high ( );

		if ( count < 0 && owner >= 0 ) {

			count = 2000;
			limit = db->functions [ static_cast < size_t > ( owner ) ].end;

		} else if ( count < 0 )
			count = 40;

		base::list < wchar_t > listing;

		mcp_disassemble ( address, static_cast < int32_t > ( base::clamp < int64_t > ( count, 1, 2000 ) ), limit, listing );

		for ( wchar_t c : listing )
			text.out.push ( c );

		if ( listing.empty ( ) )
			text.line ( L"No decoded instructions there. It may be data." );

		return finish ( true );

	}

	if ( base::same_text ( name, "decompile" ) ) {

		uint64_t address = 0;

		if ( !mcp_address ( args, "function", address, problem ) )
			return finish ( false );

		int32_t owner = db->function_at ( address );

		if ( owner < 0 ) {

			problem.add ( L"That address isn't inside a function." );

			return finish ( false );

		}

		const decompiler::result* code = pseudocode_for ( db->functions [ static_cast < size_t > ( owner ) ].start );

		if ( !code || !code->ok ) {

			problem.add ( L"The decompiler couldn't handle this function. Use disassemble instead." );

			return finish ( false );

		}

		for ( const decompiler::line& item : code->lines ) {

			for ( int32_t i = 0; i < item.indent; i++ )
				text.add ( L"    " );

			for ( int32_t i = 0; i < item.length; i++ )
				text.out.push ( code->text [ static_cast < size_t > ( item.text + i ) ] );

			text.out.push ( L'\n' );

		}

		return finish ( true );

	}

	if ( base::same_text ( name, "xrefs_to" ) ) {

		uint64_t address = 0;
		int32_t first = 0;
		int32_t count = 0;

		if ( !mcp_address ( args, "address", address, problem ) )
			return finish ( false );

		db->refs_to ( address, first, count );

		for ( int32_t r = first; r < first + count && r < first + 2000; r++ ) {

			const analysis::xref& item = db->refs [ static_cast < size_t > ( r ) ];
			uint64_t source = file.base + item.source;
			int32_t owner = db->function_at ( source );
			base::writer row;

			row.hex ( source, wide ? 9 : 8 ).add ( L"  " ).add ( analysis::ref_name ( item.type ) ).add ( L"  " );

			if ( owner >= 0 ) {

				function_name ( owner, label, 200 );
				row.add ( label ).add ( L"+" ).hex ( source - db->functions [ static_cast < size_t > ( owner ) ].start );

			}

			text.line ( row.text ( ) );

		}

		text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( count ) ).add ( count == 1 ? L" reference." : L" references." ).text ( ) );

		return finish ( true );

	}

	if ( base::same_text ( name, "calls_from" ) ) {

		uint64_t address = 0;

		if ( !mcp_address ( args, "function", address, problem ) )
			return finish ( false );

		int32_t owner = db->function_at ( address );

		if ( owner < 0 ) {

			problem.add ( L"That address isn't inside a function." );

			return finish ( false );

		}

		const analysis::function_info& info = db->functions [ static_cast < size_t > ( owner ) ];
		base::list < uint64_t > seen;

		for ( const analysis::xref& item : db->refs ) {

			uint64_t source = file.base + item.source;
			uint64_t target = file.base + item.target;
			bool call_like = item.type == analysis::ref::CALL || ( item.type == analysis::ref::JUMP && db->function_starting ( target ) >= 0 ) || ( item.type == analysis::ref::READ && file.section_at ( target ) >= 0 );

			if ( !call_like || db->function_at ( source ) != owner || source < info.start )
				continue;

			bool import_slot = false;

			for ( const loader::import& entry : file.imports )
				import_slot = import_slot || entry.slot == target;

			if ( item.type == analysis::ref::READ && !import_slot )
				continue;

			bool repeated = false;

			for ( uint64_t value : seen )
				repeated = repeated || value == target;

			if ( repeated )
				continue;

			seen.push ( target );

			base::writer row;

			row.hex ( target, wide ? 9 : 8 ).add ( L"  " );

			if ( symbol_name ( target, true, label, 200 ) )
				row.add ( label );

			row.add ( import_slot ? L"  (import)" : L"" );
			text.line ( row.text ( ) );

		}

		if ( seen.empty ( ) )
			text.line ( L"This function makes no direct calls." );

		return finish ( true );

	}

	if ( base::same_text ( name, "search_code" ) || base::same_text ( name, "query_functions" ) ) {

		bool query = base::same_text ( name, "query_functions" );

		text_arg ( args, query ? "query" : "text", buffer, 1024 );

		if ( !buffer [ 0 ] ) {

			problem.add ( L"The search text is empty." );

			return finish ( false );

		}

		if ( query ) {

			if ( !run_query ( buffer ) ) {

				for ( int32_t i = output.count ( ) - 1; i >= 0 && !problem.size ( ); i-- ) {

					if ( output [ static_cast < size_t > ( i ) ].kind == log_kind::FAIL )
						problem.add ( output [ static_cast < size_t > ( i ) ].text.get ( ) );

				}

				if ( !problem.size ( ) )
					problem.add ( L"The query failed." );

				return finish ( false );

			}

		} else
			search_everything ( buffer );

		int32_t shown = base::min ( search_hits.count ( ), 2000 );

		for ( int32_t i = 0; i < shown; i++ ) {

			const search_hit& item = search_hits [ static_cast < size_t > ( i ) ];
			int32_t owner = db->function_at ( item.address );
			base::writer row;

			row.hex ( item.address, wide ? 9 : 8 ).add ( L"  " );

			if ( !query && owner >= 0 ) {

				function_name ( owner, label, 200 );
				row.add ( label ).add ( L"+" ).hex ( item.address - db->functions [ static_cast < size_t > ( owner ) ].start ).add ( L"  " );

			}

			row.add ( item.note.get ( ) );
			text.line ( row.text ( ) );

		}

		text.line ( base::writer ( ).grouped ( static_cast < uint64_t > ( search_hits.count ( ) ) ).add ( search_hits.count ( ) == 1 ? L" match." : L" matches." ).text ( ) );

		return finish ( true );

	}

	if ( base::same_text ( name, "rename" ) || base::same_text ( name, "set_comment" ) || base::same_text ( name, "set_note" ) || base::same_text ( name, "set_prototype" ) ) {

		bool rename = base::same_text ( name, "rename" );
		bool note = base::same_text ( name, "set_note" );
		bool shape = base::same_text ( name, "set_prototype" );
		bool comment = !rename && !note && !shape;
		uint64_t address = 0;

		if ( !mcp_address ( args, rename || comment ? "address" : "function", address, problem ) )
			return finish ( false );

		text_arg ( args, rename ? "name" : ( shape ? "prototype" : "text" ), buffer, 250 );

		if ( note || shape ) {

			int32_t owner = db->function_at ( address );

			if ( owner < 0 ) {

				problem.add ( L"That address isn't inside a function." );

				return finish ( false );

			}

			address = db->functions [ static_cast < size_t > ( owner ) ].start;

		}

		if ( rename ) {

			for ( wchar_t* c = buffer; *c; c++ ) {

				bool word = ( *c >= L'a' && *c <= L'z' ) || ( *c >= L'A' && *c <= L'Z' ) || ( *c >= L'0' && *c <= L'9' ) || *c == L'_' || *c == L':' || *c == L'@' || *c == L'?' || *c == L'$' || *c == L'~';

				if ( !word )
					*c = L'_';

			}

			if ( buffer [ 0 ] >= L'0' && buffer [ 0 ] <= L'9' ) {

				problem.add ( L"Names can't start with a digit." );

				return finish ( false );

			}

		}

		if ( shape && buffer [ 0 ] ) {

			decompiler::c_prototype parsed;
			base::writer complaint;

			if ( !decompiler::parse_prototype ( buffer, file.wide ( ), parsed, complaint ) ) {

				problem.add ( L"That prototype doesn't parse: " ).add ( complaint.text ( ) );

				return finish ( false );

			}

		}

		remember_undo ( );
		set_user_text ( rename ? renamed : ( note ? notes : ( shape ? prototypes : comments ) ), address, buffer );

		if ( rename || shape ) {

			names_revision++;
			code_cache.clear ( );

		}

		changed ( );

		if ( autosave )
			save_project ( );

		symbol_name ( address, true, label, 200 );
		text.line ( base::writer ( ).add ( buffer [ 0 ] ? L"Set at " : L"Cleared at " ).hex ( address, wide ? 9 : 8 ).add ( L" (" ).add ( label ).add ( L"). Call save_project to keep it." ).text ( ) );

		return finish ( true );

	}

	if ( base::same_text ( name, "compare_with" ) ) {

		text_arg ( args, "path", buffer, 1024 );

		const json::value* same = args ? args->get ( "show_same" ) : nullptr;
		bool show_same = same && same->kind == json::type::BOOLEAN && same->flag;
		static const wchar_t* const states [ 4 ] = { L"same", L"changed", L"only here", L"only there" };

		if ( !buffer [ 0 ] || !compare_with ( buffer ) ) {

			problem.add ( L"Couldn't compare with " ).add ( buffer ).add ( L"." );

			return finish ( false );

		}

		text.line ( base::writer ( ).add ( L"Compared with " ).add ( other_file->path.get ( ) ).add ( L": " ).number ( diff_counts [ 0 ] ).add ( L" same, " ).number ( diff_counts [ 1 ] ).add ( L" changed, " ).number ( diff_counts [ 2 ] ).add ( L" only here, " ).number ( diff_counts [ 3 ] ).add ( L" only there." ).text ( ) );

		int32_t shown = 0;

		for ( const diff_row& item : diff_rows ) {

			if ( ( item.state == 0 && !show_same ) || shown >= 3000 )
				continue;

			base::writer row;
			wchar_t other [ 200 ];

			row.add ( states [ item.state ] ).add ( L"  " );

			if ( item.here >= 0 ) {

				function_name ( item.here, label, 200 );
				row.hex ( db->functions [ static_cast < size_t > ( item.here ) ].start, wide ? 9 : 8 ).add ( L"  " ).add ( label );

			}

			if ( item.there >= 0 ) {

				other_name ( item.there, other, 200 );
				row.add ( item.here >= 0 ? L"  <->  " : L"" ).hex ( other_db->functions [ static_cast < size_t > ( item.there ) ].start, wide ? 9 : 8 ).add ( L"  " ).add ( other );

			}

			text.line ( row.text ( ) );
			shown++;

		}

		return finish ( true );

	}

	if ( base::same_text ( name, "emulate" ) ) {

		uint64_t address = 0;
		wchar_t setup [ 512 ];

		if ( !mcp_address ( args, "function", address, problem ) )
			return finish ( false );

		text_arg ( args, "registers", setup, 512 );

		emulate_function ( address, setup, &text.out );

		return finish ( true );

	}

	if ( base::same_text ( name, "save_project" ) ) {

		save_project ( );

		if ( dirty ) {

			problem.add ( L"Couldn't save the project." );

			return finish ( false );

		}

		text.line ( L"Saved the project beside the binary." );

		return finish ( true );

	}

	for ( int32_t i = 0; i < mcp_bridge_count; i++ ) {

		if ( !base::same_text ( name, mcp_bridges [ i ].name ) )
			continue;

		base::list < wchar_t > source;
		base::list < wchar_t > captured;

		bridge_source ( mcp_bridges [ i ].script, args, source );

		bool ok = run_script_text ( source.data ( ), L"mcp", &captured );

		text.out = base::move ( captured );

		if ( text.out.empty ( ) )
			text.line ( ok ? L"Done." : L"The call failed." );

		out = base::move ( text.out );

		return ok;

	}

	problem.add ( L"Unknown tool." );

	return finish ( false );

}

void ui::app::mcp_message ( const json::value& message, json::writer& reply ) {

	const json::value* id = message.get ( "id" );
	const json::value* method = message.get ( "method" );
	const char* name = method ? method->string ( ) : "";

	if ( !id || id->kind == json::type::NONE )
		return;

	reply.open_object ( ).key ( "jsonrpc" ).string ( "2.0" ).key ( "id" ).copy ( *id );

	if ( base::same_text ( name, "initialize" ) ) {

		const json::value* params = message.get ( "params" );
		const json::value* version = params ? params->get ( "protocolVersion" ) : nullptr;

		reply.key ( "result" ).open_object ( );
		reply.key ( "protocolVersion" ).string ( version && version->kind == json::type::STRING ? version->string ( ) : "2025-06-18" );
		reply.key ( "capabilities" ).open_object ( ).key ( "tools" ).open_object ( ).key ( "listChanged" ).boolean ( false ).close_object ( ).close_object ( );
		reply.key ( "serverInfo" ).open_object ( ).key ( "name" ).string ( "plumb" ).key ( "version" ).string ( "0.4" ).close_object ( );
		reply.key ( "instructions" ).string ( "Plumb is a disassembler and decompiler for Windows executables. Open a file with open_binary (unless one was given on the command line), then explore with list_functions, query_functions, decompile, disassemble, xrefs_to and calls_from. Record findings with rename, set_note, set_comment and set_prototype, and call save_project to keep them in the .plumb project that the Plumb app opens." );
		reply.close_object ( );

	} else if ( base::same_text ( name, "ping" ) ) {

		reply.key ( "result" ).open_object ( ).close_object ( );

	} else if ( base::same_text ( name, "tools/list" ) ) {

		reply.key ( "result" ).open_object ( ).key ( "tools" ).open_array ( );

		for ( const mcp_tool& tool : mcp_tools ) {

			json::value schema;

			json::parse ( tool.schema, static_cast < size_t > ( base::text_length ( tool.schema ) ), schema );
			reply.open_object ( ).key ( "name" ).string ( tool.name ).key ( "description" ).string ( tool.description ).key ( "inputSchema" ).copy ( schema ).close_object ( );

		}

		for ( int32_t i = 0; i < mcp_bridge_count; i++ ) {

			reply.open_object ( ).key ( "name" ).string ( mcp_bridges [ i ].name ).key ( "description" ).string ( mcp_bridges [ i ].description ).key ( "inputSchema" );
			write_schema ( mcp_bridges [ i ].params, reply );
			reply.close_object ( );

		}

		reply.close_array ( ).close_object ( );

	} else if ( base::same_text ( name, "tools/call" ) ) {

		const json::value* params = message.get ( "params" );
		const json::value* tool = params ? params->get ( "name" ) : nullptr;
		const json::value* args = params ? params->get ( "arguments" ) : nullptr;
		base::list < wchar_t > text;
		bool ok = mcp_call ( tool ? tool->string ( ) : "", args, text );

		reply.key ( "result" ).open_object ( ).key ( "content" ).open_array ( ).open_object ( ).key ( "type" ).string ( "text" ).key ( "text" ).string ( text.data ( ) ? text.data ( ) : L"", text.count ( ) ).close_object ( ).close_array ( );
		reply.key ( "isError" ).boolean ( !ok ).close_object ( );

	} else {

		reply.key ( "error" ).open_object ( ).key ( "code" ).number ( -32601 ).key ( "message" ).string ( "Method not found" ).close_object ( );

	}

	reply.close_object ( );

}

int32_t ui::app::serve_mcp ( ) {

	HANDLE input = GetStdHandle ( STD_INPUT_HANDLE );
	HANDLE output_handle = GetStdHandle ( STD_OUTPUT_HANDLE );
	base::list < char > pending_bytes;
	char chunk [ 65536 ];

	if ( !input || input == INVALID_HANDLE_VALUE || !output_handle || output_handle == INVALID_HANDLE_VALUE )
		return 1;

	while ( true ) {

		DWORD got = 0;

		if ( !ReadFile ( input, chunk, sizeof ( chunk ), &got, nullptr ) || !got )
			break;

		for ( DWORD i = 0; i < got; i++ )
			pending_bytes.push ( chunk [ i ] );

		int32_t start = 0;

		for ( int32_t i = 0; i < pending_bytes.count ( ); i++ ) {

			if ( pending_bytes [ static_cast < size_t > ( i ) ] != '\n' )
				continue;

			json::value message;
			json::writer reply;

			if ( i - start >= 3 && static_cast < uint8_t > ( pending_bytes [ static_cast < size_t > ( start ) ] ) == 0xEF && static_cast < uint8_t > ( pending_bytes [ static_cast < size_t > ( start + 1 ) ] ) == 0xBB && static_cast < uint8_t > ( pending_bytes [ static_cast < size_t > ( start + 2 ) ] ) == 0xBF )
				start += 3;

			int32_t length = i - start;

			if ( length > 0 && json::parse ( pending_bytes.data ( ) + start, static_cast < size_t > ( length ), message ) ) {

				if ( message.kind == json::type::ARRAY ) {

					reply.open_array ( );

					for ( const json::value& item : message.items )
						mcp_message ( item, reply );

					reply.close_array ( );

				} else
					mcp_message ( message, reply );

			} else if ( length > 1 )
				reply.raw ( "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}" );

			if ( reply.size ( ) > 2 ) {

				DWORD wrote = 0;

				WriteFile ( output_handle, reply.text ( ), static_cast < DWORD > ( reply.size ( ) ), &wrote, nullptr );
				WriteFile ( output_handle, "\n", 1, &wrote, nullptr );

			}

			start = i + 1;

		}

		base::list < char > rest;

		for ( int32_t i = start; i < pending_bytes.count ( ); i++ )
			rest.push ( pending_bytes [ static_cast < size_t > ( i ) ] );

		pending_bytes = base::move ( rest );

	}

	files::settle ( );

	return 0;

}