#include "binscope/symbol.hpp"

#include "binscope/elf_constants.hpp"
#include "binscope/error.hpp"

#include <cstdlib>
#include <memory>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define BINSCOPE_HAVE_CXXABI 1
#else
#define BINSCOPE_HAVE_CXXABI 0
#endif

namespace binscope {

using namespace elf;

bool Symbol::is_undefined() const { return section_index == SHN_UNDEF && !is_null(); }

bool Symbol::is_function() const { return type == STT_FUNC || type == STT_GNU_IFUNC; }

bool SymbolFilter::matches(const Symbol& symbol) const {
    if (!active()) return true;
    if (symbol.is_null()) return false;
    if (defined_only && symbol.section_index == SHN_UNDEF) return false;
    if (undefined_only && !symbol.is_undefined()) return false;
    if (functions_only && !symbol.is_function()) return false;
    return true;
}

namespace {

std::string table_label(const Section& s) {
    return "symbol table [" + std::to_string(s.index) + "] '" + s.name + "'";
}

// Validates sh_link of a symbol table and returns the string table bytes.
ByteView string_table_for(const ElfFile& file, const Section& table) {
    const Section* strtab = file.section_at(table.link);
    if (table.link == 0 || strtab == nullptr) {
        throw ParseError(table_label(table) + " links to invalid string table index " +
                         std::to_string(table.link));
    }
    if (strtab->type != SHT_STRTAB) {
        throw ParseError(table_label(table) + " links to section [" + std::to_string(table.link) +
                         "] which is " + section_type_name(strtab->type) + ", not STRTAB");
    }
    return file.section_data(*strtab);
}

}  // namespace

SymbolTable parse_symbol_table(const ElfFile& file, const Section& section,
                               std::vector<std::string>& warnings) {
    if (section.type != SHT_SYMTAB && section.type != SHT_DYNSYM) {
        throw ParseError("section [" + std::to_string(section.index) + "] is not a symbol table");
    }
    if (section.entsize != kSymSize) {
        throw ParseError(table_label(section) + " has entry size " + std::to_string(section.entsize) +
                         ", expected " + std::to_string(kSymSize) + " for Elf64_Sym");
    }
    if (section.size % kSymSize != 0) {
        throw ParseError(table_label(section) + " size " + std::to_string(section.size) +
                         " is not a multiple of the entry size");
    }

    const ByteView data = file.section_data(section);
    const ByteView strings = string_table_for(file, section);

    SymbolTable table;
    table.section_index = section.index;
    table.section_name = section.name;
    table.section_type = section.type;
    table.string_table_index = section.link;
    table.first_global = section.info;

    const std::uint64_t count = section.size / kSymSize;
    table.symbols.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const ByteView e = data.subview(i * kSymSize, kSymSize, "symbol entry");
        Symbol sym;
        sym.index = static_cast<std::uint32_t>(i);
        sym.name_offset = e.u32(0);
        const std::uint8_t info = e.u8(4);
        const std::uint8_t other = e.u8(5);
        sym.binding = static_cast<std::uint8_t>(info >> 4);
        sym.type = static_cast<std::uint8_t>(info & 0x0f);
        sym.visibility = static_cast<std::uint8_t>(other & 0x03);
        sym.section_index = e.u16(6);
        sym.value = e.u64(8);
        sym.size = e.u64(16);

        if (const auto name = strings.c_string(sym.name_offset)) {
            sym.name = std::string(*name);
        } else {
            sym.name_valid = false;
            sym.name = "<corrupt name @" + hex(sym.name_offset) + ">";
            warnings.push_back(table_label(section) + " entry " + std::to_string(i) +
                               ": name offset " + hex(sym.name_offset) +
                               " is outside the string table or unterminated");
        }

        // STT_SECTION symbols are conventionally unnamed; show the section instead.
        const std::uint16_t shndx = sym.section_index;
        if (shndx == SHN_UNDEF) {
            sym.section_name = "";
        } else if (shndx == SHN_ABS) {
            sym.section_name = "*ABS*";
        } else if (shndx == SHN_COMMON) {
            sym.section_name = "*COMMON*";
        } else if (shndx == SHN_XINDEX) {
            // The real index lives in a SHT_SYMTAB_SHNDX section, which BinScope
            // does not decode (it only appears with >= 0xff00 sections).
            sym.section_name = "<extended index: unsupported>";
            sym.section_index_valid = false;
            warnings.push_back(table_label(section) + " entry " + std::to_string(i) +
                               " uses SHN_XINDEX; extended symbol section indices are not supported");
        } else if (shndx >= SHN_LORESERVE) {
            sym.section_name = "<reserved index " + hex(shndx) + ">";
        } else if (const Section* target = file.section_at(shndx)) {
            sym.section_name = target->name;
        } else {
            sym.section_index_valid = false;
            sym.section_name = "<invalid section index " + std::to_string(shndx) + ">";
            warnings.push_back(table_label(section) + " entry " + std::to_string(i) +
                               ": section index " + std::to_string(shndx) + " is out of range");
        }
        if (sym.type == STT_SECTION && sym.name.empty() && sym.section_index_valid) {
            sym.name = sym.section_name;
        }
        table.symbols.push_back(std::move(sym));
    }
    return table;
}

