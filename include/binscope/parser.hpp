#pragma once

#include "binscope/byte_reader.hpp"
#include "binscope/elf_header.hpp"
#include "binscope/section.hpp"

#include <string>
#include <vector>

namespace binscope {

// Low-level decoding of the ELF64 file header, section header table and program
// header table. Every function treats the input as untrusted: offsets, counts
// and entry sizes are validated against the real buffer before use, and
// structural violations raise ParseError / UnsupportedError.
//
// Non-fatal oddities (e.g. a section name offset that points outside the string
// table) are appended to `warnings` instead of aborting the whole analysis.

// Decodes and validates Elf64_Ehdr. Counts that depend on extended numbering
// are left unresolved here; parse_section_headers() fills them in.
ElfHeader parse_elf_header(ByteView file);

// Decodes the section header table and resolves section names. Updates
// `header.section_count`, `header.section_name_table_index` and
// `header.program_header_count` (extended numbering lives in section 0).
std::vector<Section> parse_section_headers(ByteView file, ElfHeader& header,
                                           std::vector<std::string>& warnings);

// Decodes the program header table. Requires the effective
// `header.program_header_count` computed by parse_section_headers().
std::vector<ProgramHeader> parse_program_headers(ByteView file, const ElfHeader& header);

}  // namespace binscope
