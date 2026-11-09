#pragma once

#include <filesystem>

#include "aether/foundation/result.hpp"
#include "aether/render/mesh.hpp"
#include "aether/render/mesh_registry.hpp"
#include "aether/world/serialization.hpp"
#include "aether/world/world.hpp"

namespace aether::editor {

// The demo level: a sun, six orbiting planets and their moons, built from
// cubes, named pivots (Empty actors) and Spin components.
void build_demo_scene(world::World& world, render::MeshId cube);

// Writes the demo level to `path` with a fixed seed, so the output is
// byte-for-byte reproducible (used for samples/levels/solar_system.alevel).
[[nodiscard]] Result<void> save_demo_level(const std::filesystem::path& path);

// Every component an editor level can contain: core (Name, Transform),
// render (MeshInstance, by mesh name) and gameplay (Spin).
void register_level_components(world::SerializerRegistry& registry, const render::MeshRegistry& meshes);

}  // namespace aether::editor
