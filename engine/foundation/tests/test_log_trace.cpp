#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aether/foundation/log.hpp"
#include "aether/foundation/trace.hpp"
#include "aether_test.hpp"

using namespace aether;

namespace {
// Raw string literals stay outside CHECK(...): MSVC's traditional
// preprocessor mis-tokenizes them inside macro arguments.
bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}
}  // namespace

AETHER_TEST(logger_filters_by_level) {
    Logger log;
    auto mem = std::make_shared<MemorySink>();
    log.add_sink(mem);
    log.set_min_level(LogLevel::Warn);
    AETHER_LOG(log, LogLevel::Info, "test", "hidden {}", 1);
    AETHER_LOG(log, LogLevel::Error, "test", "shown {}", 2);
    const auto entries = mem->entries();
    REQUIRE(entries.size() == 1);
    CHECK_EQ(entries[0].message, std::string("shown 2"));
    CHECK_EQ(entries[0].module, std::string("test"));
    CHECK(entries[0].thread > 0);
}

AETHER_TEST(json_sink_escapes_text) {
    Logger log;
    std::ostringstream out;
    log.add_sink(std::make_shared<JsonLinesSink>(out));
    log.log(LogLevel::Info, "io", "path \"C:\\x\"\nnext", "file.cpp", 7);
    const std::string s = out.str();
    constexpr std::string_view kMessage = R"("message":"path \"C:\\x\"\nnext")";
    constexpr std::string_view kLevel = R"("level":"info")";
    constexpr std::string_view kLine = R"("line":7)";
    CHECK(contains(s, kMessage));
    CHECK(contains(s, kLevel));
    CHECK(contains(s, kLine));
    CHECK(s.back() == '\n');
}

AETHER_TEST(memory_sink_is_bounded) {
    Logger log;
    auto mem = std::make_shared<MemorySink>(3);
    log.add_sink(mem);
    for (int i = 0; i < 10; ++i) AETHER_LOG(log, LogLevel::Info, "m", "{}", i);
    const auto entries = mem->entries();
    REQUIRE(entries.size() == 3);
    CHECK_EQ(entries[0].message, std::string("7"));
    CHECK_EQ(entries[2].message, std::string("9"));
}

AETHER_TEST(logger_is_thread_safe) {
    Logger log;
    auto mem = std::make_shared<MemorySink>(100'000);
    log.add_sink(mem);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&log, t] {
            for (int i = 0; i < 500; ++i) AETHER_LOG(log, LogLevel::Info, "mt", "{}:{}", t, i);
        });
    for (auto& th : threads) th.join();
    CHECK_EQ(mem->entries().size(), std::size_t{2000});
}

AETHER_TEST(trace_exports_chrome_json) {
    TraceCollector trace;
    { ScopedTrace s(trace, "update", "world"); }
    { ScopedTrace s(trace, "render \"main\"", "render"); }
    const auto events = trace.snapshot();
    REQUIRE(events.size() == 2);
    CHECK_EQ(events[0].name, std::string("update"));
    std::ostringstream out;
    trace.write_chrome_json(out);
    const std::string json = out.str();
    CHECK(json.rfind("{\"traceEvents\":[", 0) == 0);
    CHECK(json.find("\"ph\":\"X\"") != std::string::npos);
    constexpr std::string_view kEscapedName = R"(render \"main\")";
    CHECK(contains(json, kEscapedName));
}

AETHER_TEST(trace_respects_capacity_and_enable) {
    TraceCollector trace(2);
    for (int i = 0; i < 5; ++i) { ScopedTrace s(trace, "x"); }
    CHECK_EQ(trace.snapshot().size(), std::size_t{2});
    CHECK_EQ(trace.dropped(), std::size_t{3});
    trace.clear();
    trace.set_enabled(false);
    { ScopedTrace s(trace, "y"); }
    CHECK_EQ(trace.snapshot().size(), std::size_t{0});
}
