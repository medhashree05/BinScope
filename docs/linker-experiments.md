# Linker experiments

These experiments use BinScope to follow a C++ program from source, to object file, to linked
executable. All output below was captured from real runs. Long tables are shortened where
marked `…`, and nothing has been edited otherwise.

**Environment used for the recorded results:** Ubuntu 26.04 on WSL2, x86-64,
`g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`, GNU ld 2.46 (`ld.bfd`), with GCC's Ubuntu default of
position-independent executables (`--enable-default-pie`). Other compilers, binutils versions
or distributions will produce different offsets, sizes and sometimes different relocation
types. The *kinds* of change described here are what carry over.

Build BinScope first (see the README). Then run the commands from the repository root:

```bash
B=./build/binscope
```

---

## Experiment A: one translation unit

[`examples/hello.cpp`](../examples/hello.cpp) contains:
* an initialized global `greeting_count`;
* a zero-initialized global `call_total`;
* a `static const` string;
* a `static` helper `scaled()`;
* `greet()`, which calls `printf`;
* `main()`.

```bash
g++ -c examples/hello.cpp -o hello.o
g++ examples/hello.cpp -o hello
$B symbols hello.o
$B compare hello.o hello
```

**Symbols in the object file.** Every value is a section offset, and `printf` is the only
undefined symbol:

```
    4  0x0000000000000000    20  OBJECT   LOCAL   DEFAULT    5  .rodata  defined    _ZL9kGreeting
    5  0x0000000000000000    21  FUNC     LOCAL   DEFAULT    1  .text    defined    _ZL6scaledi
    6  0x0000000000000000     4  OBJECT   GLOBAL  DEFAULT    3  .data    defined    greeting_count
    7  0x0000000000000000     4  OBJECT   GLOBAL  DEFAULT    4  .bss     defined    call_total
    8  0x0000000000000015    98  FUNC     GLOBAL  DEFAULT    1  .text    defined    _Z5greeti
    9  0x0000000000000000     0  NOTYPE   GLOBAL  DEFAULT  UND           undefined  printf
   10  0x0000000000000077    32  FUNC     GLOBAL  DEFAULT    1  .text    defined    main
```

The data maps onto sections as you'd expect: `greeting_count` is in `.data`, `call_total` is
in `.bss`, and the string is in `.rodata`. The `static` items have `LOCAL` binding, and their
names are mangled with an `L` (`_ZL6scaledi`) to mark internal linkage.

**After linking**, the same symbols have virtual addresses:

```
  Defined in both: 6
    Name            Before                      After                             Note
    _Z5greeti       .text+0x15 (FUNC, 98 B)     0x115e in .text (FUNC, 98 B)      values in different address spaces
    _ZL6scaledi     .text+0x0 (FUNC, 21 B)      0x1149 in .text (FUNC, 21 B)      values in different address spaces
    _ZL9kGreeting   .rodata+0x0 (OBJECT, 20 B)  0x2010 in .rodata (OBJECT, 20 B)  values in different address spaces
    call_total      .bss+0x0 (OBJECT, 4 B)      0x4018 in .bss (OBJECT, 4 B)      values in different address spaces
    greeting_count  .data+0x0 (OBJECT, 4 B)     0x4010 in .data (OBJECT, 4 B)     values in different address spaces
    main            .text+0x77 (FUNC, 32 B)     0x11c0 in .text (FUNC, 32 B)      values in different address spaces

Where the linker placed this object's input sections (inferred)
  Input section  Placed at  Supporting symbols
  .bss           0x4018                      1
  .data          0x4010                      1
  .rodata        0x2010                      1
  .text          0x1149                      3
```

Every symbol from `hello.o`'s `.text` moved by the same amount, 0x1149, so that is where the
linker put this object's `.text`. A function's address is the placement plus its section
offset: `greet` is at 0x1149 + 0x15 = 0x115e.

**Checked independently.** I relinked with `g++ hello.o -o hello_map -Wl,-Map=hello.map`. The
`.text` bytes are identical to `hello`. GNU ld's map file reports the same four placements:

```
 .text          0x0000000000001149       0x97 hello.o
 .rodata        0x0000000000002010       0x1d hello.o
 .data          0x0000000000004010        0x4 hello.o
 .bss           0x0000000000004018        0x4 hello.o
```

