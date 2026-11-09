#include "aether/render/mesh_registry.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::render;

AETHER_TEST(mesh_instance_round_trips_by_name) {
    MeshRegistry meshes;
    meshes.add(std::string(kBuiltinCube), 7);
    world::SerializerRegistry reg;
    register_components(reg, meshes);
    world::World a({.seed = 1});
    const world::Entity e = a.create();
    a.add<MeshInstance>(e, MeshInstance{7, {0.1f, 0.2f, 0.3f}});
    const Json doc = world::save_level(a, reg);
    const Json& mesh = *doc.find("entities")->as_array()[0].find("components")->find("MeshInstance");
    CHECK_EQ(mesh.find("mesh")->as_string(), std::string("builtin:cube"));

    // A different session may assign a different runtime id to the same name.
    MeshRegistry other;
    other.add(std::string(kBuiltinCube), 3);
    world::SerializerRegistry reg2;
    register_components(reg2, other);
    world::World b({.seed = 2});
    REQUIRE(world::load_level(doc, b, reg2).has_value());
    const MeshInstance* m = b.get<MeshInstance>(b.find(a.id_of(e)));
    REQUIRE(m != nullptr);
    CHECK_EQ(m->mesh, 3u);
    CHECK(m->color.z == 0.3f);
}

AETHER_TEST(unknown_mesh_names_are_kept_not_dropped) {
    MeshRegistry meshes;
    world::SerializerRegistry reg;
    register_components(reg, meshes);
    auto doc = parse_json(R"({"format": "aether.level", "version": 1, "entities": [
        {"id": "00000000-0000-4000-8000-000000000001",
         "components": {"MeshInstance": {"mesh": "assets/statue.glb", "color": [1, 1, 1]}}}]})");
    world::World w({.seed = 1});
    auto r = world::load_level(doc.value(), w, reg);
    REQUIRE(r.has_value());
    CHECK_EQ(r.value().warnings.size(), std::size_t{1});
    const Json saved = world::save_level(w, reg);
    const Json* kept = saved.find("entities")->as_array()[0].find("components")->find("MeshInstance");
    REQUIRE(kept != nullptr);
    CHECK_EQ(kept->find("mesh")->as_string(), std::string("assets/statue.glb"));
}
