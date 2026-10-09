#pragma once

#include "binscope/byte_reader.hpp"
#include "binscope/elf_header.hpp"
#include "binscope/section.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace binscope {

// An ELF64 image loaded into memory with its header, section table and program
// header table decoded and validated.
//
// Construction fails with IoError, ParseError or UnsupportedError; a constructed
// ElfFile always has a structurally valid header and section header table.
// Higher-level tables (symbols, relocations) are decoded on demand from it, so
// a corrupt symbol table does not prevent `inspect` or `sections` from working.
class ElfFile {
public:
    static ElfFile load(const std::filesystem::path& path);
    static ElfFile from_bytes(std::vector<std::uint8_t> bytes, std::string display_name);

    const std::string& name() const noexcept { return name_; }
    const ElfHeader& header() const noexcept { return header_; }
    const std::vector<Section>& sections() const noexcept { return sections_; }
    const std::vector<ProgramHeader>& program_headers() const noexcept { return segments_; }

    // Non-fatal problems found while parsing (corrupt names, out-of-range contents...).
    const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    ByteView bytes() const noexcept { return ByteView(bytes_); }
    std::uint64_t file_size() const noexcept { return bytes_.size(); }

    // nullptr when the index is out of range.
    const Section* section_at(std::uint64_t index) const noexcept;
    // First section with this exact name, or nullptr.
    const Section* find_section(std::string_view name) const noexcept;

    // Bytes of a section that is stored in the file. Throws ParseError if the
    // section has no file contents (SHT_NOBITS/SHT_NULL) or lies out of bounds.
    ByteView section_data(const Section& section) const;

private:
    ElfFile(std::vector<std::uint8_t> bytes, std::string name);

    std::vector<std::uint8_t> bytes_;
    std::string name_;
    ElfHeader header_;
    std::vector<Section> sections_;
    std::vector<ProgramHeader> segments_;
    std::vector<std::string> warnings_;
};

// Reads a whole file into memory. Throws IoError on failure.
std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path);

}  // namespace binscope