std::vector<SymbolTable> parse_symbol_tables(const ElfFile& file, std::vector<std::string>& warnings) {
    std::vector<SymbolTable> tables;
    for (const Section& s : file.sections()) {
        if (s.type == SHT_SYMTAB || s.type == SHT_DYNSYM) {
            tables.push_back(parse_symbol_table(file, s, warnings));
        }
    }
    return tables;
}

std::string symbol_binding_name(std::uint8_t binding) {
    switch (binding) {
        case STB_LOCAL: return "LOCAL";
        case STB_GLOBAL: return "GLOBAL";
        case STB_WEAK: return "WEAK";
        case STB_GNU_UNIQUE: return "UNIQUE";
        default: return "<" + std::to_string(binding) + ">";
    }
}

std::string symbol_type_name(std::uint8_t type) {
    switch (type) {
        case STT_NOTYPE: return "NOTYPE";
        case STT_OBJECT: return "OBJECT";
        case STT_FUNC: return "FUNC";
        case STT_SECTION: return "SECTION";
        case STT_FILE: return "FILE";
        case STT_COMMON: return "COMMON";
        case STT_TLS: return "TLS";
        case STT_GNU_IFUNC: return "IFUNC";
        default: return "<" + std::to_string(type) + ">";
    }
}

std::string symbol_visibility_name(std::uint8_t visibility) {
    switch (visibility) {
        case STV_DEFAULT: return "DEFAULT";
        case STV_INTERNAL: return "INTERNAL";
        case STV_HIDDEN: return "HIDDEN";
        case STV_PROTECTED: return "PROTECTED";
        default: return "<" + std::to_string(visibility) + ">";
    }
}

std::string symbol_index_label(std::uint16_t section_index) {
    switch (section_index) {
        case SHN_UNDEF: return "UND";
        case SHN_ABS: return "ABS";
        case SHN_COMMON: return "COM";
        case SHN_XINDEX: return "XINDEX";
        default: return std::to_string(section_index);
    }
}

std::string symbol_status(const Symbol& symbol) {
    if (symbol.is_null()) return "-";
    switch (symbol.section_index) {
        case SHN_UNDEF: return "undefined";
        case SHN_ABS: return "absolute";
        case SHN_COMMON: return "common";
        default: return "defined";
    }
}

std::string demangle(const std::string& name) {
#if BINSCOPE_HAVE_CXXABI
    if (name.rfind("_Z", 0) != 0) return name;
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> result(
        abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status), std::free);
    if (status == 0 && result) return result.get();
#endif
    return name;
}

}  // namespace binscope
