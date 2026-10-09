#pragma once

#include "binscope/elf_file.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace binscope {

// One decoded Elf64_Sym entry.
struct Symbol {
    std::uint32_t index = 0;
    std::string name;
    std::uint32_t name_offset = 0;
    bool name_valid = true;
    std::uint64_t value = 0;  // section-relative offset in ET_REL, virtual address otherwise
    std::uint64_t size = 0;
    std::uint8_t binding = 0;     // STB_* (upper 4 bits of st_info)
    std::uint8_t type = 0;        // STT_* (lower 4 bits of st_info)
    std::uint8_t visibility = 0;  // STV_* (lower 2 bits of st_other)
    std::uint16_t section_index = 0;  // st_shndx, including SHN_UNDEF/ABS/COMMON/XINDEX
    std::string section_name;     // resolved name, or a description of the special index
    bool section_index_valid = true;

    // SHN_UNDEF: the symbol is referenced here but defined elsewhere (another
    // object file or a shared library). Entry 0 of every table is a null
    // placeholder that is technically SHN_UNDEF too; is_null() identifies it.
    bool is_undefined() const;
    bool is_null() const { return index == 0; }
    bool is_function() const;
};

struct SymbolTable {
    std::uint32_t section_index = 0;
    std::string section_name;        // ".symtab" or ".dynsym" normally
    std::uint32_t section_type = 0;  // SHT_SYMTAB or SHT_DYNSYM
    std::uint32_t string_table_index = 0;
    std::uint32_t first_global = 0;  // sh_info: index of the first non-local symbol
    std::vector<Symbol> symbols;
};

// Selection applied to parsed symbol records (all enabled criteria must match).
// The null entry 0 never matches a filter once any criterion is enabled.
struct SymbolFilter {
    bool defined_only = false;
    bool undefined_only = false;
    bool functions_only = false;

    bool active() const { return defined_only || undefined_only || functions_only; }
    bool matches(const Symbol& symbol) const;
};

// Decodes one SHT_SYMTAB/SHT_DYNSYM section. Structural problems (wrong entry
// size, contents out of bounds, invalid string-table link) throw ParseError;
// per-entry problems (bad name offset, bad section index) become warnings.
SymbolTable parse_symbol_table(const ElfFile& file, const Section& section,
                               std::vector<std::string>& warnings);

// All symbol tables in section order (.symtab and/or .dynsym).
std::vector<SymbolTable> parse_symbol_tables(const ElfFile& file, std::vector<std::string>& warnings);

std::string symbol_binding_name(std::uint8_t binding);
std::string symbol_type_name(std::uint8_t type);
std::string symbol_visibility_name(std::uint8_t visibility);
std::string symbol_index_label(std::uint16_t section_index);  // "UND", "ABS", "COM", "12"
std::string symbol_status(const Symbol& symbol);  // "defined", "undefined", "absolute", "common"

// C++ demangling via the toolchain's ABI library when available; returns the
// input unchanged if it is not a mangled name or demangling is unavailable.
std::string demangle(const std::string& name);

}  // namespace binscope
