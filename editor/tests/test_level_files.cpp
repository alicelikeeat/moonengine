#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "aether/editor/demo_scene.hpp"
#include "aether/editor/snapshot.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::editor;

namespace {
std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
const std::filesystem::path kSample = std::filesystem::path(AETHER_SOURCE_DIR) / "samples/levels/solar_system.alevel";
}  // namespace

AETHER_TEST(committed_demo_level_matches_a_fresh_export) {
    const auto tmp = std::filesystem::temp_directory_path() / "aether_demo_export.alevel";
    REQUIRE(save_demo_level(tmp).has_value());
    const std::string fresh = read_file(tmp);
    std::filesystem::remove(tmp);
    // If this fails, regenerate: aether_editor --export-demo samples/levels/solar_system.alevel
    CHECK(!fresh.empty());
    CHECK(fresh == read_file(kSample));
}

AETHER_TEST(committed_demo_level_loads_cleanly) {
    render::MeshRegistry meshes;
    meshes.add(std::string(render::kBuiltinCube), 0);
    world::SerializerRegistry reg;
    register_level_components(reg, meshes);
    world::World w({.seed = 1});
    auto report = world::load_level_file(kSample, w, reg);
    REQUIRE(report.has_value());
    CHECK(report.value().warnings.empty());
    // sun, sun glow, system, floor, sun light, 6 planets with orbits, 12 moons with orbits
    CHECK_EQ(w.entity_count(), std::size_t{41});
    CHECK(world::hierarchy::validate(w).empty());
}

AETHER_TEST(save_and_reload_preserves_editor_state) {
    render::MeshRegistry meshes;
    meshes.add(std::string(render::kBuiltinCube), 0);
    world::SerializerRegistry reg;
    register_level_components(reg, meshes);
    world::World a({.seed = 5});
    build_demo_scene(a, 0);
    const auto before = capture_world(a);

    const auto path = std::filesystem::temp_directory_path() / "aether_roundtrip.alevel";
    REQUIRE(world::save_level_file(path, a, reg).has_value());
    world::World b({.seed = 6});
    REQUIRE(world::load_level_file(path, b, reg).has_value());
    std::filesystem::remove(path);
    const auto after = capture_world(b);

    REQUIRE(after.size() == before.size());
    int mismatches = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const EntityState& x = before[i];
        const EntityState& y = after[i];
        const bool same = x.id == y.id && x.parent == y.parent && x.child_index == y.child_index &&
                          x.name.has_value() == y.name.has_value() && (!x.name || x.name->value == y.name->value) &&
                          x.transform.has_value() == y.transform.has_value() &&
                          (!x.transform || (x.transform->position == y.transform->position &&
                                            x.transform->scale == y.transform->scale)) &&
                          x.mesh.has_value() == y.mesh.has_value() && (!x.mesh || x.mesh->color == y.mesh->color) &&
                          x.spin.has_value() == y.spin.has_value() && (!x.spin || x.spin->speed == y.spin->speed);
        if (!same) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
}

AETHER_TEST(schema_example_loads_in_the_engine) {
    // The documented example must be accepted by the engine as well as the schema.
    render::MeshRegistry meshes;
    meshes.add(std::string(render::kBuiltinCube), 0);
    world::SerializerRegistry reg;
    register_level_components(reg, meshes);
    world::World w({.seed = 1});
    auto report = world::load_level_file(
        std::filesystem::path(AETHER_SOURCE_DIR) / "protocols/level/examples/level/minimal.json", w, reg);
    REQUIRE(report.has_value());
    CHECK_EQ(report.value().entities, std::size_t{2});
    REQUIRE(report.value().warnings.size() == 1);  // the plug-in component is kept, not dropped
    CHECK(report.value().warnings[0].find("FuturePluginThing") != std::string::npos);
}
