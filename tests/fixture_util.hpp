#pragma once

// Helpers for locating compiler-generated fixtures and for crafting malformed
// variants of them in memory.
//
// The byte-patching helpers decode ELF fields independently of the BinScope
// parser (using fixed ELF64 field offsets), so a parser bug cannot hide itself
// by also corrupting the test's view of the file.

#include "binscope/elf_file.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixture {

inline std::filesystem::path path(const std::string& name) {
    return std::filesystem::path(BINSCOPE_FIXTURE_DIR) / name;
}

inline std::vector<std::uint8_t> bytes(const std::string& name) {
    return binscope::read_file_bytes(path(name));
}

inline std::uint64_t get(const std::vector<std::uint8_t>& b, std::size_t off, unsigned width) {
    if (off + width > b.size()) throw std::out_of_range("fixture::get out of range");
    std::uint64_t v = 0;
    for (unsigned i = 0; i < width; ++i) v |= static_cast<std::uint64_t>(b[off + i]) << (8U * i);
    return v;
}

inline void put(std::vector<std::uint8_t>& b, std::size_t off, unsigned width, std::uint64_t v) {
    if (off + width > b.size()) throw std::out_of_range("fixture::put out of range");
    for (unsigned i = 0; i < width; ++i) b[off + i] = static_cast<std::uint8_t>(v >> (8U * i));
}

// ELF64 header field offsets.
constexpr std::size_t kPhoff = 32, kShoff = 40, kPhnum = 56, kShnum = 60, kShstrndx = 62;
// Elf64_Shdr field offsets.
constexpr std::size_t kShName = 0, kShType = 4, kShOffset = 24, kShSize = 32, kShLink = 40,
                      kShEntsize = 56;

// File offset of section header `index` (no validation beyond the raw table).
inline std::size_t section_header_offset(const std::vector<std::uint8_t>& b, std::size_t index) {
    return static_cast<std::size_t>(get(b, kShoff, 8)) + index * 64;
}

// Index of the first section whose sh_type equals `type`; throws if absent.
inline std::size_t find_section_by_type(const std::vector<std::uint8_t>& b, std::uint32_t type) {
    const std::size_t count = static_cast<std::size_t>(get(b, kShnum, 2));
    for (std::size_t i = 0; i < count; ++i) {
        if (get(b, section_header_offset(b, i) + kShType, 4) == type) return i;
    }
    throw std::runtime_error("fixture has no section of type " + std::to_string(type));
}

inline binscope::ElfFile parse(std::vector<std::uint8_t> b, const std::string& name = "test") {
    return binscope::ElfFile::from_bytes(std::move(b), name);
}

}  // namespace fixture
