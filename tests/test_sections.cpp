// Section-table decoding and reporting.

#include "binscope/cli.hpp"
#include "binscope/elf_constants.hpp"
#include "binscope/elf_file.hpp"
#include "binscope/section.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <sstream>

using namespace binscope;
using namespace binscope::elf;

namespace {

const Section& require_section(const ElfFile& f, const char* name) {
    const Section* s = f.find_section(name);
    if (s == nullptr) {
        bt::report_failure(__FILE__, __LINE__, std::string("missing section ") + name);
        throw bt::AbortTest{};
    }
    return *s;
}

}  // namespace

TEST_CASE(object_file_has_expected_sections) {
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    const Section& text = require_section(f, ".text");
    CHECK_EQ(text.type, SHT_PROGBITS);
    CHECK_EQ(section_flags_string(text.flags), std::string("AX"));
    CHECK_EQ(text.addr, 0U);  // relocatable objects are not placed in memory yet

    const Section& data = require_section(f, ".data");
    CHECK_EQ(data.type, SHT_PROGBITS);
    CHECK_EQ(section_flags_string(data.flags), std::string("WA"));

    const Section& bss = require_section(f, ".bss");
    CHECK_EQ(bss.type, SHT_NOBITS);
    CHECK(bss.size >= 4);  // simple_zeroed

    CHECK(f.find_section(".rodata") != nullptr);
}

TEST_CASE(symtab_links_to_strtab) {
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    const Section& symtab = require_section(f, ".symtab");
    CHECK_EQ(symtab.type, SHT_SYMTAB);
    CHECK_EQ(symtab.entsize, kSymSize);
    const Section* strtab = f.section_at(symtab.link);
    REQUIRE(strtab != nullptr);
    CHECK_EQ(strtab->name, std::string(".strtab"));
    CHECK_EQ(strtab->type, SHT_STRTAB);
}

TEST_CASE(rela_text_targets_text) {
    const ElfFile f = ElfFile::load(fixture::path("main.o"));
    const Section& rela = require_section(f, ".rela.text");
    CHECK_EQ(rela.type, SHT_RELA);
    CHECK(rela.flags & SHF_INFO_LINK);  // sh_info names the section being patched
    const Section* target = f.section_at(rela.info);
    REQUIRE(target != nullptr);
    CHECK_EQ(target->name, std::string(".text"));
    CHECK_EQ(f.section_at(rela.link)->type, SHT_SYMTAB);
}

TEST_CASE(executable_has_dynamic_linking_sections) {
    const ElfFile f = ElfFile::load(fixture::path("program"));
    CHECK_EQ(require_section(f, ".dynsym").type, SHT_DYNSYM);
    CHECK_EQ(require_section(f, ".dynstr").type, SHT_STRTAB);
    CHECK_EQ(require_section(f, ".dynamic").type, SHT_DYNAMIC);
    CHECK(require_section(f, ".text").addr != 0);  // linked: real virtual addresses
}

TEST_CASE(type_and_flag_names_have_fallbacks) {
    CHECK_EQ(section_type_name(SHT_RELA), std::string("RELA"));
    CHECK_EQ(section_type_name(SHT_DYNSYM), std::string("DYNSYM"));
    CHECK(section_type_name(0x12345).find("0x12345") != std::string::npos);
    CHECK(section_type_name(0x60000123).find("LOOS") != std::string::npos);
    CHECK_EQ(section_flags_string(SHF_WRITE | SHF_ALLOC | 0x1000000000ULL), std::string("WAx"));
    CHECK_EQ(section_flags_verbose(0), std::string("none"));
    CHECK_EQ(section_flags_verbose(SHF_ALLOC | SHF_EXECINSTR), std::string("ALLOC|EXEC"));
}

TEST_CASE(well_known_sections_are_explained) {
    for (const char* name : {".text", ".rodata", ".data", ".bss", ".symtab", ".strtab", ".dynsym",
                             ".dynstr", ".rela.text", ".rela.dyn", ".rela.plt", ".debug_info"}) {
        if (section_purpose(name).empty()) {
            bt::report_failure(__FILE__, __LINE__, std::string("no description for ") + name);
        }
    }
    CHECK(section_purpose(".my_custom_section").empty());
}

TEST_CASE(sections_command_lists_table_and_notes) {
    std::ostringstream out, err;
    const int code = cli::run({"sections", fixture::path("simple.o").string()}, out, err);
    CHECK_EQ(code, cli::kExitSuccess);
    const std::string s = out.str();
    CHECK(s.find(".text") != std::string::npos);
    CHECK(s.find("NOBITS") != std::string::npos);
    CHECK(s.find("What these sections hold") != std::string::npos);
    CHECK(err.str().empty());
}
