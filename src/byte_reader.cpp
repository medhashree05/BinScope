#include "binscope/byte_reader.hpp"

#include "binscope/error.hpp"

#include <cstring>
#include <limits>
#include <sstream>

namespace binscope {

const char* to_string(ErrorKind kind) noexcept {
    switch (kind) {
        case ErrorKind::Io: return "I/O error";
        case ErrorKind::Malformed: return "malformed ELF";
        case ErrorKind::Unsupported: return "unsupported";
    }
    return "error";
}

std::optional<std::uint64_t> checked_add(std::uint64_t a, std::uint64_t b) noexcept {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        return std::nullopt;
    }
    return a + b;
}

std::optional<std::uint64_t> checked_mul(std::uint64_t a, std::uint64_t b) noexcept {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        return std::nullopt;
    }
    return a * b;
}

std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

bool ByteView::contains(std::uint64_t offset, std::uint64_t length) const noexcept {
    // Written as two comparisons so that offset + length can never overflow.
    return offset <= size_ && length <= size_ - offset;
}

ByteView ByteView::subview(std::uint64_t offset, std::uint64_t length,
                           const std::string& what) const {
    if (!contains(offset, length)) {
        throw ParseError(what + " (offset " + hex(offset) + ", size " + hex(length) +
                         ") extends past the end of the data (size " + hex(size_) + ")");
    }
    return ByteView(data_ + offset, static_cast<std::size_t>(length));
}

std::uint64_t ByteView::read_le(std::uint64_t offset, unsigned width) const {
    if (!contains(offset, width)) {
        throw ParseError("attempted to read " + std::to_string(width) + " bytes at offset " +
                         hex(offset) + " beyond the end of the data (size " + hex(size_) + ")");
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(data_[offset + i]) << (8U * i);
    }
    return value;
}

std::uint8_t ByteView::u8(std::uint64_t offset) const {
    return static_cast<std::uint8_t>(read_le(offset, 1));
}

std::uint16_t ByteView::u16(std::uint64_t offset) const {
    return static_cast<std::uint16_t>(read_le(offset, 2));
}

std::uint32_t ByteView::u32(std::uint64_t offset) const {
    return static_cast<std::uint32_t>(read_le(offset, 4));
}

std::uint64_t ByteView::u64(std::uint64_t offset) const { return read_le(offset, 8); }

std::int64_t ByteView::i64(std::uint64_t offset) const {
    // memcpy gives a well-defined two's-complement reinterpretation in C++17.
    const std::uint64_t raw = u64(offset);
    std::int64_t value = 0;
    std::memcpy(&value, &raw, sizeof value);
    return value;
}

std::optional<std::string_view> ByteView::c_string(std::uint64_t offset) const noexcept {
    if (offset >= size_) {
        return std::nullopt;
    }
    const auto start = static_cast<std::size_t>(offset);
    const void* terminator = std::memchr(data_ + start, 0, size_ - start);
    if (terminator == nullptr) {
        return std::nullopt;
    }
    const auto length =
        static_cast<std::size_t>(static_cast<const std::uint8_t*>(terminator) - (data_ + start));
    return std::string_view(reinterpret_cast<const char*>(data_ + start), length);
}

}  // namespace binscope
