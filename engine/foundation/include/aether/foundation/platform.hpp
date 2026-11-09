// Thin platform abstraction. Everything OS-specific in Foundation lives
// behind these functions so other modules never include system headers.
#pragma once

#include <cstdint>
#include <string_view>

namespace aether::platform {

enum class Os : std::uint8_t { Windows, Linux, MacOS, Unknown };

[[nodiscard]] constexpr Os current_os() noexcept {
#if defined(_WIN32)
    return Os::Windows;
#elif defined(__APPLE__)
    return Os::MacOS;
#elif defined(__linux__)
    return Os::Linux;
#else
    return Os::Unknown;
#endif
}

[[nodiscard]] std::string_view os_name() noexcept;

// Number of hardware threads, never less than 1.
[[nodiscard]] unsigned hardware_threads() noexcept;

// A small, stable, process-unique number for the calling thread (1, 2, 3...).
// Used in logs and traces instead of opaque std::thread::id values.
[[nodiscard]] std::uint32_t current_thread_index() noexcept;

// Best-effort: names the calling thread for debuggers and profilers.
void set_current_thread_name(std::string_view name) noexcept;

// Monotonic time in nanoseconds since an unspecified epoch.
[[nodiscard]] std::uint64_t monotonic_ns() noexcept;

}  // namespace aether::platform
