#pragma once

// ELF constants from the System V gABI and the x86-64 psABI.
//
// BinScope defines these itself rather than including <elf.h> so the parser has
// no dependency on the host's system headers. tests/test_constants.cpp checks
// these values against the system <elf.h> when it is available.

#include <cstddef>
#include <cstdint>

namespace binscope::elf {

// --- e_ident ---------------------------------------------------------------
inline constexpr std::uint8_t kMagic[4] = {0x7f, 'E', 'L', 'F'};
inline constexpr std::size_t EI_CLASS = 4;
inline constexpr std::size_t EI_DATA = 5;
inline constexpr std::size_t EI_VERSION = 6;
inline constexpr std::size_t EI_OSABI = 7;
inline constexpr std::size_t EI_ABIVERSION = 8;
inline constexpr std::size_t EI_NIDENT = 16;

inline constexpr std::uint8_t ELFCLASSNONE = 0;
inline constexpr std::uint8_t ELFCLASS32 = 1;
inline constexpr std::uint8_t ELFCLASS64 = 2;

inline constexpr std::uint8_t ELFDATANONE = 0;
inline constexpr std::uint8_t ELFDATA2LSB = 1;
inline constexpr std::uint8_t ELFDATA2MSB = 2;

inline constexpr std::uint32_t EV_CURRENT = 1;

inline constexpr std::uint8_t ELFOSABI_SYSV = 0;
inline constexpr std::uint8_t ELFOSABI_GNU = 3;  // a.k.a. ELFOSABI_LINUX
inline constexpr std::uint8_t ELFOSABI_FREEBSD = 9;

// --- e_type ----------------------------------------------------------------
inline constexpr std::uint16_t ET_NONE = 0;
inline constexpr std::uint16_t ET_REL = 1;
inline constexpr std::uint16_t ET_EXEC = 2;
inline constexpr std::uint16_t ET_DYN = 3;
inline constexpr std::uint16_t ET_CORE = 4;

// --- e_machine (subset) ------------------------------------------------------
inline constexpr std::uint16_t EM_NONE = 0;
inline constexpr std::uint16_t EM_386 = 3;
inline constexpr std::uint16_t EM_ARM = 40;
inline constexpr std::uint16_t EM_X86_64 = 62;
inline constexpr std::uint16_t EM_AARCH64 = 183;
inline constexpr std::uint16_t EM_CUDA = 190;
inline constexpr std::uint16_t EM_AMDGPU = 224;
inline constexpr std::uint16_t EM_RISCV = 243;
inline constexpr std::uint16_t EM_BPF = 247;

// --- On-disk structure sizes for ELF64 --------------------------------------
inline constexpr std::uint64_t kEhdrSize = 64;
inline constexpr std::uint64_t kPhdrSize = 56;
inline constexpr std::uint64_t kShdrSize = 64;
inline constexpr std::uint64_t kSymSize = 24;
inline constexpr std::uint64_t kRelSize = 16;
inline constexpr std::uint64_t kRelaSize = 24;

// --- Special section indices ------------------------------------------------
inline constexpr std::uint16_t SHN_UNDEF = 0;
inline constexpr std::uint16_t SHN_LORESERVE = 0xff00;
inline constexpr std::uint16_t SHN_ABS = 0xfff1;
inline constexpr std::uint16_t SHN_COMMON = 0xfff2;
inline constexpr std::uint16_t SHN_XINDEX = 0xffff;

// Extended program-header numbering marker (e_phnum == PN_XNUM).
inline constexpr std::uint16_t PN_XNUM = 0xffff;

// --- Section types (sh_type) ------------------------------------------------
inline constexpr std::uint32_t SHT_NULL = 0;
inline constexpr std::uint32_t SHT_PROGBITS = 1;
inline constexpr std::uint32_t SHT_SYMTAB = 2;
inline constexpr std::uint32_t SHT_STRTAB = 3;
inline constexpr std::uint32_t SHT_RELA = 4;
inline constexpr std::uint32_t SHT_HASH = 5;
inline constexpr std::uint32_t SHT_DYNAMIC = 6;
inline constexpr std::uint32_t SHT_NOTE = 7;
inline constexpr std::uint32_t SHT_NOBITS = 8;
inline constexpr std::uint32_t SHT_REL = 9;
inline constexpr std::uint32_t SHT_SHLIB = 10;
inline constexpr std::uint32_t SHT_DYNSYM = 11;
inline constexpr std::uint32_t SHT_INIT_ARRAY = 14;
inline constexpr std::uint32_t SHT_FINI_ARRAY = 15;
inline constexpr std::uint32_t SHT_PREINIT_ARRAY = 16;
inline constexpr std::uint32_t SHT_GROUP = 17;
inline constexpr std::uint32_t SHT_SYMTAB_SHNDX = 18;
inline constexpr std::uint32_t SHT_RELR = 19;
inline constexpr std::uint32_t SHT_GNU_ATTRIBUTES = 0x6ffffff5;
inline constexpr std::uint32_t SHT_GNU_HASH = 0x6ffffff6;
inline constexpr std::uint32_t SHT_GNU_verdef = 0x6ffffffd;
inline constexpr std::uint32_t SHT_GNU_verneed = 0x6ffffffe;
inline constexpr std::uint32_t SHT_GNU_versym = 0x6fffffff;
inline constexpr std::uint32_t SHT_X86_64_UNWIND = 0x70000001;
inline constexpr std::uint32_t SHT_LOOS = 0x60000000;
inline constexpr std::uint32_t SHT_HIOS = 0x6fffffff;
inline constexpr std::uint32_t SHT_LOPROC = 0x70000000;
inline constexpr std::uint32_t SHT_HIPROC = 0x7fffffff;
inline constexpr std::uint32_t SHT_LOUSER = 0x80000000;
inline constexpr std::uint32_t SHT_HIUSER = 0xffffffff;

// --- Section flags (sh_flags) -----------------------------------------------
inline constexpr std::uint64_t SHF_WRITE = 0x1;
inline constexpr std::uint64_t SHF_ALLOC = 0x2;
inline constexpr std::uint64_t SHF_EXECINSTR = 0x4;
inline constexpr std::uint64_t SHF_MERGE = 0x10;
inline constexpr std::uint64_t SHF_STRINGS = 0x20;
inline constexpr std::uint64_t SHF_INFO_LINK = 0x40;
inline constexpr std::uint64_t SHF_LINK_ORDER = 0x80;
inline constexpr std::uint64_t SHF_OS_NONCONFORMING = 0x100;
inline constexpr std::uint64_t SHF_GROUP = 0x200;
inline constexpr std::uint64_t SHF_TLS = 0x400;
inline constexpr std::uint64_t SHF_COMPRESSED = 0x800;
inline constexpr std::uint64_t SHF_GNU_RETAIN = 0x200000;
inline constexpr std::uint64_t SHF_X86_64_LARGE = 0x10000000;
inline constexpr std::uint64_t SHF_EXCLUDE = 0x80000000;

// --- Program header types (p_type) ------------------------------------------
inline constexpr std::uint32_t PT_NULL = 0;
inline constexpr std::uint32_t PT_LOAD = 1;
inline constexpr std::uint32_t PT_DYNAMIC = 2;
inline constexpr std::uint32_t PT_INTERP = 3;
inline constexpr std::uint32_t PT_NOTE = 4;
inline constexpr std::uint32_t PT_SHLIB = 5;
inline constexpr std::uint32_t PT_PHDR = 6;
inline constexpr std::uint32_t PT_TLS = 7;
inline constexpr std::uint32_t PT_GNU_EH_FRAME = 0x6474e550;
inline constexpr std::uint32_t PT_GNU_STACK = 0x6474e551;
inline constexpr std::uint32_t PT_GNU_RELRO = 0x6474e552;
inline constexpr std::uint32_t PT_GNU_PROPERTY = 0x6474e553;
inline constexpr std::uint32_t PT_GNU_SFRAME = 0x6474e554;

inline constexpr std::uint32_t PF_X = 0x1;
inline constexpr std::uint32_t PF_W = 0x2;
inline constexpr std::uint32_t PF_R = 0x4;

// --- Symbol binding / type / visibility -------------------------------------
inline constexpr std::uint8_t STB_LOCAL = 0;
inline constexpr std::uint8_t STB_GLOBAL = 1;
inline constexpr std::uint8_t STB_WEAK = 2;
inline constexpr std::uint8_t STB_GNU_UNIQUE = 10;

inline constexpr std::uint8_t STT_NOTYPE = 0;
inline constexpr std::uint8_t STT_OBJECT = 1;
inline constexpr std::uint8_t STT_FUNC = 2;
inline constexpr std::uint8_t STT_SECTION = 3;
inline constexpr std::uint8_t STT_FILE = 4;
inline constexpr std::uint8_t STT_COMMON = 5;
inline constexpr std::uint8_t STT_TLS = 6;
inline constexpr std::uint8_t STT_GNU_IFUNC = 10;

inline constexpr std::uint8_t STV_DEFAULT = 0;
inline constexpr std::uint8_t STV_INTERNAL = 1;
inline constexpr std::uint8_t STV_HIDDEN = 2;
inline constexpr std::uint8_t STV_PROTECTED = 3;

// --- x86-64 relocation types (psABI, table "Relocation Types") ---------------
inline constexpr std::uint32_t R_X86_64_NONE = 0;
inline constexpr std::uint32_t R_X86_64_64 = 1;
inline constexpr std::uint32_t R_X86_64_PC32 = 2;
inline constexpr std::uint32_t R_X86_64_GOT32 = 3;
inline constexpr std::uint32_t R_X86_64_PLT32 = 4;
inline constexpr std::uint32_t R_X86_64_COPY = 5;
inline constexpr std::uint32_t R_X86_64_GLOB_DAT = 6;
inline constexpr std::uint32_t R_X86_64_JUMP_SLOT = 7;
inline constexpr std::uint32_t R_X86_64_RELATIVE = 8;
inline constexpr std::uint32_t R_X86_64_GOTPCREL = 9;
inline constexpr std::uint32_t R_X86_64_32 = 10;
inline constexpr std::uint32_t R_X86_64_32S = 11;
inline constexpr std::uint32_t R_X86_64_16 = 12;
inline constexpr std::uint32_t R_X86_64_PC16 = 13;
inline constexpr std::uint32_t R_X86_64_8 = 14;
inline constexpr std::uint32_t R_X86_64_PC8 = 15;
inline constexpr std::uint32_t R_X86_64_DTPMOD64 = 16;
inline constexpr std::uint32_t R_X86_64_DTPOFF64 = 17;
inline constexpr std::uint32_t R_X86_64_TPOFF64 = 18;
inline constexpr std::uint32_t R_X86_64_TLSGD = 19;
inline constexpr std::uint32_t R_X86_64_TLSLD = 20;
inline constexpr std::uint32_t R_X86_64_DTPOFF32 = 21;
inline constexpr std::uint32_t R_X86_64_GOTTPOFF = 22;
inline constexpr std::uint32_t R_X86_64_TPOFF32 = 23;
inline constexpr std::uint32_t R_X86_64_PC64 = 24;
inline constexpr std::uint32_t R_X86_64_GOTOFF64 = 25;
inline constexpr std::uint32_t R_X86_64_GOTPC32 = 26;
inline constexpr std::uint32_t R_X86_64_SIZE32 = 32;
inline constexpr std::uint32_t R_X86_64_SIZE64 = 33;
inline constexpr std::uint32_t R_X86_64_IRELATIVE = 37;
inline constexpr std::uint32_t R_X86_64_GOTPCRELX = 41;
inline constexpr std::uint32_t R_X86_64_REX_GOTPCRELX = 42;

}  // namespace binscope::elf
