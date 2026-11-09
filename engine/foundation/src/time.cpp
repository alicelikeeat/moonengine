#include "aether/foundation/time.hpp"

#include "aether/foundation/assert.hpp"
#include "aether/foundation/platform.hpp"

namespace aether {

Stopwatch::Stopwatch() noexcept : start_ns_(platform::monotonic_ns()) {}
void Stopwatch::restart() noexcept { start_ns_ = platform::monotonic_ns(); }
std::uint64_t Stopwatch::elapsed_ns() const noexcept { return platform::monotonic_ns() - start_ns_; }
double Stopwatch::elapsed_seconds() const noexcept { return static_cast<double>(elapsed_ns()) * 1e-9; }

FixedTimestep::FixedTimestep(std::uint64_t step_ns, std::uint32_t max_steps_per_frame) noexcept
    : step_ns_(step_ns), max_steps_(max_steps_per_frame) {
    AETHER_ASSERT(step_ns > 0, "fixed step must be positive");
    AETHER_ASSERT(max_steps_per_frame > 0, "max steps per frame must be positive");
}

FixedStepResult FixedTimestep::advance(std::uint64_t frame_ns) noexcept {
    FixedStepResult result;
    accumulator_ns_ += frame_ns;
    std::uint64_t steps = accumulator_ns_ / step_ns_;
    if (steps > max_steps_) {
        // Drop the excess instead of spiralling: keep only the remainder.
        steps = max_steps_;
        accumulator_ns_ %= step_ns_;
        result.clamped = true;
    } else {
        accumulator_ns_ -= steps * step_ns_;
    }
    result.steps = static_cast<std::uint32_t>(steps);
    result.alpha = static_cast<double>(accumulator_ns_) / static_cast<double>(step_ns_);
    total_steps_ += steps;
    return result;
}

}  // namespace aether
