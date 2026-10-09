#include "binscope/relocation.hpp"

#include "binscope/elf_constants.hpp"
#include "binscope/error.hpp"
#include "binscope/symbol.hpp"

#include <map>

namespace binscope {

using namespace elf;

namespace {

std::string label(const Section& s) {
    return "relocation section [" + std::to_string(s.index) + "] '" + s.name + "'";
}

// x86-64 psABI relocation table. Formula symbols:
//   S symbol value, A addend, P address of the place being relocated,
//   B load base of the object, G offset of the symbol's GOT entry,
//   GOT address of the GOT, L address of the symbol's PLT entry, Z symbol size.
struct X86RelocInfo {
    std::uint32_t type;
    const char* name;
    const char* formula;
    const char* explanation;
};

constexpr X86RelocInfo kX86_64[] = {
    {0, "R_X86_64_NONE", "none", "no relocation; placeholder entry"},
    {1, "R_X86_64_64", "S + A", "absolute 64-bit address of the symbol plus addend"},
    {2, "R_X86_64_PC32", "S + A - P",
     "32-bit displacement from the patched location (PC-relative) to the symbol"},
    {3, "R_X86_64_GOT32", "G + A", "32-bit offset of the symbol's GOT entry"},
    {4, "R_X86_64_PLT32", "L + A - P",
     "PC-relative call target via the PLT (direct if the symbol is resolved locally)"},
    {5, "R_X86_64_COPY", "none",
     "loader copies the symbol's initial data from a shared library into the executable"},
    {6, "R_X86_64_GLOB_DAT", "S",
     "loader stores the symbol's run-time address in a GOT entry"},
    {7, "R_X86_64_JUMP_SLOT", "S",
     "loader stores a function's address in its PLT GOT slot (may be bound lazily)"},
    {8, "R_X86_64_RELATIVE", "B + A",
     "loader adds the load base to a link-time address; no symbol lookup needed"},
    {9, "R_X86_64_GOTPCREL", "G + GOT + A - P",
     "PC-relative 32-bit displacement to the symbol's GOT entry"},
    {10, "R_X86_64_32", "S + A", "absolute address truncated to 32 bits (zero-extended)"},
    {11, "R_X86_64_32S", "S + A", "absolute address truncated to 32 bits (sign-extended)"},
    {12, "R_X86_64_16", "S + A", "absolute 16-bit value"},
    {13, "R_X86_64_PC16", "S + A - P", "PC-relative 16-bit displacement"},
    {14, "R_X86_64_8", "S + A", "absolute 8-bit value"},
    {15, "R_X86_64_PC8", "S + A - P", "PC-relative 8-bit displacement"},
    {16, "R_X86_64_DTPMOD64", "module id", "TLS: ID of the module containing the symbol"},
    {17, "R_X86_64_DTPOFF64", "offset in TLS block", "TLS: offset of the symbol in its module's TLS block"},
    {18, "R_X86_64_TPOFF64", "offset from thread pointer", "TLS: offset in the initial TLS block"},
    {19, "R_X86_64_TLSGD", "GOT entry for TLS GD", "TLS general-dynamic access sequence"},
    {20, "R_X86_64_TLSLD", "GOT entry for TLS LD", "TLS local-dynamic access sequence"},
    {21, "R_X86_64_DTPOFF32", "offset in TLS block", "TLS: 32-bit offset in the module's TLS block"},
    {22, "R_X86_64_GOTTPOFF", "GOT entry for TP offset", "TLS initial-exec access through the GOT"},
    {23, "R_X86_64_TPOFF32", "offset from thread pointer", "TLS local-exec: 32-bit offset from the thread pointer"},
    {24, "R_X86_64_PC64", "S + A - P", "PC-relative 64-bit displacement"},
    {25, "R_X86_64_GOTOFF64", "S + A - GOT", "64-bit offset of the symbol from the GOT"},
    {26, "R_X86_64_GOTPC32", "GOT + A - P", "PC-relative 32-bit displacement to the GOT"},
    {27, "R_X86_64_GOT64", "G + A", "64-bit offset of the symbol's GOT entry"},
    {28, "R_X86_64_GOTPCREL64", "G + GOT - P + A", "PC-relative 64-bit displacement to the GOT entry"},
    {29, "R_X86_64_GOTPC64", "GOT - P + A", "PC-relative 64-bit displacement to the GOT"},
    {30, "R_X86_64_GOTPLT64", "G + A", "64-bit GOT entry offset, PLT-related (large model)"},
    {31, "R_X86_64_PLTOFF64", "L - GOT + A", "64-bit offset of the PLT entry from the GOT"},
    {32, "R_X86_64_SIZE32", "Z + A", "32-bit size of the symbol"},
    {33, "R_X86_64_SIZE64", "Z + A", "64-bit size of the symbol"},
    {34, "R_X86_64_GOTPC32_TLSDESC", "TLS descriptor", "TLS descriptor: PC-relative GOT offset"},
    {35, "R_X86_64_TLSDESC_CALL", "none", "TLS descriptor: marks the descriptor call"},
    {36, "R_X86_64_TLSDESC", "TLS descriptor", "TLS descriptor filled in by the loader"},
    {37, "R_X86_64_IRELATIVE", "indirect (B + A)",
     "an IFUNC resolver at B + A is called at start-up and its result stored (by the dynamic "
     "loader, or by libc start-up code in static executables)"},
    {38, "R_X86_64_RELATIVE64", "B + A", "64-bit load-base-relative value"},
    {41, "R_X86_64_GOTPCRELX", "G + GOT + A - P",
     "like GOTPCREL, but the linker may relax the GOT load into a direct reference"},
    {42, "R_X86_64_REX_GOTPCRELX", "G + GOT + A - P",
     "like GOTPCRELX for REX-prefixed instructions; relaxable by the linker"},
};

const X86RelocInfo* x86_64_info(std::uint32_t type) {
    for (const auto& info : kX86_64) {
        if (info.type == type) return &info;
    }
    return nullptr;
}

}  // namespace

std::string relocation_type_name(std::uint16_t machine, std::uint32_t type) {
    if (machine == EM_X86_64) {
        if (const auto* info = x86_64_info(type)) return info->name;
    }
    return "type " + std::to_string(type);
}

std::string relocation_formula(std::uint16_t machine, std::uint32_t type) {
    if (machine == EM_X86_64) {
        if (const auto* info = x86_64_info(type)) return info->formula;
    }
    return {};
}

std::string relocation_explanation(std::uint16_t machine, std::uint32_t type) {
    if (machine == EM_X86_64) {
        if (const auto* info = x86_64_info(type)) return info->explanation;
    }
    return {};
}

std::string relocation_encoding_name(RelocationEncoding encoding) {
    return encoding == RelocationEncoding::Rela ? "RELA (explicit addend)"
                                                : "REL (implicit addend)";
}

std::size_t count_relr_sections(const ElfFile& file) {
    std::size_t n = 0;
    for (const Section& s : file.sections()) n += (s.type == SHT_RELR) ? 1U : 0U;
    return n;
}

std::vector<RelocationSection> parse_relocation_sections(const ElfFile& file,
                                                         std::vector<std::string>& warnings) {
    std::vector<RelocationSection> result;
    // Several relocation sections usually share one symbol table; decode each once.
    std::map<std::uint32_t, SymbolTable> symbol_tables;

    for (const Section& s : file.sections()) {
        if (s.type != SHT_REL && s.type != SHT_RELA) continue;

        const bool rela = s.type == SHT_RELA;
        const std::uint64_t entry_size = rela ? kRelaSize : kRelSize;
        if (s.entsize != entry_size) {
            throw ParseError(label(s) + " has entry size " + std::to_string(s.entsize) +
                             ", expected " + std::to_string(entry_size) +
                             (rela ? " for Elf64_Rela" : " for Elf64_Rel"));
        }
        if (s.size % entry_size != 0) {
            throw ParseError(label(s) + " size " + std::to_string(s.size) +
                             " is not a multiple of the entry size");
        }
        const ByteView data = file.section_data(s);

        RelocationSection rs;
        rs.section_index = s.index;
        rs.section_name = s.name;
        rs.encoding = rela ? RelocationEncoding::Rela : RelocationEncoding::Rel;
        rs.symbol_table_index = s.link;
        rs.target_section_index = s.info;

        const SymbolTable* symbols = nullptr;
        if (s.link != 0) {
            const Section* symtab = file.section_at(s.link);
            if (symtab == nullptr || (symtab->type != SHT_SYMTAB && symtab->type != SHT_DYNSYM)) {
                throw ParseError(label(s) + " sh_link " + std::to_string(s.link) +
                                 " does not refer to a symbol table");
            }
            rs.symbol_table_name = symtab->name;
            auto it = symbol_tables.find(s.link);
            if (it == symbol_tables.end()) {
                it = symbol_tables.emplace(s.link, parse_symbol_table(file, *symtab, warnings)).first;
            }
            symbols = &it->second;
        }

        if (s.info != 0) {
            if (const Section* target = file.section_at(s.info)) {
                rs.target_section_name = target->name;
            } else {
                warnings.push_back(label(s) + " sh_info " + std::to_string(s.info) +
                                   " is not a valid section index");
                rs.target_section_name = "<invalid section " + std::to_string(s.info) + ">";
            }
        }

        const std::uint64_t count = s.size / entry_size;
        rs.entries.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const ByteView e = data.subview(i * entry_size, entry_size, "relocation entry");
            Relocation r;
            r.index = static_cast<std::uint32_t>(i);
            r.offset = e.u64(0);
            const std::uint64_t info = e.u64(8);
            // ELF64_R_SYM / ELF64_R_TYPE: symbol index in the high word, type in the low word.
            r.symbol_index = static_cast<std::uint32_t>(info >> 32);
            r.type = static_cast<std::uint32_t>(info & 0xffffffffU);
            if (rela) r.addend = e.i64(16);

            if (r.symbol_index == 0) {
                r.symbol_name = "";  // no symbol (e.g. R_X86_64_RELATIVE)
            } else if (symbols == nullptr) {
                r.symbol_valid = false;
                r.symbol_name = "<no symbol table>";
                warnings.push_back(label(s) + " entry " + std::to_string(i) + " references symbol " +
                                   std::to_string(r.symbol_index) +
                                   " but the section has no linked symbol table");
            } else if (r.symbol_index >= symbols->symbols.size()) {
                r.symbol_valid = false;
                r.symbol_name = "<invalid symbol index " + std::to_string(r.symbol_index) + ">";
                warnings.push_back(label(s) + " entry " + std::to_string(i) + ": symbol index " +
                                   std::to_string(r.symbol_index) + " is outside '" +
                                   rs.symbol_table_name + "' (" +
                                   std::to_string(symbols->symbols.size()) + " entries)");
            } else {
                const Symbol& sym = symbols->symbols[r.symbol_index];
                r.symbol_name = sym.name;
                r.symbol_value = sym.value;
            }
            rs.entries.push_back(std::move(r));
        }
        result.push_back(std::move(rs));
    }
    return result;
}

}  // namespace binscope
