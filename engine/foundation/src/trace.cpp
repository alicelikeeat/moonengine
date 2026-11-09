#include "aether/foundation/trace.hpp"

#include <cstdio>

#include "aether/foundation/text.hpp"

namespace aether {

void TraceCollector::record(TraceEvent event) {
    std::lock_guard lock(mutex_);
    if (events_.size() >= max_events_) {
        ++dropped_;
        return;
    }
    events_.push_back(std::move(event));
}

std::vector<TraceEvent> TraceCollector::snapshot() const {
    std::lock_guard lock(mutex_);
    return events_;
}

std::size_t TraceCollector::dropped() const {
    std::lock_guard lock(mutex_);
    return dropped_;
}

void TraceCollector::clear() {
    std::lock_guard lock(mutex_);
    events_.clear();
    dropped_ = 0;
}

void TraceCollector::write_chrome_json(std::ostream& out) const {
    std::lock_guard lock(mutex_);
    std::string json = "{\"traceEvents\":[";
    char num[64];
    bool first = true;
    for (const auto& e : events_) {
        if (!first) json += ',';
        first = false;
        json += "{\"name\":\"";
        append_json_escaped(json, e.name);
        json += "\",\"cat\":\"";
        append_json_escaped(json, e.category);
        // Chrome trace timestamps are microseconds.
        std::snprintf(num, sizeof(num), "\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f",
                      static_cast<double>(e.start_ns) / 1000.0,
                      static_cast<double>(e.duration_ns) / 1000.0);
        json += num;
        json += ",\"pid\":1,\"tid\":";
        json += std::to_string(e.thread);
        json += '}';
    }
    json += "],\"displayTimeUnit\":\"ms\"}";
    out << json;
}

ScopedTrace::~ScopedTrace() {
    if (!collector_.enabled() || start_ns_ == 0) return;
    const std::uint64_t end = platform::monotonic_ns();
    try {
        collector_.record(TraceEvent{name_, category_, start_ns_, end - start_ns_,
                                     platform::current_thread_index()});
    } catch (...) {
        // Tracing must never take down the caller.
    }
}

}  // namespace aether
