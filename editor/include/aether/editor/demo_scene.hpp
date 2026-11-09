#pragma once

#include "aether/render/mesh.hpp"
#include "aether/world/world.hpp"

namespace aether::editor {

// The demo level: a sun, six orbiting planets and their moons, built from
// cubes, named pivots (Empty actors) and Spin components.
void build_demo_scene(world::World& world, render::MeshId cube);

}  // namespace aether::editor
