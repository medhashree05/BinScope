# BinScope: ELF Binary & Linker Analysis Toolkit

BinScope is a command-line tool, written in C++17, that reads ELF64 object files,
executables and shared libraries directly, and explains what the compiler and linker
produced. It decodes headers, sections, symbols and relocations with its own bounds-checked
parser, and it compares an object file with its linked output to show what linking changed.

```
$ binscope compare math_main.o math_program
...
  Resolved by the static linker (undefined before, defined after): 3
    Name             Before  After                         Note
    _Z3addii         UND     0x11a4 in .text (FUNC, 39 B)  definition linked in
    _Z8multiplyii    UND     0x11cb in .text (FUNC, 38 B)  definition linked in
    math_call_count  UND     0x4014 in .bss (OBJECT, 4 B)  definition linked in

  Left for the dynamic loader (still undefined after, but a dynamic symbol): 1
    printf  UND     UND @GLIBC_2.2.5 [dynsym]  bound at load/run time to version GLIBC_2.2.5
```

> BinScope is an analysis tool. It is **not** a linker, loader or debugger, and it doesn't
> implement the whole ELF specification. [Supported formats and limitations](#supported-formats-and-limitations)
> lists exactly what it handles.

## Why this project exists

Linker errors like "undefined reference to `add(int, int)`", and questions like "why does my
optimized build have *more* sections?", are hard to reason about without seeing the data
structures involved. Tools like `readelf` and `objdump` show those structures, but they don't
explain them or connect an object file to the executable built from it. BinScope does both:

* every report explains what it shows (section purposes, relocation formulas, what a symbol
  value means in this kind of file);
* `compare` sorts every symbol into the step of the build that dealt with it: resolved by the
  static linker, deferred to the dynamic loader, or unmatched;
* it infers where the linker placed each input section. This was checked against GNU ld's
  map file in [the experiments](docs/linker-experiments.md#experiment-a-one-translation-unit).

## Features

| Command | What it shows |
|---------|---------------|
| `binscope inspect <file>` | ELF header (class, byte order, type, machine, entry point, table locations), program headers and interpreter, and a summary of symbol and relocation tables |
| `binscope sections <file>` | section table: names, types, flags, addresses, offsets, sizes, entry sizes, link/info, alignment, plus what each well-known section holds |
| `binscope symbols <file>` | `.symtab` and `.dynsym`: value, size, type, binding, visibility, section, defined/undefined status. Filters: `--defined`, `--undefined`, `--functions`; `--demangle` for C++ names |
| `binscope relocs <file>` | REL and RELA sections: offset, type, symbol, addend (explicit or implicit), target section, and the x86-64 psABI formula and meaning of each type used |
| `binscope compare <before> <after>` | section, symbol and relocation differences, with each symbol classified and input-section placement inferred |

Engineering highlights:

* **A real parser, not a wrapper.** BinScope never runs `readelf` or `objdump`. The whole tool
  is about 3,300 lines of C++17 with no dependencies beyond the standard library.
* **Built for hostile input.** Every offset, count, entry size and string reference is
  checked against the actual buffer, with overflow-safe arithmetic. No file bytes are cast to
  structs. Tests cover truncation at every length, 9,000 deterministic random mutations, and
  more than 30 targeted corruption cases. Everything also runs under ASan and UBSan.
* **Checked against an independent tool.** A test compares every section, symbol and
  relocation of every fixture field by field with GNU `readelf`.
* **Clear failure modes.** I/O errors, malformed files and unsupported features are separate
  error kinds with separate messages, and the exit status is 0, 1 or 2 (success, analysis
  failure, usage error).

## Supported formats and limitations

**Supported:** ELF64 little-endian files of type `ET_REL`, `ET_EXEC` and `ET_DYN`
(PIE executables and shared objects). Relocation types are named and explained for
**x86-64**. Sections, symbols and raw relocation fields decode for any ELF64 LE machine.
Extended section and program-header numbering (`e_shnum == 0`, `SHN_XINDEX`, `PN_XNUM`) is
supported.

**Not supported. BinScope reports these explicitly and never guesses:**

* ELF32 and big-endian files: rejected with an `unsupported` error.
* Relocation names for architectures other than x86-64: shown as `type N`.
* `SHT_RELR` packed relocations: detected and reported, not decoded.
* Symbols whose section index is `SHN_XINDEX` (needs `SHT_SYMTAB_SHNDX`): reported with a
  warning, not resolved.
* GNU symbol versioning tables (`.gnu.version*`) are not decoded. Version suffixes appear
  only where the linker wrote them into `.symtab` names (e.g. `puts@GLIBC_2.2.5`).
* No DWARF, `.dynamic` entry, note or compressed-section (`SHF_COMPRESSED`) decoding, and no
  disassembly.
* Relocation formulas are *described*, not *applied*. BinScope doesn't compute final patched
  values.
* `compare` matches symbols by name. Names that occur more than once in a table are reported
  as "cannot be matched confidently", not paired up.

## Architecture

```
CLI (cli.cpp) ─▶ Formatter (formatter.cpp) ─▶ Analyzer (analyzer.cpp)
                                                  │
         Parser: parser.cpp · elf_file.cpp · sections.cpp · symbols.cpp · relocations.cpp
                                                  │
                         ByteView (byte_reader.cpp): bounds-checked little-endian reads
```

| Layer | Responsibility |
|-------|----------------|
| `ByteView` | non-owning byte view; every read is range-checked; overflow-safe `checked_add` / `checked_mul` |
| Parser | decodes and validates the header, section and program header tables (eagerly), and symbol and relocation tables (on demand) |
| Analyzer | per-file summaries; object-vs-linked comparison and placement inference |
| Formatter | aligned text tables and explanations |
| CLI | argument parsing, exit codes; reports on stdout, diagnostics on stderr |

[docs/architecture.md](docs/architecture.md) covers the data flow, validation rules and
error-handling policy.

## Prerequisites

* Linux (tested on Ubuntu 26.04 under WSL2, x86-64)
* A C++17 compiler (tested with GCC 15.2)
* CMake ≥ 3.16 (tested with 4.2.3), including CTest
* For the tests: `g++` to compile fixtures, and optionally binutils `strip` and `readelf` (the
  tests that need them are skipped if they are missing)

On Ubuntu or Debian:

```bash
sudo apt install build-essential cmake
```

## Building

```bash
git clone https://github.com/medhashree05/BinScope.git
cd BinScope
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/binscope --help
```

Optional install: `cmake --install build --prefix ~/.local`.

CMake options:

| Option | Default | Effect |
|--------|---------|--------|
| `BINSCOPE_BUILD_TESTS` | `ON` | build the test suite and fixtures |
| `BINSCOPE_ENABLE_SANITIZERS` | `OFF` | build with `-fsanitize=address,undefined` |
| `BINSCOPE_WARNINGS_AS_ERRORS` | `OFF` | add `-Werror` |

The code compiles cleanly with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion -Wold-style-cast -Wnull-dereference`.

## Usage

```
binscope inspect <file>
binscope sections <file>
binscope symbols <file> [--defined | --undefined] [--functions] [--demangle]
binscope relocs <file> [--demangle]
binscope compare <before> <after> [--demangle]
binscope --help | --version
```

Use `--` before a file name that starts with `-`. Paths containing spaces work as usual when
quoted.

### Example output

All of the output below was produced by BinScope on files built from
[`examples/`](examples/) with GCC 15.2. Some tables are shortened.

```bash
g++ -c examples/math_utils.cpp -o math_utils.o
g++ -c examples/math_main.cpp  -o math_main.o
g++ math_main.o math_utils.o -o math_program
```

**Header and summary (`inspect`)**

```
$ binscope inspect math_main.o
ELF header
  Magic:                            7f 45 4c 46 (valid ELF)
  Class:                            ELF64
  Data encoding:                    little-endian (two's complement)
  Type:                             REL (relocatable object file)
  Machine:                          x86-64 (AMD64)
  Entry point:                      0x0 (none)
  Section headers:                  14 entries at file offset 0x360 (864), 64 bytes each
  Section-name string table index:  13 (.shstrtab)
  ...
Summary
  Kind: relocatable object - input to the linker; addresses are not assigned yet
  Symbol table .symtab: 9 entries (4 defined, 4 undefined)
  Relocation section .rela.text: 5 entries, RELA (explicit addend), patches .text
  Relocation section .rela.eh_frame: 1 entry, RELA (explicit addend), patches .eh_frame
```

**Sections**

```
$ binscope sections math_main.o
  Nr  Name                Type      Flags  Address             Offset     Size      EntSize  Link  Info  Align
   1  .text               PROGBITS  AX     0x0000000000000000  0x000040   0x00005b              0     0      1
   2  .rela.text          RELA      I      0x0000000000000000  0x000258   0x000078  0x18       11     1      8
   3  .data               PROGBITS  WA     0x0000000000000000  0x00009b   0x000000              0     0      1
   4  .bss                NOBITS    WA     0x0000000000000000  0x00009b*  0x000000              0     0      1
   5  .rodata             PROGBITS  A      0x0000000000000000  0x00009b   0x00001c              0     0      1
  ...
  *  NOBITS: occupies memory at run time but no bytes in the file
What these sections hold
  .text               executable machine code
  .rela.text          relocations with explicit addends for the named section
  ...
```

**Undefined symbols**

```
$ binscope symbols math_main.o --undefined --demangle
  showing 4 of 9 entries (filter: undefined)
  Num  Value               Size  Type    Bind    Vis      Ndx  Section  Status     Name
    5  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  add(int, int)
    6  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  multiply(int, int)
    7  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  math_call_count
    8  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  printf
```

**Relocations**

```
$ binscope relocs math_main.o
Relocation section '.rela.text' [2]: 5 entries, RELA (explicit addend)
  patches section [1] '.text'; symbols from [11] '.symtab'
  Idx  Offset          Type            Sym#  Sym.Value           Symbol           Addend
    0  0x000000000017  R_X86_64_PLT32     5  0x0000000000000000  _Z3addii           -0x4
    1  0x000000000029  R_X86_64_PLT32     6  0x0000000000000000  _Z8multiplyii      -0x4
    2  0x000000000032  R_X86_64_PC32      7  0x0000000000000000  math_call_count    -0x4
    3  0x00000000003f  R_X86_64_PC32      3  0x0000000000000000  .rodata            -0x4
    4  0x00000000004b  R_X86_64_PLT32     8  0x0000000000000000  printf             -0x4
...
Relocation types used
  Type            Count  Calculation  Meaning
  R_X86_64_PC32       3  S + A - P    32-bit displacement from the patched location (PC-relative) to the symbol
  R_X86_64_PLT32      3  L + A - P    PC-relative call target via the PLT (direct if the symbol is resolved locally)
```

**Errors** go to stderr, with a non-zero exit status:

```
$ binscope inspect notes.txt          # exit status 1
binscope: error: malformed ELF: not an ELF file (bad magic number; expected 7f 45 4c 46)
$ binscope inspect truncated.o        # exit status 1
binscope: error: malformed ELF: truncated ELF header: file has 40 bytes, an ELF64 header needs 64
$ binscope symbols                    # exit status 2
binscope: error: 'symbols' needs a file argument
usage: binscope symbols <file> [--defined | --undefined] [--functions] [--demangle]
```

## Object file vs. executable: what linking changes

`binscope compare math_main.o math_program` (full output and discussion in
[docs/linker-experiments.md](docs/linker-experiments.md#experiment-b-an-unresolved-external-symbol)):

* The three references to `math_utils.o` (`add`, `multiply`, `math_call_count`) go from
  `UND` to addresses in `.text` and `.bss`. **The static linker resolved them.**
* `printf` stays undefined and becomes a dynamic symbol bound to `GLIBC_2.2.5`. **It is
  resolved by the dynamic loader at run time.**
* `.rela.text` disappears because the linker *applied* those relocations. But `.rela.dyn` (8
  entries) and `.rela.plt` (1 entry) appear, so the linked program has **more** relocation
  entries (9) than the object file had (6).
* Symbol values change meaning, from section offsets to virtual addresses. BinScope shows
  both and doesn't subtract them blindly. Instead it infers each input section's placement
  where every matched symbol agrees.

The experiments document also covers a single translation unit, static vs. dynamic linking
(even the static binary keeps 22 `IRELATIVE` relocations), and `-O0` vs `-O2`.

## Testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

# Release, and ASan + UBSan:
scripts/build_and_test.sh release
scripts/build_and_test.sh sanitize
scripts/build_and_test.sh all        # Debug + Release + sanitizers
```

The build compiles small fixture programs from [`tests/fixtures/`](tests/fixtures/) with the
system compiler, so the repository contains no binaries. The fixtures are:
* objects at `-O0` and `-O2`, and a data-only object;
* a PIE (`ET_DYN`) and a non-PIE (`ET_EXEC`) executable;
* a shared library;
* stripped variants with no `.symtab`.

| Test | Covers |
|------|--------|
| `test_parser` | byte reader and overflow checks; headers of object, PIE, non-PIE and shared object; `NOBITS`; extended numbering |
| `test_sections` | section types, flags, links, explanations, `sections` command |
| `test_symbols` | binding/type/visibility, undefined externals, filters, stripped files, demangling |
| `test_relocations` | RELA and REL decoding, symbol resolution, x86-64 names and formulas, malformed relocation sections, mutation loop |
| `test_compare` | object vs. PIE / non-PIE / object, placement inference, ambiguous names, incomplete comparisons |
| `test_malformed_files` | empty, text, random, truncated (at every length), ELF32, big-endian, bad versions, bad offsets, counts and entry sizes, corrupt string and symbol tables, mutation loops |
| `test_cli` | exit codes, stdout/stderr separation, missing arguments, unknown options, directories, paths with spaces and leading dashes |
| `test_constants` | compile-time check of every constant against the system `<elf.h>` |
| `test_readelf_crosscheck` | field-by-field agreement with GNU `readelf` for all fixtures |

The CTest suite has 13 entries: the 9 test programs above, plus 4 end-to-end runs of the
binary that check exit codes. The suite has been run and passes in Debug, Release and
ASan/UBSan builds on the environment listed under [Prerequisites](#prerequisites). The
fixture tests assume an x86-64 Linux toolchain.

## Project layout

```
include/binscope/   public headers (one per module)
src/                parser, analyzer, formatter and CLI implementation
tests/              unit tests, test harness, fixture sources
examples/           programs used in docs/linker-experiments.md
docs/               architecture, ELF fundamentals, linker experiments
scripts/            build_and_test.sh
```

## Future improvements

* Decode `.dynamic` (`DT_NEEDED`, `DT_FLAGS`, `DT_RELA*`) and GNU symbol versioning, so
  `.dynsym` entries show their required library and version.
* Decode `SHT_RELR` and `SHT_SYMTAB_SHNDX`.
* Optionally *apply* relocation formulas to show resolved call targets, cross-checked against
  disassembly.
* AArch64 relocation tables, plus ELF32 and big-endian support.
* JSON output for scripting, and a libFuzzer harness alongside the deterministic mutation
  tests.
* Look at NVIDIA CUDA device ELF (`EM_CUDA`) objects embedded in fat binaries. BinScope
  already recognizes the machine type. Their section and relocation structure is the next
  natural step toward PTX/SASS toolchain analysis.

## What this project teaches about compilation and linking

* A compiler emits **relocatable objects**: code and data with placeholders, plus a
  symbol table that names what the file defines and what it needs from elsewhere.
* **Relocations** are the contract between compiler and linker. Each says *where* to patch,
  *which* symbol to use and *how* to compute the value. "Undefined reference" errors are
  relocations the linker couldn't satisfy, and the offsets in the error message are the
  relocation offsets.
* The **static linker** merges sections, assigns addresses, resolves symbols and applies
  relocations. It hands whatever involves a shared library to the **dynamic loader**, through
  `.dynsym`, `.rela.dyn` and `.rela.plt`.
* Whether a number is a section offset, a file offset or a virtual address depends on the
  file type. Comparing them correctly means knowing which one you have.
* Optimization changes structure in ways you can't always predict. In these experiments it
  added sections, inlined functions away, and swapped `printf` for `__printf_chk`.
