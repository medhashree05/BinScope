#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace binscope {

// Overflow-checked arithmetic for offsets and sizes taken from untrusted input.
std::optional<std::uint64_t> checked_add(std::uint64_t a, std::uint64_t b) noexcept;
std::optional<std::uint64_t> checked_mul(std::uint64_t a, std::uint64_t b) noexcept;

// Formats a value as 0x-prefixed hexadecimal (used in diagnostics and reports).
std::string hex(std::uint64_t value);

// A non-owning, bounds-checked window over file bytes.
//
// ELF structures are decoded field by field from little-endian bytes instead of
// reinterpret_cast-ing file memory into structs. That avoids unaligned access,
// strict-aliasing violations and host-endianness assumptions, and it means every
// read goes through a range check against the real buffer length.
class ByteView {
public:
    ByteView() = default;
    ByteView(const std::uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}
    explicit ByteView(const std::vector<std::uint8_t>& bytes) noexcept
        : data_(bytes.data()), size_(bytes.size()) {}

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    // True when [offset, offset + length) lies entirely inside the view.
    bool contains(std::uint64_t offset, std::uint64_t length) const noexcept;

    // Returns the sub-range or throws ParseError naming `what`.
    ByteView subview(std::uint64_t offset, std::uint64_t length, const std::string& what) const;

    // Little-endian reads; each throws ParseError if the read would leave the view.
    std::uint8_t u8(std::uint64_t offset) const;
    std::uint16_t u16(std::uint64_t offset) const;
    std::uint32_t u32(std::uint64_t offset) const;
    std::uint64_t u64(std::uint64_t offset) const;
    std::int64_t i64(std::uint64_t offset) const;

    // Reads a NUL-terminated string starting at `offset`. Returns nullopt if the
    // offset is out of range or no terminator exists before the end of the view.
    std::optional<std::string_view> c_string(std::uint64_t offset) const noexcept;

private:
    std::uint64_t read_le(std::uint64_t offset, unsigned width) const;

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace binscope
