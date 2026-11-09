// Time utilities: a monotonic stopwatch and a fixed-step simulation clock.
#pragma once

#include <cstdint>

namespace aether {

class Stopwatch {
public:
    Stopwatch() noexcept;
    void restart() noexcept;
    [[nodiscard]] std::uint64_t elapsed_ns() const noexcept;
    [[nodiscard]] double elapsed_seconds() const noexcept;

private:
    std::uint64_t start_ns_;
};

struct FixedStepResult {
    // Number of fixed simulation steps to run this frame.
    std::uint32_t steps = 0;
    // Interpolation factor in [0, 1) between the previous and current
    // simulation state, for rendering.
    double alpha = 0.0;
    // True when frame time exceeded max_steps_per_frame and was dropped.
    bool clamped = false;
};

// Accumulates variable frame time and converts it into whole fixed steps.
// Time is counted in integer nanoseconds so that identical input sequences
// always produce identical step sequences (needed for deterministic replay).
class FixedTimestep {
public:
    explicit FixedTimestep(std::uint64_t step_ns = 16'666'667, std::uint32_t max_steps_per_frame = 8) noexcept;

    FixedStepResult advance(std::uint64_t frame_ns) noexcept;
    void reset() noexcept { accumulator_ns_ = 0; total_steps_ = 0; }

    [[nodiscard]] std::uint64_t step_ns() const noexcept { return step_ns_; }
    [[nodiscard]] double step_seconds() const noexcept { return static_cast<double>(step_ns_) * 1e-9; }
    [[nodiscard]] std::uint64_t total_steps() const noexcept { return total_steps_; }

private:
    std::uint64_t step_ns_;
    std::uint32_t max_steps_;
    std::uint64_t accumulator_ns_ = 0;
    std::uint64_t total_steps_ = 0;
};

}  // namespace aether