`printf` is still undefined after linking. It is listed under *Left for the dynamic loader*,
with version `GLIBC_2.2.5`, because libc is a shared library. The executable also gains 28
symbols that `hello.o` never had, such as `_start`, `_init`, `__libc_start_main` and
`frame_dummy`. They come from the C runtime start-up objects the compiler driver adds to
every link, and from symbols the linker creates (`_GLOBAL_OFFSET_TABLE_`, `_DYNAMIC`, `_end`).

---

## Experiment B: an unresolved external symbol

[`math_main.cpp`](../examples/math_main.cpp) calls `add()` and `multiply()` and reads
`math_call_count`. All three are only *declared* in
[`math_utils.hpp`](../examples/math_utils.hpp) and *defined* in
[`math_utils.cpp`](../examples/math_utils.cpp).

```bash
g++ -c examples/math_utils.cpp -o math_utils.o
g++ -c examples/math_main.cpp -o math_main.o
$B symbols math_main.o --undefined
$B relocs math_main.o
```

**Undefined symbols in `math_main.o`:**

```
Symbol table '.symtab' (section [11], 9 entries, names in [12] '.strtab')
  showing 4 of 9 entries (filter: undefined)
  Num  Value               Size  Type    Bind    Vis      Ndx  Section  Status     Name
    5  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  _Z3addii
    6  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  _Z8multiplyii
    7  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  math_call_count
    8  0x0000000000000000     0  NOTYPE  GLOBAL  DEFAULT  UND           undefined  printf
```

The compiler knows these names but not their types or locations, so they are `NOTYPE` with
section index `UND`.

**Relocations that refer to them:**

```
Relocation section '.rela.text' [2]: 5 entries, RELA (explicit addend)
  patches section [1] '.text'; symbols from [11] '.symtab'
  Idx  Offset          Type            Sym#  Sym.Value           Symbol           Addend
    0  0x000000000017  R_X86_64_PLT32     5  0x0000000000000000  _Z3addii           -0x4
    1  0x000000000029  R_X86_64_PLT32     6  0x0000000000000000  _Z8multiplyii      -0x4
    2  0x000000000032  R_X86_64_PC32      7  0x0000000000000000  math_call_count    -0x4
    3  0x00000000003f  R_X86_64_PC32      3  0x0000000000000000  .rodata            -0x4
    4  0x00000000004b  R_X86_64_PLT32     8  0x0000000000000000  printf             -0x4
```

Each `call` instruction has a 4-byte displacement that the compiler left empty, and the
`PLT32` entries describe how to fill it in: L + A − P. The read of `math_call_count` is a
RIP-relative `PC32` (S + A − P). The format string is reached through the *section symbol*
`.rodata`, not a named symbol. The addend is −4 because x86 measures RIP-relative
displacements from the end of the instruction, and the 4-byte field is the last thing in it.

**Linking without the definitions fails, and the failure points at the same relocations:**

```bash
g++ math_main.o -o should_fail
```
```
math_main.cpp:(.text+0x17): undefined reference to `add(int, int)'
math_main.cpp:(.text+0x29): undefined reference to `multiply(int, int)'
math_main.cpp:(.text+0x32): undefined reference to `math_call_count'
```

The linker reports `.text+0x17`, `+0x29` and `+0x32`, exactly the `Offset` values of
relocations 0, 1 and 2 above. An "undefined reference" error *is* an unresolvable relocation.
`printf` isn't in the error list, because the linker found it in `libc.so.6`.

**Linking with both objects succeeds:**

```bash
g++ math_main.o math_utils.o -o math_program
./math_program            # sum=5 product=20 calls=2
$B compare math_main.o math_program
```

```
  Resolved by the static linker (undefined before, defined after): 3
    Name             Before  After                         Note
    _Z3addii         UND     0x11a4 in .text (FUNC, 39 B)  definition linked in
    _Z8multiplyii    UND     0x11cb in .text (FUNC, 38 B)  definition linked in
    math_call_count  UND     0x4014 in .bss (OBJECT, 4 B)  definition linked in

  Left for the dynamic loader (still undefined after, but a dynamic symbol): 1
    Name    Before  After                      Note
    printf  UND     UND @GLIBC_2.2.5 [dynsym]  bound at load/run time to version GLIBC_2.2.5

