// Aether Engine sample: a small solar-system of cubes.
//
// Shows the pieces working together: ECS entities and components, a
// Schedule-driven system, the scene hierarchy (moons orbit planets that
// orbit the sun, purely through parent transforms), a fixed simulation
// timestep, and the Vulkan renderer presenting to a GLFW window.
//
// Usage: aether_spinning_cubes [--level file.alevel] [--content DIR] [--frames N] [--validation] [--no-vsync]
//   --level F      run a level saved by the editor instead of the built-in scene
//   --content DIR  folder with the models the level uses (default: ./Content,
//                  else Content next to the executable)
//   --frames N     exit after N frames (used by CI smoke tests)
//   --validation   enable Khronos validation if installed
//   --no-vsync     present as fast as possible
//   Esc closes the window.
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "aether/foundation/job_system.hpp"
#include "aether/foundation/log.hpp"
#include "aether/foundation/math.hpp"
#include "aether/foundation/time.hpp"
#include "aether/render/renderer.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/schedule.hpp"
#include "aether/world/serialization.hpp"
#include "aether/gameplay/spin.hpp"
#include "aether/render/light.hpp"
#include "aether/render/mesh_registry.hpp"
#include "aether/render/model_library.hpp"

using namespace aether;
using namespace aether::math;
using namespace aether::world;

namespace {

using gameplay::Spin;

struct Options {
    std::string level;  // level file to run instead of the built-in scene
    std::string content;
    long frames = -1;   // -1: run until closed
    bool validation = false;
    bool vsync = true;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--frames" && i + 1 < argc) o.frames = std::strtol(argv[++i], nullptr, 10);
        else if (a == "--level" && i + 1 < argc) o.level = argv[++i];
        else if (a == "--content" && i + 1 < argc) o.content = argv[++i];
        else if (a == "--validation") o.validation = true;
        else if (a == "--no-vsync") o.vsync = false;
        else std::cerr << "ignoring unknown argument: " << a << "\n";
    }
    return o;
}

Entity spawn(World& w, render::MeshId mesh, Vec3 pos, float scale, Vec3 color, Spin spin) {
    const Entity e = w.create();
    w.add<Transform>(e, Transform{pos, Quat::identity(), {scale, scale, scale}});
    w.add<render::MeshInstance>(e, render::MeshInstance{mesh, color});
    w.add<Spin>(e, spin);
    return e;
}

