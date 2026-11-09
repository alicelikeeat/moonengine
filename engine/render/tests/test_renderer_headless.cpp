// Renders offscreen on whatever Vulkan device is available (lavapipe in
// CI) and checks real pixels. Skips when no Vulkan 1.3 device exists.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "aether/render/renderer.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::math;
using namespace aether::render;
using namespace aether::world;

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
