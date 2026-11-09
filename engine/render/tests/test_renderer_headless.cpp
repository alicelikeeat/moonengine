// Renders offscreen on whatever Vulkan device is available (lavapipe in
// CI) and checks real pixels. Skips when no Vulkan 1.3 device exists.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "aether/render/model_library.hpp"
#include "aether/render/renderer.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"
#include "aether/world/serialization.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::math;
using namespace aether::render;
using namespace aether::world;

#if defined(__linux__)
#include <dlfcn.h>
namespace {
// Keep Mesa's software Vulkan driver loaded for the whole process. The
// Vulkan loader unloads drivers at vkDestroyInstance; LeakSanitizer then
// attributes the driver's own leftover allocations to whatever library
// reuses those addresses, which defeats the driver-only suppressions in
// tools/sanitizers. Pinning it keeps leak reports accurate. No effect when
// the driver is not installed.
const bool kDriverPinned = dlopen("libvulkan_lvp.so", RTLD_LAZY | RTLD_NODELETE) != nullptr;
}  // namespace
#endif

namespace {

constexpr Vec3 kClear{0.1f, 0.2f, 0.3f};

struct Rig {
    Logger logger;
    std::shared_ptr<MemorySink> log = std::make_shared<MemorySink>();
    std::unique_ptr<Renderer> renderer;
    MeshId cube = 0;
};

std::unique_ptr<Rig> make_rig(std::uint32_t w = 64, std::uint32_t h = 64) {
    auto rig = std::make_unique<Rig>();
    rig->logger.add_sink(rig->log);
    rig->logger.set_min_level(LogLevel::Debug);
    RendererDesc desc;
    desc.width = w;
    desc.height = h;
    desc.validation = true;  // used when the layer is installed
    desc.clear_color = kClear;
    desc.logger = &rig->logger;
    auto r = Renderer::create(desc);
    if (!r) {
        if (r.error().code == ErrorCode::Unsupported) AETHER_SKIP("no Vulkan 1.3 device: " + r.error().message);
        std::fprintf(stderr, "    renderer creation failed: %s\n", r.error().message.c_str());
        REQUIRE(r.has_value());
    }
    rig->renderer = std::move(r).value();
    auto mesh = rig->renderer->upload_mesh(make_cube(1.0f));
    REQUIRE(mesh.has_value());
    rig->cube = mesh.value();
    return rig;
}

struct Rgb { int r, g, b; };

Rgb pixel(const std::vector<std::uint8_t>& px, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (std::size_t{y} * width + x) * 4;
    return {px[i], px[i + 1], px[i + 2]};
}

bool close(Rgb a, Rgb b, int tol = 3) {
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}

Rgb to_rgb8(Vec3 c) {
    return {static_cast<int>(std::lround(c.x * 255)), static_cast<int>(std::lround(c.y * 255)),
            static_cast<int>(std::lround(c.z * 255))};
}

Entity spawn_cube(World& w, MeshId mesh, Vec3 position, Vec3 color) {
    const Entity e = w.create();
    w.add<Transform>(e, Transform{position});
    w.add<MeshInstance>(e, MeshInstance{mesh, color});
    return e;
}

}  // namespace

