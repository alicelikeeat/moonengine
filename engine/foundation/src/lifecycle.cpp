#include "aether/foundation/lifecycle.hpp"

#include <string>

namespace aether {

std::string_view to_string(LifecycleState state) noexcept {
    switch (state) {
        case LifecycleState::Uninitialized: return "uninitialized";
        case LifecycleState::Initializing: return "initializing";
        case LifecycleState::Running: return "running";
        case LifecycleState::Paused: return "paused";
        case LifecycleState::DeviceLost: return "device_lost";
        case LifecycleState::ShuttingDown: return "shutting_down";
        case LifecycleState::Stopped: return "stopped";
    }
    return "unknown";
}

bool Lifecycle::can_transition(LifecycleState from, LifecycleState to) noexcept {
    using S = LifecycleState;
    switch (from) {
        case S::Uninitialized: return to == S::Initializing;
        case S::Initializing: return to == S::Running || to == S::ShuttingDown;
        case S::Running: return to == S::Paused || to == S::DeviceLost || to == S::ShuttingDown;
        case S::Paused: return to == S::Running || to == S::DeviceLost || to == S::ShuttingDown;
        case S::DeviceLost: return to == S::Running || to == S::ShuttingDown;
        case S::ShuttingDown: return to == S::Stopped;
        case S::Stopped: return false;
    }
    return false;
}

Result<void> Lifecycle::transition(LifecycleState to) {
    const LifecycleState from = state_;
    if (!can_transition(from, to)) {
        return Error{ErrorCode::InvalidState,
                     "cannot transition from " + std::string(to_string(from)) + " to " +
                         std::string(to_string(to))};
    }
    state_ = to;
    for (const auto& listener : listeners_) listener(from, to);
    return ok();
}

}  // namespace aether
