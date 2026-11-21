# Plumb Script

Plumb Script (`.psc`) is Plumb's own scripting language, in the spirit of IDC: C-like syntax, dynamic values and direct access to everything Plumb knows about the open file.

## Running scripts

- **Alt+F7** (File > Run script file) picks a `.psc` file and runs it. **Ctrl+F7** runs the last script again.
- Dropping a `.psc` file on the window runs it.
- The command line in the output pane runs a file with `run C:\scripts\tag.psc`, or a one-liner with `psc printf("%d functions", len(functions()))`.
- `Plumb.exe --mcp` offers a `run_script` tool, so an AI assistant can run scripts too. It gets back everything the script printed.

Everything a script prints goes to the output pane. Errors name the file, the line and the column, like `tag.psc(12,9): 'count' isn't defined. Declare it with var.`, and nothing runs when the script doesn't parse. All of a script's renames, comments and patches are one undo step.

## The language

```c
#include "common.psc"

var seen = 0;

func describe(address) {
    return format("%s at %s", name(address), hex(address));
}

func main() {
    for (var i = 0; i < 3; i++)
        print(describe(functions()[i]));
}
```

- Statements at the top level run first, then `main()` if the script defines it.
- `var` declares variables. Top-level `var`s are global; everything else lives until the end of its block. Assigning to a name that was never declared is an error caught before the script runs.
- `func name(a, b) { ... }` defines a function (`static` works too, for IDC habits). Missing arguments are `none`, and functions without `return` give `none`. Recursion is allowed 200 calls deep.
- Control flow: `if`/`else`, `while`, `do`/`while`, `for (init; test; step)`, `break`, `continue`, `return`. A `return` at the top level ends the script.
- `#include "file.psc"` pulls in another file, relative to the one including it. Each file is included once.
- Comments are `//` and `/* */`.

### Values

| Type | Written as | Notes |
| --- | --- | --- |
| number | `42`, `0x140001000`, `0b1010`, `1_000_000`, `'MZ'` | 64-bit signed integers. Character constants pack up to eight bytes. |
| text | `"hello\n"` | Escapes `\n \r \t \0 \\ \" \xHH \uHHHH`. `s[i]` is one character, negative indexes count from the end. |
| list | `[1, "two", [3]]` | Any mix of values. `l[i]` reads and `l[i] = v` writes. |
| none | `none` (or `null`) | The result of a missing value, like the name of an unnamed address. |

`true` and `false` are 1 and 0. `0`, `""`, `[]` and `none` are false; everything else is true.

### Operators

C's operators with C's precedence: `+ - * / %`, `& | ^ ~ << >>`, `== != < <= > >=`, `&& || !`, `?:`, `=` and the compound assignments `+= -= *= /= %= &= |= ^= <<= >>=`, and `++`/`--` before or after. `+` also joins text (with any value on the other side, so `"n=" + 5` is `"n=5"`) and concatenates lists. `==` compares text by content and lists element by element. Division by zero is an error, not a crash.

### Core functions

| Function | Result |
| --- | --- |
| `print(a, b, ...)` | Prints the values separated by spaces |
| `printf(format, ...)`, `msg(...)` | Prints formatted text |
| `format(format, ...)` | Formatted text: `%d %i %u %x %X %o %b %c %s %p`, with `-`, `0`, `+`, `#`, width, `*` and precision |
| `len(x)` | Length of text or a list |
| `str(x)`, `hex(n [, digits])`, `int(text [, radix])` | Conversions. `hex(255)` is `"0xFF"`, `int` reads `0x` and `0b` prefixes and gives `none` for bad text |
| `chr(n)`, `ord(text)` | Character from a code, code of the first character |
| `substr(t, start [, length])`, `find(t, part [, from])` | Slicing and searching, `find` gives -1 when absent |
| `replace(t, from, to)`, `upper(t)`, `lower(t)`, `trim(t)` | Text edits |
| `split(t, separator)`, `join(list [, separator])` | Between text and lists |
| `starts(t, prefix)`, `ends(t, suffix)`, `contains(t or list, x)` | Tests |
| `append(l, v)`, `insert(l, i, v)`, `remove(l, i)`, `pop(l)` | List edits in place |
| `copy(l)`, `reverse(l)`, `sort(l)`, `range([start,] stop [, step])` | List helpers. `sort` orders numbers and text |
| `min(...)`, `max(...)`, `abs(n)` | Numbers, or `min`/`max` of a list |
| `typeof(x)` | `"number"`, `"text"`, `"list"` or `"none"` |
| `clock()` | Milliseconds, for timing |
| `assert(test [, message])`, `error(message)`, `exit()` | Stop with an error, or stop quietly |

