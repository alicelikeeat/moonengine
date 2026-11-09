// Renderer interface.
//
// The public API exposes no Vulkan types so other backends (native Metal is
// planned, ADR 0005) can implement the same interface. The first backend is
// Vulkan 1.3 with dynamic rendering, loaded at run time through volk.
//
// Two modes:
//  - Windowed: pass instance extensions and a create_surface callback from
//    the windowing library; render() presents to the window.
//  - Headless: leave create_surface empty; render() draws to an offscreen
//    RGBA8 image that read_pixels() returns. Used by tests and tools.
//
// The renderer draws every entity that has GlobalTransform + MeshInstance.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "aether/foundation/log.hpp"
#include "aether/foundation/math.hpp"
#include "aether/foundation/result.hpp"
#include "aether/render/camera.hpp"
#include "aether/render/light.hpp"
#include "aether/render/mesh.hpp"
#include "aether/world/world.hpp"

namespace aether::render {

struct RendererDesc {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    // Enables Khronos validation if the layer is installed (a warning is
    // logged otherwise). Meant for development; it costs performance.
    bool validation = false;
    bool vsync = true;
    // Instance extensions the window system needs (e.g. from GLFW).
    std::vector<std::string> instance_extensions;
    // Receives the VkInstance as void*, returns a VkSurfaceKHR handle as an
    // integer (0 on failure). Leave empty for headless rendering.
    std::function<std::uint64_t(void* vk_instance)> create_surface;
    // Optional; receives device selection, validation messages and errors.
    Logger* logger = nullptr;
    math::Vec3 clear_color{0.05f, 0.06f, 0.08f};
    // Editor/UI mode (windowed only). The caller must have created a Dear
    // ImGui context first. The scene is drawn into an offscreen viewport
    // texture (see viewport_texture()) and the ImGui draw data of the frame
    // is drawn to the window on top.
    bool enable_ui = false;
};

// Image-wide lighting not tied to an entity.
struct Environment {
    // Ambient light from above and below (a hemisphere "sky light"); also
    // what metals reflect, since there is no environment map yet.
    math::Vec3 sky_color{0.42f, 0.52f, 0.68f};
    math::Vec3 ground_color{0.16f, 0.14f, 0.12f};
    float ambient_intensity = 1.0f;
    float exposure = 1.0f;  // applied before ACES tone mapping
    bool shadows = true;    // directional shadow map on/off
};

struct RenderOptions {
    world::Entity highlight = world::kNullEntity;  // drawn with the selection tint
    bool grid = false;                             // ground grid on the XZ plane
    Environment environment;
};

struct GpuInfo {
    std::string name;
    std::string api;          // e.g. "Vulkan 1.3.280"
    bool software = false;    // CPU implementation such as llvmpipe
    bool validation = false;  // validation layer active
};

struct FrameStats {
    std::uint32_t draws = 0;          // scene draws (not counting the shadow pass)
    std::uint32_t shadow_draws = 0;   // draws into the shadow map
    std::uint32_t point_lights = 0;   // point lights used this frame
    bool sun_shadows = false;         // a directional shadow map was rendered
    bool presented = false;  // false when skipped (minimised, swapchain rebuilt)
};

class Renderer {
public:
    static Result<std::unique_ptr<Renderer>> create(const RendererDesc& desc);
    virtual ~Renderer() = default;

    virtual Result<MeshId> upload_mesh(const MeshData& mesh) = 0;
    // Uploads RGBA8 pixels and builds mipmaps on the GPU.
    virtual Result<TextureId> upload_texture(const TextureData& texture) = 0;

    // Call when the window size changes; takes effect on the next frame.
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    Result<FrameStats> render(world::World& world, const Camera& camera) { return render(world, camera, RenderOptions{}); }
    virtual Result<FrameStats> render(world::World& world, const Camera& camera, const RenderOptions& options) = 0;

    // ---- UI mode (RendererDesc::enable_ui) ----------------------------
    // Call once per frame before ImGui::NewFrame().
    virtual void begin_ui_frame() = 0;
    // Size of the scene view in pixels; takes effect after the next render()
    // so the texture used by the current frame's UI stays valid.
    virtual void set_viewport_size(std::uint32_t width, std::uint32_t height) = 0;
    // ImTextureID of the scene view for ImGui::Image (0 without UI mode).
    [[nodiscard]] virtual std::uint64_t viewport_texture() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t viewport_width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t viewport_height() const noexcept = 0;

    // Headless only: RGBA8 pixels of the last frame, row-major, top row first.
    virtual Result<std::vector<std::uint8_t>> read_pixels() = 0;

    [[nodiscard]] virtual const GpuInfo& gpu() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    // Validation errors reported so far (always 0 without validation).
    [[nodiscard]] virtual std::uint32_t validation_errors() const noexcept = 0;
};

}  // namespace aether::render
