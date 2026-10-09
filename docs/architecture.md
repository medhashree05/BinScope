# BinScope architecture

BinScope is a small C++17 code base split into four layers. Each layer depends only on the
layers below it.

```
             ┌──────────────────────────────────────────────┐
  argv  ───▶ │ CLI            src/cli.cpp, src/main.cpp     │  argument parsing, exit codes,
             │                                              │  stdout vs stderr
             └──────────────┬───────────────────────────────┘
                            │ ElfFile + decoded tables
             ┌──────────────▼───────────────────────────────┐
             │ Formatter      src/formatter.cpp             │  text tables and explanations
             └──────────────┬───────────────────────────────┘
                            │ FileSummary, ComparisonReport
             ┌──────────────▼───────────────────────────────┐
             │ Analyzer       src/analyzer.cpp              │  summaries, object-vs-linked
             │                                              │  comparison, placement inference
             └──────────────┬───────────────────────────────┘
                            │ Section / Symbol / Relocation records
             ┌──────────────▼───────────────────────────────┐
             │ Parser         src/parser.cpp, elf_file.cpp, │  validated decoding of
             │                sections.cpp, symbols.cpp,    │  untrusted bytes
             │                relocations.cpp               │
             ├──────────────────────────────────────────────┤
             │ ByteView       src/byte_reader.cpp           │  bounds-checked LE reads
             └──────────────────────────────────────────────┘
```

Everything except `main()` is built into the `binscope_core` static library. The tests link
that library directly, so they can test the parser on in-memory byte buffers and drive the
CLI through `cli::run()` with string streams.

## Data flow

1. **Input.** `ElfFile::load(path)` reads the whole file into a `std::vector<uint8_t>`. Paths
   are `std::filesystem::path`, so names containing spaces work. Missing files, directories
   and unreadable files raise `IoError`.
2. **Eager structural parsing.** The `ElfFile` constructor decodes three things, and a
   successfully constructed `ElfFile` always has a valid header and section table:
   * the ELF header (`parse_elf_header`);
   * the section header table, including extended numbering and section names
     (`parse_section_headers`);
   * the program header table (`parse_program_headers`).
3. **On-demand table parsing.** Symbol tables (`parse_symbol_tables`) and relocation sections
   (`parse_relocation_sections`) are decoded only when a command needs them. A corrupt
   `.symtab` therefore doesn't stop `inspect` or `sections` from working. Relocation parsing
   decodes each linked symbol table once and resolves every `r_info` symbol index against it.
4. **Analysis.** Two functions build plain data structures with no formatting:
   * `summarize()` builds the per-file overview that `inspect` shows.
   * `compare_files()` matches sections, symbols and relocation sections between two files.
     It classifies each symbol and infers where input sections were placed.
5. **Formatting.** The formatter turns those structures into column-aligned text through
   `TextTable`, and adds explanations: what a section holds, what a relocation formula means,
   and why two values can't be compared.
6. **Output.** Reports go to stdout. Errors and warnings go to stderr, prefixed with
   `binscope: error:` or `binscope: warning:`.

## Module responsibilities

| Module | Header | Responsibility |
|--------|--------|----------------|
| Byte reader | `byte_reader.hpp` | `ByteView`: non-owning view with checked `u8/u16/u32/u64/i64` little-endian reads, `subview`, `c_string`; `checked_add` / `checked_mul` |
| Errors | `error.hpp` | `IoError`, `ParseError` (malformed), `UnsupportedError`, all derived from `binscope::Error` |
| Constants | `elf_constants.hpp` | gABI / x86-64 psABI constants, defined locally (checked against `<elf.h>` by a test) |
| Header | `elf_header.hpp` | `ElfHeader`, `ProgramHeader`, name decoding for header fields |
| Sections | `section.hpp` | `Section`, type/flag names, explanations of well-known sections |
| Parser | `parser.hpp` | header, section table, program header decoding and validation |
| ELF file | `elf_file.hpp` | owns the bytes; gives access to sections; `section_data()` refuses `NOBITS` and out-of-bounds sections |
| Symbols | `symbol.hpp` | `Symbol`, `SymbolTable`, `SymbolFilter`, decoding, demangling |
| Relocations | `relocation.hpp` | `Relocation`, `RelocationSection`, REL/RELA decoding, x86-64 type names, formulas and explanations |
| Analyzer | `analyzer.hpp` | `FileSummary`, `ComparisonReport`, symbol-version splitting |
| Formatter | `formatter.hpp` | `TextTable` and the report printers for each command |
| CLI | `cli.hpp` | command dispatch, option parsing, exit codes |

