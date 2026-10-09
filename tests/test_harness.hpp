#pragma once

// Minimal self-contained test harness (no external dependencies).
//
//   TEST_CASE(name) { CHECK(cond); REQUIRE(cond); CHECK_THROWS_AS(expr, Type); }
//
// CHECK records a failure and continues; REQUIRE aborts the current test case.
// Each test executable links test_main.cpp, which runs every registered case.

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace bt {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct AbortTest {};

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline void report_failure(const char* file, int line, const std::string& message) {
    ++failure_count();
    std::cerr << file << ":" << line << ": FAILED: " << message << '\n';
}

int run_all();

}  // namespace bt

#define BT_CONCAT_INNER(a, b) a##b
#define BT_CONCAT(a, b) BT_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                               \
    static void name();                                               \
    static const ::bt::Registrar BT_CONCAT(name, _registrar)(#name, &name); \
    static void name()

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) ::bt::report_failure(__FILE__, __LINE__, #cond); \
    } while (false)

#define REQUIRE(cond)                                                 \
    do {                                                              \
        if (!(cond)) {                                                \
            ::bt::report_failure(__FILE__, __LINE__, #cond);          \
            throw ::bt::AbortTest{};                                  \
        }                                                             \
    } while (false)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto& bt_a = (a);                                                         \
        const auto& bt_b = (b);                                                         \
        if (!(bt_a == bt_b)) {                                                          \
            std::ostringstream bt_msg;                                                  \
            bt_msg << #a " == " #b " (" << bt_a << " vs " << bt_b << ")";               \
            ::bt::report_failure(__FILE__, __LINE__, bt_msg.str());                     \
        }                                                                               \
    } while (false)

#define CHECK_THROWS_AS(expr, Type)                                                     \
    do {                                                                                \
        bool bt_caught = false;                                                         \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const Type&) {                                                         \
            bt_caught = true;                                                           \
        } catch (const std::exception& bt_e) {                                          \
            ::bt::report_failure(__FILE__, __LINE__,                                    \
                                 std::string(#expr " threw the wrong exception: ") +    \
                                     bt_e.what());                                      \
            bt_caught = true;                                                           \
        }                                                                               \
        if (!bt_caught)                                                                 \
            ::bt::report_failure(__FILE__, __LINE__, #expr " did not throw " #Type);   \
    } while (false)

#define CHECK_NOTHROW(expr)                                                             \
    do {                                                                                \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const std::exception& bt_e) {                                          \
            ::bt::report_failure(__FILE__, __LINE__,                                    \
                                 std::string(#expr " threw: ") + bt_e.what());          \
        }                                                                               \
    } while (false)
