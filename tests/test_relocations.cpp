// REL/RELA decoding, symbol resolution and x86-64 type naming.

#include "binscope/cli.hpp"
#include "binscope/elf_constants.hpp"
#include "binscope/error.hpp"
#include "binscope/formatter.hpp"
#include "binscope/relocation.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <limits>
#include <sstream>

using namespace binscope;
using namespace binscope::elf;

namespace {

std::vector<RelocationSection> relocs_of(const ElfFile& f, std::vector<std::string>& warnings) {
    return parse_relocation_sections(f, warnings);
}

std::vector<RelocationSection> relocs_of(const ElfFile& f) {
    std::vector<std::string> warnings;
    auto result = parse_relocation_sections(f, warnings);
    if (!warnings.empty()) bt::report_failure(__FILE__, __LINE__, "unexpected warning: " + warnings[0]);
    return result;
}

const RelocationSection* named(const std::vector<RelocationSection>& v, const std::string& name) {
    for (const auto& rs : v) {
        if (rs.section_name == name) return &rs;
    }
    return nullptr;
}

const Relocation* against(const RelocationSection& rs, const std::string& symbol) {
    for (const auto& r : rs.entries) {
        if (r.symbol_name == symbol) return &r;
    }
    return nullptr;
}

std::size_t rela_text_header(const std::vector<std::uint8_t>& b) {
    return fixture::section_header_offset(b, fixture::find_section_by_type(b, SHT_RELA));
}

}  // namespace

TEST_CASE(object_relocations_reference_external_symbols) {
    const ElfFile f = ElfFile::load(fixture::path("main.o"));
    const auto all = relocs_of(f);
    const RelocationSection* text = named(all, ".rela.text");
    REQUIRE(text != nullptr);
    CHECK(text->encoding == RelocationEncoding::Rela);
    CHECK_EQ(text->target_section_name, std::string(".text"));
    CHECK_EQ(text->symbol_table_name, std::string(".symtab"));
    CHECK(!text->entries.empty());

    // The call to helper_add() cannot be resolved until link time.
    const Relocation* call = against(*text, "_Z10helper_addii");
    REQUIRE(call != nullptr);
    CHECK_EQ(call->type, R_X86_64_PLT32);
    REQUIRE(call->addend.has_value());
    CHECK_EQ(*call->addend, -4);  // displacement is measured from the end of the 4-byte field

    CHECK(against(*text, "puts") != nullptr);
    CHECK(against(*text, "helper_counter") != nullptr);
    for (const auto& r : text->entries) {
        CHECK(r.addend.has_value());
        CHECK(r.symbol_valid);
        CHECK(r.offset < f.find_section(".text")->size);  // section-relative in ET_REL
    }
}

TEST_CASE(section_symbols_are_named_after_their_section) {
    const ElfFile f = ElfFile::load(fixture::path("main.o"));
    const auto all = relocs_of(f);
    const RelocationSection* text = named(all, ".rela.text");
    REQUIRE(text != nullptr);
    CHECK(against(*text, ".rodata") != nullptr);  // string literal passed to puts
}

TEST_CASE(linked_executable_keeps_only_dynamic_relocations) {
    const ElfFile f = ElfFile::load(fixture::path("program"));
    const auto all = relocs_of(f);
    CHECK(named(all, ".rela.text") == nullptr);  // consumed by the static linker

    const RelocationSection* dyn = named(all, ".rela.dyn");
    REQUIRE(dyn != nullptr);
    CHECK_EQ(dyn->symbol_table_name, std::string(".dynsym"));
    bool saw_relative = false;
    for (const auto& r : dyn->entries) {
        if (r.type == R_X86_64_RELATIVE) {
            saw_relative = true;
            CHECK_EQ(r.symbol_index, 0U);  // RELATIVE needs no symbol lookup
        }
    }
    CHECK(saw_relative);  // PIE: absolute pointers must be adjusted by the load base

    // puts is bound by the dynamic loader through the PLT/GOT.
    bool puts_dynamic = false;
    for (const auto& rs : all) {
        for (const auto& r : rs.entries) {
            if (r.symbol_name == "puts" && (r.type == R_X86_64_JUMP_SLOT || r.type == R_X86_64_GLOB_DAT)) {
                puts_dynamic = true;
            }
        }
    }
    CHECK(puts_dynamic);
}

