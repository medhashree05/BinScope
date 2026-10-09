#include "test_harness.hpp"

int bt::run_all() {
    int failed_cases = 0;
    for (const TestCase& tc : registry()) {
        const int before = failure_count();
        try {
            tc.fn();
        } catch (const AbortTest&) {
            // failure already reported by REQUIRE
        } catch (const std::exception& e) {
            report_failure(tc.name, 0, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            report_failure(tc.name, 0, "unexpected non-standard exception");
        }
        const bool ok = failure_count() == before;
        if (!ok) ++failed_cases;
        std::cout << (ok ? "[  PASS  ] " : "[  FAIL  ] ") << tc.name << '\n';
    }
    std::cout << registry().size() - static_cast<std::size_t>(failed_cases) << "/"
              << registry().size() << " test cases passed\n";
    return failed_cases == 0 ? 0 : 1;
}

int main() { return bt::run_all(); }
