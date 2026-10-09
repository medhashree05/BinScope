# ELF fundamentals

This is a short guide to the parts of the ELF format that BinScope decodes. It covers ELF64
little-endian files as produced for Linux x86-64. The authoritative references are the
System V gABI and the x86-64 psABI (see [References](#references)).

## One file, two views

An ELF file can be read in two ways.

* The **section view** (the section header table) is what the *linker* works with. Code, data,
  symbol tables, string tables and relocations each live in their own named section.
* The **segment view** (the program header table) is what the *loader* works with. Each
  segment says "map these file bytes at this virtual address with these permissions".

A relocatable object (`.o`) has only the section view, because it is never loaded directly.
Executables and shared objects have both views, and a single segment usually covers several
sections. For example, one read-and-execute `LOAD` segment holds `.init`, `.plt` and `.text`.

```
 file offset 0 ┌──────────────────────────┐
               │ ELF header (64 bytes)    │  identifies the file and locates both tables
               ├──────────────────────────┤
               │ program header table     │  segments (executables / shared objects only)
               ├──────────────────────────┤
               │ .text  .rodata  .data    │  section contents
               │ .symtab .strtab .rela.*  │
               │ ...                      │
               ├──────────────────────────┤
               │ section header table     │  one 64-byte entry per section (GCC places it last)
               └──────────────────────────┘
```

## The ELF header

The first 16 bytes (`e_ident`) can be read without knowing anything else about the file:

| Bytes | Field | Meaning |
|-------|-------|---------|
| 0–3 | magic | `7f 45 4c 46` ("\x7fELF") |
| 4 | `EI_CLASS` | 1 = ELF32, 2 = ELF64 |
| 5 | `EI_DATA` | 1 = little-endian, 2 = big-endian |
| 6 | `EI_VERSION` | must be 1 |
| 7 | `EI_OSABI` | 0 = System V, 3 = GNU/Linux, … |

The class and byte order determine how every later field is laid out, so BinScope checks
them first. It rejects ELF32 and big-endian files with an *unsupported* error, not a
*malformed* one, because those files are valid; BinScope just doesn't handle them.

The rest of the 64-byte ELF64 header gives the following:

* `e_type`: `ET_REL` (object file), `ET_EXEC` (executable at a fixed address), `ET_DYN`
  (shared object *or* position-independent executable; the header alone cannot tell which),
  or `ET_CORE`.
* `e_machine`: target architecture (62 = x86-64).
* `e_entry`: the virtual address where execution starts (0 for object files).
* `e_phoff`, `e_phentsize`, `e_phnum`: location and shape of the program header table.
* `e_shoff`, `e_shentsize`, `e_shnum`: location and shape of the section header table.
* `e_shstrndx`: index of the section that holds the section *names* (`.shstrtab`).

**Extended numbering.** `e_shnum` is only 16 bits wide. A file with 0xff00 or more sections
stores 0 in `e_shnum` and the real count in `sh_size` of section 0. Likewise,
`e_shstrndx == SHN_XINDEX` means the real index is in section 0's `sh_link`, and
`e_phnum == PN_XNUM` means the real count is in its `sh_info`. BinScope implements all three
and tests them by re-encoding a real object file this way.

## Sections

Each section header (`Elf64_Shdr`) records the following:

| Field | Meaning |
|-------|---------|
| `sh_name` | offset of the name inside `.shstrtab` |
| `sh_type` | what the contents are (`PROGBITS`, `SYMTAB`, `RELA`, `NOBITS`, …) |
| `sh_flags` | `W` writable, `A` occupies memory at run time, `X` executable, … |
| `sh_addr` | virtual address at run time (0 in object files) |
| `sh_offset`, `sh_size` | where the contents are in the file |
| `sh_link`, `sh_info` | type-specific cross references (see below) |
| `sh_addralign`, `sh_entsize` | alignment, and the size of each entry for table sections |

Sections you will meet in nearly every C++ build:

| Section | Holds |
|---------|-------|
| `.text` | machine code |
| `.rodata` | read-only data: string literals, constant tables |
| `.data` | initialized writable globals |
| `.bss` | zero-initialized globals; type `NOBITS`, so it takes **no bytes in the file** |
| `.symtab` / `.strtab` | the full symbol table and its names |
| `.dynsym` / `.dynstr` | the subset of symbols the dynamic loader needs, and their names |
| `.rela.text` | relocations to apply to `.text` |
| `.rela.dyn`, `.rela.plt` | relocations the dynamic loader applies at start-up or on first call |
| `.debug_*` | DWARF debugging information (present when compiled with `-g`) |

**`SHT_NOBITS` matters.** A `.bss` header still has `sh_offset` and `sh_size`, but no file
bytes stand behind them. A parser that reads "the contents of `.bss`" from the file is reading
whatever happens to follow. BinScope never reads `NOBITS` contents.

**Cross references.** For a symbol table, `sh_link` is the index of its string table and
`sh_info` is one past the last local symbol. For a relocation section, `sh_link` is the
symbol table it uses and `sh_info` is the section it patches (marked by the `I` /
`SHF_INFO_LINK` flag).

## String tables

A string table is a run of NUL-terminated strings. Other structures refer to a name by its
byte offset into the table. A malformed file can give an offset past the end of the table,
or a final string with no terminator. BinScope bounds every lookup by the *section's* size,
not the file's, and reports such names as `<corrupt name @0x…>` with a warning.

## Symbol tables

Each `Elf64_Sym` entry is 24 bytes:

| Field | Meaning |
|-------|---------|
| `st_name` | offset into the linked string table |
| `st_info` | binding (upper 4 bits) and type (lower 4 bits) |
| `st_other` | visibility (lower 2 bits) |
| `st_shndx` | index of the section that *defines* the symbol, or a special value |
| `st_value` | see below |
| `st_size` | size in bytes of the object or function |

* **Binding:** `LOCAL` (visible only inside its object file, e.g. `static` functions),
  `GLOBAL` (visible to other object files), or `WEAK` (global, but may be overridden or left
  unresolved).
* **Type:** `FUNC`, `OBJECT` (a variable), `SECTION` (stands for a section itself, used as a
  relocation target), `FILE` (the source file name), or `NOTYPE`.
* **Special section indices:** `SHN_UNDEF` (0) means *referenced here but defined elsewhere*,
  so the symbol is an external reference. `SHN_ABS` means the value is an absolute constant.
  `SHN_COMMON` marks a tentative definition.
* **What `st_value` means** depends on the file:
  * in an `ET_REL` object, it is an **offset within the defining section**;
  * in an executable or shared object, it is a **virtual address**. For `ET_DYN` that address
    is relative to the base where the file is loaded.

C++ names are *mangled* so they can encode parameter types: `add(int, int)` becomes
`_Z3addii`. `binscope symbols --demangle` shows the readable form.

## Relocations

The compiler doesn't know where code and data will end up in memory, and it doesn't know the
address of anything defined in another file. So it leaves placeholder bytes and records a
**relocation**: "patch the bytes at this location, using this symbol, with this formula".

```
Elf64_Rel  { r_offset; r_info; }            16 bytes, addend stored in the patched bytes
Elf64_Rela { r_offset; r_info; r_addend; }  24 bytes, addend stored in the entry (x86-64 uses RELA)

r_info = (symbol_index << 32) | type
```

What `r_offset` means depends on the file, the same way `st_value` does. In an object file it
is an offset within the section that `sh_info` names. In linked files it is a virtual address.

Common x86-64 types (S = symbol value, A = addend, P = address being patched, B = load base,
L = PLT entry, G/GOT = Global Offset Table entry/address):

| Type | Formula | Typical use |
|------|---------|-------------|
| `R_X86_64_64` | S + A | absolute 64-bit pointer in data |
| `R_X86_64_PC32` | S + A − P | RIP-relative access to a variable |
| `R_X86_64_PLT32` | L + A − P | `call` to a function that may live in a shared library |
| `R_X86_64_GOTPCREL(X)` | G + GOT + A − P | load an address from the GOT |
| `R_X86_64_RELATIVE` | B + A | pointer in a PIE or library, adjusted by the load base |
| `R_X86_64_GLOB_DAT` | S | GOT entry for a data symbol, filled in by the loader |
| `R_X86_64_JUMP_SLOT` | S | GOT slot used by a PLT stub, possibly bound on first call |

The addend is usually −4 for PC-relative fields. The CPU computes RIP-relative addresses
from the *end* of the instruction, and the 4-byte field is at the end of the instruction.

## Object files, executables and shared objects

| | `ET_REL` object | `ET_EXEC` / PIE `ET_DYN` executable | `ET_DYN` shared object |
|---|---|---|---|
| Program headers | none | yes, with `PT_INTERP` if dynamically linked | yes, no `PT_INTERP` |
| Addresses | not assigned (0) | assigned | assigned, relative to the load base |
| Undefined symbols | normal | only symbols left for the dynamic loader | only symbols left for the dynamic loader |
| Relocations | `.rela.text`, `.rela.data`, … for the static linker | `.rela.dyn`, `.rela.plt` for the loader | `.rela.dyn`, `.rela.plt` for the loader |

## Linking in one paragraph

The static linker (`ld`, driven by `g++`) gathers the object files plus the C runtime start-up
objects (`crt1.o`, `crti.o`, `crtbegin.o`, …). It concatenates same-named input sections into
output sections and assigns virtual addresses. It then **resolves** each undefined symbol to a
definition in another input. With those addresses known, it **applies** the relocations it
can, so `.rela.text` disappears from the output. Some symbols stay undefined because they live
in shared libraries, `puts` in `libc.so.6` for example. For those, the linker writes
*dynamic* relocations (`.rela.dyn`, `.rela.plt`) and dynamic symbols (`.dynsym`). The
**dynamic loader** (`ld-linux-x86-64.so.2`, named by `PT_INTERP`) processes them when the
program starts. [linker-experiments.md](linker-experiments.md) shows each of these steps on
real files.

## References

* System V Application Binary Interface, *Generic ABI* (gABI): ELF object file format.
* *System V Application Binary Interface, AMD64 Architecture Processor Supplement* (x86-64
  psABI): relocation types and their calculations.
* `man 5 elf` on Linux.
