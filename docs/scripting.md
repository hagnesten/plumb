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

Addresses are numbers, but every function that takes an address also accepts a name (`"CreateFileW"`, `"fn_140001000"`) or `"+1A40"` for an RVA. Functions give `none` when there's nothing there, like the name of an unnamed address or a read from an unmapped one.

| Function | Result |
| --- | --- |
| `here()`, `jump(a)` | The caret's address, and moving the caret |
| `image_base()`, `entry()`, `image_end()` | Image layout |
| `byte(a)`, `word(a)`, `dword(a)`, `qword(a)`, `bytes(a, n)` | Little-endian reads, or a list of bytes |
| `patch_byte(a, v)`, `patch_bytes(a, list or "90 90")`, `assemble(a, "xor eax, eax")` | Patching, kept in the project and re-analyzed like patches made by hand |
| `name(a)`, `set_name(a, n)`, `locate(name)` | Names. `set_name` on an address inside a function renames the function |
| `comment(a)`, `set_comment(a, t)` | Comments, shown in the listing and the C |
| `note(f)`, `set_note(f, t)` | Function notes |
| `prototype(f)`, `set_prototype(f, "int __fastcall f(char* s)")` | C prototypes, used by the C view and callers |
| `functions()` | Every function start |
| `function_at(a)`, `function_end(a)`, `function_size(a)`, `function_name(a)`, `is_function(a)` | The function containing an address |
| `is_code(a)` | Whether an instruction starts at `a` |
| `instruction(a)`, `mnemonic(a)`, `size(a)`, `next(a)`, `operands(a)` | Decoded instructions, as text and their parts |
| `operand_kind(a, n)`, `operand_value(a, n)`, `operand_register(a, n)` | `"register"`, `"immediate"`, `"memory"` or `"address"`, the immediate, displacement or target, and the register or base register |
| `refs_to(a)`, `refs_from(a)`, `callers(f)` | Cross-references as lists of addresses |
| `decompile(f)` | The function's C as text |
| `strings()`, `string(a)` | String addresses, and the ASCII or UTF-16 text at an address |
| `sections()`, `section_at(a)` | `[name, start, end, "rwx"]` for each section, and the section name at an address |
| `imports()`, `exports()` | `[module, name, slot]` and `[name, address]` |
| `find_bytes("48 8B ?? 05" [, from])` | The first address with those bytes, `??` matching anything |
| `save()` | Saves the project |

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
