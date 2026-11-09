#include "aether/foundation/assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "aether/foundation/platform.hpp"

namespace aether {
namespace {

void default_handler(const char* expr, const char* message, const char* file, int line) {
    std::fprintf(stderr, "[aether] assertion failed: %s\n  message: %s\n  at %s:%d (thread %u)\n",
                 expr, message ? message : "", file, line, platform::current_thread_index());
    std::fflush(stderr);
}

std::atomic<AssertHandler> g_handler{&default_handler};

}  // namespace

AssertHandler set_assert_handler(AssertHandler handler) noexcept {
    return g_handler.exchange(handler ? handler : &default_handler);
}

void assert_failed(const char* expr, const char* message, const char* file, int line) noexcept {
    g_handler.load()(expr, message, file, line);
    std::abort();
}

}  // namespace aether
