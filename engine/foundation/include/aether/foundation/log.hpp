// Structured logging.
//
// A Logger is an explicit object owned by whoever creates the engine
// instance and passed to the systems that need it; there is no hidden
// global logger. Records carry level, module, thread, timestamp and source
// location so failures can be traced to a specific thread and module.
#pragma once

#include <cstdint>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace aether {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string_view module;
    std::string_view message;
    std::uint32_t thread = 0;
    std::uint64_t timestamp_ns = 0;
    const char* file = "";
    int line = 0;
};

class LogSink {
public:
    virtual ~LogSink() = default;
    // Called with the logger's lock held; sinks need no locking of their own.
    virtual void write(const LogRecord& record) = 0;
    virtual void flush() {}
};

// Human readable: "[   12.345678] INFO  render  (t3) message".
class TextSink final : public LogSink {
public:
    explicit TextSink(std::ostream& out) : out_(out) {}
    void write(const LogRecord& record) override;
    void flush() override;

private:
    std::ostream& out_;
};

// One JSON object per line, for tools and AI agents.
class JsonLinesSink final : public LogSink {
public:
    explicit JsonLinesSink(std::ostream& out) : out_(out) {}
    void write(const LogRecord& record) override;
    void flush() override;

private:
    std::ostream& out_;
};

// Keeps the most recent `capacity` records in memory (diagnostics, tests).
class MemorySink final : public LogSink {
public:
    struct Entry {
        LogLevel level;
        std::string module;
        std::string message;
        std::uint32_t thread;
        std::uint64_t timestamp_ns;
    };

    explicit MemorySink(std::size_t capacity = 1024) : capacity_(capacity) {}
    void write(const LogRecord& record) override;
    // Snapshot; safe to call while other threads log through the Logger.
    [[nodiscard]] std::vector<Entry> entries() const;

private:
    mutable std::mutex mutex_;
    std::deque<Entry> entries_;
    std::size_t capacity_;
};

class Logger {
public:
    void add_sink(std::shared_ptr<LogSink> sink);
    void set_min_level(LogLevel level) noexcept { min_level_ = level; }
    [[nodiscard]] LogLevel min_level() const noexcept { return min_level_; }
    [[nodiscard]] bool enabled(LogLevel level) const noexcept { return level >= min_level_; }

    void log(LogLevel level, std::string_view module, std::string_view message,
             const char* file = "", int line = 0);
    void flush();

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<LogSink>> sinks_;
    LogLevel min_level_ = LogLevel::Info;
};

}  // namespace aether

// AETHER_LOG(logger, aether::LogLevel::Info, "world", "loaded {} entities", n);
// Formatting only happens when the level is enabled.
#define AETHER_LOG(logger, level, module, ...)                                     \
    do {                                                                           \
        if ((logger).enabled(level))                                               \
            (logger).log(level, module, std::format(__VA_ARGS__), __FILE__, __LINE__); \
    } while (false)
