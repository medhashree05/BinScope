#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace binscope::cli {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitFailure = 1;  // file could not be read or analyzed
inline constexpr int kExitUsage = 2;    // bad command line

// Runs one BinScope command. `args` excludes the program name. Reports go to
// `out`; usage errors, analysis errors and warnings go to `err`.
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

std::string usage_text();

}  // namespace binscope::cli