## The Plumb API

Scripts can do everything you can do by hand: rename, comment, retype, build structs, patch, search, set breakpoints and drive the debugger, change settings and run any menu command.

### Conventions

- **Addresses** are numbers. Every argument written `a` also takes a name (`"CreateFileW"`, `"fn_140001000"`) or an RVA like `"+1A40"`.
- **Function arguments**, written `f`, can be any address inside the function. Plumb uses the function's start.
- **Missing results** come back as `none`: the name of an unnamed address, a read from unmapped memory, a struct field that isn't there.
- **Errors** stop the script and name the line, for example a struct that doesn't exist, a type that doesn't parse or the wrong number of arguments. Check first with `struct_exists`, `is_function` and similar.
- **Setters** give `1` when they succeed. `del_*` functions give `1` if there was something to delete.
- **Undo:** everything a script changes is one undo step. Ctrl+Z after a script takes it all back.
- **Analysis:** functions that read the analysis fail until it has finished. Call `wait()` first in a script that opens a file.
- **Prompts:** there are no `ask_*` functions, because a script can't stop to wait for a dialog. Put the values at the top of the script instead.

### Files and project

| Function | Result |
| --- | --- |
| `is_open()` | `1` if a file is open. Works with nothing open |
| `open(path)`, `close()` | Opens a file (and its project, if there is one), or closes it. Works with nothing open |
| `wait()` | Waits for the analysis, including a re-analysis after patches. `1` when it's ready |
| `save()` | Saves the project. `1` when nothing is left unsaved |
| `undo()`, `redo()` | Steps through the undo history. `1` if there was a step to take |
| `file_path()`, `file_name()`, `project_path()` | The open file's full path and file name, and the project file (`none` before the first save) |
| `file_size()`, `machine()`, `is_64bit()`, `is_dll()`, `is_dirty()` | File facts. `machine()` is text such as `"x86-64"` |
| `image_base()`, `entry()`, `image_end()` | Image layout |
| `sections()`, `section_at(a)` | `[name, start, end, "rwx"]` for each section, and the section name at an address |
| `imports()`, `exports()` | `[module, name, slot]` and `[name, address]`. Ordinal imports are named `"#12"` |

### Reading and patching bytes

| Function | Result |
| --- | --- |
| `byte(a)`, `word(a)`, `dword(a)`, `qword(a)` | Little-endian reads of the current bytes, patches included |
| `bytes(a, n)` | A list of up to `n` bytes, shorter if it runs off the mapped image |
| `original_byte(a)` | The byte from before any patch |
| `patch_byte(a, v)`, `patch_word(a, v)`, `patch_dword(a, v)`, `patch_qword(a, v)` | Writes a value, little-endian |
| `patch_bytes(a, bytes)` | Writes a list like `[0x90, 0x90]` or hex text like `"90 90"` |
| `assemble(a, "xor eax, eax")` | Assembles an instruction and patches it in at `a`, like the Assemble prompt |
| `is_patched(a [, n])` | Whether any of the `n` bytes at `a` is patched |
| `patches()` | `[address, original, value]` for every patched byte |
| `revert(a)`, `revert_all()` | Undoes the patch run touching `a`, or every patch. `revert_all` gives how many bytes it restored |
| `write_patched()` | Writes the patched file next to the original, like File > Export patched |
| `xor_decode(a [, key])` | Decodes XOR-encoded bytes at `a` with a hex key like `"5A"` or `"12 34"`, optionally followed by a length, or `"auto"` to guess the key |
| `string(a)`, `strings()` | The ASCII or UTF-16 text at an address, and every string the loader found |
| `stack_string(a)` | A string built on the stack by the instructions at `a`, if Plumb recognized one |

