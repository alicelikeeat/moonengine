#include "aether/render/mesh_registry.hpp"
#include "aether/render/model_library.hpp"
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

AETHER_TEST(mesh_instance_texture_round_trips_and_resolves_on_demand) {
    MeshRegistry meshes;
    meshes.add(std::string(kBuiltinCube), 1);
    meshes.add("asset:crate.glb#mesh0.0", 2);
    meshes.add_texture("asset:crate.glb#texture0", 5);
    world::SerializerRegistry reg;
    register_components(reg, meshes);
    world::World a({.seed = 1});
    const world::Entity plain = a.create();
    a.add<MeshInstance>(plain, MeshInstance{1, {1, 1, 1}});
    const world::Entity textured = a.create();
    a.add<MeshInstance>(textured, MeshInstance{2, {1, 1, 1}, 5});
    const Json doc = world::save_level(a, reg);
    const auto& entities = doc.find("entities")->as_array();
    CHECK(entities[0].find("components")->find("MeshInstance")->find("texture") == nullptr);
    CHECK_EQ(entities[1].find("components")->find("MeshInstance")->find("texture")->as_string(),
             std::string("asset:crate.glb#texture0"));

    // A fresh session knows only the cube; the resolver "loads" the asset.
    MeshRegistry other;
    other.add(std::string(kBuiltinCube), 1);
    int resolves = 0;
    other.set_resolver([&](std::string_view name) {
        ++resolves;
        if (name.substr(0, 16) == "asset:crate.glb#") {
            other.add("asset:crate.glb#mesh0.0", 9);
            other.add_texture("asset:crate.glb#texture0", 4);
        }
    });
    world::SerializerRegistry reg2;
    register_components(reg2, other);
    world::World b({.seed = 2});
    auto loaded = world::load_level(doc, b, reg2);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().warnings.empty());
    CHECK_EQ(resolves, 1);
    const MeshInstance* m = b.get<MeshInstance>(b.find(a.id_of(textured)));
    REQUIRE(m != nullptr);
    CHECK_EQ(m->mesh, 9u);
    CHECK_EQ(m->texture, 4u);
    CHECK_EQ(b.get<MeshInstance>(b.find(a.id_of(plain)))->texture, kWhiteTexture);
}

AETHER_TEST(asset_paths_are_normalised_and_kept_inside_the_content_folder) {
    CHECK(normalize_asset_path("models\\crate.glb").value() == std::string("models/crate.glb"));
    CHECK(normalize_asset_path("./a//b/../c.glb").value() == std::string("a/c.glb"));
    CHECK(!normalize_asset_path(""));
    CHECK(!normalize_asset_path("."));
    CHECK(!normalize_asset_path("../x.glb"));
    CHECK(!normalize_asset_path("a/../../x.glb"));
    CHECK(!normalize_asset_path("/etc/x.glb"));
    CHECK(!normalize_asset_path("C:/x.glb"));
    CHECK(!normalize_asset_path("\\\\server\\share\\x.glb"));
}
