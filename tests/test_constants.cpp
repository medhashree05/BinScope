// Cross-checks BinScope's self-contained ELF constants against the system
// <elf.h> when the host provides one. The checks are compile-time; the test
// executable only reports whether they ran.
//
// <elf.h> defines the constants as macros with the same names BinScope uses,
// so BinScope's values are captured under prefixed names *before* <elf.h> is
// included; afterwards each bare name expands to the system's value.

#include "binscope/elf_constants.hpp"
#include "test_harness.hpp"

#define BINSCOPE_CHECKED_CONSTANTS(X)                                                          \
    X(ELFCLASS64) X(ELFCLASS32) X(ELFDATA2LSB) X(ELFDATA2MSB) X(ET_REL) X(ET_EXEC) X(ET_DYN)   \
    X(EM_X86_64) X(EM_AARCH64) X(SHN_UNDEF) X(SHN_ABS) X(SHN_COMMON) X(SHN_XINDEX)              \
    X(SHN_LORESERVE) X(PN_XNUM) X(SHT_NULL) X(SHT_PROGBITS) X(SHT_SYMTAB) X(SHT_STRTAB)        \
    X(SHT_RELA) X(SHT_NOBITS) X(SHT_REL) X(SHT_DYNSYM) X(SHT_GROUP) X(SHT_SYMTAB_SHNDX)        \
    X(SHT_GNU_HASH) X(SHT_GNU_versym) X(SHF_WRITE) X(SHF_ALLOC) X(SHF_EXECINSTR)               \
    X(SHF_INFO_LINK) X(SHF_TLS) X(SHF_COMPRESSED) X(PT_LOAD) X(PT_DYNAMIC) X(PT_INTERP)        \
    X(PT_GNU_STACK) X(PT_GNU_RELRO) X(STB_LOCAL) X(STB_GLOBAL) X(STB_WEAK) X(STT_OBJECT)       \
    X(STT_FUNC) X(STT_SECTION) X(STT_FILE) X(STT_GNU_IFUNC) X(STV_HIDDEN) X(STV_PROTECTED)     \
    X(R_X86_64_64) X(R_X86_64_PC32) X(R_X86_64_PLT32) X(R_X86_64_GOTPCREL) X(R_X86_64_COPY)    \
    X(R_X86_64_GLOB_DAT) X(R_X86_64_JUMP_SLOT) X(R_X86_64_RELATIVE) X(R_X86_64_32S)            \
    X(R_X86_64_TPOFF32) X(R_X86_64_IRELATIVE) X(R_X86_64_GOTPCRELX) X(R_X86_64_REX_GOTPCRELX)

#define BINSCOPE_CAPTURE(name) constexpr auto binscope_##name = binscope::elf::name;
BINSCOPE_CHECKED_CONSTANTS(BINSCOPE_CAPTURE)

#if __has_include(<elf.h>)
#include <elf.h>
#define BINSCOPE_HAVE_SYSTEM_ELF_H 1

#define BINSCOPE_COMPARE(name) \
    static_assert(static_cast<unsigned long long>(binscope_##name) == (name), #name " mismatch");
BINSCOPE_CHECKED_CONSTANTS(BINSCOPE_COMPARE)

static_assert(binscope::elf::kEhdrSize == sizeof(Elf64_Ehdr));
static_assert(binscope::elf::kShdrSize == sizeof(Elf64_Shdr));
static_assert(binscope::elf::kPhdrSize == sizeof(Elf64_Phdr));
static_assert(binscope::elf::kSymSize == sizeof(Elf64_Sym));
static_assert(binscope::elf::kRelSize == sizeof(Elf64_Rel));
static_assert(binscope::elf::kRelaSize == sizeof(Elf64_Rela));
#else
#define BINSCOPE_HAVE_SYSTEM_ELF_H 0
#endif

TEST_CASE(constants_match_system_elf_h) {
#if BINSCOPE_HAVE_SYSTEM_ELF_H
    CHECK(true);  // the static_asserts above already enforced the match
#else
    std::cout << "  (skipped: no <elf.h> on this host)\n";
#endif
}
