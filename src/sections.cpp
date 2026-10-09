#include "binscope/section.hpp"

#include "binscope/byte_reader.hpp"
#include "binscope/elf_constants.hpp"

#include <utility>

namespace binscope {

using namespace elf;

bool Section::has_file_contents() const { return type != SHT_NOBITS && type != SHT_NULL; }

std::string section_type_name(std::uint32_t type) {
    switch (type) {
        case SHT_NULL: return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB: return "SYMTAB";
        case SHT_STRTAB: return "STRTAB";
        case SHT_RELA: return "RELA";
        case SHT_HASH: return "HASH";
        case SHT_DYNAMIC: return "DYNAMIC";
        case SHT_NOTE: return "NOTE";
        case SHT_NOBITS: return "NOBITS";
        case SHT_REL: return "REL";
        case SHT_SHLIB: return "SHLIB";
        case SHT_DYNSYM: return "DYNSYM";
        case SHT_INIT_ARRAY: return "INIT_ARRAY";
        case SHT_FINI_ARRAY: return "FINI_ARRAY";
        case SHT_PREINIT_ARRAY: return "PREINIT_ARRAY";
        case SHT_GROUP: return "GROUP";
        case SHT_SYMTAB_SHNDX: return "SYMTAB_SHNDX";
        case SHT_RELR: return "RELR";
        case SHT_GNU_ATTRIBUTES: return "GNU_ATTRIBUTES";
        case SHT_GNU_HASH: return "GNU_HASH";
        case SHT_GNU_verdef: return "VERDEF";
        case SHT_GNU_verneed: return "VERNEED";
        case SHT_GNU_versym: return "VERSYM";
        case SHT_X86_64_UNWIND: return "X86_64_UNWIND";
        default: break;
    }
    if (type >= SHT_LOOS && type <= SHT_HIOS) return "LOOS+" + hex(type - SHT_LOOS);
    if (type >= SHT_LOPROC && type <= SHT_HIPROC) return "LOPROC+" + hex(type - SHT_LOPROC);
    if (type >= SHT_LOUSER) return "LOUSER+" + hex(type - SHT_LOUSER);
    return "unknown (" + hex(type) + ")";
}

namespace {

struct FlagInfo {
    std::uint64_t bit;
    char letter;       // same letters as `readelf -S` uses
    const char* name;
};

constexpr FlagInfo kFlags[] = {
    {SHF_WRITE, 'W', "WRITE"},
    {SHF_ALLOC, 'A', "ALLOC"},
    {SHF_EXECINSTR, 'X', "EXEC"},
    {SHF_MERGE, 'M', "MERGE"},
    {SHF_STRINGS, 'S', "STRINGS"},
    {SHF_INFO_LINK, 'I', "INFO_LINK"},
    {SHF_LINK_ORDER, 'L', "LINK_ORDER"},
    {SHF_OS_NONCONFORMING, 'O', "OS_NONCONFORMING"},
    {SHF_GROUP, 'G', "GROUP"},
    {SHF_TLS, 'T', "TLS"},
    {SHF_COMPRESSED, 'C', "COMPRESSED"},
    {SHF_GNU_RETAIN, 'R', "GNU_RETAIN"},
    {SHF_X86_64_LARGE, 'l', "X86_64_LARGE"},
    {SHF_EXCLUDE, 'E', "EXCLUDE"},
};

std::uint64_t known_flag_mask() {
    std::uint64_t mask = 0;
    for (const auto& f : kFlags) mask |= f.bit;
    return mask;
}

}  // namespace

std::string section_flags_string(std::uint64_t flags) {
    std::string out;
    for (const auto& f : kFlags) {
        if (flags & f.bit) out += f.letter;
    }
    if (flags & ~known_flag_mask()) out += 'x';  // bits BinScope does not decode
    return out;
}

std::string section_flags_verbose(std::uint64_t flags) {
    std::string out;
    for (const auto& f : kFlags) {
        if (flags & f.bit) {
            if (!out.empty()) out += '|';
            out += f.name;
        }
    }
    if (const std::uint64_t unknown = flags & ~known_flag_mask()) {
        if (!out.empty()) out += '|';
        out += hex(unknown);
    }
    return out.empty() ? "none" : out;
}

std::string section_purpose(const std::string& name) {
    // Exact names first, then well-known prefixes (e.g. ".rela.text.startup").
    static const std::pair<const char*, const char*> kExact[] = {
        {".text", "executable machine code"},
        {".rodata", "read-only data such as string literals and constant tables"},
        {".data", "initialized, writable global and static variables"},
        {".bss", "zero-initialized globals; occupies memory but no file bytes (NOBITS)"},
        {".tbss", "zero-initialized thread-local storage (NOBITS)"},
        {".tdata", "initialized thread-local storage"},
        {".symtab", "full symbol table used by linkers and debuggers"},
        {".strtab", "names referenced by .symtab"},
        {".shstrtab", "names of the sections themselves"},
        {".dynsym", "dynamic symbols the run-time loader needs to resolve"},
        {".dynstr", "names referenced by .dynsym and .dynamic"},
        {".dynamic", "dynamic-linking table read by the loader (DT_NEEDED, etc.)"},
        {".interp", "path of the program interpreter (dynamic loader)"},
        {".rela.dyn", "dynamic relocations applied by the loader at start-up"},
        {".rela.plt", "PLT/GOT relocations, usually resolved lazily on first call"},
        {".plt", "procedure linkage table stubs for calls into shared objects"},
        {".plt.got", "PLT stubs that jump through .got entries"},
        {".plt.sec", "second PLT used with Intel CET / IBT"},
        {".got", "global offset table: addresses filled in by the loader"},
        {".got.plt", "GOT entries used by PLT stubs"},
        {".init_array", "pointers to constructors run before main()"},
        {".fini_array", "pointers to destructors run at exit"},
        {".init", "legacy process initialization code"},
        {".fini", "legacy process termination code"},
        {".eh_frame", "call-frame information used for stack unwinding and C++ exceptions"},
        {".eh_frame_hdr", "binary-search index into .eh_frame"},
        {".comment", "compiler/linker version strings"},
        {".note.GNU-stack", "marker telling the linker whether the stack must be executable"},
        {".note.gnu.property", "GNU properties (e.g. CET/IBT/SHSTK markers)"},
        {".note.gnu.build-id", "unique build identifier"},
        {".note.ABI-tag", "minimum kernel ABI the binary targets"},
        {".gnu.hash", "GNU-style hash table for fast dynamic symbol lookup"},
        {".hash", "SysV-style hash table for dynamic symbol lookup"},
        {".gnu.version", "symbol version indices for .dynsym"},
        {".gnu.version_r", "versions required from shared libraries"},
        {".gnu.version_d", "versions defined by this object"},
        {".group", "COMDAT section group (deduplicated across object files)"},
        {".symtab_shndx", "extended section indices for .symtab"},
    };
    for (const auto& [n, purpose] : kExact) {
        if (name == n) return purpose;
    }

    static const std::pair<const char*, const char*> kPrefix[] = {
        {".rela.", "relocations with explicit addends for the named section"},
        {".rel.", "relocations with implicit addends for the named section"},
        {".debug_", "DWARF debugging information"},
        {".text.", "executable code (function-specific or grouped)"},
        {".rodata.", "read-only data (grouped or merged)"},
        {".data.rel.ro", "data that needs relocation, then becomes read-only (RELRO)"},
        {".data.", "initialized writable data (grouped)"},
        {".bss.", "zero-initialized data (grouped)"},
        {".note.", "note section with vendor/tool metadata"},
    };
    for (const auto& [prefix, purpose] : kPrefix) {
        if (name.rfind(prefix, 0) == 0) return purpose;
    }
    return {};
}

}  // namespace binscope