TEST_CASE(file_without_relocations) {
#if BINSCOPE_HAVE_STRIP
    const ElfFile f = ElfFile::load(fixture::path("nosym.o"));
    CHECK(relocs_of(f).empty());
    std::ostringstream out, err;
    CHECK_EQ(cli::run({"relocs", fixture::path("nosym.o").string()}, out, err), cli::kExitSuccess);
    CHECK(out.str().find("No REL/RELA relocation sections") != std::string::npos);
#endif
}

TEST_CASE(rel_encoding_has_implicit_addends) {
    // x86-64 toolchains emit RELA; re-type .rela.text as SHT_REL with 16-byte
    // entries to exercise the REL decoder. Each REL entry starts with the same
    // r_offset/r_info layout, so the first entry must decode identically.
    auto b = fixture::bytes("main.o");
    const ElfFile original = fixture::parse(b);
    const auto original_relocs = relocs_of(original);
    const RelocationSection* original_text = named(original_relocs, ".rela.text");
    REQUIRE(original_text != nullptr && !original_text->entries.empty());
    const Relocation first = original_text->entries.front();

    const std::size_t hdr = rela_text_header(b);
    fixture::put(b, hdr + fixture::kShType, 4, SHT_REL);
    fixture::put(b, hdr + fixture::kShEntsize, 8, kRelSize);
    const std::uint64_t size = fixture::get(b, hdr + fixture::kShSize, 8);

    const ElfFile f = fixture::parse(b);
    std::vector<std::string> warnings;
    const auto all = relocs_of(f, warnings);
    const RelocationSection* rel = named(all, ".rela.text");
    REQUIRE(rel != nullptr);
    CHECK(rel->encoding == RelocationEncoding::Rel);
    CHECK_EQ(rel->entries.size(), size / kRelSize);
    for (const auto& r : rel->entries) CHECK(!r.addend.has_value());
    CHECK_EQ(rel->entries.at(0).offset, first.offset);
    CHECK_EQ(rel->entries.at(0).type, first.type);
    CHECK_EQ(rel->entries.at(0).symbol_name, first.symbol_name);
}

TEST_CASE(type_names_formulas_and_fallbacks) {
    CHECK_EQ(relocation_type_name(EM_X86_64, R_X86_64_PC32), std::string("R_X86_64_PC32"));
    CHECK_EQ(relocation_type_name(EM_X86_64, R_X86_64_GOTPCREL), std::string("R_X86_64_GOTPCREL"));
    CHECK_EQ(relocation_type_name(EM_X86_64, R_X86_64_JUMP_SLOT), std::string("R_X86_64_JUMP_SLOT"));
    CHECK_EQ(relocation_type_name(EM_X86_64, 250), std::string("type 250"));
    CHECK_EQ(relocation_type_name(EM_AARCH64, 257), std::string("type 257"));
    CHECK_EQ(relocation_formula(EM_X86_64, R_X86_64_PC32), std::string("S + A - P"));
    CHECK_EQ(relocation_formula(EM_X86_64, R_X86_64_RELATIVE), std::string("B + A"));
    CHECK(relocation_formula(EM_X86_64, 250).empty());
    CHECK(!relocation_explanation(EM_X86_64, R_X86_64_PLT32).empty());
}

TEST_CASE(signed_hex_handles_extremes) {
    CHECK_EQ(signed_hex(-4), std::string("-0x4"));
    CHECK_EQ(signed_hex(0), std::string("+0x0"));
    CHECK_EQ(signed_hex(std::numeric_limits<std::int64_t>::min()), std::string("-0x8000000000000000"));
}

TEST_CASE(relocs_command_explains_types) {
    std::ostringstream out, err;
    CHECK_EQ(cli::run({"relocs", fixture::path("main.o").string()}, out, err), cli::kExitSuccess);
    const std::string s = out.str();
    CHECK(s.find("R_X86_64_PLT32") != std::string::npos);
    CHECK(s.find("Relocation types used") != std::string::npos);
    CHECK(s.find("S + A - P") != std::string::npos);
    CHECK(err.str().empty());
}

