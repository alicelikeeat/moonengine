// CPU performance tracing.
//
// Scoped events are collected into a TraceCollector and exported in the
// open Chrome Trace Event JSON format, viewable in Perfetto or
// chrome://tracing without proprietary tools.
#pragma once

#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include "aether/foundation/platform.hpp"

namespace aether {

struct TraceEvent {
    std::string name;
    std::string category;
    std::uint64_t start_ns = 0;
    std::uint64_t duration_ns = 0;
    std::uint32_t thread = 0;
};

class TraceCollector {
public:
    explicit TraceCollector(std::size_t max_events = 1u << 20) : max_events_(max_events) {}

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    void record(TraceEvent event);
    [[nodiscard]] std::vector<TraceEvent> snapshot() const;
    [[nodiscard]] std::size_t dropped() const;
    void clear();

    void write_chrome_json(std::ostream& out) const;

private:
    mutable std::mutex mutex_;
    std::vector<TraceEvent> events_;
    std::size_t max_events_;
    std::size_t dropped_ = 0;
    bool enabled_ = true;
};

class ScopedTrace {
public:
    ScopedTrace(TraceCollector& collector, const char* name, const char* category = "engine") noexcept
        : collector_(collector), name_(name), category_(category),
          start_ns_(collector.enabled() ? platform::monotonic_ns() : 0) {}
    ~ScopedTrace();

    ScopedTrace(const ScopedTrace&) = delete;
    ScopedTrace& operator=(const ScopedTrace&) = delete;

private:
    TraceCollector& collector_;
    const char* name_;
    const char* category_;
    std::uint64_t start_ns_;
};

}  // namespace aether