Relocation sections (entry counts)
  Section         Before  After
  .rela.text           5      -
  .rela.eh_frame       1      -
  .rela.dyn            -      8
  .rela.plt            -      1
  total                6      9
```

**What changed:**
* The three references to `math_utils.o` were resolved at static link time and now have
  addresses. `math_call_count` is in `.bss` because it is zero-initialized.
* `.rela.text` and `.rela.eh_frame` are gone. The linker *applied* those relocations while it
  wrote `.text` and `.eh_frame`.
* `.interp`, `.dynamic`, `.dynsym`, `.dynstr`, `.plt`, `.got`, `.init_array` and others
  appeared (31 sections against 14), along with 14 program headers.

**What did not change:**
* `printf` is *still undefined*. The static linker only recorded that `libc.so.6` must supply
  it.
* There are more relocations, not fewer: 9 dynamic relocations replace the 6 static ones.
  Linking moved relocation work from the static linker to the loader. It did not remove it.
* `main` has the same size (91 bytes): the instructions are the same, only the displacement
  fields were filled in.

---

## Experiment C: dynamic and static linking

```bash
$B inspect math_program
$B relocs math_program
```

```
Summary
  Sections: 31, segments: 14
  Kind: dynamically linked executable (loaded via its program interpreter)
  Symbol table .dynsym: 7 entries (0 defined, 6 undefined)
  Symbol table .symtab: 40 entries (33 defined, 6 undefined)
  Relocation section .rela.dyn: 8 entries, RELA (explicit addend)
  Relocation section .rela.plt: 1 entry, RELA (explicit addend), patches .got
```

```
Relocation section '.rela.dyn' [8]: 8 entries, RELA (explicit addend)
    0  0x000000003db8  R_X86_64_RELATIVE     0                      -                            +0x1140
    1  0x000000003dc0  R_X86_64_RELATIVE     0                      -                            +0x1100
    2  0x000000004008  R_X86_64_RELATIVE     0                      -                            +0x4008
    3  0x000000003fd8  R_X86_64_GLOB_DAT     1  0x0000000000000000  __libc_start_main               +0x0
    …  (three more GLOB_DAT for weak runtime hooks, and one for __cxa_finalize)
Relocation section '.rela.plt' [9]: 1 entry, RELA (explicit addend)
    0  0x000000003fd0  R_X86_64_JUMP_SLOT     3  0x0000000000000000  printf    +0x0
```

How the dynamic-linking sections work together:

* **`.dynsym` / `.dynstr`** hold only the symbols the loader must deal with: six undefined
  references to libc and its optional hooks. The full `.symtab` (40 entries) isn't needed at
  run time. Stripping removes it but keeps `.dynsym` (the `program_stripped` test fixture
  checks this).
* **`.rela.dyn`** has two kinds of entry.
  * `R_X86_64_RELATIVE` entries need no symbol (`Sym# 0`). The executable is
    position-independent (`ET_DYN`), so pointers stored in data, such as the `.init_array`
    entry at 0x3db8 pointing to 0x1140 (`frame_dummy`), are only correct after the loader adds
    the load base B.
  * `R_X86_64_GLOB_DAT` entries fill GOT slots with the run-time addresses of data and
    function symbols looked up in shared libraries.
* **`.rela.plt`** has the `R_X86_64_JUMP_SLOT` for `printf`. The call in `main` goes through a
  PLT stub that jumps via this GOT slot.
* **`PT_INTERP`** names `/lib64/ld-linux-x86-64.so.2`, the program that processes these
  relocations before `main` runs.

**Static linking** worked in this environment, because the static C and C++ libraries are
installed:

```bash
g++ -static math_main.o math_utils.o -o math_static
$B inspect math_static
$B compare math_program math_static
```

```
  Type:                             EXEC (executable (fixed load address))
  Entry point:                      0x401720
  …
Summary
  Sections: 28, segments: 11
  Kind: statically linked executable (no program interpreter)
  Symbol table .symtab: 2076 entries (2075 defined, 0 undefined)
  Relocation section .rela.plt: 22 entries, RELA (explicit addend), patches .got.plt
```

