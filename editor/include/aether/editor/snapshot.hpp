// Entity snapshots: the editor-visible state of entities, keyed by Uuid.
//
// Used for undo/redo and for Play mode (the level is captured on Play and
// restored on Stop). Restoring recreates entities with their original
// UUIDs, parents and child order, so references by Uuid stay valid even
// though runtime Entity handles change.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "aether/gameplay/spin.hpp"
#include "aether/foundation/uuid.hpp"
#include "aether/render/mesh.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"
#include "aether/world/serialization.hpp"
#include "aether/world/world.hpp"

namespace aether::editor {

using gameplay::Spin;

struct EntityState {
    Uuid id;
    Uuid parent;                 // nil for roots
    std::size_t child_index = 0; // position among the parent's children
    std::optional<world::Name> name;
    std::optional<world::Transform> transform;
    std::optional<render::MeshInstance> mesh;
    std::optional<Spin> spin;
    std::optional<world::UnknownComponents> unknown;  // data from newer versions or plug-ins
};

// State of one entity (its components and parent link, not its children).
[[nodiscard]] EntityState capture(world::World& world, world::Entity e);

// `root` and all descendants, parents before children.
[[nodiscard]] std::vector<EntityState> capture_subtree(world::World& world, world::Entity root);

// Every entity in the world, roots in creation order, parents before children.
[[nodiscard]] std::vector<EntityState> capture_world(world::World& world);

// Recreates entities from states produced by capture_subtree/capture_world.
// Entities whose Uuid already exists are updated instead of recreated.
void restore(world::World& world, const std::vector<EntityState>& states);

// Sets an existing entity's components to `state` (adds/removes components
// as needed). Does not change hierarchy links.
void apply_components(world::World& world, world::Entity e, const EntityState& state);

// Display name: Name component, else "Entity <short uuid>".
[[nodiscard]] std::string display_name(world::World& world, world::Entity e);

}  // namespace aether::editor
