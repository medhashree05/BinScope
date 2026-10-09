#pragma once

#include <stdexcept>
#include <string>

namespace binscope {

// Distinguishes *why* an analysis failed so the CLI (and tests) can tell an
// unreadable file apart from a corrupt ELF image or a valid-but-unsupported one.
enum class ErrorKind {
    Io,           // file could not be opened or read
    Malformed,    // bytes violate the ELF format or are internally inconsistent
    Unsupported,  // well-formed, but uses a feature BinScope does not implement
};

const char* to_string(ErrorKind kind) noexcept;

class Error : public std::runtime_error {
public:
    Error(ErrorKind kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}

    ErrorKind kind() const noexcept { return kind_; }

private:
    ErrorKind kind_;
};

class IoError : public Error {
public:
    explicit IoError(const std::string& message) : Error(ErrorKind::Io, message) {}
};

class ParseError : public Error {
public:
    explicit ParseError(const std::string& message) : Error(ErrorKind::Malformed, message) {}
};

class UnsupportedError : public Error {
public:
    explicit UnsupportedError(const std::string& message)
        : Error(ErrorKind::Unsupported, message) {}
};

}  // namespace binscope
