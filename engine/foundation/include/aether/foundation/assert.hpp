// Assertions that stay enabled in every build type.
//
// A failed assertion is a programming error: it reports file, line, thread
// and message through the installed handler, then aborts the process.
#pragma once

namespace aether {

using AssertHandler = void (*)(const char* expr, const char* message,
                               const char* file, int line);

// Installs a process-wide handler and returns the previous one. The handler
// must not return; if it does, the process aborts anyway. Passing nullptr
// restores the default handler, which prints to stderr.
AssertHandler set_assert_handler(AssertHandler handler) noexcept;

[[noreturn]] void assert_failed(const char* expr, const char* message,
                                const char* file, int line) noexcept;

}  // namespace aether

#define AETHER_ASSERT(expr, message)                                       \
    do {                                                                   \
        if (!(expr)) ::aether::assert_failed(#expr, message, __FILE__, __LINE__); \
    } while (false)
