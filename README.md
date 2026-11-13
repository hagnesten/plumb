<img src="docs/media/icon.png" width="64" align="right" alt="">

# Plumb

A disassembler, decompiler and debugger for Windows executables. Plumb is written in C++ with no C runtime and no third-party code, and the whole program is a single executable.

![The listing with the inspector tab on the left and the output pane below](docs/media/listing.png)

## What it does

### Disassembly

- Table-driven x86 and x86-64 decoder for the legacy, VEX and EVEX encodings. That covers all of AVX-512 (including FP16, BF16, VNNI, VBMI and IFMA), AMX, AVX-VNNI and AVX-IFMA, SHA and SHA512, SM3 and SM4, GFNI, VAES, Key Locker, CET, TSX, SGX, VMX, SVM, SEV-SNP, TDX and FRED, plus AMD's 3DNow!, FMA4 and SSE4a. Opmasks, zeroing, embedded rounding and SAE, broadcasts, compressed displacements and VSIB addressing are all decoded.
- The VEX and EVEX tables are generated from the encodings themselves instead of being typed in by hand. Every opcode, prefix and operand form in the legacy, VEX and EVEX maps was decoded in 32-bit and 64-bit mode and compared with llvm-objdump, with dumpbin settling disagreements. Of the 689,717 encodings both accept, 688,183 come out the same. In the other 1,534, Plumb follows the Intel manual where llvm doesn't, for example 66-prefixed jumps in 64-bit mode and the broadcast displacement of `vgf2p8affineqb`.
- Across 12.7 million instructions from 11 Windows system binaries (ntdll, kernelbase, ntoskrnl, onnxruntime, DirectML and others, 32-bit and 64-bit), 99.992% match llvm-objdump exactly, and ntdll matches 100%. Most of the rest are encodings llvm rejects but the processor runs, such as the `F6 /1` form of `test` and the `D0 /6` form of `sal`, found in data that sits inside code sections.
- Not supported yet: Intel APX (REX2 and the EVEX-promoted general-purpose instructions), AMD XOP and the Xeon Phi only AVX-512PF prefetches.
- Text listing with full-width addresses, labels, function boundaries and `end of` markers, alignment padding, string and import rows, switch tables and vtables.
- The listing behaves like a text editor. Click to place a caret, then drag or Shift-click to select any span (a register, an operand or whole lines) and Ctrl+C copies exactly that. Double-click a name to follow it, or anything else to select the word.
- Opcode bytes are hidden by default. Turn them on under Window.
- Number display per instruction: hex or decimal (H), character (R), binary (B) and float (F). Immediates that look like floats get a `float 1.0` hint.
- Graph view (Space) with layered layout, colored edges (taken, not taken, unconditional, switch case), zoom, pan and a minimap.
- Stack strings built from immediate stores, including the `mov reg, imm` then store pattern compilers emit, are put back together and shown next to the first store.
- Clicking a register, name or number marks every use of it in the listing, graph and C. Registers match across sizes, so `rcx` also marks `ecx` and `cl`.
- Hovering a call or jump target for a moment shows a peek of its first instructions, its string or its import. The pin keeps it open in the corner while you keep reading.
- The scrollbar doubles as a map of the image: section kinds down one edge, and ticks for the cursor, bookmarks, breakpoints, patches and search hits.

### Patching

- Ctrl+Alt+A assembles an instruction over the one under the caret. The box starts with the current instruction, takes names and addresses as operands, and fills leftover bytes with `nop`. If the new code is longer, you're told what it overwrote.
- Ctrl+Alt+B writes raw hex bytes, from the listing or the hex view. Ctrl+Alt+R reverts the patched run under the caret.
- Patched addresses turn yellow, and the image is analyzed again in the background so functions, references and the C view follow the change.
- Patches are part of the project and of undo and redo. Patch > Write patched copy saves `name.patched.exe` beside the original, or under `%APPDATA%\Plumb\patched` when that folder is read-only. The original file is never touched.

### Names and symbols

- Matching PDB files load on open, from beside the binary, from the path recorded in it, or from local symbol caches (`_NT_SYMBOL_PATH`, `C:\symbols`, `C:\ProgramData\dbg\sym`). Public symbols and every procedure in the module streams become function names, and globals get their real names. The PDB reader is Plumb's own.
- Tools > Learn names from this file adds every named function to a name library, keyed by a hash of its body with relocations masked out. Every file opened afterwards gets matching functions named automatically. Learning from a build with symbols names the same functions in a stripped build of related code.

### Analysis

