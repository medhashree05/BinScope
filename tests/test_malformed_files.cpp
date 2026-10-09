// Malformed and unsupported inputs must be rejected with a BinScope error (or
// tolerated with a warning), never crash or read out of bounds. Run this suite
// under -DBINSCOPE_ENABLE_SANITIZERS=ON to have ASan/UBSan verify the latter.

#include "binscope/elf_constants.hpp"
#include "binscope/elf_file.hpp"
#include "binscope/error.hpp"
#include "binscope/symbol.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <cstring>
#include <limits>
#include <string>

using namespace binscope;
using namespace binscope::elf;

namespace {

std::string error_message(std::vector<std::uint8_t> b) {
    try {
        (void)fixture::parse(std::move(b));
    } catch (const Error& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Deterministic generator so failures are reproducible.
struct Lcg {
    std::uint64_t state;
    std::uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    }
};

// Parsing must either succeed or throw binscope::Error; anything else
// (another exception type, a crash, a sanitizer report) is a bug.
bool parses_or_reports(std::vector<std::uint8_t> b) {
    try {
        const ElfFile f = fixture::parse(std::move(b));
        for (const Section& s : f.sections()) {
            if (s.has_file_contents() && s.contents_in_bounds) (void)f.section_data(s);
        }
        return true;
    } catch (const Error&) {
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

TEST_CASE(empty_file_is_rejected) {
    CHECK_THROWS_AS(fixture::parse({}), ParseError);
    CHECK(contains(error_message({}), "empty"));
}

TEST_CASE(text_file_is_rejected) {
    const std::string text = "This is a plain text file, not an ELF binary.\n";
    std::vector<std::uint8_t> b(text.begin(), text.end());
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
    CHECK(contains(error_message(b), "bad magic"));
}

TEST_CASE(random_bytes_are_rejected) {
    Lcg rng{12345};
    std::vector<std::uint8_t> b(4096);
    for (auto& byte : b) byte = static_cast<std::uint8_t>(rng.next());
    b[0] = 0x00;  // guarantee the magic does not match by chance
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
}

TEST_CASE(truncated_identification_is_rejected) {
    std::vector<std::uint8_t> b = {0x7f, 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB};
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
    CHECK(contains(error_message(b), "truncated ELF identification"));
}

TEST_CASE(truncated_header_is_rejected) {
    auto b = fixture::bytes("simple.o");
    b.resize(40);
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
    CHECK(contains(error_message(b), "truncated ELF header"));
}

TEST_CASE(every_truncation_of_an_object_fails_safely) {
    // The section header table sits at the end of GCC objects, so every strict
    // prefix of the file must be rejected cleanly.
    const auto full = fixture::bytes("simple.o");
    for (std::size_t len = 0; len < full.size(); ++len) {
        std::vector<std::uint8_t> b(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(len));
        bool rejected = false;
        try {
            (void)fixture::parse(std::move(b));
        } catch (const Error&) {
            rejected = true;
        }
        if (!rejected) {
            bt::report_failure(__FILE__, __LINE__, "prefix of length " + std::to_string(len) +
                                                       " was accepted");
            break;
        }
    }
}

TEST_CASE(elf32_is_reported_as_unsupported) {
    auto b = fixture::bytes("simple.o");
    b[EI_CLASS] = ELFCLASS32;
    CHECK_THROWS_AS(fixture::parse(b), UnsupportedError);
}

TEST_CASE(big_endian_is_reported_as_unsupported) {
    auto b = fixture::bytes("simple.o");
    b[EI_DATA] = ELFDATA2MSB;
    CHECK_THROWS_AS(fixture::parse(b), UnsupportedError);
}

TEST_CASE(invalid_class_encoding_and_version_are_malformed) {
    auto bad_class = fixture::bytes("simple.o");
    bad_class[EI_CLASS] = 7;
    CHECK_THROWS_AS(fixture::parse(bad_class), ParseError);

    auto bad_data = fixture::bytes("simple.o");
    bad_data[EI_DATA] = 0;
    CHECK_THROWS_AS(fixture::parse(bad_data), ParseError);

    auto bad_ident_version = fixture::bytes("simple.o");
    bad_ident_version[EI_VERSION] = 0;
    CHECK_THROWS_AS(fixture::parse(bad_ident_version), ParseError);

    auto bad_version = fixture::bytes("simple.o");
    fixture::put(bad_version, 20, 4, 2);
    CHECK_THROWS_AS(fixture::parse(bad_version), ParseError);
}

TEST_CASE(wrong_entry_sizes_are_malformed) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, 58, 2, 40);  // e_shentsize
    CHECK_THROWS_AS(fixture::parse(b), ParseError);

    auto p = fixture::bytes("program");
    fixture::put(p, 54, 2, 32);  // e_phentsize
    CHECK_THROWS_AS(fixture::parse(p), ParseError);
}

TEST_CASE(section_header_offset_past_end_is_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, fixture::kShoff, 8, b.size());
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
}

TEST_CASE(section_header_offset_overflow_is_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, fixture::kShoff, 8, std::numeric_limits<std::uint64_t>::max() - 8);
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
}

