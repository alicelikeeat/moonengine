#include "aether/world/hierarchy.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace aether::world {
namespace hierarchy {

Entity parent_of(const World& world, Entity e) noexcept {
    const Parent* p = world.get<Parent>(e);
    return p ? p->entity : kNullEntity;
}

std::span<const Entity> children_of(const World& world, Entity e) noexcept {
    const Children* c = world.get<Children>(e);
    return c ? std::span<const Entity>(c->entities) : std::span<const Entity>{};
}

bool is_ancestor(const World& world, Entity ancestor, Entity e) noexcept {
    // The step limit makes this terminate even on a corrupted (cyclic) chain.
    std::size_t steps = world.entity_count();
    for (Entity cur = parent_of(world, e); !cur.is_null() && steps-- > 0; cur = parent_of(world, cur))
        if (cur == ancestor) return true;
    return false;
}

Result<void> set_parent(World& world, Entity child, Entity parent) {
    if (!world.alive(child)) return Error{ErrorCode::NotFound, "child entity is not alive"};
    if (!world.alive(parent)) return Error{ErrorCode::NotFound, "parent entity is not alive"};
    if (child == parent) return Error{ErrorCode::InvalidArgument, "an entity cannot be its own parent"};
    // A childless entity cannot be an ancestor, so attaching a leaf (the
    // common case) skips the O(depth) walk up the parent chain.
    if (world.has<Children>(child) && is_ancestor(world, child, parent))
        return Error{ErrorCode::InvalidArgument, "parent is a descendant of child; the link would create a cycle"};
    if (parent_of(world, child) == parent) return ok();

    clear_parent(world, child);
    world.add<Parent>(child, Parent{parent});
    if (Children* c = world.get<Children>(parent))
        c->entities.push_back(child);
    else
        world.add<Children>(parent, Children{{child}});
    return ok();
}

bool clear_parent(World& world, Entity child) {
    const Parent* p = world.get<Parent>(child);
    if (!p) return false;
    const Entity parent = p->entity;
    if (Children* c = world.get<Children>(parent)) {
        auto& list = c->entities;
        list.erase(std::remove(list.begin(), list.end(), child), list.end());
        if (list.empty()) world.remove<Children>(parent);
    }
    world.remove<Parent>(child);
    return true;
}

std::size_t destroy_recursive(World& world, Entity root) {
    if (!world.alive(root)) return 0;
    std::vector<Entity> subtree{root};
    for (std::size_t i = 0; i < subtree.size() && subtree.size() <= world.entity_count(); ++i)
        for (const Entity c : children_of(world, subtree[i]))
            if (world.alive(c)) subtree.push_back(c);
    clear_parent(world, root);
    std::size_t destroyed = 0;
    for (const Entity e : subtree) destroyed += world.destroy(e) ? 1 : 0;
    return destroyed;
}

std::vector<std::string> validate(World& world) {
    std::vector<std::string> problems;
    const auto name = [&](Entity e) { return world.id_of(e).to_string(); };

    world.each<const Parent>([&](Entity e, const Parent& p) {
        if (!world.alive(p.entity)) {
            problems.push_back("entity " + name(e) + " has a dangling parent");
            return;
        }
        const auto kids = children_of(world, p.entity);
        if (std::count(kids.begin(), kids.end(), e) != 1)
            problems.push_back("parent " + name(p.entity) + " does not list child " + name(e) + " exactly once");
        if (is_ancestor(world, e, e)) problems.push_back("entity " + name(e) + " is part of a parent cycle");
    });
    world.each<const Children>([&](Entity e, const Children& c) {
        std::unordered_set<Entity> seen;
        for (const Entity child : c.entities) {
            if (!seen.insert(child).second)
                problems.push_back("entity " + name(e) + " lists a child twice");
            if (!world.alive(child))
                problems.push_back("entity " + name(e) + " lists a dead child");
            else if (parent_of(world, child) != e)
                problems.push_back("child " + name(child) + " of " + name(e) + " points to a different parent");
        }
        if (c.entities.empty()) problems.push_back("entity " + name(e) + " has an empty Children component");
    });
    return problems;
}

}  // namespace hierarchy

namespace {

// Component ids resolved once per propagate_transforms() call.
struct Ids {
    ComponentId transform, global, children, parent;
};

// Walks one root's subtree with an explicit stack, so depth is unbounded.
void propagate_subtree(World& world, const Ids& ids, Entity root, const math::Mat4& root_matrix) {
    std::vector<std::pair<Entity, math::Mat4>> stack;
    const auto push_children = [&](Entity e, const math::Mat4& m) {
        if (const Children* c = world.get<Children>(e, ids.children))
            for (const Entity child : c->entities) stack.emplace_back(child, m);
    };
    push_children(root, root_matrix);
    // Bound the walk so a corrupted Children list cannot loop forever.
    std::size_t budget = world.entity_count();
    while (!stack.empty() && budget-- > 0) {
        const auto [e, parent_matrix] = stack.back();
        stack.pop_back();
        // get() returns nullptr for dead entities, so no separate alive check.
        const Transform* local = world.get<Transform>(e, ids.transform);
        const math::Mat4 m = local ? parent_matrix * local->matrix() : parent_matrix;
        if (GlobalTransform* g = world.get<GlobalTransform>(e, ids.global)) g->matrix = m;
        if (world.alive(e)) push_children(e, m);
    }
}

}  // namespace

void propagate_transforms(World& world, JobSystem* jobs) {
    std::vector<Entity> missing;
    world.each<const Transform>([&](Entity e, const Transform&) {
        if (!world.has<GlobalTransform>(e)) missing.push_back(e);
    });
    for (const Entity e : missing) world.add<GlobalTransform>(e);

    const Ids ids{world.component_id<Transform>(), world.component_id<GlobalTransform>(),
                  world.component_id<Children>(), world.component_id<Parent>()};

    const auto process_root = [&world, &ids](Entity e, const Transform& t, GlobalTransform& g) {
        const Parent* p = world.get<Parent>(e, ids.parent);
        if (p && world.alive(p->entity)) return;  // not a root
        g.matrix = t.matrix();
        propagate_subtree(world, ids, e, g.matrix);
    };
    // Roots without a Transform of their own (pure grouping nodes) still
    // carry their children; they contribute an identity matrix.
    const auto process_group_root = [&world, &ids](Entity e, const Children&) {
        if (world.get<Transform>(e, ids.transform)) return;  // handled by process_root
        const Parent* p = world.get<Parent>(e, ids.parent);
        if (p && world.alive(p->entity)) return;
        propagate_subtree(world, ids, e, math::Mat4::identity());
    };

    // Subtrees of distinct roots are disjoint, so they may run in parallel.
    if (jobs) {
        world.par_each<const Transform, GlobalTransform>(*jobs, 64, process_root);
        world.par_each<const Children>(*jobs, 64, process_group_root);
    } else {
        world.each<const Transform, GlobalTransform>(process_root);
        world.each<const Children>(process_group_root);
    }
}

}  // namespace aether::world
