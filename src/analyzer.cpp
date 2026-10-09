#include "binscope/analyzer.hpp"

#include "binscope/elf_constants.hpp"
#include "binscope/error.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace binscope {

using namespace elf;

namespace {

// Relocation decoding re-reads the linked symbol tables, which can repeat
// warnings already reported for those tables; keep the first occurrence of each.
void remove_duplicate_warnings(std::vector<std::string>& warnings) {
    std::vector<std::string> unique;
    for (std::string& w : warnings) {
        if (std::find(unique.begin(), unique.end(), w) == unique.end()) unique.push_back(std::move(w));
    }
    warnings = std::move(unique);
}

}  // namespace

FileSummary summarize(const ElfFile& file) {
    FileSummary s;
    s.file_name = file.name();
    s.type = file.header().type;
    s.section_count = file.sections().size();
    s.segment_count = file.program_headers().size();

    for (const ProgramHeader& p : file.program_headers()) {
        if (p.type == PT_INTERP) s.has_interpreter = true;
        if (p.type == PT_DYNAMIC) s.has_dynamic_section = true;
    }
    for (const Section& sec : file.sections()) {
        if (sec.type == SHT_DYNAMIC) s.has_dynamic_section = true;
        if (sec.type == SHT_SYMTAB) s.has_symtab = true;
        if (sec.name.rfind(".debug_", 0) == 0) s.has_debug_info = true;
    }

    // Symbol and relocation tables are decoded independently so that one
    // corrupt table does not hide information about the other.
    try {
        for (const SymbolTable& t : parse_symbol_tables(file, s.warnings)) {
            SymbolTableSummary ts;
            ts.name = t.section_name;
            ts.entries = t.symbols.size();
            for (const Symbol& sym : t.symbols) {
                if (sym.is_null()) continue;
                (sym.is_undefined() ? ts.undefined : ts.defined)++;
            }
            s.symbol_tables.push_back(ts);
        }
    } catch (const Error& e) {
        s.symbol_error = e.what();
    }

    try {
        for (const RelocationSection& r : parse_relocation_sections(file, s.warnings)) {
            s.relocation_sections.push_back(
                {r.section_name, relocation_encoding_name(r.encoding), r.entries.size(),
                 r.target_section_name});
        }
    } catch (const Error& e) {
        s.relocation_error = e.what();
    }
    s.relr_sections = count_relr_sections(file);

    remove_duplicate_warnings(s.warnings);
    return s;
}

std::pair<std::string, std::string> split_symbol_version(const std::string& name) {
    const std::size_t at = name.find('@');
    if (at == std::string::npos || at == 0) return {name, ""};
    std::size_t version_start = at + 1;
    if (version_start < name.size() && name[version_start] == '@') ++version_start;  // "@@" = default
    return {name.substr(0, at), name.substr(version_start)};
}

std::string match_category_title(MatchCategory category) {
    switch (category) {
        case MatchCategory::ResolvedByStaticLinker:
            return "Resolved by the static linker (undefined before, defined after)";
        case MatchCategory::DefinedOnlyAfter:
            return "Undefined before, defined after (after is not linked output)";
        case MatchCategory::DeferredToDynamicLoader:
            return "Left for the dynamic loader (still undefined after, but a dynamic symbol)";
        case MatchCategory::UndefinedInBoth:
            return "Undefined in both (not handed to the dynamic loader)";
        case MatchCategory::DefinedInBoth:
            return "Defined in both";
        case MatchCategory::BecameUndefined:
            return "Defined before but undefined after";
        case MatchCategory::OnlyInBefore:
            return "Only in before (no symbol with this name after)";
        case MatchCategory::OnlyInAfter:
            return "Only in after (no symbol with this name before)";
        case MatchCategory::Ambiguous:
            return "Cannot be matched confidently (name occurs more than once in a table)";
    }
    return "?";
}

