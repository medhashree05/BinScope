#pragma once

#include <cstdint>
#include <string>

namespace binscope {

// Decoded ELF64 file header (Elf64_Ehdr) plus the counts BinScope derives from it.
struct ElfHeader {
    // e_ident
    std::uint8_t file_class = 0;
    std::uint8_t data_encoding = 0;
    std::uint8_t ident_version = 0;
    std::uint8_t os_abi = 0;
    std::uint8_t abi_version = 0;

    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    std::uint32_t version = 0;
    std::uint64_t entry = 0;
    std::uint64_t phoff = 0;
    std::uint64_t shoff = 0;
    std::uint32_t flags = 0;
    std::uint16_t ehsize = 0;
    std::uint16_t phentsize = 0;
    std::uint16_t phnum_raw = 0;
    std::uint16_t shentsize = 0;
    std::uint16_t shnum_raw = 0;
    std::uint16_t shstrndx_raw = 0;

    // Effective values after applying extended numbering. When a file has more
    // than 0xff00 sections, e_shnum is 0 and the real count lives in sh_size of
    // section 0; e_shstrndx and e_phnum use sh_link / sh_info the same way.
    std::uint64_t section_count = 0;
    std::uint32_t section_name_table_index = 0;
    std::uint32_t program_header_count = 0;
    bool extended_section_numbering = false;
    bool extended_program_numbering = false;
};

// One entry of the program header table (Elf64_Phdr): how the loader maps the file.
struct ProgramHeader {
    std::uint32_t index = 0;
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint64_t offset = 0;
    std::uint64_t vaddr = 0;
    std::uint64_t paddr = 0;
    std::uint64_t filesz = 0;
    std::uint64_t memsz = 0;
    std::uint64_t align = 0;
};

// Human-readable decoding of header fields. Unknown values fall back to a
// numeric representation rather than guessing.
std::string elf_class_name(std::uint8_t file_class);
std::string data_encoding_name(std::uint8_t encoding);
std::string os_abi_name(std::uint8_t os_abi);
std::string elf_type_name(std::uint16_t type);         // e.g. "REL"
std::string elf_type_description(std::uint16_t type);  // e.g. "relocatable object file"
std::string machine_name(std::uint16_t machine);
std::string segment_type_name(std::uint32_t type);
std::string segment_flags_string(std::uint32_t flags);  // "R E", "RW " ...

}  // namespace binscope
