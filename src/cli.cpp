#include "binscope/cli.hpp"

#include "binscope/analyzer.hpp"
#include "binscope/elf_file.hpp"
#include "binscope/error.hpp"
#include "binscope/formatter.hpp"
#include "binscope/symbol.hpp"

#include <exception>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace binscope::cli {

namespace {

constexpr const char* kVersion = "0.1.0";

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Positional arguments and --flags for one command. Anything after "--" is
// treated as a positional argument, so file names that start with '-' work.
struct ParsedArgs {
    std::vector<std::string> positional;
    std::set<std::string> flags;
};

ParsedArgs parse_command_args(const std::vector<std::string>& args, std::size_t first,
                              const std::set<std::string>& allowed_flags) {
    ParsedArgs parsed;
    bool only_positional = false;
    for (std::size_t i = first; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (!only_positional && a == "--") {
            only_positional = true;
        } else if (!only_positional && a.size() > 1 && a[0] == '-') {
            if (allowed_flags.count(a) == 0) {
                throw UsageError("unknown option '" + a + "' for '" + args[0] + "'");
            }
            parsed.flags.insert(a);
        } else {
            parsed.positional.push_back(a);
        }
    }
    return parsed;
}

void expect_files(const ParsedArgs& parsed, const std::string& command, std::size_t count,
                  const std::string& synopsis) {
    if (parsed.positional.size() < count) {
        throw UsageError("'" + command + "' needs " +
                         (count == 1 ? std::string("a file argument") :
                                       std::to_string(count) + " file arguments") +
                         "\nusage: " + synopsis);
    }
    if (parsed.positional.size() > count) {
        throw UsageError("too many arguments for '" + command + "'\nusage: " + synopsis);
    }
}

ElfFile load_and_warn(const std::string& path, std::ostream& err) {
    ElfFile file = ElfFile::load(path);
    print_warnings(err, file);
    return file;
}

int cmd_inspect(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const ParsedArgs parsed = parse_command_args(args, 1, {});
    expect_files(parsed, "inspect", 1, "binscope inspect <file>");
    const ElfFile file = load_and_warn(parsed.positional[0], err);

    out << "File: " << file.name() << "\n\n";
    print_file_header(out, file);
    out << '\n';
    print_program_headers(out, file);

    const FileSummary summary = summarize(file);
    print_extra_warnings(err, file, summary.warnings);
    out << '\n';
    print_summary(out, summary);
    return kExitSuccess;
}

int cmd_relocs(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const ParsedArgs parsed = parse_command_args(args, 1, {"--demangle"});
    expect_files(parsed, "relocs", 1, "binscope relocs <file> [--demangle]");
    const ElfFile file = load_and_warn(parsed.positional[0], err);

    std::vector<std::string> warnings;
    const std::vector<RelocationSection> sections = parse_relocation_sections(file, warnings);
    print_extra_warnings(err, file, warnings);

    out << "File: " << file.name() << " (" << elf_type_name(file.header().type) << ")\n\n";
    print_relocations(out, file, sections, parsed.flags.count("--demangle") != 0);
    return kExitSuccess;
}

int cmd_sections(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const ParsedArgs parsed = parse_command_args(args, 1, {});
    expect_files(parsed, "sections", 1, "binscope sections <file>");
    const ElfFile file = load_and_warn(parsed.positional[0], err);

    out << "File: " << file.name() << " (" << elf_type_name(file.header().type) << ")\n\n";
    print_sections(out, file);
    return kExitSuccess;
}

int cmd_symbols(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const std::string synopsis =
        "binscope symbols <file> [--defined | --undefined] [--functions] [--demangle]";
    const ParsedArgs parsed =
        parse_command_args(args, 1, {"--defined", "--undefined", "--functions", "--demangle"});
    expect_files(parsed, "symbols", 1, synopsis);

    SymbolReportOptions options;
    options.filter.defined_only = parsed.flags.count("--defined") != 0;
    options.filter.undefined_only = parsed.flags.count("--undefined") != 0;
    options.filter.functions_only = parsed.flags.count("--functions") != 0;
    options.demangle = parsed.flags.count("--demangle") != 0;
    if (options.filter.defined_only && options.filter.undefined_only) {
        throw UsageError("--defined and --undefined are mutually exclusive\nusage: " + synopsis);
    }

    const ElfFile file = load_and_warn(parsed.positional[0], err);
    std::vector<std::string> warnings;
    const std::vector<SymbolTable> tables = parse_symbol_tables(file, warnings);
    print_extra_warnings(err, file, warnings);

    out << "File: " << file.name() << " (" << elf_type_name(file.header().type) << ")\n\n";
    if (tables.empty()) {
        out << "No symbol tables (.symtab or .dynsym) in this file; it may have been stripped.\n";
        return kExitSuccess;
    }
    for (std::size_t i = 0; i < tables.size(); ++i) {
        if (i != 0) out << '\n';
        print_symbol_table(out, file, tables[i], options);
    }
    return kExitSuccess;
}

int cmd_compare(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const ParsedArgs parsed = parse_command_args(args, 1, {"--demangle"});
    expect_files(parsed, "compare", 2, "binscope compare <before> <after> [--demangle]");
    const ElfFile before = load_and_warn(parsed.positional[0], err);
    const ElfFile after = load_and_warn(parsed.positional[1], err);

    const ComparisonReport report = compare_files(before, after);
    for (const std::string& w : report.warnings) err << "binscope: warning: " << w << '\n';
    print_comparison(out, report, parsed.flags.count("--demangle") != 0);
    return kExitSuccess;
}

using CommandFn = std::function<int(const std::vector<std::string>&, std::ostream&, std::ostream&)>;

const std::map<std::string, CommandFn>& commands() {
    static const std::map<std::string, CommandFn> table = {
        {"inspect", cmd_inspect},
        {"sections", cmd_sections},
        {"symbols", cmd_symbols},
        {"relocs", cmd_relocs},
        {"compare", cmd_compare},
    };
    return table;
}

}  // namespace