Patches are saved in the project and the file is re-analyzed after them, the same as patches made by hand. Call `wait()` before reading the new analysis.

### Names, comments, notes and bookmarks

| Function | Result |
| --- | --- |
| `name(a)`, `locate(name)` | The name at an address (user, import, export, symbol or `fn_`), and the address of a name |
| `set_name(a, n)`, `del_name(a)` | Renames. An address inside a function renames the function, unless it's a label. Odd characters become `_` |
| `names()` | `[address, name]` for every name set by hand or by a script |
| `comment(a)`, `set_comment(a, t)`, `del_comment(a)`, `comments()` | Comments, shown in the listing and in the C |
| `note(f)`, `set_note(f, t)`, `del_note(f)`, `notes()` | Function notes, shown in the Notes panel and the report |
| `bookmark(a)`, `set_bookmark(a [, label])`, `del_bookmark(a)`, `bookmarks()` | Bookmarks. Without a label the bookmark is named after the address |
| `prototype(f)`, `set_prototype(f, text)`, `del_prototype(f)`, `prototypes()` | C prototypes like `"int __fastcall parse(char* s, int n)"`, used by the C view and its callers |

Setting a comment, note or name to `""` removes it, the same as the `del_` function.

### Functions and instructions

| Function | Result |
| --- | --- |
| `functions()` | Every function start, in address order |
| `function_at(a)`, `function_end(a)`, `function_size(a)`, `function_name(a)` | The function containing an address |
| `is_function(a)`, `is_code(a)`, `is_label(a)` | Whether a function starts at `a`, an instruction starts at `a`, or `a` is a jump target |
| `function_info(f)` | `[start, end, size, instructions, blocks, callers, flags]` |
| `function_flags(f)` | Words for how the function was found and what it is: `"entry"`, `"export"`, `"tls"`, `"pdata"`, `"called"`, `"pointer"`, `"prologue"`, `"jumped"`, `"symbols"`, `"thunk"`, `"noreturn"`, `"leaf"`, `"named"`, `"virtual"` |
| `instructions(f)` | Every instruction start in the function, sorted |
| `blocks(f)` | `[start, end, instructions, successors, returns]` for each basic block |
| `edges(f)` | `[from, to, kind]` between block starts. Kinds are `"fall"`, `"taken"`, `"not_taken"`, `"jump"` and `"case"`; case edges add the case value |
| `jump_table(a)` | For a switch jump or its table: `[jump, table, count, [targets]]` |
| `head(a)`, `prev(a)`, `next(a)` | The instruction containing `a`, the one before it and the one after |
| `instruction(a)`, `mnemonic(a)`, `size(a)`, `operands(a)` | The instruction as listing text with names, its mnemonic, its length and its operand count |
| `operand_kind(a, n)` | `"register"`, `"immediate"`, `"memory"`, `"address"` or `"other"` |
| `operand_value(a, n)` | The immediate, the displacement, or the target of a RIP-relative or direct operand |
| `operand_register(a, n)`, `operand_size(a, n)` | The register (or base register of a memory operand), and the operand size in bytes |
| `flow(a)`, `target(a)` | `"call"`, `"jump"`, `"branch"`, `"return"`, `"stop"` or `"none"`, and where a call or jump goes |
| `disassemble(a [, lines])` | Listing text for `lines` instructions (20 if left out), with labels |

### Cross-references

| Function | Result |
| --- | --- |
| `refs_to(a)`, `refs_from(a)` | Addresses that refer to `a`, and addresses `a` refers to |
| `refs_to_info(a)`, `refs_from_info(a)` | The same as `[address, kind]`, with kinds such as `"call"`, `"jump"`, `"read"`, `"write"` and `"address"` |
| `callers(f)`, `callees(f)` | Functions that call `f`, and the targets `f` calls (imports included) |

### Decompiler and variables