namespace {

FileOverview overview(const ElfFile& f) {
    return {f.name(), f.header().type, f.header().machine, f.header().entry, f.sections().size(),
            f.program_headers().size()};
}

// Symbols worth matching by name: skip the null entry, section and file
// symbols (they describe the file layout, not program entities), and unnamed
// or corrupt entries.
bool matchable(const Symbol& s) {
    return !s.is_null() && s.type != STT_SECTION && s.type != STT_FILE && !s.name.empty() &&
           s.name_valid;
}

struct CollectedSymbols {
    std::map<std::string, SymbolSide> sides;
    std::set<std::string> ambiguous;
};

CollectedSymbols collect_symbols(const std::vector<SymbolTable>& tables) {
    CollectedSymbols c;
    for (const SymbolTable& table : tables) {
        const bool dynamic = table.section_type == SHT_DYNSYM;
        std::map<std::string, int> occurrences;
        for (const Symbol& sym : table.symbols) {
            if (!matchable(sym)) continue;
            const auto [base, version] = split_symbol_version(sym.name);
            if (++occurrences[base] > 1) c.ambiguous.insert(base);

            const bool defined = sym.section_index != SHN_UNDEF;
            auto it = c.sides.find(base);
            const bool first = it == c.sides.end();
            SymbolSide& side = first ? c.sides[base] : it->second;
            // .symtab and .dynsym describe the same entity; prefer a defining entry.
            if (first || (defined && !side.defined)) {
                side.defined = defined;
                side.value = sym.value;
                side.size = sym.size;
                side.type = sym.type;
                side.binding = sym.binding;
                side.section = defined ? sym.section_name : "";
            }
            if (!version.empty()) side.version = version;
            (dynamic ? side.in_dynsym : side.in_symtab) = true;
        }
    }
    return c;
}

int category_rank(MatchCategory c) { return static_cast<int>(c); }

// Section names can repeat (e.g. several ".group" or ".text.<fn>" sections in
// objects with COMDAT groups); later occurrences get a "#n" suffix so each is
// matched with its counterpart in order.
template <typename T, typename NameOf>
std::vector<std::pair<std::string, const T*>> keyed_by_name(const std::vector<T>& items, NameOf name_of) {
    std::vector<std::pair<std::string, const T*>> keyed;
    std::map<std::string, int> seen;
    for (const T& item : items) {
        const std::string name = name_of(item);
        const int n = ++seen[name];
        keyed.emplace_back(n == 1 ? name : name + " #" + std::to_string(n), &item);
    }
    return keyed;
}

}  // namespace