TEST_CASE(section_count_past_end_is_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, fixture::kShnum, 2, 0xfeff);
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
    CHECK(contains(error_message(b), "section header table"));
}

TEST_CASE(section_count_without_table_is_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, fixture::kShoff, 8, 0);
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
}

TEST_CASE(extended_count_past_end_is_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, fixture::kShnum, 2, 0);
    fixture::put(b, fixture::section_header_offset(b, 0) + fixture::kShSize, 8,
                 std::numeric_limits<std::uint64_t>::max() / 2);
    CHECK_THROWS_AS(fixture::parse(b), ParseError);
}

TEST_CASE(program_header_table_past_end_is_rejected) {
    auto b = fixture::bytes("program");
    fixture::put(b, fixture::kPhoff, 8, b.size() - 8);
    CHECK_THROWS_AS(fixture::parse(b), ParseError);

    auto z = fixture::bytes("program");
    fixture::put(z, fixture::kPhoff, 8, 0);
    CHECK_THROWS_AS(fixture::parse(z), ParseError);
}

TEST_CASE(out_of_range_name_table_index_is_a_warning) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, fixture::kShstrndx, 2, 0x1000);
    const ElfFile f = fixture::parse(b);
    CHECK(!f.warnings().empty());
    CHECK_EQ(f.header().section_name_table_index, 0U);
    for (const Section& s : f.sections()) CHECK(s.name.empty());
}

TEST_CASE(invalid_section_name_offset_is_a_warning) {
    auto b = fixture::bytes("simple.o");
    const std::size_t text_hdr = fixture::section_header_offset(b, 1);
    fixture::put(b, text_hdr + fixture::kShName, 4, 0xfffffff0);
    const ElfFile f = fixture::parse(b);
    REQUIRE(f.sections().size() > 1);
    CHECK(!f.sections()[1].name_valid);
    CHECK(contains(f.sections()[1].name, "corrupt"));
    CHECK(!f.warnings().empty());
}

TEST_CASE(out_of_range_section_contents_are_flagged_not_read) {
    auto b = fixture::bytes("simple.o");
    const std::size_t symtab = fixture::find_section_by_type(b, SHT_SYMTAB);
    const std::size_t hdr = fixture::section_header_offset(b, symtab);
    fixture::put(b, hdr + fixture::kShOffset, 8, b.size() - 4);
    const ElfFile f = fixture::parse(b);
    const Section* s = f.section_at(symtab);
    REQUIRE(s != nullptr);
    CHECK(!s->contents_in_bounds);
    CHECK(!f.warnings().empty());
    CHECK_THROWS_AS(f.section_data(*s), ParseError);

    // A size that overflows offset + size must be caught as well.
    auto o = fixture::bytes("simple.o");
    fixture::put(o, hdr + fixture::kShSize, 8, std::numeric_limits<std::uint64_t>::max());
    const ElfFile g = fixture::parse(o);
    CHECK(!g.section_at(symtab)->contents_in_bounds);
}

// --- Symbol tables -----------------------------------------------------------

namespace {

std::vector<SymbolTable> symbols_of(std::vector<std::uint8_t> b, std::vector<std::string>& warnings) {
    const ElfFile f = fixture::parse(std::move(b));
    return parse_symbol_tables(f, warnings);
}

std::size_t symtab_header(const std::vector<std::uint8_t>& b) {
    return fixture::section_header_offset(b, fixture::find_section_by_type(b, SHT_SYMTAB));
}

// File offset of symbol entry `index` in .symtab.
std::size_t symbol_entry(const std::vector<std::uint8_t>& b, std::size_t index) {
    return static_cast<std::size_t>(fixture::get(b, symtab_header(b) + fixture::kShOffset, 8)) +
           index * 24;
}

}  // namespace

TEST_CASE(symbol_table_with_wrong_entry_size_is_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, symtab_header(b) + fixture::kShEntsize, 8, 16);
    std::vector<std::string> w;
    CHECK_THROWS_AS(symbols_of(b, w), ParseError);
}

TEST_CASE(symbol_table_with_partial_entry_is_rejected) {
    auto b = fixture::bytes("simple.o");
    const std::size_t hdr = symtab_header(b);
    fixture::put(b, hdr + fixture::kShSize, 8, fixture::get(b, hdr + fixture::kShSize, 8) - 5);
    std::vector<std::string> w;
    CHECK_THROWS_AS(symbols_of(b, w), ParseError);
}