// --- Malformed relocation sections -------------------------------------------

TEST_CASE(wrong_relocation_entry_size_is_rejected) {
    auto b = fixture::bytes("main.o");
    fixture::put(b, rela_text_header(b) + fixture::kShEntsize, 8, 16);
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    CHECK_THROWS_AS(relocs_of(f, w), ParseError);
}

TEST_CASE(partial_relocation_entry_is_rejected) {
    auto b = fixture::bytes("main.o");
    const std::size_t hdr = rela_text_header(b);
    fixture::put(b, hdr + fixture::kShSize, 8, fixture::get(b, hdr + fixture::kShSize, 8) + 3);
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    CHECK_THROWS_AS(relocs_of(f, w), ParseError);
}

TEST_CASE(relocation_contents_out_of_range_are_rejected) {
    auto b = fixture::bytes("main.o");
    fixture::put(b, rela_text_header(b) + fixture::kShOffset, 8, b.size() - 8);
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    CHECK_THROWS_AS(relocs_of(f, w), ParseError);
}

TEST_CASE(relocation_link_must_be_a_symbol_table) {
    auto b = fixture::bytes("main.o");
    fixture::put(b, rela_text_header(b) + fixture::kShLink, 4, 1);  // .text
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    CHECK_THROWS_AS(relocs_of(f, w), ParseError);
}

TEST_CASE(out_of_range_symbol_index_is_a_warning) {
    auto b = fixture::bytes("main.o");
    const std::size_t hdr = rela_text_header(b);
    const std::size_t first = static_cast<std::size_t>(fixture::get(b, hdr + fixture::kShOffset, 8));
    fixture::put(b, first + 8 + 4, 4, 0xffff);  // high word of r_info = symbol index
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    const auto all = relocs_of(f, w);
    const RelocationSection* text = named(all, ".rela.text");
    REQUIRE(text != nullptr);
    CHECK(!text->entries.at(0).symbol_valid);
    CHECK(text->entries.at(0).symbol_name.find("invalid symbol index") != std::string::npos);
    CHECK(!w.empty());
}

TEST_CASE(symbol_reference_without_symbol_table_is_a_warning) {
    auto b = fixture::bytes("main.o");
    fixture::put(b, rela_text_header(b) + fixture::kShLink, 4, 0);
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    const auto all = relocs_of(f, w);
    const RelocationSection* text = named(all, ".rela.text");
    REQUIRE(text != nullptr && !text->entries.empty());
    CHECK(!text->entries.front().symbol_valid);
    CHECK(!w.empty());
}

TEST_CASE(invalid_target_section_is_a_warning) {
    auto b = fixture::bytes("main.o");
    fixture::put(b, rela_text_header(b) + 44, 4, 0x5000);  // sh_info
    const ElfFile f = fixture::parse(b);
    std::vector<std::string> w;
    CHECK_NOTHROW(relocs_of(f, w));
    CHECK(!w.empty());
}

TEST_CASE(random_relocation_mutations_never_escape_as_crashes) {
    const auto base = fixture::bytes("main.o");
    const std::size_t hdr = rela_text_header(base);
    const std::size_t off = static_cast<std::size_t>(fixture::get(base, hdr + fixture::kShOffset, 8));
    const std::size_t size = static_cast<std::size_t>(fixture::get(base, hdr + fixture::kShSize, 8));
    std::uint64_t state = 99;
    auto next = [&state] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    };
    for (int iter = 0; iter < 3000; ++iter) {
        auto b = base;
        for (int e = 0; e < 4; ++e) b[off + next() % size] = static_cast<std::uint8_t>(next());
        if (next() % 4 == 0) b[hdr + next() % 64] = static_cast<std::uint8_t>(next());
        try {
            const ElfFile f = fixture::parse(std::move(b));
            std::vector<std::string> w;
            (void)parse_relocation_sections(f, w);
            std::ostringstream sink;
            print_relocations(sink, f, parse_relocation_sections(f, w), true);
        } catch (const Error&) {
        } catch (...) {
            bt::report_failure(__FILE__, __LINE__, "non-BinScope exception at iteration " +
                                                       std::to_string(iter));
            break;
        }
    }
}