AETHER_TEST(headless_clear_only) {
    auto rig = make_rig();
    World w({.seed = 1});
    auto stats = rig->renderer->render(w, Camera{});
    REQUIRE(stats.has_value());
    CHECK_EQ(stats.value().draws, 0u);
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    REQUIRE(px.value().size() == 64u * 64u * 4u);
    int wrong = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            if (!close(pixel(px.value(), 64, x, y), to_rgb8(kClear))) ++wrong;
    CHECK_EQ(wrong, 0);
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_cube_front_face_is_lit_and_culled_correctly) {
    auto rig = make_rig();
    std::printf("    device: %s, %s%s\n", rig->renderer->gpu().name.c_str(), rig->renderer->gpu().api.c_str(),
                rig->renderer->gpu().validation ? ", validation on" : "");
    World w({.seed = 1});
    const Vec3 color{1.0f, 0.5f, 0.25f};
    spawn_cube(w, rig->cube, {0, 0, 0}, color);
    propagate_transforms(w);
    Camera cam;
    cam.position = {0, 0, 3};
    auto stats = rig->renderer->render(w, cam);
    REQUIRE(stats.has_value());
    CHECK_EQ(stats.value().draws, 1u);
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());

    // The camera sees the +Z face. Shader: color * (0.25 + 0.75 * max(n.L, 0))
    // with L = normalize(0.4, 0.8, 0.6). Seeing the back (-Z) face instead,
    // i.e. wrong winding or culling, would give only the 0.25 ambient term.
    const float lit = 0.25f + 0.75f * (0.6f / std::sqrt(0.4f * 0.4f + 0.8f * 0.8f + 0.6f * 0.6f));
    const Rgb center = pixel(px.value(), 64, 32, 32);
    CHECK(close(center, to_rgb8(color * lit)));
    std::printf("    center pixel = (%d, %d, %d), expected (%d, %d, %d)\n", center.r, center.g, center.b,
                to_rgb8(color * lit).r, to_rgb8(color * lit).g, to_rgb8(color * lit).b);
    CHECK(close(pixel(px.value(), 64, 0, 0), to_rgb8(kClear)));
    CHECK(close(pixel(px.value(), 64, 63, 63), to_rgb8(kClear)));
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_orientation_and_hierarchy) {
    auto rig = make_rig();
    World w({.seed = 1});
    // A parent at the origin with a child moved up and right: the child must
    // land in the top-right quadrant of the image.
    const Entity parent = w.create();
    w.add<Transform>(parent);
    const Entity child = spawn_cube(w, rig->cube, {1.2f, 1.2f, 0}, {0, 1, 0});
    w.get<Transform>(child)->scale = {0.5f, 0.5f, 0.5f};
    REQUIRE(hierarchy::set_parent(w, child, parent).has_value());
    propagate_transforms(w);
    Camera cam;
    cam.position = {0, 0, 5};
    REQUIRE(rig->renderer->render(w, cam).has_value());
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    int quadrant[2][2] = {{0, 0}, {0, 0}};  // [top?][right?]
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            if (!close(pixel(px.value(), 64, x, y), to_rgb8(kClear))) ++quadrant[y < 32 ? 1 : 0][x >= 32 ? 1 : 0];
    CHECK(quadrant[1][1] > 0);
    CHECK_EQ(quadrant[0][0] + quadrant[0][1] + quadrant[1][0], 0);
}

AETHER_TEST(headless_object_behind_camera_is_not_visible) {
    auto rig = make_rig();
    World w({.seed = 1});
    spawn_cube(w, rig->cube, {0, 0, 10}, {1, 1, 1});
    propagate_transforms(w);
    Camera cam;
    cam.position = {0, 0, 3};  // looking toward -Z, cube is behind
    REQUIRE(rig->renderer->render(w, cam).has_value());
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    int drawn = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            if (!close(pixel(px.value(), 64, x, y), to_rgb8(kClear))) ++drawn;
    CHECK_EQ(drawn, 0);
}