- Multithreaded recursive descent seeded from the entry point, exports, TLS callbacks, `.pdata`, code pointers in data (including vtable runs) and prologue scans. A 19 MB code section is done in about 150 ms on 16 threads.
- Cross-references for calls, jumps, reads, writes and pointers. X lists the references to the name under the caret, whether that's a call target, a global or the function you're in.
- MSVC switch table recovery: image-relative, table-relative, pointer tables and byte index tables.
- RTTI class recovery: class names, base class chains, vtables and virtual functions, named `Class::vftable` and `Class::vfunc_N` throughout. The Classes panel (Alt+6) lists them all.
- Tools > Export classes as C++ writes a header with every recovered class: its direct bases, its virtual functions in vtable order using your prototypes, and which slots are inherited. It's also copied to the clipboard.
- Call graph (Ctrl+4) shows two levels of callers and callees around a function. Double-click a node to center it, Enter opens it in the disassembly.
- Function queries in the command line: `functions where calls VirtualAlloc and size > 0x100 and not thunk`. Conditions are `calls`, `calledby`, `refs` (strings or names), `name`, `size`, `callers`, `blocks`, `instructions`, `thunk`, `leaf`, `noreturn`, `exported`, `entry`, `named`, `noted`, `typed` and `covered`, each optionally prefixed with `not`.
- Search everything (Ctrl+Shift+F) looks through the text of every instruction. `#0x40` finds immediates and displacements with that value, and `c:text` searches the C of every function.
- XOR decoding (Ctrl+Alt+X) takes a key in hex, with an optional length, or `auto` to try every single-byte key. The decoded text is logged and added as a comment.
- Signature maker (Ctrl+Alt+S) that finds the shortest unique pattern and wildcards relocatable bytes, with output in IDA, x64dbg, code and mask, or byte array form. The signature finder (Ctrl+Alt+F) accepts the same formats or plain text.

### Comparing builds

- Tools > Compare with another build analyzes a second version of the file and pairs up its functions: first by identical bodies, then by name, then by position between matched neighbours. The Compare panel lists every function as same, changed, only here or only there, with the size difference.

### Emulation

- Ctrl+Alt+E runs the function under the cursor in Plumb's own x86 and x86-64 emulator, without starting the program. Calls inside the image are followed, imports return zero and are listed with their arguments, and starting registers can be given like `rcx=0x40 rdx=5`.
- Readable text the function writes is reported with the instruction that wrote it and added there as a comment, which recovers strings decrypted at run time. The final result shows API hashes and other computed values.

### Decompiler

- F5 shows a function as C. Tab switches between C and disassembly, and each function can have its own tab.
- Structured output: `if`/`else` chains, `while`, `do`/`while`, `switch` and `goto` only as a last resort. Dead stores and duplicate expressions are removed, and stack arguments are matched to calls.
- Inferred struct layouts for pointer parameters, and float literals where the types show them.
- Rename a variable (N) or set its type (Y), then the function decompiles again with that type. Y on the function name edits the whole prototype, including return type, name and parameters. Callers pick up the new argument count, and bad types produce a warning instead of failing silently.
- User-defined structs. Press S on a pointer variable to build a struct from how the function uses it, then rename fields (N) and retype them (Y) right in the C. Structs can also be built and edited in the Structures panel (Alt+7), and any variable typed `Name*` reads as `v->field`.
- Comments (;) show up in both the disassembly and the C.
- Line numbers, VS-style coloring, and an optional `call ( arg )` spacing style.
- Tools > Write ternaries as if and else turns every `a ? b : c` into an `if`/`else`. Nested ones are moved into a temporary filled just before the statement. The only ones left are inside `while` and `else if` conditions, where moving them would change when they run.

### Debugger

- F9 starts the program and stops at its entry point. F7 steps into, F8 steps over, F12 pauses and Ctrl+F2 stops.
- F2 toggles a breakpoint, which shows as a dot in the listing. Breakpoints are kept with the project and moved to the runtime base when ASLR relocates the image.
- Registers appear in the inspector tab while paused, the current instruction is marked in the listing, and `OutputDebugString` messages go to the output pane.
- Shift+F2 sets a condition on a breakpoint, like `rax == 0` or `rcx > 0x10`. The program only stops when it holds.
- While paused, the inspector shows the call stack, the top of the stack with return addresses named, and watch expressions like `[rsp+0x28]` or `g_count+8`.
- Shift+F9 runs the program with coverage. Every basic block is armed once, the listing marks the instructions that ran, and `functions where covered` lists the functions that did.
- Works on 64-bit and 32-bit (WOW64) executables.

### Workspace

