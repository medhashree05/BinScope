// Header, section-table and program-header parsing of valid compiler output.

#include "binscope/byte_reader.hpp"
#include "binscope/elf_constants.hpp"
#include "binscope/elf_file.hpp"
#include "binscope/error.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <limits>

using namespace binscope;
using namespace binscope::elf;

namespace {

bool has_segment(const ElfFile& f, std::uint32_t type) {
    for (const auto& p : f.program_headers()) {
        if (p.type == type) return true;
    }
    return false;
}

}  // namespace

TEST_CASE(byte_view_bounds_checks_never_overflow) {
    const std::vector<std::uint8_t> data(16, 0xab);
    const ByteView v(data);
    CHECK(v.contains(0, 16));
    CHECK(v.contains(16, 0));
    CHECK(!v.contains(17, 0));
    CHECK(!v.contains(1, 16));
    CHECK(!v.contains(8, std::numeric_limits<std::uint64_t>::max()));
    CHECK(!v.contains(std::numeric_limits<std::uint64_t>::max(), 2));
    CHECK_THROWS_AS(v.u64(9), ParseError);
    CHECK_THROWS_AS(v.u8(16), ParseError);
    CHECK_EQ(v.u32(12), 0xababababU);
}

TEST_CASE(byte_view_reads_little_endian_values) {
    const std::vector<std::uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const ByteView v(data);
    CHECK_EQ(v.u16(0), 0x0201U);
    CHECK_EQ(v.u32(0), 0x04030201U);
    CHECK_EQ(v.u64(0), 0x0807060504030201ULL);
    const std::vector<std::uint8_t> minus_one(8, 0xff);
    CHECK_EQ(ByteView(minus_one).i64(0), -1);
}

TEST_CASE(byte_view_c_string_requires_terminator) {
    const std::vector<std::uint8_t> data = {'a', 'b', 0, 'c', 'd'};
    const ByteView v(data);
    REQUIRE(v.c_string(0).has_value());
    CHECK(*v.c_string(0) == "ab");
    CHECK(*v.c_string(2) == "");
    CHECK(!v.c_string(3).has_value());  // runs off the end without a NUL
    CHECK(!v.c_string(99).has_value());
}

TEST_CASE(checked_arithmetic_detects_overflow) {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    CHECK(!checked_add(max, 1).has_value());
    CHECK(checked_add(max - 1, 1).value() == max);
    CHECK(!checked_mul(max / 2 + 1, 2).has_value());
    CHECK(checked_mul(0, max).value() == 0);
}

TEST_CASE(relocatable_object_header) {
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    const ElfHeader& h = f.header();
    CHECK_EQ(h.file_class, ELFCLASS64);
    CHECK_EQ(h.data_encoding, ELFDATA2LSB);
    CHECK_EQ(h.type, ET_REL);
    CHECK_EQ(h.machine, EM_X86_64);
    CHECK_EQ(h.entry, 0U);
    CHECK(f.program_headers().empty());
    CHECK(h.section_count > 0);
    CHECK_EQ(f.sections().size(), h.section_count);
    CHECK(!h.extended_section_numbering);
    CHECK(f.warnings().empty());

    const Section* names = f.section_at(h.section_name_table_index);
    REQUIRE(names != nullptr);
    CHECK_EQ(names->name, std::string(".shstrtab"));
    CHECK(f.find_section(".text") != nullptr);
    CHECK(f.find_section(".symtab") != nullptr);
}

TEST_CASE(section_zero_is_null) {
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    REQUIRE(!f.sections().empty());
    CHECK_EQ(f.sections()[0].type, SHT_NULL);
    CHECK_EQ(f.sections()[0].name, std::string(""));
}

TEST_CASE(pie_executable_header) {
    const ElfFile f = ElfFile::load(fixture::path("program"));
    CHECK_EQ(f.header().type, ET_DYN);
    CHECK(f.header().entry != 0);
    CHECK(!f.program_headers().empty());
    CHECK(has_segment(f, PT_INTERP));
    CHECK(has_segment(f, PT_LOAD));
    CHECK(has_segment(f, PT_DYNAMIC));
}

TEST_CASE(non_pie_executable_header) {
    const ElfFile f = ElfFile::load(fixture::path("program_nopie"));
    CHECK_EQ(f.header().type, ET_EXEC);
    CHECK(f.header().entry != 0);
    CHECK(has_segment(f, PT_LOAD));
}

TEST_CASE(shared_object_header) {
    const ElfFile f = ElfFile::load(fixture::path("libhelper.so"));
    CHECK_EQ(f.header().type, ET_DYN);
    CHECK(has_segment(f, PT_DYNAMIC));
    CHECK(!has_segment(f, PT_INTERP));  // libraries have no program interpreter
    CHECK(f.find_section(".dynsym") != nullptr);
}

TEST_CASE(nobits_section_has_no_file_data) {
    const ElfFile f = ElfFile::load(fixture::path("simple.o"));
    const Section* bss = f.find_section(".bss");
    REQUIRE(bss != nullptr);
    CHECK_EQ(bss->type, SHT_NOBITS);
    CHECK(!bss->has_file_contents());
    CHECK_THROWS_AS(f.section_data(*bss), ParseError);

    const Section* text = f.find_section(".text");
    REQUIRE(text != nullptr);
    CHECK_EQ(f.section_data(*text).size(), text->size);
}

TEST_CASE(extended_section_numbering_is_resolved) {
    // Re-encode a real object the way a file with >= 0xff00 sections would be:
    // e_shnum = 0 and e_shstrndx = SHN_XINDEX, with the real values in section 0.
    auto b = fixture::bytes("simple.o");
    const ElfFile original = fixture::parse(b);
    const std::uint64_t count = fixture::get(b, fixture::kShnum, 2);
    const std::uint64_t strndx = fixture::get(b, fixture::kShstrndx, 2);
    const std::size_t sec0 = fixture::section_header_offset(b, 0);
    fixture::put(b, fixture::kShnum, 2, 0);
    fixture::put(b, fixture::kShstrndx, 2, SHN_XINDEX);
    fixture::put(b, sec0 + fixture::kShSize, 8, count);
    fixture::put(b, sec0 + fixture::kShLink, 4, strndx);

    const ElfFile f = fixture::parse(b);
    CHECK(f.header().extended_section_numbering);
    CHECK_EQ(f.header().section_count, count);
    CHECK_EQ(f.header().section_name_table_index, strndx);
    REQUIRE(f.sections().size() == original.sections().size());
    for (std::size_t i = 1; i < f.sections().size(); ++i) {
        CHECK_EQ(f.sections()[i].name, original.sections()[i].name);
    }
}

TEST_CASE(extended_program_header_numbering_is_resolved) {
    auto b = fixture::bytes("program");
    const std::uint64_t phnum = fixture::get(b, fixture::kPhnum, 2);
    fixture::put(b, fixture::kPhnum, 2, PN_XNUM);
    fixture::put(b, fixture::section_header_offset(b, 0) + 44, 4, phnum);  // sh_info

    const ElfFile f = fixture::parse(b);
    CHECK(f.header().extended_program_numbering);
    CHECK_EQ(f.program_headers().size(), phnum);
}
