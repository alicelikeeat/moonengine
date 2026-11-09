// Component types shared by the World tests.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

struct Position {
    static constexpr std::string_view kComponentName = "Position";
    float x = 0, y = 0, z = 0;
};

struct Velocity {
    static constexpr std::string_view kComponentName = "Velocity";
    float x = 0, y = 0, z = 0;
};

struct Health {
    static constexpr std::string_view kComponentName = "Health";
    int value = 100;
};

struct Name {
    static constexpr std::string_view kComponentName = "Name";
    std::string value;
};

struct Unnamed {
    int v = 0;
};

// Counts live instances; deliberately not assignable so the World must
// never rely on assignment.
struct Tracked {
    static constexpr std::string_view kComponentName = "Tracked";
    static inline std::atomic<int> live{0};
    int value;
    explicit Tracked(int v) : value(v) { ++live; }
    Tracked(Tracked&& o) noexcept : value(o.value) { ++live; }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    Tracked& operator=(Tracked&&) = delete;
    ~Tracked() { --live; }
};

struct Owned {
    static constexpr std::string_view kComponentName = "Owned";
    std::unique_ptr<int> ptr;
};

struct alignas(64) CacheLine {
    static constexpr std::string_view kComponentName = "CacheLine";
    float data[16] = {};
};
