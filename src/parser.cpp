#include "binscope/parser.hpp"

#include "binscope/elf_constants.hpp"
#include "binscope/error.hpp"

#include <algorithm>
#include <limits>

namespace binscope {

using namespace elf;

namespace {

// Field offsets inside Elf64_Ehdr.
constexpr std::uint64_t kEhdrType = 16;
constexpr std::uint64_t kEhdrMachine = 18;
constexpr std::uint64_t kEhdrVersion = 20;
constexpr std::uint64_t kEhdrEntry = 24;
constexpr std::uint64_t kEhdrPhoff = 32;
constexpr std::uint64_t kEhdrShoff = 40;
constexpr std::uint64_t kEhdrFlags = 48;
constexpr std::uint64_t kEhdrEhsize = 52;
constexpr std::uint64_t kEhdrPhentsize = 54;
constexpr std::uint64_t kEhdrPhnum = 56;
constexpr std::uint64_t kEhdrShentsize = 58;
constexpr std::uint64_t kEhdrShnum = 60;
constexpr std::uint64_t kEhdrShstrndx = 62;

// Decodes one Elf64_Shdr from a view that is exactly kShdrSize bytes long.
Section decode_section_header(ByteView entry, std::uint32_t index) {
    Section s;
    s.index = index;
    s.name_offset = entry.u32(0);
    s.type = entry.u32(4);
    s.flags = entry.u64(8);
    s.addr = entry.u64(16);
    s.offset = entry.u64(24);
    s.size = entry.u64(32);
    s.link = entry.u32(40);
    s.info = entry.u32(44);
    s.addralign = entry.u64(48);
    s.entsize = entry.u64(56);
    return s;
}

bool has_elf_magic_prefix(ByteView file) {
    const std::size_t n = std::min<std::size_t>(file.size(), sizeof kMagic);
    for (std::size_t i = 0; i < n; ++i) {
        if (file.u8(i) != kMagic[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

ElfHeader parse_elf_header(ByteView file) {
    if (file.empty()) {
        throw ParseError("file is empty");
    }
    if (!has_elf_magic_prefix(file)) {
        throw ParseError("not an ELF file (bad magic number; expected 7f 45 4c 46)");
    }
    if (file.size() < EI_NIDENT) {
        throw ParseError("truncated ELF identification: file has " + std::to_string(file.size()) +
                         " bytes, e_ident needs " + std::to_string(EI_NIDENT));
    }

    ElfHeader h;
    h.file_class = file.u8(EI_CLASS);
    h.data_encoding = file.u8(EI_DATA);
    h.ident_version = file.u8(EI_VERSION);
    h.os_abi = file.u8(EI_OSABI);
    h.abi_version = file.u8(EI_ABIVERSION);

    // Class and byte order decide how every later field is laid out, so they are
    // checked before anything else is decoded.
    if (h.file_class == ELFCLASS32) {
        throw UnsupportedError("ELF32 (32-bit) files are not supported; BinScope handles ELF64 only");
    }
    if (h.file_class != ELFCLASS64) {
        throw ParseError("invalid ELF class " + std::to_string(h.file_class));
    }
    if (h.data_encoding == ELFDATA2MSB) {
        throw UnsupportedError(
            "big-endian ELF files are not supported; BinScope handles little-endian only");
    }
    if (h.data_encoding != ELFDATA2LSB) {
        throw ParseError("invalid ELF data encoding " + std::to_string(h.data_encoding));
    }
    if (h.ident_version != EV_CURRENT) {
        throw ParseError("invalid ELF identification version " + std::to_string(h.ident_version));
    }
    if (file.size() < kEhdrSize) {
        throw ParseError("truncated ELF header: file has " + std::to_string(file.size()) +
                         " bytes, an ELF64 header needs " + std::to_string(kEhdrSize));
    }

    h.type = file.u16(kEhdrType);
    h.machine = file.u16(kEhdrMachine);
    h.version = file.u32(kEhdrVersion);
    h.entry = file.u64(kEhdrEntry);
    h.phoff = file.u64(kEhdrPhoff);
    h.shoff = file.u64(kEhdrShoff);
    h.flags = file.u32(kEhdrFlags);
    h.ehsize = file.u16(kEhdrEhsize);
    h.phentsize = file.u16(kEhdrPhentsize);
    h.phnum_raw = file.u16(kEhdrPhnum);
    h.shentsize = file.u16(kEhdrShentsize);
    h.shnum_raw = file.u16(kEhdrShnum);
    h.shstrndx_raw = file.u16(kEhdrShstrndx);

    if (h.version != EV_CURRENT) {
        throw ParseError("invalid e_version " + std::to_string(h.version));
    }
    if (h.ehsize < kEhdrSize) {
        throw ParseError("e_ehsize is " + std::to_string(h.ehsize) + ", smaller than the " +
                         std::to_string(kEhdrSize) + "-byte ELF64 header");
    }
    // Entry sizes are only meaningful when the corresponding table exists. A
    // mismatching size would make every per-entry offset computation wrong.
    if (h.shoff != 0 && h.shentsize != kShdrSize) {
        throw ParseError("unexpected e_shentsize " + std::to_string(h.shentsize) +
                         " (ELF64 section headers are " + std::to_string(kShdrSize) + " bytes)");
    }
    if (h.phnum_raw != 0 && h.phentsize != kPhdrSize) {
        throw ParseError("unexpected e_phentsize " + std::to_string(h.phentsize) +
                         " (ELF64 program headers are " + std::to_string(kPhdrSize) + " bytes)");
    }

    // Provisional values; parse_section_headers() applies extended numbering.
    h.section_count = h.shnum_raw;
    h.section_name_table_index = h.shstrndx_raw;
    h.program_header_count = h.phnum_raw;
    return h;
}

std::vector<Section> parse_section_headers(ByteView file, ElfHeader& header,
                                           std::vector<std::string>& warnings) {
    std::vector<Section> sections;

    if (header.shoff == 0) {
        if (header.shnum_raw != 0) {
            throw ParseError("e_shnum is " + std::to_string(header.shnum_raw) +
                             " but e_shoff is 0 (no section header table)");
        }
        if (header.phnum_raw == PN_XNUM) {
            throw ParseError(
                "e_phnum uses extended numbering (PN_XNUM) but there is no section header table");
        }
        header.section_count = 0;
        header.section_name_table_index = 0;
        return sections;
    }

    // Section 0 is always read first: with extended numbering it carries the real
    // section count (sh_size), name-table index (sh_link) and phnum (sh_info).
    const Section first =
        decode_section_header(file.subview(header.shoff, kShdrSize, "section header table"), 0);

    std::uint64_t count = header.shnum_raw;
    if (header.shnum_raw == 0) {
        count = first.size;
        header.extended_section_numbering = true;
        if (count == 0) {
            warnings.push_back("e_shnum is 0 and section 0 sh_size is 0; treating file as having no sections");
        }
    }
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw ParseError("section count " + std::to_string(count) + " is implausibly large");
    }

    const auto table_size = checked_mul(count, kShdrSize);
    if (!table_size || !file.contains(header.shoff, *table_size)) {
        throw ParseError("section header table (" + std::to_string(count) + " entries at offset " +
                         hex(header.shoff) + ") extends past the end of the file (size " +
                         hex(file.size()) + ")");
    }
    header.section_count = count;

    std::uint32_t name_index = header.shstrndx_raw;
    if (header.shstrndx_raw == SHN_XINDEX) {
        name_index = first.link;
        header.extended_section_numbering = true;
    } else if (header.shstrndx_raw >= SHN_LORESERVE) {
        warnings.push_back("e_shstrndx " + hex(header.shstrndx_raw) +
                           " is a reserved index; section names are unavailable");
        name_index = 0;
    }
    if (name_index != 0 && name_index >= count) {
        warnings.push_back("section name string table index " + std::to_string(name_index) +
                           " is out of range (" + std::to_string(count) +
                           " sections); section names are unavailable");
        name_index = 0;
    }

    if (header.phnum_raw == PN_XNUM) {
        header.program_header_count = first.info;
        header.extended_program_numbering = true;
    }

    sections.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const ByteView entry = file.subview(header.shoff + i * kShdrSize, kShdrSize, "section header");
        Section s = decode_section_header(entry, static_cast<std::uint32_t>(i));
        if (s.has_file_contents() && !file.contains(s.offset, s.size)) {
            s.contents_in_bounds = false;
            warnings.push_back("section [" + std::to_string(i) + "] contents (offset " +
                               hex(s.offset) + ", size " + hex(s.size) +
                               ") extend past the end of the file");
        }
        sections.push_back(std::move(s));
    }

    if (!sections.empty() && sections[0].type != SHT_NULL) {
        warnings.push_back("section [0] is not SHT_NULL as the ELF specification requires");
    }

    // Resolve names through the section-name string table (.shstrtab).
    header.section_name_table_index = name_index;
    if (name_index == 0) {
        return sections;
    }
    const Section& names = sections[name_index];
    if (names.type != SHT_STRTAB) {
        warnings.push_back("section name table [" + std::to_string(name_index) +
                           "] is not SHT_STRTAB (type " + section_type_name(names.type) + ")");
    }
    if (!names.has_file_contents() || !names.contents_in_bounds) {
        warnings.push_back("section name table [" + std::to_string(name_index) +
                           "] has no readable contents; section names are unavailable");
        return sections;
    }
    const ByteView name_data = file.subview(names.offset, names.size, "section name table");
    for (Section& s : sections) {
        if (const auto name = name_data.c_string(s.name_offset)) {
            s.name = std::string(*name);
        } else {
            s.name_valid = false;
            s.name = "<corrupt name @" + hex(s.name_offset) + ">";
            warnings.push_back("section [" + std::to_string(s.index) + "] name offset " +
                               hex(s.name_offset) +
                               " is outside the section name table or unterminated");
        }
    }
    return sections;
}

std::vector<ProgramHeader> parse_program_headers(ByteView file, const ElfHeader& header) {
    std::vector<ProgramHeader> segments;
    const std::uint64_t count = header.program_header_count;
    if (count == 0) {
        return segments;
    }
    if (header.phoff == 0) {
        throw ParseError("program header count is " + std::to_string(count) + " but e_phoff is 0");
    }
    const auto table_size = checked_mul(count, kPhdrSize);
    if (!table_size || !file.contains(header.phoff, *table_size)) {
        throw ParseError("program header table (" + std::to_string(count) + " entries at offset " +
                         hex(header.phoff) + ") extends past the end of the file (size " +
                         hex(file.size()) + ")");
    }

    segments.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const ByteView e = file.subview(header.phoff + i * kPhdrSize, kPhdrSize, "program header");
        ProgramHeader p;
        p.index = static_cast<std::uint32_t>(i);
        p.type = e.u32(0);
        p.flags = e.u32(4);
        p.offset = e.u64(8);
        p.vaddr = e.u64(16);
        p.paddr = e.u64(24);
        p.filesz = e.u64(32);
        p.memsz = e.u64(40);
        p.align = e.u64(48);
        segments.push_back(p);
    }
    return segments;
}

}  // namespace binscope
