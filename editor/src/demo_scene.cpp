#include "aether/editor/demo_scene.hpp"

#include <string>

#include "aether/gameplay/spin.hpp"
#include "aether/render/mesh_registry.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"

namespace aether::editor {

using gameplay::Spin;
using math::Quat;
using math::Vec3;

namespace {
world::Entity make(world::World& w, const std::string& name, Vec3 pos, float scale, Quat rot = Quat::identity()) {
    const world::Entity e = w.create();
    w.add<world::Name>(e, world::Name{name});
    w.add<world::Transform>(e, world::Transform{pos, rot, {scale, scale, scale}});
    return e;
}
}  // namespace

void build_demo_scene(world::World& w, render::MeshId cube) {
    const char* planets[] = {"Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn"};
    const Vec3 palette[] = {{0.3f, 0.6f, 1.0f}, {0.4f, 0.9f, 0.5f}, {0.9f, 0.35f, 0.4f},
                            {0.7f, 0.5f, 1.0f}, {0.2f, 0.85f, 0.85f}, {0.95f, 0.6f, 0.3f}};

    const world::Entity system = make(w, "Solar System", {0, 1.5f, 0}, 1.0f);
    const world::Entity sun = make(w, "Sun", {0, 0, 0}, 2.2f);
    w.add<render::MeshInstance>(sun, render::MeshInstance{cube, {1.0f, 0.72f, 0.2f}});
    w.add<Spin>(sun, Spin{{0, 1, 0}, 0.25f});
    (void)world::hierarchy::set_parent(w, sun, system);

    for (int i = 0; i < 6; ++i) {
        const float fi = static_cast<float>(i);
        const world::Entity orbit = make(w, std::string(planets[i]) + " Orbit", {0, 0, 0}, 1.0f,
                                         Quat::from_axis_angle({0, 1, 0}, fi));
        w.add<Spin>(orbit, Spin{{0, 1, 0}, 0.9f / (1.0f + 0.5f * fi)});
        (void)world::hierarchy::set_parent(w, orbit, system);

        const world::Entity planet = make(w, planets[i], {3.5f + 1.7f * fi, 0, 0}, 0.9f);
        w.add<render::MeshInstance>(planet, render::MeshInstance{cube, palette[i]});
        w.add<Spin>(planet, Spin{{0.3f, 1, 0.2f}, 1.5f});
        (void)world::hierarchy::set_parent(w, planet, orbit);

        for (int m = 0; m < i % 3 + 1; ++m) {
            const float fm = static_cast<float>(m);
            const std::string moon_name = std::string(planets[i]) + " Moon " + std::to_string(m + 1);
            const world::Entity moon_orbit = make(w, moon_name + " Orbit", {0, 0, 0}, 1.0f,
                                                  Quat::from_axis_angle({1, 0, 0}, 0.6f * fm));
            w.add<Spin>(moon_orbit, Spin{{0, 1, 0}, 2.5f + fm});
            (void)world::hierarchy::set_parent(w, moon_orbit, planet);
            const world::Entity moon = make(w, moon_name, {1.3f + 0.7f * fm, 0, 0}, 0.35f);
            w.add<render::MeshInstance>(moon, render::MeshInstance{cube, {0.85f, 0.85f, 0.9f}});
            w.add<Spin>(moon, Spin{{1, 1, 0}, 3.0f});
            (void)world::hierarchy::set_parent(w, moon, moon_orbit);
        }
    }

    const world::Entity floor = make(w, "Floor", {0, -0.07f, 0}, 1.0f);  // top just below the grid
    w.get<world::Transform>(floor)->scale = {50, 0.1f, 50};  // matches the 50 m editor grid
    w.add<render::MeshInstance>(floor, render::MeshInstance{cube, {0.09f, 0.095f, 0.105f}});
    world::propagate_transforms(w);
}

Result<void> save_demo_level(const std::filesystem::path& path) {
    world::World w({.seed = 20261109});  // fixed seed: the same file every time
    render::MeshRegistry meshes;
    meshes.add(std::string(render::kBuiltinCube), 0);
    build_demo_scene(w, 0);
    world::SerializerRegistry registry;
    register_level_components(registry, meshes);
    return world::save_level_file(path, w, registry, "Aether Editor demo export");
}

void register_level_components(world::SerializerRegistry& registry, const render::MeshRegistry& meshes) {
    world::register_core_components(registry);
    render::register_components(registry, meshes);
    gameplay::register_components(registry);
}

}  // namespace aether::editor