## Input validation

The input file is treated as hostile. In practice that means the following:

* **No struct overlays.** Fields are decoded one at a time from little-endian bytes at fixed
  offsets. File memory is never `reinterpret_cast` to `Elf64_*` structs, which rules out
  unaligned access, strict-aliasing violations and host-endianness assumptions.
* **Every read is range-checked.** `ByteView::contains(offset, length)` is written as
  `offset <= size && length <= size - offset`, so the check itself can't overflow. Table
  sizes are computed with `checked_mul` before use.
* **Counts are validated before allocation.** A table's full extent (`count × entry size`)
  must fit in the file before `reserve()` is called. A forged `e_shnum` or `sh_size` can
  therefore never trigger a huge allocation.
* **Entry sizes are verified:** `e_shentsize == 64`, `e_phentsize == 56`, symbol
  `sh_entsize == 24`, REL `16`, RELA `24`, and section sizes must be whole multiples of the
  entry size.
* **Cross references are verified.** A symbol table's `sh_link` must name an `SHT_STRTAB`
  section. A relocation section's `sh_link` must name `SHT_SYMTAB` or `SHT_DYNSYM`. Symbol
  and section indices are checked against the real table sizes.
* **Strings are bounded by their section.** `c_string()` looks for the NUL terminator only
  inside the string table's own bytes.
* **`SHT_NOBITS` contents are never read.**

## Error handling policy

| Situation | Behaviour |
|-----------|-----------|
| File cannot be read | `IoError`, exit status 1 |
| Not ELF / truncated / structurally inconsistent header or section table | `ParseError`, exit status 1 |
| ELF32, big-endian, `SHN_XINDEX` symbol indices, `SHT_RELR` contents | `UnsupportedError`, or a warning plus a clear note in the report |
| A whole table is unusable (wrong entry size, contents outside the file, bad `sh_link`) | `ParseError` for the command that needs the table; `inspect` and `compare` record it and continue with the rest |
| A single entry is bad (name offset outside the string table, section or symbol index out of range) | the entry is kept with a `<corrupt …>` / `<invalid …>` placeholder, and a warning goes to stderr |
| Bad command line | usage message on stderr, exit status 2 |

Exceptions are caught in exactly one place, `cli::run()`. There, each exception becomes a
message prefixed with its kind (`I/O error`, `malformed ELF`, `unsupported`).

## Testing approach

* **Fixtures are compiled during the build** from `tests/fixtures/*.cpp` by the same
  compiler, so no binaries are committed. The set covers objects at `-O0` and `-O2`, a PIE,
  a non-PIE `ET_EXEC`, a shared library, and stripped variants.
* **Malformed inputs are made in memory.** Tests patch fixture bytes using their *own*
  fixed-offset ELF helpers (`tests/fixture_util.hpp`), which never call the parser under
  test. Three deterministic mutation loops of 3,000 iterations each cover the header and
  section table, the symbol table, and the relocation entries. Another test truncates an
  object file at every possible length.
* **readelf is an independent oracle.** `test_readelf_crosscheck` runs GNU `readelf` only
  inside the test and compares sections, symbols and relocations field by field against the
  parsed records.
* **Sanitizers.** `-DBINSCOPE_ENABLE_SANITIZERS=ON` builds everything with ASan and UBSan,
  and makes the first error fatal (`-fno-sanitize-recover=all`).