AETHER_TEST(headless_resize_and_errors) {
    auto rig = make_rig();
    CHECK(!rig->renderer->read_pixels());  // nothing rendered yet
    World w({.seed = 1});
    rig->renderer->resize(48, 20);
    REQUIRE(rig->renderer->render(w, Camera{}).has_value());
    CHECK_EQ(rig->renderer->width(), 48u);
    CHECK_EQ(rig->renderer->height(), 20u);
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    CHECK_EQ(px.value().size(), std::size_t{48 * 20 * 4});

    auto empty = rig->renderer->upload_mesh(MeshData{});
    REQUIRE(!empty);
    CHECK(empty.error().code == ErrorCode::InvalidArgument);
    MeshData bad = make_cube();
    bad.indices.push_back(999);
    CHECK(!rig->renderer->upload_mesh(bad));

    // Unknown mesh ids are skipped, not drawn out of bounds.
    spawn_cube(w, 12345, {0, 0, 0}, {1, 1, 1});
    propagate_transforms(w);
    auto stats = rig->renderer->render(w, Camera{});
    REQUIRE(stats.has_value());
    CHECK_EQ(stats.value().draws, 0u);
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_highlight_tints_only_the_selected_entity) {
    auto rig = make_rig();
    World w({.seed = 1});
    const Vec3 color{0.2f, 0.4f, 0.8f};
    const Entity left = spawn_cube(w, rig->cube, {-1.2f, 0, 0}, color);
    spawn_cube(w, rig->cube, {1.2f, 0, 0}, color);
    propagate_transforms(w);
    Camera cam;
    cam.position = {0, 0, 6};
    RenderOptions options;
    options.highlight = left;
    REQUIRE(rig->renderer->render(w, cam, options).has_value());
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    const Rgb l = pixel(px.value(), 64, 18, 32);
    const Rgb r = pixel(px.value(), 64, 46, 32);
    // Same object colour and lighting; only the highlighted one shifts toward orange.
    CHECK(l.r > r.r + 40);
    CHECK(l.b < r.b);
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_grid_draws_on_the_ground_plane_only) {
    auto rig = make_rig();
    World w({.seed = 1});
    Camera cam;
    cam.position = {0.3f, 3, 8};
    cam.target = {0.3f, 0, 0};
    RenderOptions options;
    options.grid = true;
    REQUIRE(rig->renderer->render(w, cam, options).has_value());
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    // The horizon is where a view ray is parallel to the ground: the camera
    // pitches down by atan(3/8), so it sits that far above the image centre.
    const float pitch = std::atan(3.0f / 8.0f);
    const float horizon_row = 32.0f - std::tan(pitch) / std::tan(cam.fov_y * 0.5f) * 32.0f;
    int above = 0, below = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            if (!close(pixel(px.value(), 64, x, y), to_rgb8(kClear)))
                (static_cast<float>(y) < horizon_row - 1.0f ? above : below)++;
    // Looking down at the XZ plane: lines appear below the horizon only.
    CHECK(below > 100);
    CHECK_EQ(above, 0);
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_texture_multiplies_base_color) {
    auto rig = make_rig();
    // 2x1 texture: left half red, right half blue, sRGB-encoded. The cube's
    // front face maps u 0..1 left to right, so the halves show side by side.
    TextureData tex{2, 1, {255, 0, 0, 255, 0, 0, 255, 255}};
    auto id = rig->renderer->upload_texture(tex);
    REQUIRE(id.has_value());
    CHECK(id.value() != kWhiteTexture);
    CHECK(!rig->renderer->upload_texture(TextureData{0, 0, {}}));
    CHECK(!rig->renderer->upload_texture(TextureData{2, 2, {1, 2, 3}}));

    World w({.seed = 1});
    const Entity e = spawn_cube(w, rig->cube, {0, 0, 0}, {1, 1, 1});
    w.get<MeshInstance>(e)->texture = id.value();
    propagate_transforms(w);
    Camera cam;
    cam.position = {0, 0, 3};
    REQUIRE(rig->renderer->render(w, cam).has_value());
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    const Rgb left = pixel(px.value(), 64, 26, 32);
    const Rgb right = pixel(px.value(), 64, 38, 32);
    std::printf("    left = (%d, %d, %d), right = (%d, %d, %d)\n", left.r, left.g, left.b, right.r, right.g, right.b);
    CHECK(left.r > 150 && left.g < 20 && left.b < 20);
    CHECK(right.b > 150 && right.r < 20 && right.g < 20);
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_model_library_loads_spawns_and_renders_sample_models) {
    auto rig = make_rig();
    auto lib = ModelLibrary::create(*rig->renderer, AETHER_SAMPLE_CONTENT, &rig->logger);
    REQUIRE(lib.has_value());
    auto& library = *lib.value();
    const auto models = library.list_models();
    REQUIRE(models.size() == 3u);
    CHECK_EQ(models[0], std::string("models/crate.glb"));
    CHECK_EQ(models[2], std::string("models/tree/tree.gltf"));

    for (const auto& path : models) {
        auto m = library.load(path);
        if (!m) std::fprintf(stderr, "    %s: %s\n", path.c_str(), m.error().message.c_str());
        REQUIRE(m.has_value());
        CHECK(m.value()->triangles > 0u);
        CHECK(m.value()->warnings.empty());
    }
    // Cached by normalised path; escaping the content root is refused.
    CHECK(library.load("models/./x/../crate.glb").value() == library.load("models\\crate.glb").value());
    CHECK(!library.load("../README.md"));
    CHECK(!library.load("/etc/passwd"));
    CHECK(!library.load("models/missing.glb"));

    // The tree: root "tree" > "Tree" (trunk) > "Canopy" with two primitives
    // (two materials) as children.
    World w({.seed = 1});
    auto tree = library.spawn(w, "models/tree/tree.gltf", {0, 0, 0});
    REQUIRE(tree.has_value());
    CHECK_EQ(w.get<Name>(tree.value())->value, std::string("tree"));
    int named = 0, drawn = 0;
    w.each<Name>([&](Entity, Name&) { ++named; });
    w.each<MeshInstance>([&](Entity, MeshInstance&) { ++drawn; });
    CHECK_EQ(named, 5);  // tree, Tree, Canopy, Canopy [0], Canopy [1]
    CHECK_EQ(drawn, 3);

    // The crate is textured; it renders brown-ish wood, not white.
    World cw({.seed = 1});
    auto crate = library.spawn(cw, "models/crate.glb", {0, -0.5f, 0});
    REQUIRE(crate.has_value());
    propagate_transforms(cw);
    Camera cam;
    cam.position = {0, 0, 3};
    auto stats = rig->renderer->render(cw, cam);
    REQUIRE(stats.has_value());
    CHECK_EQ(stats.value().draws, 1u);
    auto px = rig->renderer->read_pixels();
    REQUIRE(px.has_value());
    const Rgb c = pixel(px.value(), 64, 32, 40);
    std::printf("    crate pixel = (%d, %d, %d)\n", c.r, c.g, c.b);
    CHECK(c.r > c.g && c.g > c.b);
    CHECK(c.r > 30);  // sRGB texels are linearised; the target stores linear values
    // Bounds feed picking: the crate is a unit box.
    MeshId crate_mesh = 0;
    cw.each<MeshInstance>([&](Entity, MeshInstance& m) { crate_mesh = m.mesh; });
    CHECK(crate_mesh != library.cube());
    CHECK(std::abs(library.bounds()[crate_mesh].max.x - 0.5f) < 1e-5f);
    CHECK_EQ(rig->renderer->validation_errors(), 0u);
}

