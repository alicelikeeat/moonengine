#include <cstdio>
#include "aether/render/mesh_registry.hpp"
#include "aether/render/light.hpp"
#include "aether/render/model_library.hpp"
#include "aether/render/sky.hpp"
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

AETHER_TEST(material_fields_and_lights_round_trip) {
    MeshRegistry meshes;
    meshes.add(std::string(kBuiltinCube), 1);
    meshes.add_texture("asset:a.glb#texture0.linear", 6);
    world::SerializerRegistry reg;
    register_components(reg, meshes);
    world::World a({.seed = 1});
    const world::Entity plain = a.create();
    a.add<MeshInstance>(plain, MeshInstance{1, {1, 1, 1}});
    const world::Entity fancy = a.create();
    MeshInstance m{1, {0.5f, 0.5f, 0.5f}};
    m.metallic = 1.0f;
    m.roughness = 0.25f;
    m.emissive = {2, 1, 0};
    m.normal_texture = 6;
    m.metallic_roughness_texture = 6;
    a.add<MeshInstance>(fancy, m);
    const world::Entity sun = a.create();
    Light directional;
    directional.type = LightType::Directional;
    directional.intensity = 2.5f;
    directional.cast_shadows = false;
    a.add<Light>(sun, directional);
    const world::Entity bulb = a.create();
    Light point;
    point.color = {1, 0.5f, 0.25f};
    point.range = 7;
    a.add<Light>(bulb, point);

    const Json doc = world::save_level(a, reg);
    const auto& entities = doc.find("entities")->as_array();
    const Json* plain_json = entities[0].find("components")->find("MeshInstance");
    // Defaults are not written.
    for (const char* key : {"metallic", "roughness", "emissive", "normal_texture", "metallic_roughness_texture"})
        CHECK(plain_json->find(key) == nullptr);

    world::World b({.seed = 2});
    auto loaded = world::load_level(doc, b, reg);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().warnings.empty());
    const MeshInstance* got = b.get<MeshInstance>(b.find(a.id_of(fancy)));
    REQUIRE(got != nullptr);
    CHECK(got->metallic == 1.0f && got->roughness == 0.25f);
    CHECK(got->emissive == (math::Vec3{2, 1, 0}));
    CHECK_EQ(got->normal_texture, 6u);
    CHECK_EQ(got->metallic_roughness_texture, 6u);
    const Light* s = b.get<Light>(b.find(a.id_of(sun)));
    REQUIRE(s != nullptr);
    CHECK(s->type == LightType::Directional && s->intensity == 2.5f && !s->cast_shadows);
    const Light* p = b.get<Light>(b.find(a.id_of(bulb)));
    REQUIRE(p != nullptr);
    CHECK(p->type == LightType::Point && p->range == 7.0f && p->color.y == 0.5f);
}

AETHER_TEST(invalid_material_and_light_data_is_kept_with_a_warning) {
    MeshRegistry meshes;
    meshes.add(std::string(kBuiltinCube), 1);
    world::SerializerRegistry reg;
    register_components(reg, meshes);
    for (const char* bad : {R"({"MeshInstance": {"mesh": "builtin:cube", "roughness": 2}})",
                            R"({"MeshInstance": {"mesh": "builtin:cube", "emissive": [-1, 0, 0]}})",
                            R"({"MeshInstance": {"mesh": "builtin:cube", "normal_texture": "asset:x.glb#texture0"}})",
                            R"({"Light": {"type": "spot"}})", R"({"Light": {"type": "point", "range": 0}})",
                            R"({"Light": {"type": "directional", "shadows": "yes"}})"}) {
        auto doc = parse_json(std::string(R"({"format": "aether.level", "version": 1, "entities": [
            {"id": "00000000-0000-4000-8000-000000000001", "components": )") + bad + "}]}");
        REQUIRE(doc.has_value());
        world::World w({.seed = 1});
        auto r = world::load_level(doc.value(), w, reg);
        REQUIRE(r.has_value());
        CHECK_EQ(r.value().warnings.size(), std::size_t{1});
    }
}

AETHER_TEST(rotation_toward_aims_local_minus_z) {
    for (const math::Vec3 d : {math::Vec3{1, -1, 0}, math::Vec3{0, 0, -1}, math::Vec3{0, 0, 1}, math::Vec3{0, -1, 0}}) {
        const math::Vec3 got = math::rotate(rotation_toward(d), math::Vec3{0, 0, -1});
        CHECK(math::near(got, math::normalize(d)));
    }
}