std::string usage_text() {
    return "BinScope - ELF binary & linker analysis toolkit\n"
           "\n"
           "Usage:\n"
           "  binscope inspect <file>                 ELF header, segments and a summary\n"
           "  binscope sections <file>                section header table with explanations\n"
           "  binscope symbols <file> [options]       .symtab and .dynsym contents\n"
           "      --defined                           only symbols defined in this file\n"
           "      --undefined                         only undefined (external) references\n"
           "      --functions                         only STT_FUNC / STT_GNU_IFUNC symbols\n"
           "      --demangle                          show demangled C++ names\n"
           "  binscope relocs <file> [--demangle]     REL/RELA entries with explanations\n"
           "  binscope compare <before> <after> [--demangle]\n"
           "                                          how sections, symbols and relocations\n"
           "                                          differ (e.g. object file vs. linked program)\n"
           "  binscope --help                         show this help\n"
           "  binscope --version                      show the version\n"
           "\n"
           "Supported input: ELF64 little-endian files (ET_REL, ET_EXEC, ET_DYN).\n"
           "Exit status: 0 on success, 1 if a file cannot be analyzed, 2 on usage errors.\n";
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << "binscope: error: no command given\n\n" << usage_text();
        return kExitUsage;
    }
    const std::string& command = args[0];
    if (command == "--help" || command == "-h" || command == "help") {
        out << usage_text();
        return kExitSuccess;
    }
    if (command == "--version") {
        out << "binscope " << kVersion << '\n';
        return kExitSuccess;
    }

    const auto it = commands().find(command);
    if (it == commands().end()) {
        err << "binscope: error: unknown command '" << command << "'\n\n" << usage_text();
        return kExitUsage;
    }

    try {
        return it->second(args, out, err);
    } catch (const UsageError& e) {
        err << "binscope: error: " << e.what() << '\n';
        return kExitUsage;
    } catch (const Error& e) {
        err << "binscope: error: " << to_string(e.kind()) << ": " << e.what() << '\n';
        return kExitFailure;
    } catch (const std::exception& e) {
        err << "binscope: internal error: " << e.what() << '\n';
        return kExitFailure;
    }
}

}  // namespace binscope::cli
