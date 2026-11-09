// Aether Editor executable: window, ImGui context and renderer set-up, then
// the frame loop driving EditorApp.
//
// Usage: aether_editor [level.alevel] [--frames N] [--validation] [--no-vsync]
//        aether_editor --export-demo <path.alevel>   (writes the demo level, no window)
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "aether/editor/demo_scene.hpp"
#include "aether/editor/editor_app.hpp"
#include "aether/foundation/log.hpp"
#include "aether/foundation/time.hpp"
#include "aether/render/renderer.hpp"
#include "aether/world/hierarchy.hpp"

using namespace aether;

namespace {
std::vector<std::filesystem::path> g_dropped;  // filled by the GLFW drop callback
}

int main(int argc, char** argv) {
    long max_frames = -1;
    bool validation = false, vsync = true;
    std::filesystem::path open_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--frames" && i + 1 < argc) max_frames = std::strtol(argv[++i], nullptr, 10);
        else if (a == "--validation") validation = true;
        else if (a == "--no-vsync") vsync = false;
        else if (a == "--export-demo" && i + 1 < argc) {
            const auto r = editor::save_demo_level(argv[++i]);
            if (!r) std::fprintf(stderr, "export failed: %s\n", r.error().message.c_str());
            return r ? 0 : 1;
        } else if (!a.empty() && a[0] != '-') {
            open_path = std::filesystem::path(std::string(a));
        }
    }

    Logger logger;
    logger.set_min_level(LogLevel::Debug);
    auto memory_log = std::make_shared<MemorySink>(4096);
    logger.add_sink(memory_log);
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
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Aether Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "aether_editor.ini";
    float xscale = 1, yscale = 1;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    const float dpi = std::max(1.0f, xscale);
    editor::load_editor_font(15.0f * dpi);
    editor::apply_editor_theme(dpi);
    ImGui_ImplGlfw_InitForVulkan(window, true);

    int exit_code = 0;
    {
        render::RendererDesc desc;
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        desc.width = static_cast<std::uint32_t>(fb_w);
        desc.height = static_cast<std::uint32_t>(fb_h);
        desc.validation = validation;
        desc.vsync = vsync;
        desc.enable_ui = true;
        desc.logger = &logger;
        desc.clear_color = {0.32f, 0.42f, 0.56f};  // sky
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

        auto created = render::Renderer::create(desc);
        if (!created) {
            std::fprintf(stderr, "Renderer failed to start: %s\n", created.error().message.c_str());
            exit_code = 1;
        } else {
            std::unique_ptr<render::Renderer> renderer = std::move(created).value();
            glfwSetWindowUserPointer(window, renderer.get());
            glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int w, int h) {
                static_cast<render::Renderer*>(glfwGetWindowUserPointer(win))
                    ->resize(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
            });

            editor::EditorApp app(*renderer, logger, memory_log);
            if (!open_path.empty()) app.open_level(open_path);
            glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
                for (int i = 0; i < count; ++i)
                    g_dropped.push_back(std::filesystem::path(
                        std::u8string(reinterpret_cast<const char8_t*>(paths[i]))));  // GLFW gives UTF-8
            });
            Stopwatch clock;
            long frames = 0;
            std::string title;
            while (!app.quit_requested()) {
                glfwPollEvents();
                if (glfwWindowShouldClose(window)) {  // ask about unsaved changes instead of closing
                    glfwSetWindowShouldClose(window, GLFW_FALSE);
                    app.request_quit();
                }
                for (const auto& p : g_dropped)
                    if (p.extension() == ".alevel") {
                        app.open_level(p);
                        break;
                    }
                g_dropped.clear();
                if (const std::string t = app.window_title(); t != title) {
                    title = t;
                    glfwSetWindowTitle(window, title.c_str());
                }
                if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
                    glfwWaitEventsTimeout(0.1);
                    continue;
                }
                const float dt = std::min(0.1f, static_cast<float>(clock.elapsed_seconds()));
                clock.restart();

                renderer->begin_ui_frame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                ImGuizmo::BeginFrame();
                app.update(dt);
                app.draw(dt);
                world::propagate_transforms(app.world());  // apply this frame's edits
                ImGui::Render();

                auto stats = renderer->render(app.world(), app.camera(), app.render_options());
                if (!stats) {
                    std::fprintf(stderr, "Rendering failed: %s\n", stats.error().message.c_str());
                    exit_code = 1;
                    break;
                }
                app.set_frame_stats(stats.value());
                if (max_frames >= 0 && ++frames >= max_frames) break;
            }
            if (renderer->validation_errors() > 0) {
                std::fprintf(stderr, "%u Vulkan validation error(s) reported\n", renderer->validation_errors());
                exit_code = 1;
            }
            // The renderer shuts down its ImGui backend, so it goes before the context.
        }
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exit_code;
}
