#include "aether/editor/snapshot.hpp"

#include <algorithm>

namespace aether::editor {

namespace h = world::hierarchy;

EntityState capture(world::World& w, world::Entity e) {
    EntityState s;
    s.id = w.id_of(e);
    const world::Entity parent = h::parent_of(w, e);
    if (!parent.is_null() && w.alive(parent)) {
        s.parent = w.id_of(parent);
        const auto kids = h::children_of(w, parent);
        s.child_index = static_cast<std::size_t>(std::find(kids.begin(), kids.end(), e) - kids.begin());
    }
    if (const auto* c = w.get<world::Name>(e)) s.name = *c;
    if (const auto* c = w.get<world::Transform>(e)) s.transform = *c;
    if (const auto* c = w.get<render::MeshInstance>(e)) s.mesh = *c;
    if (const auto* c = w.get<Spin>(e)) s.spin = *c;
    if (const auto* c = w.get<render::Light>(e)) s.light = *c;
    if (const auto* c = w.get<render::Environment>(e)) s.environment = *c;
    if (const auto* c = w.get<simulation::RigidBody>(e)) s.rigid_body = *c;
    if (const auto* c = w.get<simulation::Collider>(e)) s.collider = *c;
    if (const auto* c = w.get<world::UnknownComponents>(e)) s.unknown = *c;
    return s;
}

std::vector<EntityState> capture_subtree(world::World& w, world::Entity root) {
    std::vector<EntityState> out;
    if (!w.alive(root)) return out;
    std::vector<world::Entity> stack{root};
    while (!stack.empty()) {
        const world::Entity e = stack.back();
        stack.pop_back();
        out.push_back(capture(w, e));
        const auto kids = h::children_of(w, e);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);  // keep order
    }
    return out;
}

std::vector<EntityState> capture_world(world::World& w) {
    std::vector<world::Entity> roots;
    // Level entities always carry at least one of these components; a bare
    // entity with no components is not part of the level.
    std::vector<world::Entity> all;
    const auto collect = [&](world::Entity e) {
        if (std::find(all.begin(), all.end(), e) == all.end()) all.push_back(e);
    };
    w.each<const world::Transform>([&](world::Entity e, const world::Transform&) { collect(e); });
    w.each<const world::Name>([&](world::Entity e, const world::Name&) { collect(e); });
    w.each<const world::Children>([&](world::Entity e, const world::Children&) { collect(e); });
    w.each<const world::Parent>([&](world::Entity e, const world::Parent&) { collect(e); });
    w.each<const render::Light>([&](world::Entity e, const render::Light&) { collect(e); });
    w.each<const render::Environment>([&](world::Entity e, const render::Environment&) { collect(e); });
    w.each<const simulation::Collider>([&](world::Entity e, const simulation::Collider&) { collect(e); });
    for (const world::Entity e : all) {
        const world::Entity p = h::parent_of(w, e);
        if (p.is_null() || !w.alive(p)) roots.push_back(e);
    }
    // Stable order: by Uuid, so captures of equal worlds compare equal.
    std::sort(roots.begin(), roots.end(), [&](world::Entity a, world::Entity b) { return w.id_of(a) < w.id_of(b); });
    std::vector<EntityState> out;
    for (const world::Entity r : roots) {
        auto part = capture_subtree(w, r);
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

void apply_components(world::World& w, world::Entity e, const EntityState& s) {
    const auto sync = [&](auto& opt, auto tag) {
        using T = typename decltype(tag)::type;
        if (opt) w.add<T>(e, *opt);
        else w.remove<T>(e);
    };
    struct NameTag { using type = world::Name; };
    struct TransformTag { using type = world::Transform; };
    struct MeshTag { using type = render::MeshInstance; };
    struct SpinTag { using type = Spin; };
    struct UnknownTag { using type = world::UnknownComponents; };
    struct LightTag { using type = render::Light; };
    struct EnvironmentTag { using type = render::Environment; };
    struct RigidBodyTag { using type = simulation::RigidBody; };
    struct ColliderTag { using type = simulation::Collider; };
    sync(s.light, LightTag{});
    sync(s.environment, EnvironmentTag{});
    sync(s.rigid_body, RigidBodyTag{});
    sync(s.collider, ColliderTag{});
    sync(s.name, NameTag{});
    sync(s.transform, TransformTag{});
    sync(s.mesh, MeshTag{});
    sync(s.spin, SpinTag{});
    sync(s.unknown, UnknownTag{});
}

void restore(world::World& w, const std::vector<EntityState>& states) {
    for (const EntityState& s : states) {
        world::Entity e = w.find(s.id);
        if (e.is_null()) {
            auto created = w.create_with_id(s.id);
            if (!created) continue;
            e = created.value();
        }
        apply_components(w, e, s);
    }
    // Links after every entity exists, in order, so child indices are valid.
    for (const EntityState& s : states) {
        const world::Entity e = w.find(s.id);
        if (e.is_null()) continue;
        if (s.parent.is_nil()) {
            h::clear_parent(w, e);
            continue;
        }
        const world::Entity parent = w.find(s.parent);
        if (parent.is_null() || !h::set_parent(w, e, parent)) continue;
        if (auto* kids = w.get<world::Children>(parent)) {
            auto& v = kids->entities;
            const auto it = std::find(v.begin(), v.end(), e);
            if (it != v.end()) {
                v.erase(it);
                v.insert(v.begin() + static_cast<std::ptrdiff_t>(std::min(s.child_index, v.size())), e);
            }
        }
    }
}

std::string display_name(world::World& w, world::Entity e) {
    if (const auto* n = w.get<world::Name>(e); n && !n->value.empty()) return n->value;
    return "Entity " + w.id_of(e).to_string().substr(0, 8);
}

}  // namespace aether::editor
