// Command-line behaviour: exit codes, stream separation and argument handling.

#include "binscope/cli.hpp"
#include "fixture_util.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace binscope;
namespace fs = std::filesystem;

namespace {

struct Result {
    int code;
    std::string out;
    std::string err;
};

Result run(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    const int code = cli::run(args, out, err);
    return {code, out.str(), err.str()};
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Temporary directory removed when the test finishes.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& leaf) : path(fs::temp_directory_path() / leaf) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE(no_arguments_is_a_usage_error) {
    const Result r = run({});
    CHECK_EQ(r.code, cli::kExitUsage);
    CHECK(r.out.empty());
    CHECK(contains(r.err, "no command"));
}

TEST_CASE(help_goes_to_stdout_and_succeeds) {
    for (const char* flag : {"--help", "-h", "help"}) {
        const Result r = run({flag});
        CHECK_EQ(r.code, cli::kExitSuccess);
        CHECK(contains(r.out, "Usage:"));
        CHECK(r.err.empty());
    }
}

TEST_CASE(unknown_command_is_a_usage_error) {
    const Result r = run({"frobnicate", "x"});
    CHECK_EQ(r.code, cli::kExitUsage);
    CHECK(contains(r.err, "unknown command"));
}

TEST_CASE(missing_file_argument_is_a_usage_error) {
    const Result r = run({"inspect"});
    CHECK_EQ(r.code, cli::kExitUsage);
    CHECK(contains(r.err, "needs a file argument"));
    CHECK(r.out.empty());
}

TEST_CASE(extra_arguments_are_a_usage_error) {
    const Result r = run({"inspect", "a.o", "b.o"});
    CHECK_EQ(r.code, cli::kExitUsage);
    CHECK(contains(r.err, "too many arguments"));
}

TEST_CASE(unknown_option_is_a_usage_error) {
    const Result r = run({"inspect", "--bogus", fixture::path("simple.o").string()});
    CHECK_EQ(r.code, cli::kExitUsage);
    CHECK(contains(r.err, "unknown option"));
}

TEST_CASE(nonexistent_file_is_an_io_error) {
    const Result r = run({"inspect", "definitely-not-here.o"});
    CHECK_EQ(r.code, cli::kExitFailure);
    CHECK(contains(r.err, "I/O error"));
    CHECK(r.out.empty());
}

TEST_CASE(directory_is_an_io_error) {
    const Result r = run({"inspect", fs::temp_directory_path().string()});
    CHECK_EQ(r.code, cli::kExitFailure);
    CHECK(contains(r.err, "is a directory"));
}

TEST_CASE(non_elf_file_is_reported_as_malformed) {
    TempDir dir("binscope_cli_text");
    const fs::path text = dir.path / "notes.txt";
    std::ofstream(text) << "just some text\n";
    const Result r = run({"inspect", text.string()});
    CHECK_EQ(r.code, cli::kExitFailure);
    CHECK(contains(r.err, "malformed ELF"));
    CHECK(r.out.empty());
}

TEST_CASE(inspect_object_reports_header) {
    const Result r = run({"inspect", fixture::path("simple.o").string()});
    CHECK_EQ(r.code, cli::kExitSuccess);
    CHECK(contains(r.out, "ELF64"));
    CHECK(contains(r.out, "REL (relocatable object file)"));
    CHECK(contains(r.out, "x86-64"));
    CHECK(r.err.empty());
}

TEST_CASE(inspect_executable_reports_segments) {
    const Result r = run({"inspect", fixture::path("program").string()});
    CHECK_EQ(r.code, cli::kExitSuccess);
    CHECK(contains(r.out, "LOAD"));
    CHECK(contains(r.out, "Program interpreter:"));
}

TEST_CASE(paths_with_spaces_and_leading_dashes_work) {
    TempDir dir("binscope cli dir with spaces");
    const fs::path spaced = dir.path / "my object file.o";
    fs::copy_file(fixture::path("simple.o"), spaced);
    CHECK_EQ(run({"inspect", spaced.string()}).code, cli::kExitSuccess);

    const fs::path dashed = dir.path / "-dash.o";
    fs::copy_file(fixture::path("simple.o"), dashed);
    const fs::path previous = fs::current_path();
    fs::current_path(dir.path);
    CHECK_EQ(run({"inspect", "--", "-dash.o"}).code, cli::kExitSuccess);
    fs::current_path(previous);
}
