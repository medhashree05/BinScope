#pragma once

#include "binscope/elf_file.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace binscope {

// One decoded Elf64_Rel or Elf64_Rela entry.
struct Relocation {
    std::uint32_t index = 0;
    // Where to patch: an offset within the target section in ET_REL files, a
    // virtual address (relative to the load base for ET_DYN) in linked files.
    std::uint64_t offset = 0;
    std::uint32_t type = 0;          // ELF64_R_TYPE(r_info): low 32 bits
    std::uint32_t symbol_index = 0;  // ELF64_R_SYM(r_info): high 32 bits
    std::optional<std::int64_t> addend;  // present for RELA, absent for REL

    std::string symbol_name;  // resolved through the linked symbol table
    std::uint64_t symbol_value = 0;
    bool symbol_valid = true;
};

enum class RelocationEncoding {
    Rel,   // SHT_REL: addend is stored in the bytes being relocated (implicit)
    Rela,  // SHT_RELA: addend is stored in the entry itself (explicit)
};

struct RelocationSection {
    std::uint32_t section_index = 0;
    std::string section_name;
    RelocationEncoding encoding = RelocationEncoding::Rela;
    std::uint32_t symbol_table_index = 0;  // sh_link (0 = no associated symbol table)
    std::string symbol_table_name;
    // sh_info: section the relocations patch. 0 for loader relocations such as
    // .rela.dyn, which apply to addresses across the whole loaded image.
    std::uint32_t target_section_index = 0;
    std::string target_section_name;
    std::vector<Relocation> entries;
};

// Decodes every SHT_REL and SHT_RELA section. Structural problems (bad entry
// size, contents out of bounds, sh_link not a symbol table) throw ParseError;
// a symbol index outside the linked table becomes a warning.
std::vector<RelocationSection> parse_relocation_sections(const ElfFile& file,
                                                         std::vector<std::string>& warnings);

// Names and explanations are architecture specific. BinScope decodes x86-64;
// other machines get a numeric fallback ("type 23").
std::string relocation_type_name(std::uint16_t machine, std::uint32_t type);
// psABI calculation, e.g. "S + A - P"; empty if unknown.
std::string relocation_formula(std::uint16_t machine, std::uint32_t type);
// One-sentence explanation; empty if unknown.
std::string relocation_explanation(std::uint16_t machine, std::uint32_t type);

std::string relocation_encoding_name(RelocationEncoding encoding);

// Number of SHT_RELR (packed relative relocation) sections, which BinScope
// detects but does not decode.
std::size_t count_relr_sections(const ElfFile& file);

}  // namespace binscope
