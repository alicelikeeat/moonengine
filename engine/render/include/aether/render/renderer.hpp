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
};

struct GpuInfo {
    std::string name;
    std::string api;          // e.g. "Vulkan 1.3.280"
    bool software = false;    // CPU implementation such as llvmpipe
    bool validation = false;  // validation layer active
};

struct FrameStats {
    std::uint32_t draws = 0;
    bool presented = false;  // false when skipped (minimised, swapchain rebuilt)
};

class Renderer {
public:
    static Result<std::unique_ptr<Renderer>> create(const RendererDesc& desc);
    virtual ~Renderer() = default;

    virtual Result<MeshId> upload_mesh(const MeshData& mesh) = 0;

    // Call when the window size changes; takes effect on the next frame.
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    virtual Result<FrameStats> render(world::World& world, const Camera& camera) = 0;

    // Headless only: RGBA8 pixels of the last frame, row-major, top row first.
    virtual Result<std::vector<std::uint8_t>> read_pixels() = 0;

    [[nodiscard]] virtual const GpuInfo& gpu() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    // Validation errors reported so far (always 0 without validation).
    [[nodiscard]] virtual std::uint32_t validation_errors() const noexcept = 0;
};

}  // namespace aether::render
