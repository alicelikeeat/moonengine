#include "aether_test.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

namespace aether::test {
namespace {

struct TestCase {
    const char* name;
    const char* file;
    int line;
    TestFn fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_current_failures = 0;

}  // namespace

Registrar::Registrar(const char* name, const char* file, int line, TestFn fn) {
    registry().push_back(TestCase{name, file, line, fn});
}

void report_failure(const char* file, int line, const std::string& message) {
    ++g_current_failures;
    std::fprintf(stderr, "  %s:%d: FAILED %s\n", file, line, message.c_str());
}

}  // namespace aether::test

int main(int argc, char** argv) {
    using namespace aether::test;
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const auto& t : registry()) std::printf("%s\n", t.name);
            return 0;
        }
        filter = argv[i];
    }

    int run = 0;
    int failed = 0;
    for (const auto& t : registry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) continue;
        ++run;
        g_current_failures = 0;
        const auto start = std::chrono::steady_clock::now();
        try {
            t.fn();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            report_failure(t.file, t.line, std::string("unhandled exception: ") + e.what());
        } catch (...) {
            report_failure(t.file, t.line, "unhandled non-standard exception");
        }
        const auto ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
        if (g_current_failures > 0) {
            ++failed;
            std::printf("[FAIL] %s (%.2f ms)\n", t.name, ms);
        } else {
            std::printf("[ OK ] %s (%.2f ms)\n", t.name, ms);
        }
    }
    std::printf("\n%d test(s) run, %d failed\n", run, failed);
    if (run == 0) {
        std::fprintf(stderr, "no tests matched\n");
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
