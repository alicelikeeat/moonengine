// Gameplay components the editor knows how to inspect, save in snapshots
// and simulate in Play mode.
#pragma once

#include <string_view>

#include "aether/foundation/math.hpp"

namespace aether::editor {

// Rotates the entity's local transform around `axis` at `speed` rad/s while
// the simulation is playing (like Unreal's RotatingMovement component).
struct Spin {
    static constexpr std::string_view kComponentName = "Spin";
    math::Vec3 axis{0, 1, 0};
    float speed = 1.0f;
};

}  // namespace aether::editor