| Function | Result |
| --- | --- |
| `decompile(f)` | The function's C as text, with the current names, types and settings |
| `c_lines(f)` | `[address, indent, text]` for each line of the C |
| `locals(f)` | `[key, name, type, "local" or "parameter"]` for every variable in the C. `key` is the decompiler's own name (`v3`, `a1`), which never changes |
| `local_name(f, v)`, `set_local_name(f, v, name)` | A variable's name. `v` is the key or the current name. `""` resets it |
| `local_type(f, v)`, `set_local_type(f, v, type)` | A variable's type, like `"uint8_t*"` or `"player_t*"`. `""` goes back to the inferred type |
| `c_number_form(a, value [, n])`, `set_c_number_form(a, value, form [, n])` | How the `n`th `value` on the C line at `a` is shown: `"hex"`, `"dec"`, `"char"`, `"bin"`, or `""` for the default |
| `number_form(a)`, `set_number_form(a, form)` | The same for the immediate in the listing at `a`, with `"float"` too |

Variable and type changes show up in the next `decompile` call, so a script can retype a variable and read the new C straight away.

### Structs

| Function | Result |
| --- | --- |
| `structs()`, `struct_exists(s)` | Every struct name, and whether one exists |
| `struct_create(s)`, `struct_delete(s)`, `struct_rename(s, new)` | Struct lifetime. Names are letters, digits and underscores |
| `struct_size(s)` | The size, up to the end of the last field |
| `struct_fields(s)` | `[offset, name, type, size]` for each field, by offset |
| `struct_field(s, field)` | One field as `[offset, name, type, size]`. `field` is an offset or a name |
| `struct_field_offset(s, name)`, `struct_field_at(s, offset)` | A field's offset, and the field covering an offset |
| `struct_add_field(s, offset, name, type)` | Adds a field and gives its offset. Offset `-1` appends it at the end. A name like `"buf[32]"` makes an array. Any field already at that offset is replaced |
| `struct_del_field(s, field)`, `struct_rename_field(s, field, name)`, `struct_set_field_type(s, field, type)` | Field edits, by offset or name |
| `struct_text(s)` | The struct as a C declaration with offsets |

Types are the built-ins (`int8_t` to `uint64_t`, `char`, `bool`, `float`, `double`, `size_t`, `DWORD`, `HANDLE`...), struct names, and pointers to either. A pointer to a name Plumb doesn't know is allowed, as in C, and treated as an opaque pointer.

```c
struct_create("player_t");
struct_add_field("player_t", 0x00, "vtable", "void*");
struct_add_field("player_t", 0x08, "health", "int32_t");
struct_add_field("player_t", -1, "name[32]", "char");
set_local_type("fn_140012A40", "a1", "player_t*");
print(decompile("fn_140012A40"));
```

### Classes

| Function | Result |
| --- | --- |
| `classes()` | `[name, vtable, methods, locator, offset]` for each class found from RTTI |
| `class_methods(c)` | The function in each vtable slot. `c` is a name or an index into `classes()` |
| `class_bases(c)` | Base class names |
| `vfunc(f)` | `[class, slot]` if `f` sits in a vtable |

### Searching and tools

| Function | Result |
| --- | --- |
| `find_bytes(pattern [, from])` | The first address with those bytes. `"48 8B ?? 05"`, with `??` matching anything |
| `find_all_bytes(pattern [, from [, limit]])` | Every match, up to `limit` (100,000 if left out) |
| `find_signature(pattern [, limit])` | Matches for an IDA-style pattern, like Search > Find signature |
| `make_signature(a [, style])`, `signature_info(a [, style])` | A unique signature for `a` in `"ida"`, `"x64dbg"`, `"code"` or `"array"` style. `make_signature` gives `none` if there's no unique one; `signature_info` gives `[pattern, unique, matches, start, via_reference, offset]` |
| `search(text)` | Search everything, like Ctrl+Shift+F: `[address, length, note]` per hit. `#1234` searches for a number and `c:text` searches the C |
| `query(text)` | Function starts matching a query, like `"calls VirtualAlloc and size > 0x100 and not thunk"` |
| `emulate(f [, setup])` | Runs the function in Plumb's emulator and gives its report. `setup` sets registers, like `"rcx=0x10 rdx=0"` |
| `compare(path)`, `close_compare()` | Compares with another build: `[same, changed, only_here, only_there]` |
| `reanalyze()` | Runs the analysis again. Call `wait()` after it |
| `learn_names()`, `apply_names()` | Teaches the name library this file's names, or applies learned names |
| `is_covered(a)` | Whether a debugger coverage run executed `a` |
| `export_report()`, `export_classes()` | Writes `name.report.md` or the class header `name.classes.h` next to the file |

