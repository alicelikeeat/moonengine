// Scene hierarchy and transforms.
//
// Parent/child links are stored on both sides (Parent on the child,
// Children on the parent) so tools can walk the tree in either direction.
// Always change links through the hierarchy:: functions, which keep both
// sides consistent and reject cycles; hierarchy::validate() reports any
// inconsistency (for example after a raw World::destroy of a parent).
//
// propagate_transforms() computes GlobalTransform = parent global * local
// for every entity, root to leaf. An entity whose parent is missing or
// dead is treated as a root.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether/foundation/math.hpp"
#include "aether/foundation/result.hpp"
#include "aether/world/world.hpp"

namespace aether::world {

// Local transform relative to the parent (or the world, for roots).
struct Transform {
    static constexpr std::string_view kComponentName = "Transform";
    math::Vec3 position{};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};

    [[nodiscard]] math::Mat4 matrix() const noexcept { return math::Mat4::from_trs(position, rotation, scale); }
};

// World-space matrix written by propagate_transforms(). Read-only for users.
struct GlobalTransform {
    static constexpr std::string_view kComponentName = "GlobalTransform";
    math::Mat4 matrix{};
};

struct Parent {
    static constexpr std::string_view kComponentName = "Parent";
    Entity entity;
};

struct Children {
    static constexpr std::string_view kComponentName = "Children";
    std::vector<Entity> entities;  // in insertion order
};

namespace hierarchy {

// Makes `child` the last child of `parent`, detaching it from any previous
// parent. Fails with not_found for dead entities and invalid_argument when
// the link would create a cycle (including child == parent).
Result<void> set_parent(World& world, Entity child, Entity parent);

// Detaches `child` from its parent. Returns false if it had none.
bool clear_parent(World& world, Entity child);

// kNullEntity if `e` has no parent.
[[nodiscard]] Entity parent_of(const World& world, Entity e) noexcept;

// Empty if `e` has no children. Invalidated by any structural change.
[[nodiscard]] std::span<const Entity> children_of(const World& world, Entity e) noexcept;

// True if `ancestor` is a (transitive) parent of `e`.
[[nodiscard]] bool is_ancestor(const World& world, Entity ancestor, Entity e) noexcept;

// Destroys `root` and all its descendants; returns how many were destroyed.
std::size_t destroy_recursive(World& world, Entity root);

// Human-readable descriptions of every inconsistency; empty when valid.
[[nodiscard]] std::vector<std::string> validate(World& world);

}  // namespace hierarchy

// Adds a GlobalTransform to every entity with a Transform that lacks one
// (structural, so not during iteration), then recomputes all world
// matrices. With `jobs`, independent root subtrees are processed in
// parallel. Entities without a Transform inside a subtree act as identity.
void propagate_transforms(World& world, JobSystem* jobs = nullptr);

}  // namespace aether::world
