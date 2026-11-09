// Spin: rotates an entity's local transform at a constant rate.
//
// The first gameplay component. It lives here, not in the editor or a
// sample, so every application (editor, runtime, tools) shares the same
// type, system and level-file representation.
#pragma once

#include <string_view>

#include "aether/foundation/math.hpp"
#include "aether/world/schedule.hpp"
#include "aether/world/serialization.hpp"

namespace aether::gameplay {

struct Spin {
    static constexpr std::string_view kComponentName = "Spin";
    math::Vec3 axis{0, 1, 0};
    float speed = 1.0f;  // radians per second
};

// Level file representation: {"axis": [x, y, z], "speed": radians_per_second}.
void register_components(world::SerializerRegistry& registry);

// Adds the system that applies Spin to Transform each fixed step.
void add_systems(world::Schedule& schedule, world::World& world);

}  // namespace aether::gameplay