### Debugger

| Function | Result |
| --- | --- |
| `debug_start()` | Starts the open executable under the debugger. Breakpoints are set first |
| `debug_state()` | `"idle"`, `"running"`, `"paused"`, `"exited"` or `"failed"`. Works with nothing open |
| `debug_wait([ms])` | Waits until the process pauses or ends, up to `ms` (10 seconds if left out), and gives the state |
| `debug_continue()`, `debug_step_into()`, `debug_step_over()` | Resume or step a paused process. Steps wait for the new state |
| `debug_pause()`, `debug_stop()` | Break into or end the process |
| `pc()`, `register(name)`, `registers()` | While paused: the instruction pointer, one register, or `[name, value]` for each |
| `debug_read(a, n)` | Reads `n` bytes of the live process. Image addresses are moved to where the image is loaded |
| `debug_evaluate(text)` | Evaluates a watch expression like `"[rsp+8]"` or `"rcx+0x10"` |
| `breakpoints()`, `add_breakpoint(a [, condition])`, `del_breakpoint(a)` | Breakpoints, optionally conditional |
| `condition(a)`, `set_condition(a, text)` | Breakpoint conditions like `"rax == 0"` or `"rcx > 0x10"`. Setting one adds the breakpoint |

```c
add_breakpoint(entry());
debug_start();
if (debug_wait(5000) == "paused")
    printf("stopped at %s, rsp = %x", hex(pc()), register("rsp"));
debug_continue();
debug_wait();
```

### Views, settings and commands

| Function | Result |
| --- | --- |
| `here()`, `jump(a)` | The caret's address, and moving the caret there with history |
| `back()`, `forward()` | Navigation history. `1` if there was somewhere to go |
| `view(kind)`, `current_view()` | Switches to `"listing"`, `"graph"`, `"c"`, `"hex"`, `"headers"` or `"calls"`, and names the current view |
| `commands()`, `command(label)` | Every menu command's label, and running one by label, like `command("Decompile function")`. `0` if the command isn't available right now |
| `setting(name)`, `set_setting(name, on)` | `"light"`, `"spaced"`, `"no_ternary"`, `"hide_casts"`, `"collapse_locals"`, `"name_virtuals"`, `"show_bytes"`, `"autosave"` and `"jit"` |
| `font(role)`, `set_font(role, face [, size])` | `[face, size]` for `"interface"`, `"disassembly"` or `"c"`. Pass `none` as the face to change only the size |
| `colors()`, `color(name)`, `set_color(name, 0xRRGGBB)`, `reset_color(name)` | Theme colors by key, for the current theme, as in View > Colors |
| `clear_output()` | Empties the output pane |

## Examples

Name every function that calls `VirtualAlloc` after the import:

```c
var target = locate("VirtualAlloc");
var users = callers(target);

for (var i = 0; i < len(users); i++) {
    if (starts(name(users[i]), "fn_"))
        set_name(users[i], format("uses_VirtualAlloc_%d", i));
}

printf("%d functions call VirtualAlloc", len(users));
```

Comment every `xor` that clears a register:

```c
var all = functions();

for (var i = 0; i < len(all); i++) {
    for (var a = all[i]; a != none && a < function_end(all[i]); a = next(a)) {
        if (mnemonic(a) == "xor" && operand_register(a, 0) == operand_register(a, 1))
            set_comment(a, "zero");
    }
}
```

## Speed

Scripts are compiled to x86-64 machine code by Plumb's own JIT when they run.

- **Before compiling:** variables are resolved to fixed slots when the script loads, so compiled code reads and writes them directly.
- **Fast paths:** arithmetic, comparisons, `++`, compound assignment and control flow run inline on numbers. Text, lists and errors drop into the same routines the interpreter uses, so results are identical either way.
- **Timings:** a ten-million-iteration loop takes about 30 ms, and `fib(27)` about 14 ms.
- **Step limit:** scripts stop with an error after 200 million steps, counted per loop iteration, so a runaway loop can't hang Plumb.
- **Fallback:** on ARM64 builds, or if compiling fails, scripts run in the interpreter.
