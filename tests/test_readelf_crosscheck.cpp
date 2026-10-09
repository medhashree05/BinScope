// Cross-checks BinScope's parsed structures against GNU readelf for every
// fixture. readelf is used here only as an independent oracle in the tests;
// BinScope itself never runs external tools.
//
// Compared fields:
//   sections    - index, name, type, address, offset, size        (readelf -SW)
//   symbols     - index, value, size, type, binding, visibility,
//                 section index, name (version suffixes removed)   (readelf -Ws)
//   relocations - offset, r_info, type name, addend               (readelf -rW)

#include "binscope/analyzer.hpp"
#include "binscope/elf_file.hpp"
#include "binscope/relocation.hpp"
#include "binscope/section.hpp"
#include "binscope/symbol.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <cstdio>
#include <map>
#include <memory>
#include <regex>
#include <sstream>

using namespace binscope;

namespace {

#ifdef BINSCOPE_READELF

std::vector<std::string> run_lines(const std::string& command) {
    std::vector<std::string> lines;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return lines;
    std::string current;
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, pipe.get()) != nullptr) {
        current += buffer;
        if (!current.empty() && current.back() == '\n') {
            current.pop_back();
            lines.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

std::string readelf(const std::string& flags, const std::string& file) {
    return std::string(BINSCOPE_READELF) + " " + flags + " '" + fixture::path(file).string() + "' 2>/dev/null";
}

std::uint64_t parse_hex(const std::string& s) { return std::stoull(s, nullptr, 16); }

const std::vector<std::string> kFixtures = {"simple.o",     "simple_O2.o",  "helper.o",
                                            "main.o",       "data_only.o",  "program",
                                            "program_nopie", "libhelper.so"};

void fail(const std::string& file, const std::string& what) {
    bt::report_failure(__FILE__, __LINE__, file + ": " + what);
}

#endif

}  // namespace

TEST_CASE(sections_match_readelf) {
#ifdef BINSCOPE_READELF
    // "  [ 1] .text   PROGBITS   0000000000000000 000040 000043 00  AX  0   0  1"
    const std::regex row(R"(^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s)");
    for (const std::string& name : kFixtures) {
        const ElfFile f = ElfFile::load(fixture::path(name));
        std::size_t compared = 0;
        for (const std::string& line : run_lines(readelf("-SW", name))) {
            std::smatch m;
            if (!std::regex_search(line, m, row)) continue;
            const std::size_t index = std::stoul(m[1]);
            if (index == 0) continue;  // NULL section has an empty name column
            const Section* s = f.section_at(index);
            if (s == nullptr) {
                fail(name, "readelf lists section " + std::to_string(index) + " that BinScope lacks");
                continue;
            }
            if (s->name != m[2].str()) fail(name, "section " + std::to_string(index) + " name " + s->name + " vs " + m[2].str());
            if (section_type_name(s->type) != m[3].str())
                fail(name, "section " + s->name + " type " + section_type_name(s->type) + " vs " + m[3].str());
            if (s->addr != parse_hex(m[4])) fail(name, "section " + s->name + " address differs");
            if (s->offset != parse_hex(m[5])) fail(name, "section " + s->name + " offset differs");
            if (s->size != parse_hex(m[6])) fail(name, "section " + s->name + " size differs");
            ++compared;
        }
        if (compared + 1 != f.sections().size()) {
            fail(name, "compared " + std::to_string(compared) + " sections, BinScope has " +
                           std::to_string(f.sections().size()));
        }
        std::cout << "  " << name << ": " << compared << " sections match readelf\n";
    }
#else
    std::cout << "  (skipped: readelf not found)\n";
#endif
}

TEST_CASE(symbols_match_readelf) {
#ifdef BINSCOPE_READELF
    const std::regex header(R"(^Symbol table '([^']+)' contains (\d+) entr)");
    // "     5: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND _Z10helper_addii"
    const std::regex row(R"(^\s*(\d+):\s+([0-9a-f]+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s?(.*)$)");
    for (const std::string& name : kFixtures) {
        const ElfFile f = ElfFile::load(fixture::path(name));
        std::vector<std::string> warnings;
        std::map<std::string, SymbolTable> tables;
        for (auto& t : parse_symbol_tables(f, warnings)) tables[t.section_name] = std::move(t);

        const SymbolTable* current = nullptr;
        std::size_t compared = 0, expected = 0;
        for (const std::string& line : run_lines(readelf("-Ws", name))) {
            std::smatch m;
            if (std::regex_search(line, m, header)) {
                auto it = tables.find(m[1].str());
                if (it == tables.end()) {
                    fail(name, "BinScope is missing symbol table " + m[1].str());
                    current = nullptr;
                    continue;
                }
                current = &it->second;
                expected += current->symbols.size();
                if (std::stoul(m[2]) != current->symbols.size()) fail(name, "symbol count differs in " + m[1].str());
                continue;
            }
            if (current == nullptr || !std::regex_match(line, m, row)) continue;
            const std::size_t index = std::stoul(m[1]);
            if (index >= current->symbols.size()) {
                fail(name, "symbol index out of range");
                continue;
            }
            const Symbol& s = current->symbols[index];
            const std::string where = current->section_name + "[" + std::to_string(index) + "] " + s.name;
            if (s.value != parse_hex(m[2])) fail(name, where + ": value differs");
            if (s.size != std::stoull(m[3])) fail(name, where + ": size differs");
            if (symbol_type_name(s.type) != m[4].str()) fail(name, where + ": type " + m[4].str());
            if (symbol_binding_name(s.binding) != m[5].str()) fail(name, where + ": binding " + m[5].str());
            if (symbol_visibility_name(s.visibility) != m[6].str()) fail(name, where + ": visibility " + m[6].str());
            if (symbol_index_label(s.section_index) != m[7].str()) fail(name, where + ": ndx " + m[7].str());

            // readelf appends dynamic symbol versions ("puts@GLIBC_2.2.5 (2)") from
            // .gnu.version, which BinScope does not decode: compare base names.
            std::string theirs = m[8].str();
            if (const auto paren = theirs.find(" ("); paren != std::string::npos) theirs.erase(paren);
            if (split_symbol_version(theirs).first != split_symbol_version(s.name).first) {
                fail(name, where + ": name vs readelf '" + theirs + "'");
            }
            ++compared;
        }
        // Every fixture has at least one symbol table, so zero means the parse of
        // readelf's output silently failed.
        if (compared != expected || compared == 0) {
            fail(name, "compared " + std::to_string(compared) + " of " + std::to_string(expected) + " symbols");
        }
        std::cout << "  " << name << ": " << compared << " symbols match readelf\n";
    }
#else
    std::cout << "  (skipped: readelf not found)\n";
#endif
}

TEST_CASE(relocations_match_readelf) {
#ifdef BINSCOPE_READELF
    const std::regex header(R"(^Relocation section '([^']+)' at offset 0x[0-9a-f]+ contains (\d+) entr)");
    // "0000000000000017  0000000500000004 R_X86_64_PLT32   0000000000000000 sym - 4"
    // "0000000000003db8  0000000000000008 R_X86_64_RELATIVE                 1140"
    const std::regex row(R"(^([0-9a-f]{16})\s+([0-9a-f]{16})\s+(\S+)\s*(.*)$)");
    const std::regex signed_addend(R"(([+-]) ([0-9a-f]+)$)");
    const std::regex bare_addend(R"(^\s*([0-9a-f]+)$)");
    std::size_t total = 0;
    for (const std::string& name : kFixtures) {
        const ElfFile f = ElfFile::load(fixture::path(name));
        std::vector<std::string> warnings;
        std::map<std::string, RelocationSection> sections;
        for (auto& rs : parse_relocation_sections(f, warnings)) sections[rs.section_name] = std::move(rs);

        const RelocationSection* current = nullptr;
        std::size_t row_in_section = 0, compared = 0, expected = 0;
        for (const std::string& line : run_lines(readelf("-rW", name))) {
            std::smatch m;
            if (std::regex_search(line, m, header)) {
                auto it = sections.find(m[1].str());
                current = it == sections.end() ? nullptr : &it->second;
                if (current == nullptr) {
                    fail(name, "BinScope is missing relocation section " + m[1].str());
                    continue;
                }
                if (std::stoul(m[2]) != current->entries.size()) fail(name, "entry count differs in " + m[1].str());
                expected += current->entries.size();
                row_in_section = 0;
                continue;
            }
            if (current == nullptr || !std::regex_match(line, m, row)) continue;
            if (row_in_section >= current->entries.size()) {
                fail(name, "readelf lists more entries in " + current->section_name);
                continue;
            }
            const Relocation& r = current->entries[row_in_section++];
            const std::string where = current->section_name + "[" + std::to_string(r.index) + "]";
            const std::uint64_t info = (static_cast<std::uint64_t>(r.symbol_index) << 32) | r.type;
            if (r.offset != parse_hex(m[1])) fail(name, where + ": offset differs");
            if (info != parse_hex(m[2])) fail(name, where + ": r_info differs");
            if (relocation_type_name(f.header().machine, r.type) != m[3].str()) fail(name, where + ": type " + m[3].str());

            const std::string rest = m[4].str();
            std::smatch a;
            std::optional<std::int64_t> addend;
            if (std::regex_search(rest, a, signed_addend)) {
                const auto magnitude = static_cast<std::int64_t>(parse_hex(a[2]));
                addend = a[1] == "-" ? -magnitude : magnitude;
            } else if (std::regex_match(rest, a, bare_addend)) {
                addend = static_cast<std::int64_t>(parse_hex(a[1]));
            }
            if (r.addend && addend && *r.addend != *addend) fail(name, where + ": addend differs");
            ++compared;
        }
        if (compared != expected) {
            fail(name, "compared " + std::to_string(compared) + " of " + std::to_string(expected) + " relocations");
        }
        total += compared;
        std::cout << "  " << name << ": " << compared << " relocations match readelf\n";
    }
    if (total == 0) fail("all fixtures", "no relocations were compared");
#else
    std::cout << "  (skipped: readelf not found)\n";
#endif
}