AETHER_TEST(environment_round_trips_and_rejects_bad_values) {
    MeshRegistry meshes;
    world::SerializerRegistry reg;
    register_components(reg, meshes);
    world::World a({.seed = 1});
    const world::Entity e = a.create();
    Environment env;
    env.sky_color = {0.1f, 0.2f, 0.3f};
    env.horizon_color = {0.4f, 0.5f, 0.6f};
    env.ground_color = {0.05f, 0.04f, 0.03f};
    env.ambient_intensity = 1.5f;
    env.exposure = 2.0f;
    env.shadows = false;
    env.sky_visible = true;
    a.add<Environment>(e, env);
    const Json doc = world::save_level(a, reg);
    world::World b({.seed = 2});
    REQUIRE(world::load_level(doc, b, reg).has_value());
    const Environment* got = b.get<Environment>(b.find(a.id_of(e)));
    REQUIRE(got != nullptr);
    CHECK(*got == env);

    for (const char* bad : {R"({"Environment": {"exposure": 0}})", R"({"Environment": {"sky": [-1, 0, 0]}})",
                            R"({"Environment": {"sky_visible": 1}})"}) {
        auto parsed = parse_json(std::string(R"({"format": "aether.level", "version": 1, "entities": [
            {"id": "00000000-0000-4000-8000-000000000001", "components": )") + bad + "}]}");
        REQUIRE(parsed.has_value());
        world::World w({.seed = 1});
        auto r = world::load_level(parsed.value(), w, reg);
        REQUIRE(r.has_value());
        CHECK_EQ(r.value().warnings.size(), std::size_t{1});
    }
}

AETHER_TEST(half_floats_round_trip) {
    for (const float v : {0.0f, 1.0f, -2.5f, 0.1f, 1000.0f, 65504.0f, 6.1e-5f, 1e-7f}) {
        const float back = half_to_float(float_to_half(v));
        CHECK(std::abs(back - v) <= std::abs(v) * 0.001f + 1e-7f);
    }
    CHECK_EQ(float_to_half(1.0f), std::uint16_t{0x3C00});
    CHECK_EQ(float_to_half(-2.0f), std::uint16_t{0xC000});
    CHECK_EQ(float_to_half(1e9f), std::uint16_t{0x7C00});  // overflow -> infinity
    CHECK(half_to_float(0x0001) > 0.0f);                   // smallest subnormal
}

AETHER_TEST(sky_bake_of_a_uniform_sky_is_uniform) {
    Environment env;
    env.sky_color = env.horizon_color = env.ground_color = {0.5f, 0.25f, 1.0f};
    env.ambient_intensity = 2.0f;
    const SkyBake bake = bake_sky(env, 8, 4);
    CHECK_EQ(bake.cube.size(), std::size_t{6 * (64 + 16 + 4 + 1) * 4});
    for (std::size_t i = 0; i < bake.cube.size(); i += 4) {
        CHECK(std::abs(half_to_float(bake.cube[i]) - 1.0f) < 0.01f);
        CHECK(std::abs(half_to_float(bake.cube[i + 1]) - 0.5f) < 0.01f);
        CHECK(std::abs(half_to_float(bake.cube[i + 2]) - 2.0f) < 0.01f);
    }
    // Irradiance / pi of a uniform sky is the sky itself: only the constant term.
    CHECK(std::abs(bake.irradiance[0].x - 1.0f) < 0.01f && std::abs(bake.irradiance[0].z - 2.0f) < 0.02f);
    for (std::size_t k = 1; k < 9; ++k) CHECK(math::length(bake.irradiance[k]) < 0.01f);
}

AETHER_TEST(sky_bake_is_brighter_above_and_blurs_with_roughness) {
    Environment env;  // blue above, dark ground
    const SkyBake bake = bake_sky(env, 16, 5);
    // Irradiance: the y coefficient is positive (more light from above).
    CHECK(bake.irradiance[1].z > 0.05f);
    // Mip 0: +Y face is the zenith colour, -Y the ground colour.
    const std::size_t face = std::size_t{16} * 16 * 4;
    const std::size_t centre = (std::size_t{8} * 16 + 8) * 4;
    CHECK(std::abs(half_to_float(bake.cube[2 * face + centre + 2]) - env.sky_color.z) < 0.03f);
    CHECK(std::abs(half_to_float(bake.cube[3 * face + centre]) - env.ground_color.x) < 0.01f);
    // Last mip (roughness 1, one texel per face): the sideways face blends
    // sky and ground, so it is darker than the sharp horizon in mip 0.
    const std::size_t last = bake.cube.size() - 6 * 4;
    const float blurred = half_to_float(bake.cube[last + 2]);  // +X, blue channel
    CHECK(blurred > env.ground_color.z + 0.05f);
    const float sharp = half_to_float(bake.cube[(std::size_t{7} * 16 + 8) * 4 + 2]);  // +X, just above the horizon
    std::printf("    +X blue: sharp %.3f, fully rough %.3f\n", static_cast<double>(sharp), static_cast<double>(blurred));
    CHECK(blurred < sharp - 0.05f);
    // Cube face directions follow the Vulkan convention.
    CHECK(cube_direction(0, 0, 0) == (math::Vec3{1, 0, 0}));
    CHECK(cube_direction(2, 0, 0) == (math::Vec3{0, 1, 0}));
    CHECK(cube_direction(4, 1, 1) == (math::Vec3{1, -1, 1}));
}