void build_scene(World& w, render::MeshId cube) {
    // The sun glows (emissive) and lights the planets (a point light).
    const Entity sun = spawn(w, cube, {0, 0, 0}, 2.2f, {1.0f, 0.72f, 0.2f}, {{0, 1, 0}, 0.25f});
    w.get<render::MeshInstance>(sun)->emissive = {1.6f, 0.9f, 0.25f};
    const Entity glow = w.create();
    w.add<Transform>(glow, Transform{});
    render::Light glow_light;
    glow_light.color = {1.0f, 0.78f, 0.45f};
    glow_light.intensity = 40.0f;
    glow_light.range = 24.0f;
    w.add<render::Light>(glow, glow_light);
    // Soft daylight from above so the far side of each planet is not black.
    const Entity daylight = w.create();
    w.add<Transform>(daylight, Transform{{0, 10, 0}, render::rotation_toward({-0.3f, -1.0f, -0.4f})});
    render::Light day;
    day.type = render::LightType::Directional;
    day.intensity = 1.2f;
    w.add<render::Light>(daylight, day);
    const Vec3 palette[] = {{0.3f, 0.6f, 1.0f}, {0.4f, 0.9f, 0.5f}, {0.9f, 0.35f, 0.4f},
                            {0.7f, 0.5f, 1.0f}, {0.2f, 0.85f, 0.85f}, {0.95f, 0.6f, 0.3f}};
    for (int i = 0; i < 6; ++i) {
        const float fi = static_cast<float>(i);
        // An invisible pivot at the centre carries each orbit, so the orbit
        // speed is independent of the planet's own spin.
        const Entity orbit = w.create();
        w.add<Transform>(orbit, Transform{{0, 0, 0}, Quat::from_axis_angle({0, 1, 0}, fi)});
        w.add<Spin>(orbit, Spin{{0, 1, 0}, 0.9f / (1.0f + 0.5f * fi)});
        const Entity planet = spawn(w, cube, {3.5f + 1.7f * fi, 0, 0}, 0.9f, palette[i], {{0.3f, 1, 0.2f}, 1.5f});
        (void)hierarchy::set_parent(w, planet, orbit);

        for (int m = 0; m < i % 3 + 1; ++m) {
            const float fm = static_cast<float>(m);
            const Entity moon_orbit = w.create();
            w.add<Transform>(moon_orbit, Transform{{0, 0, 0}, Quat::from_axis_angle({1, 0, 0}, 0.6f * fm)});
            w.add<Spin>(moon_orbit, Spin{{0, 1, 0}, 2.5f + fm});
            (void)hierarchy::set_parent(w, moon_orbit, planet);
            // Planet scale (0.9) also scales the moon's offset and size.
            const Entity moon = spawn(w, cube, {1.3f + 0.7f * fm, 0, 0}, 0.35f, {0.85f, 0.85f, 0.9f}, {{1, 1, 0}, 3.0f});
            w.get<render::MeshInstance>(moon)->metallic = 1.0f;
            w.get<render::MeshInstance>(moon)->roughness = 0.3f;
            (void)hierarchy::set_parent(w, moon, moon_orbit);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);

    Logger logger;
    logger.add_sink(std::make_shared<TextSink>(std::cout));

    glfwSetErrorCallback([](int code, const char* msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); });
    if (!glfwInit()) {
        std::fprintf(stderr, "Could not initialise the window system.\n");
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "Vulkan is not available. Install or update your GPU driver (Vulkan 1.3 required).\n");
        glfwTerminate();
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Aether Engine - Spinning Cubes", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    render::RendererDesc desc;
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    desc.width = static_cast<std::uint32_t>(fb_w);
    desc.height = static_cast<std::uint32_t>(fb_h);
    desc.validation = opt.validation;
    desc.vsync = opt.vsync;
    desc.logger = &logger;
    std::uint32_t ext_count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&ext_count);
    for (std::uint32_t i = 0; i < ext_count; ++i) desc.instance_extensions.emplace_back(exts[i]);
    desc.create_surface = [window](void* instance) -> std::uint64_t {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(static_cast<VkInstance>(instance), window, nullptr, &surface) != VK_SUCCESS) return 0;
        std::uint64_t handle = 0;
        std::memcpy(&handle, &surface, sizeof(handle));
        return handle;
    };

    int exit_code = 0;
    {
        auto created = render::Renderer::create(desc);
        if (!created) {
            std::fprintf(stderr, "Renderer failed to start: %s\n", created.error().message.c_str());
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
        std::unique_ptr<render::Renderer> renderer = std::move(created).value();
        glfwSetWindowUserPointer(window, renderer.get());
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int w, int h) {
            static_cast<render::Renderer*>(glfwGetWindowUserPointer(win))
                ->resize(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
        });

        World world;
        // Models a level refers to are imported from the content folder on demand.
        auto library = render::ModelLibrary::create(
            *renderer, render::find_content_root(opt.content, argc > 0 ? argv[0] : nullptr), &logger);
        if (!library) {
            std::fprintf(stderr, "%s\n", library.error().message.c_str());
            return 1;
        }
        if (opt.level.empty()) {
            build_scene(world, library.value()->cube());
        } else {
            // Run a level made in the editor: same components, same file format.
            SerializerRegistry registry;
            register_core_components(registry);
            render::register_components(registry, library.value()->names());
            gameplay::register_components(registry);
            auto report = load_level_file(opt.level, world, registry);
            if (!report) {
                std::fprintf(stderr, "Could not load %s: %s\n", opt.level.c_str(), report.error().message.c_str());
                return 1;
            }
            for (const auto& w : report.value().warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
            std::printf("loaded %s (%zu entities)\n", opt.level.c_str(), report.value().entities);
        }

        Schedule schedule;
        gameplay::add_systems(schedule, world);

        JobSystem jobs;
        FixedTimestep clock(16'666'667);  // 60 Hz simulation
        render::Camera camera;
        camera.position = {0, 11, 20};
        camera.fov_y = 0.9f;  // ~52 degrees
        Stopwatch frame_timer, title_timer;
        long frames = 0;
        int frames_since_title = 0;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);

            const auto steps = clock.advance(frame_timer.elapsed_ns());
            frame_timer.restart();
            for (std::uint32_t i = 0; i < steps.steps; ++i) schedule.run(world, &jobs, clock.step_seconds());
            propagate_transforms(world, &jobs);

            auto rendered = renderer->render(world, camera);
            if (!rendered) {
                std::fprintf(stderr, "Rendering failed: %s\n", rendered.error().message.c_str());
                exit_code = 1;
                break;
            }
            ++frames;
            ++frames_since_title;
            if (title_timer.elapsed_seconds() >= 0.5) {
                const double fps = frames_since_title / title_timer.elapsed_seconds();
                const std::string title = "Aether Engine - Spinning Cubes | " + renderer->gpu().name + " | " +
                                          std::to_string(static_cast<int>(fps + 0.5)) + " fps";
                glfwSetWindowTitle(window, title.c_str());
                title_timer.restart();
                frames_since_title = 0;
            }
            if (opt.frames >= 0 && frames >= opt.frames) break;
        }
        if (renderer->validation_errors() > 0) {
            std::fprintf(stderr, "%u Vulkan validation error(s) reported\n", renderer->validation_errors());
            exit_code = 1;
        }
        std::printf("rendered %ld frames on %s\n", frames, renderer->gpu().name.c_str());
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return exit_code;
}