- Up to 16 tabs across two split panes. Each tab is a listing, graph, C, hex, headers or call graph view.
- The browser on the left holds functions, imports, exports, strings, sections, classes, structures, references, search results and the inspector as tabs along its edge. The inspector (Ctrl+I) shows the address, RVA, file offset and section under the caret, the bytes there read as every integer and float type, the image details and, while debugging, the registers.
- The browser and output windows dock to the left, right or bottom: drag a window's header to move it. Window > Reset window layout puts everything back.
- Ctrl+S saves the project next to the binary under the same name, so `C:\tools\app.exe` gets `C:\tools\app.plumb`. It holds names, comments, bookmarks, number formats, variable names, types, prototypes, structs, breakpoints and patches, and opening that file opens the binary with everything restored. Projects saved by older versions as `app.exe.plumb` still load and move to the new name on the next save. If `app.plumb` already belongs to another binary in the same folder, such as `app.dll` next to `app.exe`, that one is saved as `app.dll.plumb` instead. Autosave is a setting in the File menu, and closing with unsaved changes asks first.
- Undo and redo (Ctrl+Z, Ctrl+Y) for every edit.
- Shift+N writes a note on what a function does. Notes show in the function header and the peek, and the Notes panel (Alt+0) lists every note, bookmark and comment in one place.
- Tools > Export report writes a Markdown report beside the binary with the file summary, sections, notes, named functions with prototypes, bookmarks, comments, patches, classes and imported modules.
- Browser lists sort by clicking a column header.
- Command palette (Ctrl+Shift+P), go to address or name (G), back and forward history, and a command line in the output pane.
- Dark and light themes.

| | |
| --- | --- |
| ![C view](docs/media/pseudocode.png) | ![Graph view](docs/media/graph.png) |
| ![Classes recovered from RTTI, with a vtable in the listing](docs/media/classes.png) | ![Paused at a breakpoint, with registers in the inspector tab](docs/media/debugger.png) |
| ![Call graph](docs/media/calls.png) | ![An instruction assembled over the original, with the patched bytes in yellow](docs/media/patch.png) |

![The light theme with C and disassembly side by side](docs/media/light.png)

## AI assistants

`Plumb.exe --mcp [file]` runs Plumb without a window as an MCP server over standard input and output, so an AI assistant can explore a binary with the same analysis, names and decompiler as the app. With Claude Code:

```
claude mcp add plumb -- C:\tools\Plumb.exe --mcp
```

Other clients take the same command in their MCP settings, for example `{ "command": "C:\\tools\\Plumb.exe", "args": [ "--mcp" ] }`.

The tools are `open_binary`, `binary_info`, `list_functions`, `disassemble`, `decompile`, `xrefs_to`, `calls_from`, `list_strings`, `list_imports`, `list_exports`, `search_code`, `query_functions`, `read_bytes`, `emulate`, `compare_with`, `rename`, `set_comment`, `set_note`, `set_prototype` and `save_project`. Edits made by the assistant go into the same `.plumb` project the app opens.

## Keys

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| G | Go to address or name | F5 | Decompile |
| Space | Graph or text | Tab | Switch between C and disassembly |
| X | References | N | Rename |
| Y | Set type or prototype | ; | Comment |
| H R B F | Number format | Ctrl+D | Bookmark |
| F9 | Start or continue | F7 / F8 | Step into / over |
| F2 | Breakpoint | Ctrl+F2 | Stop debugging |
| Ctrl+Alt+S | Make signature | Ctrl+Alt+F | Find signature |
| S | Struct from variable | Alt+7 | Structures |
| Ctrl+Alt+A | Assemble | Ctrl+Alt+B | Patch bytes |
| Ctrl+Alt+R | Revert patch | Ctrl+Alt+X | Decode XOR |
| Ctrl+Shift+F | Search everything | Ctrl+4 | Call graph |
| Ctrl+I | Inspector | Alt+1 to Alt+9 | Browser tabs |
| Ctrl+B | Show or hide the browser | Ctrl+J | Show or hide the output |
| Ctrl+S | Save project | Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+1 / 2 / 3 | New listing, hex or headers tab | Ctrl+\\ | Split |
| Shift+N | Function note | Alt+0 | Notes and bookmarks |
| Shift+F9 | Run with coverage | Shift+F2 | Breakpoint condition |
| Ctrl+Alt+E | Emulate function | | |

## Building

You need Visual Studio 2022 (any edition, or the Build Tools) with the **Desktop development with C++** workload and **C++ Clang tools for Windows**, plus a Windows 10 or 11 SDK.

Open `plumb.sln` and build the Release x64 configuration, or from a developer prompt:

```
msbuild plumb.sln -p:Configuration=Release -p:Platform=x64
```

The executable lands in `build\release\Plumb.exe`. Pass a file path to open it on startup, or drop a file on the window. `--software` renders on the CPU when the graphics driver misbehaves.

## Source

- `src/runtime` Entry point, allocator and compiler support routines that replace the C runtime
- `src/base` Containers, text formatting, math and file I/O
- `src/loader` PE parsing, imports and exports, string scanning and address translation
- `src/x86` Decoder, Intel-syntax formatter and assembler
- `src/analysis` Function discovery, cross-references, switch tables, RTTI, graphs and signatures
- `src/decompiler` Lifting, data flow cleanup, control flow structuring, C output and type parsing
- `src/debugger` The Win32 debug loop
- `src/render` Direct3D 11 renderer, DirectWrite text and shaders
- `src/ui` Views, panels, docking, menus and commands
- `src/host` The window, input and frame pacing