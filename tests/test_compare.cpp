// Linking analysis: object file vs. linked output, and object vs. object.

#include "binscope/analyzer.hpp"
#include "binscope/cli.hpp"
#include "binscope/elf_constants.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <sstream>

using namespace binscope;
using namespace binscope::elf;

namespace {

const SymbolMatch* match_for(const ComparisonReport& r, const std::string& name) {
    for (const auto& m : r.symbols) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

const RelocationDiff* reloc_diff(const ComparisonReport& r, const std::string& name) {
    for (const auto& d : r.relocations) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

bool any_note_contains(const ComparisonReport& r, const std::string& needle) {
    for (const auto& n : r.notes) {
        if (n.find(needle) != std::string::npos) return true;
    }
    return false;
}

ComparisonReport compare(const std::string& a, const std::string& b) {
    return compare_files(ElfFile::load(fixture::path(a)), ElfFile::load(fixture::path(b)));
}

}  // namespace

TEST_CASE(split_symbol_version_handles_all_spellings) {
    CHECK(split_symbol_version("puts@GLIBC_2.2.5") == std::make_pair(std::string("puts"), std::string("GLIBC_2.2.5")));
    CHECK(split_symbol_version("foo@@VERS_1") == std::make_pair(std::string("foo"), std::string("VERS_1")));
    CHECK(split_symbol_version("plain") == std::make_pair(std::string("plain"), std::string()));
    CHECK(split_symbol_version("@odd") == std::make_pair(std::string("@odd"), std::string()));
}

TEST_CASE(object_versus_linked_executable) {
    const ComparisonReport r = compare("main.o", "program");
    CHECK_EQ(r.before.type, ET_REL);
    CHECK_EQ(r.after.type, ET_DYN);
    CHECK(r.incomplete.empty());

    // Defined in helper.o, so the static linker resolved these references.
    const SymbolMatch* add = match_for(r, "_Z10helper_addii");
    REQUIRE(add != nullptr);
    CHECK(add->category == MatchCategory::ResolvedByStaticLinker);
    CHECK(!add->before->defined);
    CHECK(add->after->defined);
    CHECK_EQ(add->after->section, std::string(".text"));

    const SymbolMatch* counter = match_for(r, "helper_counter");
    REQUIRE(counter != nullptr);
    CHECK(counter->category == MatchCategory::ResolvedByStaticLinker);

    // puts lives in libc.so: still undefined, now handed to the dynamic loader.
    const SymbolMatch* puts_match = match_for(r, "puts");
    REQUIRE(puts_match != nullptr);
    CHECK(puts_match->category == MatchCategory::DeferredToDynamicLoader);
    CHECK(puts_match->after->in_dynsym);

    // main is defined in both, but offset vs. virtual address are not comparable.
    const SymbolMatch* main_match = match_for(r, "main");
    REQUIRE(main_match != nullptr);
    CHECK(main_match->category == MatchCategory::DefinedInBoth);
    CHECK(!main_match->values_comparable);
    CHECK_EQ(main_match->before->size, main_match->after->size);  // same machine code

    // Startup code comes from crt objects, not main.o.
    const SymbolMatch* start = match_for(r, "_start");
    REQUIRE(start != nullptr);
    CHECK(start->category == MatchCategory::OnlyInAfter);
}

TEST_CASE(input_section_placement_is_inferred) {
    const ComparisonReport r = compare("main.o", "program");
    const SymbolMatch* main_match = match_for(r, "main");
    REQUIRE(main_match != nullptr);
    bool found = false;
    for (const auto& p : r.placements) {
        if (p.section != ".text") continue;
        found = true;
        CHECK_EQ(p.address, main_match->after->value - main_match->before->value);
        CHECK(p.symbols >= 1);
    }
    CHECK(found);

    // Object vs object: no linking happened, so nothing is inferred.
    CHECK(compare("simple.o", "simple_O2.o").placements.empty());
}

TEST_CASE(relocations_change_but_do_not_all_disappear) {
    const ComparisonReport r = compare("main.o", "program");
    const RelocationDiff* text = reloc_diff(r, ".rela.text");
    REQUIRE(text != nullptr);
    CHECK(text->before.has_value());
    CHECK(!text->after.has_value());

    const RelocationDiff* dyn = reloc_diff(r, ".rela.dyn");
    REQUIRE(dyn != nullptr);
    CHECK(!dyn->before.has_value());
    CHECK(dyn->after.has_value());
    CHECK(r.relocations_after > 0);
    CHECK(any_note_contains(r, "dynamic relocations"));
    CHECK(any_note_contains(r, "virtual addresses"));
}

TEST_CASE(section_changes_are_reported) {
    const ComparisonReport r = compare("main.o", "program");
    bool interp_only_after = false, text_in_both = false;
    for (const auto& d : r.sections) {
        if (d.name == ".interp") interp_only_after = !d.before && d.after;
        if (d.name == ".text") {
            text_in_both = d.before && d.after;
            if (text_in_both) {
                CHECK_EQ(d.before->addr, 0U);
                CHECK(d.after->addr != 0);
            }
        }
    }
    CHECK(interp_only_after);
    CHECK(text_in_both);
}

TEST_CASE(non_pie_executable_comparison) {
    const ComparisonReport r = compare("main.o", "program_nopie");
    CHECK_EQ(r.after.type, ET_EXEC);
    const SymbolMatch* add = match_for(r, "_Z10helper_addii");
    REQUIRE(add != nullptr);
    CHECK(add->category == MatchCategory::ResolvedByStaticLinker);
    CHECK(!any_note_contains(r, "position independent"));
}

TEST_CASE(object_versus_object_does_not_claim_linking) {
    const ComparisonReport r = compare("main.o", "helper.o");
    const SymbolMatch* add = match_for(r, "_Z10helper_addii");
    REQUIRE(add != nullptr);
    CHECK(add->category == MatchCategory::DefinedOnlyAfter);
    CHECK(any_note_contains(r, "Both files are relocatable objects"));
}

TEST_CASE(optimization_levels_compare_section_offsets) {
    const ComparisonReport r = compare("simple.o", "simple_O2.o");
    const SymbolMatch* counter = match_for(r, "simple_counter");
    REQUIRE(counter != nullptr);
    CHECK(counter->category == MatchCategory::DefinedInBoth);
    CHECK(counter->values_comparable);  // same section, same coordinate system
    const SymbolMatch* compute = match_for(r, "_Z14simple_computei");
    REQUIRE(compute != nullptr);
    CHECK(compute->category == MatchCategory::DefinedInBoth);
}

TEST_CASE(duplicate_names_are_not_matched_confidently) {
    // Make two .symtab entries of main.o share the name "helper_counter".
    auto b = fixture::bytes("main.o");
    const ElfFile original = fixture::parse(b);
    std::vector<std::string> w;
    const auto tables = parse_symbol_tables(original, w);
    std::uint32_t counter_name = 0, puts_index = 0;
    for (const auto& s : tables.at(0).symbols) {
        if (s.name == "helper_counter") counter_name = s.name_offset;
        if (s.name == "puts") puts_index = s.index;
    }
    REQUIRE(counter_name != 0 && puts_index != 0);
    const std::size_t symtab = fixture::section_header_offset(b, fixture::find_section_by_type(b, SHT_SYMTAB));
    const std::size_t entries = static_cast<std::size_t>(fixture::get(b, symtab + fixture::kShOffset, 8));
    fixture::put(b, entries + puts_index * 24, 4, counter_name);

    const ComparisonReport r =
        compare_files(fixture::parse(b, "mutated main.o"), ElfFile::load(fixture::path("program")));
    const SymbolMatch* m = match_for(r, "helper_counter");
    REQUIRE(m != nullptr);
    CHECK(m->category == MatchCategory::Ambiguous);
}

TEST_CASE(corrupt_symbol_table_makes_comparison_incomplete) {
    auto b = fixture::bytes("main.o");
    const std::size_t symtab = fixture::section_header_offset(b, fixture::find_section_by_type(b, SHT_SYMTAB));
    fixture::put(b, symtab + fixture::kShEntsize, 8, 8);
    const ComparisonReport r =
        compare_files(fixture::parse(b, "broken.o"), ElfFile::load(fixture::path("program")));
    CHECK(!r.incomplete.empty());
    CHECK(r.symbols.empty());
    CHECK(!r.sections.empty());  // the rest of the comparison still happens
}

TEST_CASE(compare_command_output_and_usage) {
    std::ostringstream out, err;
    const int code = cli::run({"compare", fixture::path("main.o").string(),
                               fixture::path("program").string(), "--demangle"},
                              out, err);
    CHECK_EQ(code, cli::kExitSuccess);
    const std::string s = out.str();
    CHECK(s.find("Resolved by the static linker") != std::string::npos);
    CHECK(s.find("helper_add(int, int)") != std::string::npos);
    CHECK(s.find("Left for the dynamic loader") != std::string::npos);
    CHECK(s.find(".rela.dyn") != std::string::npos);

    std::ostringstream out2, err2;
    CHECK_EQ(cli::run({"compare", fixture::path("main.o").string()}, out2, err2), cli::kExitUsage);
    CHECK(err2.str().find("needs 2 file arguments") != std::string::npos);
}