* There are no undefined symbols, and there is no `.interp`, `.dynsym` or `.dynamic`. The
  file is 817,040 bytes, against 16,112 for the dynamic version, because the libc and
  libstdc++ code it uses is copied in. That brings 2,076 symbols.
* It is `ET_EXEC` with a fixed base (0x400000), so no `RELATIVE` relocations are needed.
* **Relocations still remain.** All 22 are `R_X86_64_IRELATIVE`. glibc selects
  CPU-specific implementations of functions such as `memcpy` through IFUNC resolvers. In a
  static executable there is no dynamic loader, so libc's own start-up code calls the
  resolvers and fills these slots.
* Matching symbols between the dynamic and static builds produced 26 names that BinScope
  *cannot match confidently*. The static `.symtab` contains the same local names (for example
  `__PRETTY_FUNCTION__.0`) from several different libc object files. BinScope lists them
  separately and doesn't guess.

---

## Experiment D: optimization levels

```bash
g++ -O0 -c examples/hello.cpp -o hello_O0.o
g++ -O2 -c examples/hello.cpp -o hello_O2.o
$B compare hello_O0.o hello_O2.o
```

```
  Sections                       14              17
  Relocation entries (REL/RELA)  11              10

  .text               PROGBITS         0x97        0x6b  -              size changed
  .rodata             PROGBITS         0x1d        0x14  -              size changed
  .rodata.str1.1      PROGBITS            -         0x9  -              only after
  .text.startup       PROGBITS            -        0x21  -              only after
  .rela.text.startup  RELA                -        0x30  -              only after

  Defined in both: 5
    _Z5greeti       .text+0x15 (FUNC, 98 B)     .text+0x0 (FUNC, 107 B)         value changed; size changed
    main            .text+0x77 (FUNC, 32 B)     .text.startup+0x0 (FUNC, 33 B)  values in different address spaces; size changed
    …
  Only in before (no symbol with this name after): 2
    _ZL6scaledi  .text+0x0 (FUNC, 21 B)  -      not present after (e.g. inlined, discarded or renamed)
    printf       UND                     -      reference no longer present
  Only in after (no symbol with this name before): 2
    .LC0          -       .rodata.str1.1+0x0 (NOTYPE, 0 B)  defined only after
    __printf_chk  -       UND                               new undefined reference
```

**Observed differences.** Some of these go against the common idea that "optimized means
smaller in every way":

* **More sections, not fewer (14 → 17).** At `-O2` GCC moves `main` into `.text.startup`.
  The linker groups such code together, and `.text.startup` gets its own relocation section.
  The format string moves into a mergeable string section, `.rodata.str1.1` (flags `AMS`),
  so the linker can deduplicate identical strings across objects.
* **`scaled()` was inlined.** Its symbol is gone, and nothing calls it any more.
* **`greet()` got larger (98 → 107 bytes),** even though the whole `.text` section shrank
  (0x97 → 0x6b bytes) once `scaled` and `main` moved out.
* **`printf` became `__printf_chk`.** Ubuntu's GCC enables `_FORTIFY_SOURCE` by default
  when optimizing, which routes `printf` to glibc's checking variant. A toolchain without
  that default would not show this change.
* **Relocation counts barely changed (11 → 10).** The relocations moved between sections, and
  the symbols they refer to changed.

Both files are relocatable, so their symbol values are both section offsets. BinScope compares
them numerically only when the symbol stays in a section of the same name. That's why `main`
(which moved from `.text` to `.text.startup`) is reported as "values in different address
spaces".

---

## Reproducing everything

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
B=./build/binscope
g++ -c examples/hello.cpp -o hello.o && g++ examples/hello.cpp -o hello
g++ -c examples/math_utils.cpp -o math_utils.o && g++ -c examples/math_main.cpp -o math_main.o
g++ math_main.o math_utils.o -o math_program
g++ -static math_main.o math_utils.o -o math_static   # optional: needs static libc/libstdc++
g++ -O0 -c examples/hello.cpp -o hello_O0.o && g++ -O2 -c examples/hello.cpp -o hello_O2.o
$B compare hello.o hello
$B compare math_main.o math_program
$B compare math_program math_static
$B compare hello_O0.o hello_O2.o
```

The generated files are covered by `.gitignore`.
