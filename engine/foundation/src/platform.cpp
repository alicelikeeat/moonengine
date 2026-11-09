#include "aether/foundation/platform.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

namespace aether::platform {

std::string_view os_name() noexcept {
    switch (current_os()) {
        case Os::Windows: return "windows";
        case Os::Linux: return "linux";
        case Os::MacOS: return "macos";
        case Os::Unknown: break;
    }
    return "unknown";
}

unsigned hardware_threads() noexcept {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1u : n;
}

std::uint32_t current_thread_index() noexcept {
    static std::atomic<std::uint32_t> next{1};
    thread_local const std::uint32_t index = next.fetch_add(1, std::memory_order_relaxed);
    return index;
}

void set_current_thread_name(std::string_view name) noexcept {
    try {
        std::string n(name);
#if defined(_WIN32)
        std::wstring wide(n.begin(), n.end());
        SetThreadDescription(GetCurrentThread(), wide.c_str());
#elif defined(__APPLE__)
        pthread_setname_np(n.c_str());
#elif defined(__linux__)
        if (n.size() > 15) n.resize(15);  // kernel limit incl. terminator is 16
        pthread_setname_np(pthread_self(), n.c_str());
#endif
    } catch (...) {
        // Naming is diagnostic only; ignore allocation failures.
    }
}

std::uint64_t monotonic_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace aether::platform
