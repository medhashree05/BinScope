#pragma once

#include <cstdint>
#include <string>

namespace binscope {

// One decoded section header (Elf64_Shdr) with its resolved name.
struct Section {
    std::uint32_t index = 0;
    std::string name;
    std::uint32_t name_offset = 0;  // sh_name: offset into the section-name string table
    bool name_valid = true;         // false if sh_name did not point at a valid string
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::uint64_t addr = 0;  // virtual address at run time (0 in relocatable objects)
    std::uint64_t offset = 0;  // byte offset of the contents within the file
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t addralign = 0;
    std::uint64_t entsize = 0;

    // False when the header claims file contents that lie outside the file.
    // Such sections are still listed, but their contents are never read.
    bool contents_in_bounds = true;

    // SHT_NOBITS sections (.bss, .tbss) occupy memory at run time but no file
    // bytes; their sh_offset/sh_size do not describe data stored in the file.
    bool has_file_contents() const;
};

std::string section_type_name(std::uint32_t type);
std::string section_flags_string(std::uint64_t flags);   // compact, readelf-style letters
std::string section_flags_verbose(std::uint64_t flags);  // e.g. "ALLOC|EXEC"
std::string section_purpose(const std::string& name);    // empty if no description known

}  // namespace binscope
