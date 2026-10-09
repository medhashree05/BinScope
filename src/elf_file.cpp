#include "binscope/elf_file.hpp"

#include "binscope/error.hpp"
#include "binscope/parser.hpp"

#include <fstream>
#include <limits>
#include <system_error>

namespace binscope {

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    const std::string shown = path.string();

    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::exists(status)) {
        throw IoError("cannot open '" + shown + "': no such file");
    }
    if (fs::is_directory(status)) {
        throw IoError("cannot analyze '" + shown + "': is a directory");
    }
    if (!fs::is_regular_file(status)) {
        throw IoError("cannot analyze '" + shown + "': not a regular file");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw IoError("cannot open '" + shown + "' for reading");
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
        throw IoError("cannot determine the size of '" + shown + "'");
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            throw IoError("'" + shown + "' is too large to read");
        }
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw IoError("short read from '" + shown + "'");
        }
    }
    return bytes;
}

ElfFile ElfFile::load(const std::filesystem::path& path) {
    return ElfFile(read_file_bytes(path), path.string());
}

ElfFile ElfFile::from_bytes(std::vector<std::uint8_t> bytes, std::string display_name) {
    return ElfFile(std::move(bytes), std::move(display_name));
}

ElfFile::ElfFile(std::vector<std::uint8_t> bytes, std::string name)
    : bytes_(std::move(bytes)), name_(std::move(name)) {
    const ByteView file(bytes_);
    header_ = parse_elf_header(file);
    sections_ = parse_section_headers(file, header_, warnings_);
    segments_ = parse_program_headers(file, header_);
}

const Section* ElfFile::section_at(std::uint64_t index) const noexcept {
    return index < sections_.size() ? &sections_[static_cast<std::size_t>(index)] : nullptr;
}

const Section* ElfFile::find_section(std::string_view name) const noexcept {
    for (const Section& s : sections_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

ByteView ElfFile::section_data(const Section& section) const {
    const std::string label = "section [" + std::to_string(section.index) + "] '" + section.name + "'";
    if (!section.has_file_contents()) {
        throw ParseError(label + " has no contents stored in the file (type " +
                         section_type_name(section.type) + ")");
    }
    return bytes().subview(section.offset, section.size, label + " contents");
}

}  // namespace binscope
