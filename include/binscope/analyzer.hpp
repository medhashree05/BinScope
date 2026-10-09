#pragma once

#include "binscope/elf_file.hpp"
#include "binscope/relocation.hpp"
#include "binscope/symbol.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace binscope {

// ---------------------------------------------------------------------------
// Per-file overview used by `inspect`
// ---------------------------------------------------------------------------
struct SymbolTableSummary {
    std::string name;
    std::size_t entries = 0;
    std::size_t defined = 0;
    std::size_t undefined = 0;
};

struct RelocationSectionSummary {
    std::string name;
    std::string encoding;
    std::size_t entries = 0;
    std::string target;  // patched section, or empty for loader relocations
};

struct FileSummary {
    std::string file_name;
    std::uint16_t type = 0;
    std::size_t section_count = 0;
    std::size_t segment_count = 0;
    bool has_interpreter = false;    // PT_INTERP: started via the dynamic loader
    bool has_dynamic_section = false;  // .dynamic / PT_DYNAMIC
    bool has_symtab = false;
    bool has_debug_info = false;
    std::vector<SymbolTableSummary> symbol_tables;
    std::vector<RelocationSectionSummary> relocation_sections;
    std::size_t relr_sections = 0;
    // Set when a table could not be decoded; the rest of the summary still holds.
    std::optional<std::string> symbol_error;
    std::optional<std::string> relocation_error;
    std::vector<std::string> warnings;
};

FileSummary summarize(const ElfFile& file);

// ---------------------------------------------------------------------------
// Two-file comparison used by `compare` (typically object file vs. linked output)
// ---------------------------------------------------------------------------

// How one named symbol appears in one file (.symtab and .dynsym merged).
struct SymbolSide {
    bool defined = false;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    std::uint8_t type = 0;
    std::uint8_t binding = 0;
    std::string section;  // defining section name ("" if undefined)
    std::string version;  // e.g. "GLIBC_2.2.5" from a "name@GLIBC_2.2.5" spelling
    bool in_symtab = false;
    bool in_dynsym = false;
};

enum class MatchCategory {
    ResolvedByStaticLinker,  // undefined before, defined in a linked "after" file
    DefinedOnlyAfter,        // undefined before, defined after, but "after" is not linked output
    DeferredToDynamicLoader, // undefined in both, but a dynamic symbol after: bound at run time
    UndefinedInBoth,         // undefined in both and not handed to the dynamic loader
    DefinedInBoth,
    BecameUndefined,         // defined before, undefined after (unusual)
    OnlyInBefore,
    OnlyInAfter,
    Ambiguous,               // name occurs more than once in a table: no confident match
};

struct SymbolMatch {
    std::string name;  // matching key: symbol name without any @version suffix
    MatchCategory category = MatchCategory::Ambiguous;
    std::optional<SymbolSide> before;
    std::optional<SymbolSide> after;
    // For DefinedInBoth: whether the two values live in the same kind of address
    // space (both section offsets in the same section, or both virtual addresses).
    bool values_comparable = false;
};

struct SectionDiff {
    std::string name;
    std::optional<Section> before;
    std::optional<Section> after;
};

struct RelocationDiff {
    std::string name;
    std::optional<std::size_t> before;  // entry count, absent if the section is missing
    std::optional<std::size_t> after;
};

// When an object file is compared with linked output, every symbol defined in
// one input section moves by the same amount: the address where the linker
// placed that input section. Reported only if all matched symbols agree.
struct PlacementEstimate {
    std::string section;
    std::uint64_t address = 0;  // inferred virtual address of the input section's start
    std::size_t symbols = 0;    // how many matched symbols support the estimate
};

struct FileOverview {
    std::string name;
    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    std::uint64_t entry = 0;
    std::size_t sections = 0;
    std::size_t segments = 0;
};

struct ComparisonReport {
    FileOverview before;
    FileOverview after;
    std::vector<SectionDiff> sections;
    std::vector<SymbolMatch> symbols;  // ordered by category, then name
    std::vector<RelocationDiff> relocations;
    std::vector<PlacementEstimate> placements;  // only for object file -> linked output
    std::size_t relocations_before = 0;
    std::size_t relocations_after = 0;
    std::vector<std::string> notes;       // explanations that apply to this pair of files
    std::vector<std::string> incomplete;  // parts that could not be compared, and why
    std::vector<std::string> warnings;
};

ComparisonReport compare_files(const ElfFile& before, const ElfFile& after);

std::string match_category_title(MatchCategory category);

// "puts@GLIBC_2.2.5" -> {"puts", "GLIBC_2.2.5"}; "x@@V1" -> {"x", "V1"}.
std::pair<std::string, std::string> split_symbol_version(const std::string& name);

}  // namespace binscope