TEST_CASE(symbol_table_with_bad_string_table_link_is_rejected) {
    auto out_of_range = fixture::bytes("simple.o");
    fixture::put(out_of_range, symtab_header(out_of_range) + fixture::kShLink, 4, 0x4000);
    std::vector<std::string> w;
    CHECK_THROWS_AS(symbols_of(out_of_range, w), ParseError);

    auto not_strtab = fixture::bytes("simple.o");
    fixture::put(not_strtab, symtab_header(not_strtab) + fixture::kShLink, 4, 1);  // .text
    CHECK_THROWS_AS(symbols_of(not_strtab, w), ParseError);
}

TEST_CASE(symbol_table_contents_out_of_range_are_rejected) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, symtab_header(b) + fixture::kShOffset, 8, b.size());
    std::vector<std::string> w;
    CHECK_THROWS_AS(symbols_of(b, w), ParseError);
}

TEST_CASE(invalid_symbol_name_offset_is_a_warning) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, symbol_entry(b, 1), 4, 0x7fffffff);
    std::vector<std::string> w;
    const auto tables = symbols_of(b, w);
    REQUIRE(tables.size() == 1);
    CHECK(!tables[0].symbols.at(1).name_valid);
    CHECK(contains(tables[0].symbols.at(1).name, "corrupt"));
    CHECK(!w.empty());
}

TEST_CASE(invalid_symbol_section_index_is_a_warning) {
    auto b = fixture::bytes("simple.o");
    fixture::put(b, symbol_entry(b, 1) + 6, 2, 0x7000);  // st_shndx
    std::vector<std::string> w;
    const auto tables = symbols_of(b, w);
    REQUIRE(tables.size() == 1);
    CHECK(!tables[0].symbols.at(1).section_index_valid);
    CHECK(contains(tables[0].symbols.at(1).section_name, "invalid section index"));
    CHECK(!w.empty());
}

TEST_CASE(unterminated_string_table_does_not_overread) {
    // Overwrite the final NUL of .strtab: the last name must be reported as
    // corrupt rather than read past the end of the section.
    auto b = fixture::bytes("simple.o");
    const std::size_t hdr = symtab_header(b);
    const std::size_t strtab = static_cast<std::size_t>(fixture::get(b, hdr + fixture::kShLink, 4));
    const std::size_t str_hdr = fixture::section_header_offset(b, strtab);
    const std::size_t str_off = static_cast<std::size_t>(fixture::get(b, str_hdr + fixture::kShOffset, 8));
    const std::size_t str_size = static_cast<std::size_t>(fixture::get(b, str_hdr + fixture::kShSize, 8));
    b[str_off + str_size - 1] = 'X';
    std::vector<std::string> w;
    CHECK_NOTHROW(symbols_of(b, w));
    CHECK(!w.empty());
}

TEST_CASE(random_symbol_table_mutations_never_escape_as_crashes) {
    const auto base = fixture::bytes("main.o");
    const std::size_t hdr = symtab_header(base);
    const std::size_t off = static_cast<std::size_t>(fixture::get(base, hdr + fixture::kShOffset, 8));
    const std::size_t size = static_cast<std::size_t>(fixture::get(base, hdr + fixture::kShSize, 8));
    Lcg rng{77};
    for (int iter = 0; iter < 3000; ++iter) {
        auto b = base;
        for (int e = 0; e < 4; ++e) {
            b[off + rng.next() % size] = static_cast<std::uint8_t>(rng.next());
        }
        if (rng.next() % 4 == 0) {  // occasionally corrupt the table's own header too
            b[hdr + rng.next() % 64] = static_cast<std::uint8_t>(rng.next());
        }
        try {
            const ElfFile f = fixture::parse(std::move(b));
            std::vector<std::string> w;
            (void)parse_symbol_tables(f, w);
        } catch (const Error&) {
        } catch (...) {
            bt::report_failure(__FILE__, __LINE__, "non-BinScope exception at iteration " +
                                                       std::to_string(iter));
            break;
        }
    }
}

TEST_CASE(random_header_mutations_never_escape_as_crashes) {
    // Lightweight, deterministic fuzzing of the header and section header table.
    const auto base = fixture::bytes("program");
    const std::size_t shoff = static_cast<std::size_t>(fixture::get(base, fixture::kShoff, 8));
    Lcg rng{2024};
    for (int iter = 0; iter < 3000; ++iter) {
        auto b = base;
        const int edits = 1 + static_cast<int>(rng.next() % 6);
        for (int e = 0; e < edits; ++e) {
            std::size_t pos;
            if (rng.next() % 2 == 0) {
                pos = rng.next() % 64;  // ELF header
            } else {
                pos = shoff + rng.next() % (b.size() - shoff);  // section header table
            }
            b[pos] = static_cast<std::uint8_t>(rng.next());
        }
        if (!parses_or_reports(std::move(b))) {
            bt::report_failure(__FILE__, __LINE__,
                               "iteration " + std::to_string(iter) + " threw a non-BinScope error");
            break;
        }
    }
}