AETHER_TEST(headless_level_with_models_loads_them_on_demand) {
    auto rig = make_rig();
    Json doc;
    {
        auto lib = ModelLibrary::create(*rig->renderer, AETHER_SAMPLE_CONTENT);
        REQUIRE(lib.has_value());
        World w({.seed = 1});
        REQUIRE(lib.value()->spawn(w, "models/torus.glb", {0, 0, 0}).has_value());
        world::SerializerRegistry reg;
        world::register_core_components(reg);
        register_components(reg, lib.value()->names());
        doc = world::save_level(w, reg);
    }
    const std::string text = to_json_string(doc);
    CHECK(text.find("asset:models/torus.glb#mesh0.0") != std::string::npos);
    CHECK(text.find("asset:models/torus.glb#texture0") != std::string::npos);

    // A new library has never seen the torus; loading the level imports it.
    auto lib = ModelLibrary::create(*rig->renderer, AETHER_SAMPLE_CONTENT);
    REQUIRE(lib.has_value());
    world::SerializerRegistry reg;
    world::register_core_components(reg);
    register_components(reg, lib.value()->names());
    World w({.seed = 2});
    auto loaded = world::load_level(doc, w, reg);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().warnings.empty());
    int textured = 0;
    w.each<MeshInstance>([&](Entity, MeshInstance& m) { textured += m.texture != kWhiteTexture ? 1 : 0; });
    CHECK_EQ(textured, 1);
}
