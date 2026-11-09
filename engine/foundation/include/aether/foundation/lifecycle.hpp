// Engine lifecycle state machine: start-up, pause, device loss/recovery and
// shut-down. Invalid transitions are rejected with an error rather than
// silently ignored, so callers (including AI agents over ACP) get a clear
// reason when an operation is not allowed in the current state.
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "aether/foundation/result.hpp"

namespace aether {

enum class LifecycleState : std::uint8_t {
    Uninitialized,
    Initializing,
    Running,
    Paused,
    DeviceLost,
    ShuttingDown,
    Stopped,
};

[[nodiscard]] std::string_view to_string(LifecycleState state) noexcept;

class Lifecycle {
public:
    using Listener = std::function<void(LifecycleState from, LifecycleState to)>;

    [[nodiscard]] LifecycleState state() const noexcept { return state_; }
    [[nodiscard]] static bool can_transition(LifecycleState from, LifecycleState to) noexcept;

    Result<void> transition(LifecycleState to);
    void on_transition(Listener listener) { listeners_.push_back(std::move(listener)); }

    // Convenience wrappers for the common paths.
    Result<void> begin_init() { return transition(LifecycleState::Initializing); }
    Result<void> finish_init() { return transition(LifecycleState::Running); }
    Result<void> pause() { return transition(LifecycleState::Paused); }
    Result<void> resume() { return transition(LifecycleState::Running); }
    Result<void> device_lost() { return transition(LifecycleState::DeviceLost); }
    Result<void> device_recovered() { return transition(LifecycleState::Running); }
    Result<void> begin_shutdown() { return transition(LifecycleState::ShuttingDown); }
    Result<void> finish_shutdown() { return transition(LifecycleState::Stopped); }

private:
    LifecycleState state_ = LifecycleState::Uninitialized;
    std::vector<Listener> listeners_;
};

}  // namespace aether
