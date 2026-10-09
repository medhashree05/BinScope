// Symbol-table decoding, filtering and reporting.

#include "binscope/cli.hpp"
#include "binscope/elf_constants.hpp"
#include "binscope/symbol.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <set>
#include <sstream>

using namespace binscope;
using namespace binscope::elf;

namespace {

std::vector<SymbolTable> tables_of(const ElfFile& f) {
    std::vector<std::string> warnings;
    auto tables = parse_symbol_tables(f, warnings);
    if (!warnings.empty()) bt::report_failure(__FILE__, __LINE__, "unexpected warning: " + warnings[0]);
    return tables;
}

const SymbolTable* table_named(const std::vector<SymbolTable>& tables, const std::string& name) {
    for (const auto& t : tables) {
        if (t.section_name == name) return &t;
    }
    return nullptr;
}

const Symbol* symbol_named(const SymbolTable& table, const std::string& name) {
    for (const auto& s : table.symbols) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

std::set<std::string> names_matching(const SymbolTable& table, const SymbolFilter& filter) {
    std::set<std::string> names;
    for (const auto& s : table.symbols) {
        if (filter.matches(s)) names.insert(s.name);
    }
    return names;
}

}  // namespace

TEST_CASE(object_symbols_are_decoded) {
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    const auto tables = tables_of(f);
    REQUIRE(tables.size() == 1);  // objects have .symtab but no .dynsym
    const SymbolTable& symtab = tables[0];
    CHECK_EQ(symtab.section_name, std::string(".symtab"));
    CHECK(symtab.symbols.at(0).is_null());

    const Symbol* compute = symbol_named(symtab, "_Z14simple_computei");
    REQUIRE(compute != nullptr);
    CHECK_EQ(compute->type, STT_FUNC);
    CHECK_EQ(compute->binding, STB_GLOBAL);
    CHECK_EQ(compute->visibility, STV_DEFAULT);
    CHECK_EQ(compute->section_name, std::string(".text"));
    CHECK(compute->size > 0);
    CHECK_EQ(symbol_status(*compute), std::string("defined"));

    const Symbol* counter = symbol_named(symtab, "simple_counter");
    REQUIRE(counter != nullptr);
    CHECK_EQ(counter->type, STT_OBJECT);
    CHECK_EQ(counter->section_name, std::string(".data"));
    CHECK_EQ(counter->size, 4U);

    const Symbol* zeroed = symbol_named(symtab, "simple_zeroed");
    REQUIRE(zeroed != nullptr);
    CHECK_EQ(zeroed->section_name, std::string(".bss"));

    const Symbol* square = symbol_named(symtab, "_ZL6squarei");  // static -> local
    REQUIRE(square != nullptr);
    CHECK_EQ(square->binding, STB_LOCAL);
    CHECK_EQ(square->type, STT_FUNC);

    const Symbol* file_sym = symbol_named(symtab, "simple.cpp");
    REQUIRE(file_sym != nullptr);
    CHECK_EQ(file_sym->type, STT_FILE);
    CHECK_EQ(file_sym->section_index, SHN_ABS);
}

TEST_CASE(local_symbols_precede_globals) {
    // ELF requires all STB_LOCAL symbols before sh_info, all others after.
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    const auto tables = tables_of(f);
    const SymbolTable& symtab = tables.at(0);
    for (const Symbol& s : symtab.symbols) {
        if (s.index < symtab.first_global) {
            CHECK_EQ(s.binding, STB_LOCAL);
        } else {
            CHECK(s.binding != STB_LOCAL);
        }
    }
}

TEST_CASE(external_references_are_undefined) {
    const ElfFile f = ElfFile::load(fixture::path("main.o"));
    const auto tables = tables_of(f);
    const SymbolTable& symtab = tables.at(0);
    SymbolFilter undefined;
    undefined.undefined_only = true;
    const auto names = names_matching(symtab, undefined);
    CHECK(names.count("_Z10helper_addii") == 1);
    CHECK(names.count("helper_counter") == 1);
    CHECK(names.count("puts") == 1);
    CHECK(names.count("main") == 0);

    const Symbol* helper_add = symbol_named(symtab, "_Z10helper_addii");
    REQUIRE(helper_add != nullptr);
    CHECK_EQ(helper_add->section_index, SHN_UNDEF);
    CHECK_EQ(helper_add->value, 0U);
    CHECK_EQ(symbol_status(*helper_add), std::string("undefined"));
}

TEST_CASE(definitions_live_in_the_other_object) {
    const ElfFile f = ElfFile::load(fixture::path("helper.o"));
    const auto tables = tables_of(f);
    const SymbolTable& symtab = tables.at(0);
    const Symbol* helper_add = symbol_named(symtab, "_Z10helper_addii");
    REQUIRE(helper_add != nullptr);
    CHECK(!helper_add->is_undefined());
    CHECK_EQ(helper_add->type, STT_FUNC);
    const Symbol* counter = symbol_named(symtab, "helper_counter");
    REQUIRE(counter != nullptr);
    CHECK_EQ(counter->section_name, std::string(".data"));
}

TEST_CASE(filters_select_parsed_records) {
    const ElfFile f = ElfFile::load(fixture::path("main.o"));
    const auto tables = tables_of(f);
    const SymbolTable& symtab = tables.at(0);

    SymbolFilter defined;
    defined.defined_only = true;
    for (const auto& s : symtab.symbols) {
        if (defined.matches(s)) CHECK(!s.is_undefined() && !s.is_null());
    }
    CHECK(names_matching(symtab, defined).count("main") == 1);

    SymbolFilter functions;
    functions.functions_only = true;
    for (const auto& s : symtab.symbols) {
        if (functions.matches(s)) CHECK(s.type == STT_FUNC || s.type == STT_GNU_IFUNC);
    }
    CHECK(names_matching(symtab, functions).count("main") == 1);

    CHECK(SymbolFilter{}.matches(symtab.symbols.at(0)));  // no filter shows everything
}

TEST_CASE(linked_executable_resolves_static_references) {
    const ElfFile f = ElfFile::load(fixture::path("program"));
    const auto tables = tables_of(f);
    const SymbolTable* symtab = table_named(tables, ".symtab");
    const SymbolTable* dynsym = table_named(tables, ".dynsym");
    REQUIRE(symtab != nullptr);
    REQUIRE(dynsym != nullptr);

    const Symbol* helper_add = symbol_named(*symtab, "_Z10helper_addii");
    REQUIRE(helper_add != nullptr);
    CHECK(!helper_add->is_undefined());  // resolved by the static linker
    CHECK(helper_add->value != 0);       // now a virtual address

    // puts is still undefined: the dynamic loader binds it to libc at run time.
    const Symbol* puts_dyn = symbol_named(*dynsym, "puts");
    REQUIRE(puts_dyn != nullptr);
    CHECK(puts_dyn->is_undefined());
}

TEST_CASE(stripped_files_have_no_symtab) {
#if BINSCOPE_HAVE_STRIP
    const ElfFile obj = ElfFile::load(fixture::path("nosym.o"));
    CHECK(tables_of(obj).empty());

    const ElfFile exe = ElfFile::load(fixture::path("program_stripped"));
    const auto tables = tables_of(exe);
    CHECK(table_named(tables, ".symtab") == nullptr);
    CHECK(table_named(tables, ".dynsym") != nullptr);  // still needed by the loader

    std::ostringstream out, err;
    CHECK_EQ(cli::run({"symbols", fixture::path("nosym.o").string()}, out, err), cli::kExitSuccess);
    CHECK(out.str().find("No symbol tables") != std::string::npos);
#else
    std::cout << "  (skipped: strip not available)\n";
#endif
}

TEST_CASE(decoder_names_and_fallbacks) {
    CHECK_EQ(symbol_binding_name(STB_WEAK), std::string("WEAK"));
    CHECK_EQ(symbol_type_name(STT_SECTION), std::string("SECTION"));
    CHECK_EQ(symbol_visibility_name(STV_HIDDEN), std::string("HIDDEN"));
    CHECK_EQ(symbol_type_name(9), std::string("<9>"));
    CHECK_EQ(symbol_index_label(SHN_UNDEF), std::string("UND"));
    CHECK_EQ(symbol_index_label(SHN_ABS), std::string("ABS"));
    CHECK_EQ(symbol_index_label(7), std::string("7"));
}

TEST_CASE(demangling) {
    CHECK_EQ(demangle("_Z10helper_addii"), std::string("helper_add(int, int)"));
    CHECK_EQ(demangle("puts"), std::string("puts"));
    CHECK_EQ(demangle("_Znot_valid"), std::string("_Znot_valid"));
}

TEST_CASE(symbols_command_filters_and_rejects_conflicts) {
    const std::string main_o = fixture::path("main.o").string();
    std::ostringstream out, err;
    CHECK_EQ(cli::run({"symbols", main_o, "--undefined"}, out, err), cli::kExitSuccess);
    const std::string s = out.str();
    CHECK(s.find("_Z10helper_addii") != std::string::npos);
    CHECK(s.find("filter: undefined") != std::string::npos);
    CHECK(s.find(" main\n") == std::string::npos);

    std::ostringstream out2, err2;
    CHECK_EQ(cli::run({"symbols", main_o, "--undefined", "--demangle"}, out2, err2),
             cli::kExitSuccess);
    CHECK(out2.str().find("helper_add(int, int)") != std::string::npos);

    std::ostringstream out3, err3;
    CHECK_EQ(cli::run({"symbols", main_o, "--defined", "--undefined"}, out3, err3),
             cli::kExitUsage);
    CHECK(err3.str().find("mutually exclusive") != std::string::npos);
}
