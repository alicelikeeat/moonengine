// Minimal, dependency-free unit test harness for Aether Engine.
//
//   AETHER_TEST(arena_allocates) { CHECK(x == 1); REQUIRE(ptr != nullptr); }
//
// CHECK records a failure and continues; REQUIRE stops the current test.
// The test binary accepts an optional substring filter and `--list`.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace aether::test {

using TestFn = void (*)();

struct Registrar {
    Registrar(const char* name, const char* file, int line, TestFn fn);
};

struct RequireFailed {};
struct Skipped {
    std::string reason;
};

void report_failure(const char* file, int line, const std::string& message);

template <typename A, typename B>
std::string describe_values(const A& a, const B& b) {
    std::ostringstream out;
    if constexpr (requires(std::ostream& os, const A& x) { os << x; }) {
        out << a;
    } else {
        out << "<value>";
    }
    out << " vs ";
    if constexpr (requires(std::ostream& os, const B& x) { os << x; }) {
        out << b;
    } else {
        out << "<value>";
    }
    return out.str();
}

}  // namespace aether::test

#define AETHER_TEST_CONCAT_INNER(a, b) a##b
#define AETHER_TEST_CONCAT(a, b) AETHER_TEST_CONCAT_INNER(a, b)

#define AETHER_TEST(name)                                                               \
    static void AETHER_TEST_CONCAT(aether_test_fn_, name)();                            \
    static const ::aether::test::Registrar AETHER_TEST_CONCAT(aether_test_reg_, name){  \
        #name, __FILE__, __LINE__, &AETHER_TEST_CONCAT(aether_test_fn_, name)};         \
    static void AETHER_TEST_CONCAT(aether_test_fn_, name)()

#define CHECK(expr)                                                                     \
    do {                                                                                \
        if (!(expr)) ::aether::test::report_failure(__FILE__, __LINE__, "CHECK(" #expr ")"); \
    } while (false)

#define REQUIRE(expr)                                                                   \
    do {                                                                                \
        if (!(expr)) {                                                                  \
            ::aether::test::report_failure(__FILE__, __LINE__, "REQUIRE(" #expr ")");   \
            throw ::aether::test::RequireFailed{};                                      \
        }                                                                               \
    } while (false)

// Ends the current test as skipped (e.g. no GPU). With the environment
// variable AETHER_TEST_NO_SKIP=1 a skip counts as a failure, so CI machines
// that are expected to run everything cannot pass by skipping.
#define AETHER_SKIP(reason) throw ::aether::test::Skipped{reason}

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto& aether_lhs_ = (a);                                                  \
        const auto& aether_rhs_ = (b);                                                  \
        if (!(aether_lhs_ == aether_rhs_))                                              \
            ::aether::test::report_failure(__FILE__, __LINE__,                          \
                "CHECK_EQ(" #a ", " #b "): " +                                          \
                ::aether::test::describe_values(aether_lhs_, aether_rhs_));             \
    } while (false)
