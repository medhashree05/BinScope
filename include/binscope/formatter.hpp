#pragma once

#include "binscope/analyzer.hpp"
#include "binscope/elf_file.hpp"
#include "binscope/relocation.hpp"
#include "binscope/symbol.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace binscope {

// Simple column-aligned plain-text table used by every report.
class TextTable {
public:
    enum class Align { Left, Right };

    void add_column(std::string title, Align align = Align::Left);
    void add_row(std::vector<std::string> cells);
    bool empty() const noexcept { return rows_.empty(); }
    void render(std::ostream& out, const std::string& indent = "  ") const;

private:
    struct Column {
        std::string title;
        Align align;
    };
    std::vector<Column> columns_;
    std::vector<std::vector<std::string>> rows_;
};

// Fixed-width, zero-padded hex for addresses and offsets ("0x0000000000401000").
std::string hex_padded(std::uint64_t value, int digits = 16);

// --- Report sections -------------------------------------------------------
void print_file_header(std::ostream& out, const ElfFile& file);
void print_program_headers(std::ostream& out, const ElfFile& file);
void print_sections(std::ostream& out, const ElfFile& file);

struct SymbolReportOptions {
    SymbolFilter filter;
    bool demangle = false;
};
void print_symbol_table(std::ostream& out, const ElfFile& file, const SymbolTable& table,
                        const SymbolReportOptions& options);

void print_relocations(std::ostream& out, const ElfFile& file,
                       const std::vector<RelocationSection>& sections, bool demangle_names);

void print_summary(std::ostream& out, const FileSummary& summary);

void print_comparison(std::ostream& out, const ComparisonReport& report, bool demangle_names);

// Signed hexadecimal with explicit sign: "+0x10", "-0x4".
std::string signed_hex(std::int64_t value);

// Prints parse-time warnings collected outside ElfFile (e.g. from symbol tables).
void print_extra_warnings(std::ostream& err, const ElfFile& file,
                          const std::vector<std::string>& warnings);

// Writes parser warnings, one per line, prefixed with the file name.
void print_warnings(std::ostream& err, const ElfFile& file);

}  // namespace binscope