ComparisonReport compare_files(const ElfFile& before, const ElfFile& after) {
    ComparisonReport r;
    r.before = overview(before);
    r.after = overview(after);
    const bool before_rel = r.before.type == ET_REL;
    const bool after_rel = r.after.type == ET_REL;

    // --- Sections ------------------------------------------------------------
    {
        auto name_of = [](const Section& s) { return s.name; };
        std::map<std::string, SectionDiff> diffs;
        std::vector<std::string> order;
        for (const auto& [key, sec] : keyed_by_name(before.sections(), name_of)) {
            if (sec->index == 0) continue;  // the mandatory null section
            diffs[key].name = key;
            diffs[key].before = *sec;
            order.push_back(key);
        }
        for (const auto& [key, sec] : keyed_by_name(after.sections(), name_of)) {
            if (sec->index == 0) continue;
            if (diffs.count(key) == 0) order.push_back(key);
            diffs[key].name = key;
            diffs[key].after = *sec;
        }
        for (const std::string& key : order) r.sections.push_back(diffs[key]);
    }

    // --- Symbols -------------------------------------------------------------
    std::optional<CollectedSymbols> sym_before, sym_after;
    try {
        sym_before = collect_symbols(parse_symbol_tables(before, r.warnings));
    } catch (const Error& e) {
        r.incomplete.push_back("symbols of '" + before.name() + "' could not be decoded: " + e.what());
    }
    try {
        sym_after = collect_symbols(parse_symbol_tables(after, r.warnings));
    } catch (const Error& e) {
        r.incomplete.push_back("symbols of '" + after.name() + "' could not be decoded: " + e.what());
    }

    if (sym_before && sym_after) {
        std::set<std::string> names;
        for (const auto& [n, side] : sym_before->sides) names.insert(n);
        for (const auto& [n, side] : sym_after->sides) names.insert(n);

        for (const std::string& name : names) {
            SymbolMatch m;
            m.name = name;
            if (auto it = sym_before->sides.find(name); it != sym_before->sides.end()) m.before = it->second;
            if (auto it = sym_after->sides.find(name); it != sym_after->sides.end()) m.after = it->second;

            if (sym_before->ambiguous.count(name) || sym_after->ambiguous.count(name)) {
                m.category = MatchCategory::Ambiguous;
            } else if (m.before && m.after) {
                if (!m.before->defined && m.after->defined) {
                    // Only call it link-time resolution if "after" is linked output.
                    m.category = after_rel ? MatchCategory::DefinedOnlyAfter
                                           : MatchCategory::ResolvedByStaticLinker;
                } else if (!m.before->defined && !m.after->defined) {
                    m.category = m.after->in_dynsym ? MatchCategory::DeferredToDynamicLoader
                                                    : MatchCategory::UndefinedInBoth;
                } else if (m.before->defined && m.after->defined) {
                    m.category = MatchCategory::DefinedInBoth;
                    // Section offsets (ET_REL) and virtual addresses (linked files)
                    // are different coordinate systems; offsets are only
                    // comparable within a section of the same name.
                    m.values_comparable =
                        before_rel == after_rel && (!before_rel || m.before->section == m.after->section);
                } else {
                    m.category = MatchCategory::BecameUndefined;
                }
            } else {
                m.category = m.before ? MatchCategory::OnlyInBefore : MatchCategory::OnlyInAfter;
            }
            r.symbols.push_back(std::move(m));
        }
        std::stable_sort(r.symbols.begin(), r.symbols.end(), [](const SymbolMatch& a, const SymbolMatch& b) {
            return category_rank(a.category) < category_rank(b.category);
        });
        // Infer where the linker placed each input section: after.value -
        // before.value must be identical for every symbol from that section.
        if (before_rel && !after_rel) {
            struct Accumulator {
                std::uint64_t delta = 0;
                std::size_t count = 0;
                bool consistent = true;
            };
            std::map<std::string, Accumulator> by_section;
            for (const SymbolMatch& m : r.symbols) {
                if (m.category != MatchCategory::DefinedInBoth) continue;
                if (m.before->section != m.after->section || m.before->section.empty()) continue;
                const std::uint64_t delta = m.after->value - m.before->value;  // modular on purpose
                Accumulator& acc = by_section[m.before->section];
                if (acc.count == 0) acc.delta = delta;
                else if (acc.delta != delta) acc.consistent = false;
                ++acc.count;
            }
            for (const auto& [section, acc] : by_section) {
                if (acc.consistent) r.placements.push_back({section, acc.delta, acc.count});
            }
        }
    } else {
        r.incomplete.push_back("symbol matching was skipped because one side's symbols are unavailable");
    }

    // --- Relocations ---------------------------------------------------------
    std::optional<std::vector<RelocationSection>> rel_before, rel_after;
    try {
        rel_before = parse_relocation_sections(before, r.warnings);
    } catch (const Error& e) {
        r.incomplete.push_back("relocations of '" + before.name() + "' could not be decoded: " + e.what());
    }
    try {
        rel_after = parse_relocation_sections(after, r.warnings);
    } catch (const Error& e) {
        r.incomplete.push_back("relocations of '" + after.name() + "' could not be decoded: " + e.what());
    }
    if (rel_before && rel_after) {
        auto name_of = [](const RelocationSection& s) { return s.section_name; };
        std::map<std::string, RelocationDiff> diffs;
        std::vector<std::string> order;
        for (const auto& [key, rs] : keyed_by_name(*rel_before, name_of)) {
            diffs[key].name = key;
            diffs[key].before = rs->entries.size();
            r.relocations_before += rs->entries.size();
            order.push_back(key);
        }
        for (const auto& [key, rs] : keyed_by_name(*rel_after, name_of)) {
            if (diffs.count(key) == 0) order.push_back(key);
            diffs[key].name = key;
            diffs[key].after = rs->entries.size();
            r.relocations_after += rs->entries.size();
        }
        for (const std::string& key : order) r.relocations.push_back(diffs[key]);
    }
    if (count_relr_sections(before) || count_relr_sections(after)) {
        r.incomplete.push_back("SHT_RELR packed relocations are present but not decoded or counted");
    }

    // --- Context-dependent explanations --------------------------------------
    if (r.before.machine != r.after.machine) {
        r.notes.push_back("The files target different machines (" + machine_name(r.before.machine) +
                          " vs " + machine_name(r.after.machine) +
                          "); the comparison is structural only.");
    }
    if (before_rel && !after_rel) {
        r.notes.push_back(
            "Symbol values in the object file are offsets within their section; in the linked "
            "file they are virtual addresses. They are shown side by side, not compared numerically.");
        r.notes.push_back(
            "Output sections are built by concatenating the same-named input sections of every "
            "object file (plus runtime startup objects), so their sizes usually grow.");
        bool consumed = false;
        for (const auto& d : r.relocations) consumed = consumed || (d.before && !d.after);
        if (consumed) {
            r.notes.push_back(
                "Relocation sections present only before were applied by the static linker while "
                "it fixed up code and data; they are not kept in the output unless the link uses "
                "--emit-relocs (-q).");
        }
        bool dynamic = false;
        for (const auto& d : r.relocations) dynamic = dynamic || (!d.before && d.after);
        if (dynamic) {
            r.notes.push_back(
                "Relocation sections present only after (.rela.dyn, .rela.plt) are dynamic "
                "relocations: the run-time loader applies them, so linking did not remove every "
                "relocation.");
        }
    }
    if (!after_rel && r.after.type == ET_DYN) {
        r.notes.push_back("The linked file is position independent (ET_DYN): its addresses are "
                          "relative to a load base chosen at run time.");
    }
    if (before_rel && after_rel) {
        r.notes.push_back(
            "Both files are relocatable objects: symbol values are section offsets and are "
            "compared only when the symbol stays in a section with the same name.");
    }
    remove_duplicate_warnings(r.warnings);
    return r;
}

}  // namespace binscope
