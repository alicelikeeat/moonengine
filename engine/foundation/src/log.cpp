#include "aether/foundation/log.hpp"

#include "aether/foundation/platform.hpp"
#include "aether/foundation/text.hpp"

namespace aether {

std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::Fatal: return "fatal";
    }
    return "unknown";
}

void TextSink::write(const LogRecord& r) {
    out_ << std::format("[{:>12.6f}] {:<5} {:<10} (t{}) {}\n",
                        static_cast<double>(r.timestamp_ns) * 1e-9, to_string(r.level), r.module,
                        r.thread, r.message);
}

void TextSink::flush() { out_.flush(); }

void JsonLinesSink::write(const LogRecord& r) {
    std::string line;
    line.reserve(128 + r.message.size());
    line += "{\"ts_ns\":";
    line += std::to_string(r.timestamp_ns);
    line += ",\"level\":\"";
    line += to_string(r.level);
    line += "\",\"module\":\"";
    append_json_escaped(line, r.module);
    line += "\",\"thread\":";
    line += std::to_string(r.thread);
    line += ",\"file\":\"";
    append_json_escaped(line, r.file ? r.file : "");
    line += "\",\"line\":";
    line += std::to_string(r.line);
    line += ",\"message\":\"";
    append_json_escaped(line, r.message);
    line += "\"}\n";
    out_ << line;
}

void JsonLinesSink::flush() { out_.flush(); }

void MemorySink::write(const LogRecord& r) {
    std::lock_guard lock(mutex_);
    if (capacity_ == 0) return;
    if (entries_.size() == capacity_) entries_.pop_front();
    entries_.push_back(Entry{r.level, std::string(r.module), std::string(r.message), r.thread,
                             r.timestamp_ns});
}

std::vector<MemorySink::Entry> MemorySink::entries() const {
    std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

void Logger::add_sink(std::shared_ptr<LogSink> sink) {
    std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::log(LogLevel level, std::string_view module, std::string_view message,
                 const char* file, int line) {
    if (!enabled(level)) return;
    const LogRecord record{level, module, message, platform::current_thread_index(),
                           platform::monotonic_ns(), file, line};
    std::lock_guard lock(mutex_);
    for (const auto& sink : sinks_) sink->write(record);
    if (level >= LogLevel::Error)
        for (const auto& sink : sinks_) sink->flush();
}

void Logger::flush() {
    std::lock_guard lock(mutex_);
    for (const auto& sink : sinks_) sink->flush();
}

}  // namespace aether
