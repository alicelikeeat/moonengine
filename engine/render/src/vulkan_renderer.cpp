// Vulkan 1.3 backend for aether::render::Renderer.
//
// Deliberately small: forward rendering with dynamic rendering, two frames
// in flight, host-visible mesh buffers, one descriptor set per frame (all
// textures as an array, the frame's uniforms, the sun's shadow map). It is
// a working slice of the renderer, not the final architecture (render
// graph, GPU-driven culling and a real memory allocator are on the roadmap).
#include <volk.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "aether/render/light.hpp"
#include "aether/render/renderer.hpp"
#include "aether/render/sky.hpp"
#include "aether/world/hierarchy.hpp"
#include "generated/shaders.inl"

namespace aether::render {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat kOffscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;
// UI mode: the scene is rendered through an sRGB view (hardware encodes
// linear shader output) and sampled by ImGui through a UNORM view of the
// same image (no decode), then written to a UNORM swapchain. ImGui's colours
// are authored for non-sRGB targets, so this keeps both UI and scene right.
constexpr VkFormat kViewportRenderFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat kViewportSampleFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr float kGridHalfExtent = 1000.0f;  // analytic grid quad; the shader fades it out
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
// Must match the textures[] array size in mesh.frag.
constexpr std::uint32_t kMaxTextures = 1024;
constexpr std::uint32_t kShadowMapSize = 2048;
constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;
constexpr std::uint32_t kShadowCascades = 3;  // must match light_view_proj[] in common.glsl
constexpr float kShadowDistance = 80.0f;      // view distance covered by the cascades
constexpr float kCascadeLambda = 0.7f;        // 0: uniform splits, 1: logarithmic
constexpr std::uint32_t kSkyCubeSize = 32;
constexpr std::uint32_t kSkyCubeMips = 6;     // mesh.frag samples mip = roughness * 5
constexpr VkFormat kSkyFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Layouts below must match engine/render/shaders/common.glsl.
struct PushConstants {
    float model[16];
    float base_color[4];      // rgb, a: highlight
    float emissive_rough[4];  // rgb: emissive, a: roughness
    std::uint32_t material[4];  // base colour, metallic-roughness and normal textures; metallic bits
};
static_assert(sizeof(PushConstants) == 112, "must fit the guaranteed 128-byte push constant size");

struct GpuPointLight {
    float position_range[4];
    float color[4];
};

struct FrameUniforms {  // std140
    float view_proj[16];
    float inv_view_proj[16];
    float light_view_proj[kShadowCascades][16];
    float camera_position[4];
    float camera_forward[4];
    float sun_direction[4];
    float sun_color[4];
    float cascade_splits[4];
    float cascade_texels[4];
    float cascade_bias[4];
    float sky_zenith[4];
    float sky_horizon[4];
    float sky_ground[4];
    float irradiance_sh[9][4];
    std::uint32_t counts[4];
    GpuPointLight points[kMaxPointLights];
};
static_assert(sizeof(FrameUniforms) == 1152, "std140 layout of the Frame block");

void set4(float* out, math::Vec3 v, float w) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    out[3] = w;
}

const char* result_name(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult(other)";
    }
}

Error vk_error(VkResult r, const char* call) {
    ErrorCode code = ErrorCode::Unknown;
    if (r == VK_ERROR_DEVICE_LOST) code = ErrorCode::DeviceLost;
    else if (r == VK_ERROR_OUT_OF_HOST_MEMORY || r == VK_ERROR_OUT_OF_DEVICE_MEMORY) code = ErrorCode::OutOfMemory;
    else if (r == VK_ERROR_INCOMPATIBLE_DRIVER || r == VK_ERROR_EXTENSION_NOT_PRESENT ||
             r == VK_ERROR_FEATURE_NOT_PRESENT) code = ErrorCode::Unsupported;
    return Error{code, std::string(call) + " failed: " + result_name(r)};
}

#define AETHER_VK_TRY(call)                                  \
    do {                                                     \
        const VkResult aether_vk_r_ = (call);                \
        if (aether_vk_r_ != VK_SUCCESS) return vk_error(aether_vk_r_, #call); \
    } while (false)

#define AETHER_TRY(expr)                                     \
    do {                                                     \
        auto aether_try_r_ = (expr);                         \
        if (!aether_try_r_) return aether_try_r_.error();    \
    } while (false)

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct GpuMesh {
    Buffer vertices;
    Buffer indices;
    std::uint32_t index_count = 0;
    Aabb bounds;
};

struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    Buffer uniforms;                         // FrameUniforms, host-visible
    Buffer sky_staging;                      // sky cube texels to upload this frame
    VkDescriptorSet set = VK_NULL_HANDLE;    // textures, uniforms, shadow map
};

// One draw of the frame, gathered before recording.
struct Draw {
    world::Entity entity;
    math::Mat4 model;
    MeshInstance instance;
};

class VulkanRenderer final : public Renderer {
public:
    ~VulkanRenderer() override;

    Result<void> init(const RendererDesc& desc);

    Result<MeshId> upload_mesh(const MeshData& mesh) override;
    Result<TextureId> upload_texture(const TextureData& texture) override;
    void resize(std::uint32_t width, std::uint32_t height) override;
    using Renderer::render;
    Result<FrameStats> render(world::World& world, const Camera& camera, const RenderOptions& options) override;
    void begin_ui_frame() override;
    void set_viewport_size(std::uint32_t width, std::uint32_t height) override;
    [[nodiscard]] std::uint64_t viewport_texture() const noexcept override;
    [[nodiscard]] std::uint32_t viewport_width() const noexcept override { return viewport_extent_.width; }
    [[nodiscard]] std::uint32_t viewport_height() const noexcept override { return viewport_extent_.height; }
    Result<std::vector<std::uint8_t>> read_pixels() override;
    [[nodiscard]] const GpuInfo& gpu() const noexcept override { return info_; }
    [[nodiscard]] std::uint32_t width() const noexcept override { return extent_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return extent_.height; }
    [[nodiscard]] std::uint32_t validation_errors() const noexcept override { return validation_errors_; }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                         VkDebugUtilsMessageTypeFlagsEXT types,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                         void* user);

    bool headless() const noexcept { return surface_ == VK_NULL_HANDLE; }
    void log(LogLevel level, const std::string& message) {
        if (desc_.logger) desc_.logger->log(level, "render", message);
    }

    Result<void> create_instance();
    Result<void> pick_device();
    Result<void> create_device();
    Result<void> create_frames();
    Result<void> create_pipeline();
    Result<void> create_targets();
    void destroy_targets();
    Result<void> create_swapchain();
    Result<void> create_grid();
    Result<void> create_descriptors();
    void write_texture_descriptor(std::uint32_t slot, VkImageView view);
    Result<void> init_ui();
    Result<void> create_viewport_target();
    void destroy_viewport_target();

    std::optional<std::uint32_t> find_memory(std::uint32_t type_bits, VkMemoryPropertyFlags props) const;
    Result<Buffer> create_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void destroy_buffer(Buffer& b);
    Result<Image> create_image(std::uint32_t w, std::uint32_t h, VkFormat format, VkImageUsageFlags usage,
                               VkImageAspectFlags aspect);
    void destroy_image(Image& img);

    // Gathers draws and lights and fills the current frame's uniforms.
    void prepare_frame(world::World& world, const Camera& camera, const RenderOptions& options, VkExtent2D extent,
                       FrameStats& stats);
    // Uploads the baked sky cube when the Environment changed.
    void record_sky_upload(VkCommandBuffer cmd);
    // Renders the sun's shadow cascades (cleared when shadows are off) and
    // leaves them readable by the scene pass.
    void record_shadows(VkCommandBuffer cmd, FrameStats& stats);
    // Draws the scene into `color` (left in COLOR_ATTACHMENT_OPTIMAL).
    void record_scene(VkCommandBuffer cmd, VkImage color, VkImageView color_view, VkExtent2D extent,
                      const RenderOptions& options, FrameStats& stats);
    static void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                              VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                              VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

    RendererDesc desc_;
    GpuInfo info_;
    std::uint32_t validation_errors_ = 0;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_props_{};

    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::array<Frame, kFramesInFlight> frames_{};
    std::uint32_t frame_index_ = 0;

    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline grid_pipeline_ = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
    VkPipeline sky_pipeline_ = VK_NULL_HANDLE;
    VkFormat scene_format_ = kOffscreenFormat;  // what the scene pipelines render to
    VkFormat color_format_ = kOffscreenFormat;  // swapchain format
    VkColorSpaceKHR color_space_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkExtent2D extent_{};
    VkExtent2D requested_extent_{};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swap_images_;
    std::vector<VkImageView> swap_views_;
    std::vector<VkSemaphore> render_finished_;  // one per swapchain image
    bool swapchain_dirty_ = false;

    Image depth_;
    Image offscreen_;
    Buffer readback_;
    bool has_frame_ = false;

    std::vector<GpuMesh> meshes_;
    Buffer grid_;  // 6 vertices: one large quad on the XZ plane

    // Descriptors: binding 0 = all textures (unused slots show white),
    // 1 = frame uniforms, 2 = shadow map. One set per frame in flight.
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkSampler texture_sampler_ = VK_NULL_HANDLE;
    VkSampler shadow_sampler_ = VK_NULL_HANDLE;
    std::vector<GpuTexture> textures_;  // [0] = white
    std::uint32_t max_texture_size_ = 4096;
    float max_anisotropy_ = 0.0f;  // 0: anisotropic filtering unavailable
    // Sun shadow cascades: one depth array image, a view per layer to render
    // into and an array view to sample.
    VkImage shadow_image_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_memory_ = VK_NULL_HANDLE;
    VkImageView shadow_array_view_ = VK_NULL_HANDLE;
    std::array<VkImageView, kShadowCascades> shadow_layer_views_{};
    // Each cascade's light-space rectangle (centre x, y, half size) for culling.
    std::array<std::array<float, 3>, kShadowCascades> cascade_rects_{};
    math::Vec3 light_right_{}, light_up_{};

    // Prefiltered sky cube for reflections, re-baked when the Environment changes.
    VkImage sky_image_ = VK_NULL_HANDLE;
    VkDeviceMemory sky_memory_ = VK_NULL_HANDLE;
    VkImageView sky_view_ = VK_NULL_HANDLE;
    VkSampler sky_sampler_ = VK_NULL_HANDLE;
    std::optional<Environment> baked_environment_;
    std::array<math::Vec3, 9> sky_irradiance_{};
    bool sky_upload_pending_ = false;
    bool sky_image_initialised_ = false;
    bool draw_sky_ = false;

    // Per-frame scene data, rebuilt by prepare_frame().
    std::vector<Draw> draws_;
    bool sun_shadows_ = false;

    // UI mode
    bool ui_ = false;
    VkDescriptorPool ui_pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    Image viewport_;                                  // view_ is the sRGB render view
    VkImageView viewport_sample_view_ = VK_NULL_HANDLE;  // UNORM view sampled by ImGui
    VkDescriptorSet viewport_set_ = VK_NULL_HANDLE;
    VkExtent2D viewport_extent_{640, 360};
    VkExtent2D pending_viewport_{640, 360};
};

// ---- Set-up ----------------------------------------------------------------

Result<void> VulkanRenderer::init(const RendererDesc& desc) {
    desc_ = desc;
    requested_extent_ = {std::max(1u, desc.width), std::max(1u, desc.height)};
    extent_ = requested_extent_;
    if (volkInitialize() != VK_SUCCESS)
        return Error{ErrorCode::Unsupported,
                     "Vulkan loader not found. Install or update your GPU driver (Vulkan 1.3 required)."};
    AETHER_TRY(create_instance());
    if (desc_.create_surface) {
        static_assert(sizeof(VkSurfaceKHR) == sizeof(std::uint64_t), "64-bit targets only");
        const std::uint64_t handle = desc_.create_surface(instance_);
        std::memcpy(&surface_, &handle, sizeof(handle));
        if (surface_ == VK_NULL_HANDLE) return Error{ErrorCode::Unknown, "window surface creation failed"};
    }
    AETHER_TRY(pick_device());
    AETHER_TRY(create_device());
    AETHER_TRY(create_frames());
    ui_ = desc_.enable_ui;
    if (ui_ && headless()) return Error{ErrorCode::InvalidArgument, "enable_ui requires a window surface"};
    if (ui_ && ImGui::GetCurrentContext() == nullptr)
        return Error{ErrorCode::InvalidState, "enable_ui requires ImGui::CreateContext() before Renderer::create"};
    if (headless()) {
        color_format_ = kOffscreenFormat;
        scene_format_ = kOffscreenFormat;
    } else {
        // Choose the surface format once; the pipeline is built against it.
        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
        if (formats.empty()) return Error{ErrorCode::Unsupported, "surface reports no formats"};
        color_format_ = formats[0].format;
        color_space_ = formats[0].colorSpace;
        // Without UI the scene goes straight to an sRGB swapchain; with UI
        // a UNORM swapchain matches how ImGui's colours are authored.
        const VkFormat wanted[2] = {ui_ ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_SRGB,
                                    ui_ ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB};
        bool found = false;
        for (const VkFormat w : wanted)
            for (const auto& f : formats)
                if (!found && f.format == w && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    color_format_ = f.format;
                    color_space_ = f.colorSpace;
                    found = true;
                }
        scene_format_ = ui_ ? kViewportRenderFormat : color_format_;
    }
    AETHER_TRY(create_descriptors());
    AETHER_TRY(create_pipeline());
    AETHER_TRY(create_grid());
    {
        auto white = upload_texture(TextureData{1, 1, {255, 255, 255, 255}, false});
        if (!white) return white.error();
    }
    AETHER_TRY(create_targets());
    if (ui_) AETHER_TRY(init_ui());
    log(LogLevel::Info, "renderer ready on " + info_.name + " (" + info_.api + ")" +
                            (info_.validation ? ", validation on" : ""));
    return ok();
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                              VkDebugUtilsMessageTypeFlagsEXT,
                                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                              void* user) {
    auto* self = static_cast<VulkanRenderer*>(user);
    const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    if (error) ++self->validation_errors_;
    self->log(error ? LogLevel::Error : LogLevel::Warn,
              std::string("vulkan: ") + (data && data->pMessage ? data->pMessage : "(no message)"));
    return VK_FALSE;
}

Result<void> VulkanRenderer::create_instance() {
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3)
        return Error{ErrorCode::Unsupported, "Vulkan 1.3 is required; update your GPU driver"};

    std::vector<const char*> extensions;
    for (const auto& e : desc_.instance_extensions) extensions.push_back(e.c_str());
    std::vector<const char*> layers;

    if (desc_.validation) {
        std::uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        const bool found = std::any_of(available.begin(), available.end(), [](const VkLayerProperties& l) {
            return std::strcmp(l.layerName, kValidationLayer) == 0;
        });
        if (found) {
            layers.push_back(kValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            info_.validation = true;
        } else {
            log(LogLevel::Warn, "validation requested but VK_LAYER_KHRONOS_validation is not installed");
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Aether Engine";
    app.pEngineName = "Aether Engine";
    app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT messenger_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = &debug_callback;
    messenger_info.pUserData = this;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (info_.validation) info.pNext = &messenger_info;  // also covers instance creation itself
    AETHER_VK_TRY(vkCreateInstance(&info, nullptr, &instance_));
    volkLoadInstance(instance_);
    if (info_.validation) AETHER_VK_TRY(vkCreateDebugUtilsMessengerEXT(instance_, &messenger_info, nullptr, &messenger_));
    return ok();
}

Result<void> VulkanRenderer::pick_device() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    int best_score = -1;
    std::string rejected;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        const auto reject = [&](const char* why) { rejected += std::string("\n  ") + props.deviceName + ": " + why; };

        if (props.apiVersion < VK_API_VERSION_1_3) { reject("Vulkan 1.3 not supported"); continue; }
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2) { reject("dynamic rendering or synchronization2 missing"); continue; }
        if (!f2.features.shaderSampledImageArrayDynamicIndexing) { reject("texture array indexing not supported"); continue; }
        if (props.limits.maxPerStageDescriptorSamplers < kMaxTextures + 1 ||
            props.limits.maxPerStageDescriptorSampledImages < kMaxTextures + 1 ||
            props.limits.maxDescriptorSetSamplers < kMaxTextures + 1) {
            reject("fewer than 1025 textures per shader stage");
            continue;
        }

        if (!headless()) {
            std::uint32_t ext_count = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
            const bool has_swapchain = std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
            if (!has_swapchain) { reject("no swapchain support"); continue; }
        }

        std::uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> families(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, families.data());
        std::optional<std::uint32_t> family;
        for (std::uint32_t i = 0; i < qcount && !family; ++i) {
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_TRUE;
            if (!headless()) vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
            if (present) family = i;
        }
        if (!family) { reject("no graphics queue that can present"); continue; }

        int score = 0;
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 4; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 3; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 2; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 1; break;
            default: score = 0;
        }
        if (score > best_score) {
            best_score = score;
            physical_ = pd;
            queue_family_ = *family;
            info_.name = props.deviceName;
            info_.software = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
            max_texture_size_ = props.limits.maxImageDimension2D;
            max_anisotropy_ = f2.features.samplerAnisotropy ? std::min(8.0f, props.limits.maxSamplerAnisotropy) : 0.0f;
            info_.api = "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
                        std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + "." +
                        std::to_string(VK_API_VERSION_PATCH(props.apiVersion));
        }
    }
    if (physical_ == VK_NULL_HANDLE)
        return Error{ErrorCode::Unsupported,
                     "no GPU supports Vulkan 1.3 with dynamic rendering" +
                         (rejected.empty() ? std::string(" (no Vulkan devices found)") : rejected)};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory_props_);
    return ok();
}

Result<void> VulkanRenderer::create_device() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = queue_family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &f13;
    features.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    features.features.samplerAnisotropy = max_anisotropy_ > 0.0f ? VK_TRUE : VK_FALSE;

    const char* swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = headless() ? 0 : 1;
    info.ppEnabledExtensionNames = headless() ? nullptr : &swapchain_ext;
    AETHER_VK_TRY(vkCreateDevice(physical_, &info, nullptr, &device_));
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return ok();
}

Result<void> VulkanRenderer::create_frames() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queue_family_;
    AETHER_VK_TRY(vkCreateCommandPool(device_, &pool, nullptr, &pool_));
    for (Frame& f : frames_) {
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        AETHER_VK_TRY(vkAllocateCommandBuffers(device_, &alloc, &f.cmd));
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        AETHER_VK_TRY(vkCreateFence(device_, &fence, nullptr, &f.fence));
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        AETHER_VK_TRY(vkCreateSemaphore(device_, &sem, nullptr, &f.image_available));
    }
    return ok();
}

Result<void> VulkanRenderer::create_pipeline() {
    const auto make_module = [&](const std::uint32_t* code, std::size_t bytes, VkShaderModule& out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes;
        info.pCode = code;
        return vkCreateShaderModule(device_, &info, nullptr, &out);
    };
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    AETHER_VK_TRY(make_module(shaders::kMeshVertSpv, sizeof(shaders::kMeshVertSpv), vert));
    if (const VkResult r = make_module(shaders::kMeshFragSpv, sizeof(shaders::kMeshFragSpv), frag); r != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vert, nullptr);
        return vk_error(r, "vkCreateShaderModule(fragment)");
    }

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    VkResult r = vkCreatePipelineLayout(device_, &layout, nullptr, &layout_);

    if (r == VK_SUCCESS) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[4] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        };
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 4;
        vertex_input.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        // The projection flips Y, so faces that wind CCW in world space
        // (seen from outside) are also CCW on screen.
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &scene_format_;
        rendering.depthAttachmentFormat = kDepthFormat;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertex_input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &msaa;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout_;
        r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
        if (r == VK_SUCCESS) {
            // Ground grid: same vertex buffer layout (positions only used),
            // its own shaders, alpha-blended, depth-tested but not written.
            VkShaderModule grid_vert = VK_NULL_HANDLE, grid_frag = VK_NULL_HANDLE;
            r = make_module(shaders::kGridVertSpv, sizeof(shaders::kGridVertSpv), grid_vert);
            if (r == VK_SUCCESS) r = make_module(shaders::kGridFragSpv, sizeof(shaders::kGridFragSpv), grid_frag);
            if (r == VK_SUCCESS) {
                stages[0].module = grid_vert;
                stages[1].module = grid_frag;
                raster.cullMode = VK_CULL_MODE_NONE;
                vertex_input.vertexAttributeDescriptionCount = 1;  // positions only
                depth.depthWriteEnable = VK_FALSE;
                depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                blend_attachment.blendEnable = VK_TRUE;
                blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
                blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
                r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &grid_pipeline_);
            }
            if (grid_vert) vkDestroyShaderModule(device_, grid_vert, nullptr);
            if (grid_frag) vkDestroyShaderModule(device_, grid_frag, nullptr);
        }
        if (r == VK_SUCCESS) {
            // Shadow map: depth only, both faces (open meshes still cast),
            // slope-scaled bias against acne on steep surfaces.
            VkShaderModule shadow_vert = VK_NULL_HANDLE;
            r = make_module(shaders::kShadowVertSpv, sizeof(shaders::kShadowVertSpv), shadow_vert);
            if (r == VK_SUCCESS) {
                stages[0].module = shadow_vert;
                info.stageCount = 1;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.depthBiasEnable = VK_TRUE;
                raster.depthBiasConstantFactor = 0.0f;
                raster.depthBiasSlopeFactor = 1.5f;
                depth.depthWriteEnable = VK_TRUE;
                depth.depthCompareOp = VK_COMPARE_OP_LESS;
                blend.attachmentCount = 0;
                VkPipelineRenderingCreateInfo shadow_rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                shadow_rendering.depthAttachmentFormat = kShadowFormat;
                info.pNext = &shadow_rendering;
                r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &shadow_pipeline_);
            }
            if (shadow_vert) vkDestroyShaderModule(device_, shadow_vert, nullptr);
        }
        if (r == VK_SUCCESS) {
            // Sky: a full-screen triangle at the far plane, drawn where no
            // geometry is (depth test equal-or-less against the cleared 1.0).
            VkShaderModule sky_vert = VK_NULL_HANDLE, sky_frag = VK_NULL_HANDLE;
            r = make_module(shaders::kSkyVertSpv, sizeof(shaders::kSkyVertSpv), sky_vert);
            if (r == VK_SUCCESS) r = make_module(shaders::kSkyFragSpv, sizeof(shaders::kSkyFragSpv), sky_frag);
            if (r == VK_SUCCESS) {
                stages[0].module = sky_vert;
                stages[1].module = sky_frag;
                info.stageCount = 2;
                VkPipelineVertexInputStateCreateInfo no_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                info.pVertexInputState = &no_input;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.depthBiasEnable = VK_FALSE;
                depth.depthWriteEnable = VK_FALSE;
                depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                blend_attachment.blendEnable = VK_FALSE;
                blend.attachmentCount = 1;
                info.pNext = &rendering;
                r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &sky_pipeline_);
            }
            if (sky_vert) vkDestroyShaderModule(device_, sky_vert, nullptr);
            if (sky_frag) vkDestroyShaderModule(device_, sky_frag, nullptr);
        }
    }
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    if (r != VK_SUCCESS) return vk_error(r, "pipeline creation");
    return ok();
}

// ---- Resources -------------------------------------------------------------

std::optional<std::uint32_t> VulkanRenderer::find_memory(std::uint32_t type_bits, VkMemoryPropertyFlags props) const {
    for (std::uint32_t i = 0; i < memory_props_.memoryTypeCount; ++i)
        if ((type_bits & (1u << i)) && (memory_props_.memoryTypes[i].propertyFlags & props) == props) return i;
    return std::nullopt;
}

Result<Buffer> VulkanRenderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer b;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    AETHER_VK_TRY(vkCreateBuffer(device_, &info, nullptr, &b.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    // Host-visible and coherent: simple and correct; a staging path to
    // device-local memory is a planned optimisation.
    const auto type = find_memory(req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!type) {
        destroy_buffer(b);
        return Error{ErrorCode::Unsupported, "no host-visible coherent memory type"};
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = *type;
    if (const VkResult r = vkAllocateMemory(device_, &alloc, nullptr, &b.memory); r != VK_SUCCESS) {
        destroy_buffer(b);
        return vk_error(r, "vkAllocateMemory(buffer)");
    }
    vkBindBufferMemory(device_, b.buffer, b.memory, 0);
    if (const VkResult r = vkMapMemory(device_, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped); r != VK_SUCCESS) {
        destroy_buffer(b);
        return vk_error(r, "vkMapMemory");
    }
    return b;
}

void VulkanRenderer::destroy_buffer(Buffer& b) {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device_, b.memory, nullptr);  // implicitly unmaps
    b = {};
}

Result<Image> VulkanRenderer::create_image(std::uint32_t w, std::uint32_t h, VkFormat format,
                                           VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image img;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    AETHER_VK_TRY(vkCreateImage(device_, &info, nullptr, &img.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, img.image, &req);
    auto type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!type) type = find_memory(req.memoryTypeBits, 0);
    if (!type) {
        destroy_image(img);
        return Error{ErrorCode::Unsupported, "no memory type for image"};
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = *type;
    if (const VkResult r = vkAllocateMemory(device_, &alloc, nullptr, &img.memory); r != VK_SUCCESS) {
        destroy_image(img);
        return vk_error(r, "vkAllocateMemory(image)");
    }
    vkBindImageMemory(device_, img.image, img.memory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = img.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    if (const VkResult r = vkCreateImageView(device_, &view, nullptr, &img.view); r != VK_SUCCESS) {
        destroy_image(img);
        return vk_error(r, "vkCreateImageView");
    }
    return img;
}

void VulkanRenderer::destroy_image(Image& img) {
    if (img.view) vkDestroyImageView(device_, img.view, nullptr);
    if (img.image) vkDestroyImage(device_, img.image, nullptr);
    if (img.memory) vkFreeMemory(device_, img.memory, nullptr);
    img = {};
}

Result<void> VulkanRenderer::create_targets() {
    if (headless()) {
        extent_ = requested_extent_;
        auto color = create_image(extent_.width, extent_.height, kOffscreenFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT);
        if (!color) return color.error();
        offscreen_ = color.value();
        auto rb = create_buffer(VkDeviceSize{extent_.width} * extent_.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        if (!rb) return rb.error();
        readback_ = rb.value();
        has_frame_ = false;
    } else {
        AETHER_TRY(create_swapchain());
        if (extent_.width == 0 || extent_.height == 0) return ok();  // minimised
    }
    if (ui_) return ok();  // the viewport target owns the scene depth buffer
    auto depth = create_image(extent_.width, extent_.height, kDepthFormat,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depth) return depth.error();
    depth_ = depth.value();
    return ok();
}

void VulkanRenderer::destroy_targets() {
    if (!ui_) destroy_image(depth_);
    destroy_image(offscreen_);
    destroy_buffer(readback_);
    for (VkImageView v : swap_views_) vkDestroyImageView(device_, v, nullptr);
    for (VkSemaphore s : render_finished_) vkDestroySemaphore(device_, s, nullptr);
    swap_views_.clear();
    render_finished_.clear();
    swap_images_.clear();
}

Result<void> VulkanRenderer::create_grid() {
    const float e = kGridHalfExtent;
    const math::Vec3 up{0, 1, 0};
    const Vertex quad[6] = {{{-e, 0, -e}, up}, {{e, 0, -e}, up}, {{e, 0, e}, up},
                            {{-e, 0, -e}, up}, {{e, 0, e}, up}, {{-e, 0, e}, up}};
    auto buf = create_buffer(sizeof(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!buf) return buf.error();
    grid_ = buf.value();
    std::memcpy(grid_.mapped, quad, sizeof(quad));
    return ok();
}

Result<void> VulkanRenderer::init_ui() {
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 1.0f;
    AETHER_VK_TRY(vkCreateSampler(device_, &sampler, nullptr, &sampler_));

    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 64;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    AETHER_VK_TRY(vkCreateDescriptorPool(device_, &pool, nullptr, &ui_pool_));

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = instance_;
    info.PhysicalDevice = physical_;
    info.Device = device_;
    info.QueueFamily = queue_family_;
    info.Queue = queue_;
    info.DescriptorPool = ui_pool_;
    info.MinImageCount = 2;
    info.ImageCount = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(swap_images_.size()));
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format_;
    if (!ImGui_ImplVulkan_Init(&info)) return Error{ErrorCode::Unknown, "ImGui Vulkan backend failed to initialise"};
    if (!ImGui_ImplVulkan_CreateFontsTexture()) return Error{ErrorCode::Unknown, "ImGui font upload failed"};
    return create_viewport_target();
}

Result<void> VulkanRenderer::create_viewport_target() {
    const std::uint32_t w = std::max(16u, pending_viewport_.width);
    const std::uint32_t h = std::max(16u, pending_viewport_.height);
    viewport_extent_ = {w, h};

    // Created by hand (not create_image) because it needs MUTABLE_FORMAT for
    // the second, UNORM view.
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kViewportRenderFormat;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    AETHER_VK_TRY(vkCreateImage(device_, &info, nullptr, &viewport_.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, viewport_.image, &req);
    auto type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!type) type = find_memory(req.memoryTypeBits, 0);
    if (!type) return Error{ErrorCode::Unsupported, "no memory type for viewport image"};
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = *type;
    AETHER_VK_TRY(vkAllocateMemory(device_, &alloc, nullptr, &viewport_.memory));
    vkBindImageMemory(device_, viewport_.image, viewport_.memory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = viewport_.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = kViewportRenderFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    AETHER_VK_TRY(vkCreateImageView(device_, &view, nullptr, &viewport_.view));
    view.format = kViewportSampleFormat;
    AETHER_VK_TRY(vkCreateImageView(device_, &view, nullptr, &viewport_sample_view_));

    auto depth = create_image(w, h, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depth) return depth.error();
    depth_ = depth.value();

    viewport_set_ = ImGui_ImplVulkan_AddTexture(sampler_, viewport_sample_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (viewport_set_ == VK_NULL_HANDLE) return Error{ErrorCode::Unknown, "could not register viewport texture"};

    // Put the image in its sampled layout so a UI frame that draws it before
    // the first scene render is still valid.
    Frame& f = frames_[frame_index_];
    AETHER_VK_TRY(vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX));
    VkCommandBuffer cmd = f.cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    image_barrier(cmd, viewport_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vkEndCommandBuffer(cmd);
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    AETHER_VK_TRY(vkResetFences(device_, 1, &f.fence));
    AETHER_VK_TRY(vkQueueSubmit2(queue_, 1, &submit, f.fence));
    AETHER_VK_TRY(vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX));
    return ok();
}

void VulkanRenderer::destroy_viewport_target() {
    if (viewport_set_) ImGui_ImplVulkan_RemoveTexture(viewport_set_);
    viewport_set_ = VK_NULL_HANDLE;
    if (viewport_sample_view_) vkDestroyImageView(device_, viewport_sample_view_, nullptr);
    viewport_sample_view_ = VK_NULL_HANDLE;
    destroy_image(viewport_);
    destroy_image(depth_);
}

void VulkanRenderer::begin_ui_frame() {
    if (ui_) ImGui_ImplVulkan_NewFrame();
}

void VulkanRenderer::set_viewport_size(std::uint32_t w, std::uint32_t h) {
    pending_viewport_ = {std::max(16u, w), std::max(16u, h)};
}

std::uint64_t VulkanRenderer::viewport_texture() const noexcept {
    std::uint64_t id = 0;
    static_assert(sizeof(VkDescriptorSet) == sizeof(std::uint64_t), "64-bit targets only");
    std::memcpy(&id, &viewport_set_, sizeof(id));
    return id;
}

Result<void> VulkanRenderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    AETHER_VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(requested_extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(requested_extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    extent_ = extent;
    if (extent.width == 0 || extent.height == 0) return ok();  // minimised: try again later

    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always available, vsync
    if (!desc_.vsync) {
        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, modes.data());
        for (VkPresentModeKHR m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) { mode = m; break; }
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
        }
    }

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = color_format_;
    info.imageColorSpace = color_space_;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;
    VkSwapchainKHR fresh = VK_NULL_HANDLE;
    AETHER_VK_TRY(vkCreateSwapchainKHR(device_, &info, nullptr, &fresh));
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = fresh;

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swap_images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swap_images_.data());
    for (VkImage image : swap_images_) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = color_format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v = VK_NULL_HANDLE;
        AETHER_VK_TRY(vkCreateImageView(device_, &view, nullptr, &v));
        swap_views_.push_back(v);
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s = VK_NULL_HANDLE;
        AETHER_VK_TRY(vkCreateSemaphore(device_, &sem, nullptr, &s));
        render_finished_.push_back(s);
    }
    swapchain_dirty_ = false;
    return ok();
}

Result<void> VulkanRenderer::create_descriptors() {
    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = kMaxTextures;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 4;
    layout.pBindings = bindings;
    AETHER_VK_TRY(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &set_layout_));

    const VkDescriptorPoolSize sizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (kMaxTextures + 2) * kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = kFramesInFlight;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    AETHER_VK_TRY(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptor_pool_));

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.anisotropyEnable = max_anisotropy_ > 0.0f ? VK_TRUE : VK_FALSE;
    sampler.maxAnisotropy = std::max(1.0f, max_anisotropy_);
    AETHER_VK_TRY(vkCreateSampler(device_, &sampler, nullptr, &texture_sampler_));

    // Hardware depth comparison with bilinear filtering (2x2 PCF per tap).
    // Outside the map counts as lit.
    VkSamplerCreateInfo shadow{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    shadow.magFilter = VK_FILTER_LINEAR;
    shadow.minFilter = VK_FILTER_LINEAR;
    shadow.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadow.addressModeU = shadow.addressModeV = shadow.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    shadow.compareEnable = VK_TRUE;
    shadow.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    AETHER_VK_TRY(vkCreateSampler(device_, &shadow, nullptr, &shadow_sampler_));

    const auto allocate = [&](VkImage image, VkDeviceMemory& memory) -> Result<void> {
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device_, image, &req);
        auto type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!type) type = find_memory(req.memoryTypeBits, 0);
        if (!type) return Error{ErrorCode::Unsupported, "no memory type for image"};
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = *type;
        AETHER_VK_TRY(vkAllocateMemory(device_, &alloc, nullptr, &memory));
        AETHER_VK_TRY(vkBindImageMemory(device_, image, memory, 0));
        return ok();
    };

    VkImageCreateInfo shadow_image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    shadow_image.imageType = VK_IMAGE_TYPE_2D;
    shadow_image.format = kShadowFormat;
    shadow_image.extent = {kShadowMapSize, kShadowMapSize, 1};
    shadow_image.mipLevels = 1;
    shadow_image.arrayLayers = kShadowCascades;
    shadow_image.samples = VK_SAMPLE_COUNT_1_BIT;
    shadow_image.tiling = VK_IMAGE_TILING_OPTIMAL;
    shadow_image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    AETHER_VK_TRY(vkCreateImage(device_, &shadow_image, nullptr, &shadow_image_));
    AETHER_TRY(allocate(shadow_image_, shadow_memory_));
    VkImageViewCreateInfo shadow_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    shadow_view.image = shadow_image_;
    shadow_view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    shadow_view.format = kShadowFormat;
    shadow_view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
    AETHER_VK_TRY(vkCreateImageView(device_, &shadow_view, nullptr, &shadow_array_view_));
    for (std::uint32_t c = 0; c < kShadowCascades; ++c) {
        shadow_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        shadow_view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, c, 1};
        AETHER_VK_TRY(vkCreateImageView(device_, &shadow_view, nullptr, &shadow_layer_views_[c]));
    }

    VkImageCreateInfo sky_image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    sky_image.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    sky_image.imageType = VK_IMAGE_TYPE_2D;
    sky_image.format = kSkyFormat;
    sky_image.extent = {kSkyCubeSize, kSkyCubeSize, 1};
    sky_image.mipLevels = kSkyCubeMips;
    sky_image.arrayLayers = 6;
    sky_image.samples = VK_SAMPLE_COUNT_1_BIT;
    sky_image.tiling = VK_IMAGE_TILING_OPTIMAL;
    sky_image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    AETHER_VK_TRY(vkCreateImage(device_, &sky_image, nullptr, &sky_image_));
    AETHER_TRY(allocate(sky_image_, sky_memory_));
    VkImageViewCreateInfo sky_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    sky_view.image = sky_image_;
    sky_view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    sky_view.format = kSkyFormat;
    sky_view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kSkyCubeMips, 0, 6};
    AETHER_VK_TRY(vkCreateImageView(device_, &sky_view, nullptr, &sky_view_));
    VkSamplerCreateInfo sky_sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sky_sampler.magFilter = VK_FILTER_LINEAR;
    sky_sampler.minFilter = VK_FILTER_LINEAR;
    sky_sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sky_sampler.addressModeU = sky_sampler.addressModeV = sky_sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sky_sampler.maxLod = VK_LOD_CLAMP_NONE;
    AETHER_VK_TRY(vkCreateSampler(device_, &sky_sampler, nullptr, &sky_sampler_));
    std::size_t sky_texels = 0;
    for (std::uint32_t mip = 0; mip < kSkyCubeMips; ++mip) sky_texels += std::size_t{6} * (kSkyCubeSize >> mip) * (kSkyCubeSize >> mip);

    for (Frame& f : frames_) {
        auto ubo = create_buffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        if (!ubo) return ubo.error();
        f.uniforms = ubo.value();
        auto staging = create_buffer(sky_texels * 4 * sizeof(std::uint16_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (!staging) return staging.error();
        f.sky_staging = staging.value();
        std::memset(f.uniforms.mapped, 0, sizeof(FrameUniforms));
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = descriptor_pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &set_layout_;
        AETHER_VK_TRY(vkAllocateDescriptorSets(device_, &alloc, &f.set));
        const VkDescriptorBufferInfo buffer_info{f.uniforms.buffer, 0, sizeof(FrameUniforms)};
        const VkDescriptorImageInfo shadow_info{shadow_sampler_, shadow_array_view_, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo sky_info{sky_sampler_, sky_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[3]{};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[2].dstSet = f.set;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &sky_info;
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = f.set;
        writes[0].dstBinding = 1;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &buffer_info;
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = f.set;
        writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &shadow_info;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
    }
    return ok();
}

void VulkanRenderer::write_texture_descriptor(std::uint32_t slot, VkImageView view) {
    const VkDescriptorImageInfo info{texture_sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (Frame& f : frames_) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = f.set;
        write.dstBinding = 0;
        write.dstArrayElement = slot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

Result<TextureId> VulkanRenderer::upload_texture(const TextureData& t) {
    if (t.width == 0 || t.height == 0) return Error{ErrorCode::InvalidArgument, "texture has no pixels"};
    if (t.width > max_texture_size_ || t.height > max_texture_size_)
        return Error{ErrorCode::InvalidArgument, "texture exceeds the GPU size limit"};
    const VkDeviceSize bytes = VkDeviceSize{t.width} * t.height * 4;
    if (t.rgba.size() != bytes) return Error{ErrorCode::InvalidArgument, "texture pixel data has the wrong size"};
    if (textures_.size() >= kMaxTextures)
        return Error{ErrorCode::OutOfMemory, "texture limit reached (" + std::to_string(kMaxTextures) + ")"};

    const VkFormat format = t.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(physical_, format, &fp);
    constexpr VkFormatFeatureFlags kBlit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const bool can_blit = (fp.optimalTilingFeatures & kBlit) == kBlit;
    std::uint32_t mips = 1;
    if (can_blit)
        for (std::uint32_t s = std::max(t.width, t.height); s > 1; s >>= 1) ++mips;

    GpuTexture tex;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {t.width, t.height, 1};
    info.mipLevels = mips;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    AETHER_VK_TRY(vkCreateImage(device_, &info, nullptr, &tex.image));
    const auto cleanup = [&] {
        if (tex.view) vkDestroyImageView(device_, tex.view, nullptr);
        if (tex.image) vkDestroyImage(device_, tex.image, nullptr);
        if (tex.memory) vkFreeMemory(device_, tex.memory, nullptr);
    };
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, tex.image, &req);
    auto type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!type) type = find_memory(req.memoryTypeBits, 0);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type.value_or(0);
    if (!type || vkAllocateMemory(device_, &alloc, nullptr, &tex.memory) != VK_SUCCESS) {
        cleanup();
        return Error{ErrorCode::OutOfMemory, "cannot allocate texture memory"};
    }
    vkBindImageMemory(device_, tex.image, tex.memory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = tex.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    if (vkCreateImageView(device_, &view, nullptr, &tex.view) != VK_SUCCESS) {
        cleanup();
        return Error{ErrorCode::Unknown, "cannot create texture view"};
    }

    auto staging = create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging) {
        cleanup();
        return staging.error();
    }
    Buffer stage = staging.value();
    std::memcpy(stage.mapped, t.rgba.data(), static_cast<std::size_t>(bytes));

    VkCommandBufferAllocateInfo cb_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cb_alloc.commandPool = pool_;
    cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkAllocateCommandBuffers(device_, &cb_alloc, &cmd) != VK_SUCCESS ||
        vkCreateFence(device_, &fence_info, nullptr, &fence) != VK_SUCCESS) {
        if (cmd) vkFreeCommandBuffers(device_, pool_, 1, &cmd);
        destroy_buffer(stage);
        cleanup();
        return Error{ErrorCode::Unknown, "cannot record texture upload"};
    }
    const auto barrier = [&](std::uint32_t base, std::uint32_t count, VkImageLayout from, VkImageLayout to,
                             VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                             VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = src_stage;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stage;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = tex.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, base, count, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    };
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    barrier(0, mips, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE,
            VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {t.width, t.height, 1};
    vkCmdCopyBufferToImage(cmd, stage.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    std::int32_t w = static_cast<std::int32_t>(t.width), h = static_cast<std::int32_t>(t.height);
    for (std::uint32_t level = 1; level < mips; ++level) {
        barrier(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {w, h, 1};
        vkCmdBlitImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }
    if (mips > 1)
        barrier(0, mips - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    barrier(mips - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vkEndCommandBuffer(cmd);
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    VkResult r = vkQueueSubmit2(queue_, 1, &submit, fence);
    if (r == VK_SUCCESS) r = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    destroy_buffer(stage);
    if (r != VK_SUCCESS) {
        cleanup();
        return vk_error(r, "texture upload");
    }

    // The descriptor sets may be in use by frames still in flight.
    vkDeviceWaitIdle(device_);
    const auto id = static_cast<TextureId>(textures_.size());
    textures_.push_back(tex);
    if (id == kWhiteTexture) {
        for (std::uint32_t slot = 0; slot < kMaxTextures; ++slot) write_texture_descriptor(slot, tex.view);
    } else {
        write_texture_descriptor(id, tex.view);
    }
    return id;
}

Result<MeshId> VulkanRenderer::upload_mesh(const MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty())
        return Error{ErrorCode::InvalidArgument, "mesh has no vertices or indices"};
    for (const std::uint32_t i : mesh.indices)
        if (i >= mesh.vertices.size()) return Error{ErrorCode::InvalidArgument, "mesh index out of range"};
    GpuMesh gpu;
    const VkDeviceSize vbytes = mesh.vertices.size() * sizeof(Vertex);
    const VkDeviceSize ibytes = mesh.indices.size() * sizeof(std::uint32_t);
    auto vb = create_buffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!vb) return vb.error();
    gpu.vertices = vb.value();
    auto ib = create_buffer(ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!ib) {
        destroy_buffer(gpu.vertices);
        return ib.error();
    }
    gpu.indices = ib.value();
    std::memcpy(gpu.vertices.mapped, mesh.vertices.data(), vbytes);
    std::memcpy(gpu.indices.mapped, mesh.indices.data(), ibytes);
    gpu.index_count = static_cast<std::uint32_t>(mesh.indices.size());
    gpu.bounds = compute_bounds(mesh);
    meshes_.push_back(gpu);
    return static_cast<MeshId>(meshes_.size() - 1);
}

void VulkanRenderer::resize(std::uint32_t w, std::uint32_t h) {
    if (w == requested_extent_.width && h == requested_extent_.height) return;
    requested_extent_ = {w, h};
    swapchain_dirty_ = true;
}

// ---- Frame -----------------------------------------------------------------

void VulkanRenderer::image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                                   VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                                   VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                                   VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void VulkanRenderer::prepare_frame(world::World& world, const Camera& camera, const RenderOptions& options,
                                   VkExtent2D extent, FrameStats& stats) {
    using math::Vec3;
    draws_.clear();
    world.each<const world::GlobalTransform, const MeshInstance>(
        [&](world::Entity entity, const world::GlobalTransform& g, const MeshInstance& inst) {
            if (inst.mesh < meshes_.size()) draws_.push_back(Draw{entity, g.matrix, inst});
        });

    // Lights. The sun is the first directional light (preferring one that
    // casts shadows); a world without any light gets a default sun.
    struct Point {
        Vec3 position;
        Light light;
        float distance2;
    };
    std::vector<Point> points;
    std::optional<std::pair<Vec3, Light>> sun;  // direction toward the light, light
    bool any_light = false;
    world.each<const world::GlobalTransform, const Light>(
        [&](world::Entity, const world::GlobalTransform& g, const Light& light) {
            any_light = true;
            if (light.type == LightType::Directional) {
                // The light travels along local -Z, so it comes from +Z.
                const Vec3 z{g.matrix.m[8], g.matrix.m[9], g.matrix.m[10]};
                if (math::length(z) <= 0) return;
                if (!sun || (light.cast_shadows && !sun->second.cast_shadows)) sun.emplace(math::normalize(z), light);
            } else {
                const Vec3 pos{g.matrix.m[12], g.matrix.m[13], g.matrix.m[14]};
                const Vec3 d = pos - camera.position;
                points.push_back({pos, light, math::dot(d, d)});
            }
        });
    if (!any_light) {
        Light fallback;
        fallback.type = LightType::Directional;
        fallback.color = {1.0f, 0.96f, 0.9f};
        fallback.intensity = 3.0f;
        sun.emplace(math::normalize(Vec3{0.4f, 0.8f, 0.6f}), fallback);
    }
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.distance2 < b.distance2; });
    if (points.size() > kMaxPointLights) points.resize(kMaxPointLights);

    // Environment: the level's own, else the caller's.
    Environment env = options.environment;
    bool found_env = false;
    world.each<const Environment>([&](const Environment& e) {
        if (!found_env) env = e;
        found_env = true;
    });
    draw_sky_ = env.sky_visible;
    stats.sky = draw_sky_;
    // Re-bake the sky light when anything that affects it changed.
    Environment bake_key = env;
    bake_key.exposure = 1.0f;
    bake_key.shadows = true;
    bake_key.sky_visible = false;
    if (!baked_environment_ || !(*baked_environment_ == bake_key)) {
        const SkyBake bake = bake_sky(bake_key, kSkyCubeSize, kSkyCubeMips);
        std::memcpy(frames_[frame_index_].sky_staging.mapped, bake.cube.data(), bake.cube.size() * sizeof(std::uint16_t));
        sky_irradiance_ = bake.irradiance;
        baked_environment_ = bake_key;
        sky_upload_pending_ = true;
    }

    FrameUniforms u{};
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const math::Mat4 view_proj = view_projection(camera, aspect);
    std::memcpy(u.view_proj, view_proj.m, sizeof(u.view_proj));
    math::Mat4 inv_view_proj;
    (void)math::inverse(view_proj, inv_view_proj);  // identity if singular: the sky is then just wrong, not undefined
    std::memcpy(u.inv_view_proj, inv_view_proj.m, sizeof(u.inv_view_proj));
    Vec3 cam_forward = camera.target - camera.position;
    cam_forward = math::length(cam_forward) > 0 ? math::normalize(cam_forward) : Vec3{0, 0, -1};
    set4(u.camera_position, camera.position, env.exposure);
    set4(u.camera_forward, cam_forward, 0);
    const float ambient = std::max(0.0f, env.ambient_intensity);
    set4(u.sky_zenith, env.sky_color * ambient, 0);
    set4(u.sky_horizon, env.horizon_color * ambient, 0);
    set4(u.sky_ground, env.ground_color * ambient, 0);
    for (std::size_t k = 0; k < 9; ++k) set4(u.irradiance_sh[k], sky_irradiance_[k], 0);
    u.counts[0] = static_cast<std::uint32_t>(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        set4(u.points[i].position_range, points[i].position, std::max(0.01f, points[i].light.range));
        set4(u.points[i].color, points[i].light.color * std::max(0.0f, points[i].light.intensity), 0);
    }
    stats.point_lights = static_cast<std::uint32_t>(points.size());

    sun_shadows_ = false;
    if (sun) {
        const Vec3 to_sun = sun->first;
        set4(u.sun_color, sun->second.color * std::max(0.0f, sun->second.intensity), 1.0f);
        if (sun->second.cast_shadows && env.shadows && !draws_.empty()) {
            // Light space: x = right, y = up, z = along the light.
            const Vec3 forward = to_sun * -1.0f;
            const Vec3 helper = std::abs(forward.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            const Vec3 right = math::normalize(math::cross(forward, helper));
            const Vec3 up = math::cross(right, forward);
            light_right_ = right;
            light_up_ = up;
            // Depth range: from the caster nearest the light to the farthest.
            float z_near = std::numeric_limits<float>::infinity(), z_far = -z_near;
            for (const Draw& d : draws_) {
                const Aabb& b = meshes_[d.instance.mesh].bounds;
                for (int c = 0; c < 8; ++c) {
                    const float z = math::dot(d.model.transform_point({(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y,
                                                                       (c & 4) ? b.max.z : b.min.z}),
                                              forward);
                    z_near = std::min(z_near, z);
                    z_far = std::max(z_far, z);
                }
            }
            z_near -= 1.0f;
            z_far += 1.0f;

            // Camera basis for the frustum slices.
            Vec3 cam_right = math::cross(cam_forward, camera.up);
            cam_right = math::length(cam_right) > 0 ? math::normalize(cam_right) : Vec3{1, 0, 0};
            const Vec3 cam_up = math::cross(cam_right, cam_forward);
            const float tan_y = std::tan(camera.fov_y * 0.5f), tan_x = tan_y * aspect;
            const float near_d = std::max(camera.near_plane, 0.01f);
            const float far_d = std::max(near_d * 2.0f, std::min(camera.far_plane, kShadowDistance));
            float slice_near = near_d;
            for (std::uint32_t c = 0; c < kShadowCascades; ++c) {
                const float t = static_cast<float>(c + 1) / static_cast<float>(kShadowCascades);
                const float slice_far = kCascadeLambda * near_d * std::pow(far_d / near_d, t) +
                                        (1.0f - kCascadeLambda) * (near_d + (far_d - near_d) * t);
                // Bounding sphere of the slice: its size does not change as
                // the camera turns, which keeps the shadows steady.
                Vec3 corners[8];
                Vec3 centre{0, 0, 0};
                for (int k = 0; k < 8; ++k) {
                    const float d = (k & 4) ? slice_far : slice_near;
                    corners[k] = camera.position + cam_forward * d + cam_right * (((k & 1) ? 1.0f : -1.0f) * tan_x * d) +
                                 cam_up * (((k & 2) ? 1.0f : -1.0f) * tan_y * d);
                    centre = centre + corners[k] * 0.125f;
                }
                float radius = 0.0f;
                for (const Vec3& corner : corners) radius = std::max(radius, math::length(corner - centre));
                radius = std::ceil(radius * 16.0f) / 16.0f;
                const float texel = 2.0f * radius / static_cast<float>(kShadowMapSize);
                // Snap the centre to whole texels so edges do not shimmer.
                const float cx = std::round(math::dot(centre, right) / texel) * texel;
                const float cy = std::round(math::dot(centre, up) / texel) * texel;
                const float depth_range = std::max(z_far - z_near, 1.0f);
                math::Mat4 m;
                const Vec3 rows[3] = {right * (1.0f / radius), up * (1.0f / radius), forward * (1.0f / depth_range)};
                const float offsets[3] = {-cx / radius, -cy / radius, -z_near / depth_range};
                for (std::size_t r = 0; r < 3; ++r) {
                    m.at(0, r) = rows[r].x;
                    m.at(1, r) = rows[r].y;
                    m.at(2, r) = rows[r].z;
                    m.at(3, r) = offsets[r];
                }
                std::memcpy(u.light_view_proj[c], m.m, sizeof(u.light_view_proj[c]));
                u.cascade_splits[c] = slice_far;
                u.cascade_texels[c] = texel;
                u.cascade_bias[c] = texel * 0.5f / depth_range;
                cascade_rects_[c] = {cx, cy, radius};
                slice_near = slice_far;
            }
            sun_shadows_ = true;
        }
        set4(u.sun_direction, to_sun, sun_shadows_ ? 1.0f : 0.0f);
    }
    stats.sun_shadows = sun_shadows_;
    std::memcpy(frames_[frame_index_].uniforms.mapped, &u, sizeof(u));
}

namespace {

PushConstants push_for(const Draw& d, const RenderOptions& options) {
    PushConstants pc{};
    std::memcpy(pc.model, d.model.m, sizeof(pc.model));
    const MeshInstance& m = d.instance;
    set4(pc.base_color, m.color, d.entity == options.highlight ? 1.0f : 0.0f);
    set4(pc.emissive_rough, m.emissive, m.roughness);
    pc.material[0] = m.texture;
    pc.material[1] = m.metallic_roughness_texture;
    pc.material[2] = m.normal_texture;
    std::memcpy(&pc.material[3], &m.metallic, sizeof(float));
    return pc;
}

}  // namespace

void VulkanRenderer::record_sky_upload(VkCommandBuffer cmd) {
    if (!sky_upload_pending_) return;
    sky_upload_pending_ = false;
    const auto barrier = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = src_stage;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stage;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = sky_image_;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kSkyCubeMips, 0, 6};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    };
    // The previous frame may still be sampling the cube; its contents are replaced.
    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            sky_image_initialised_ ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE,
            sky_image_initialised_ ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
    std::array<VkBufferImageCopy, kSkyCubeMips> regions{};
    VkDeviceSize offset = 0;
    for (std::uint32_t mip = 0; mip < kSkyCubeMips; ++mip) {
        const std::uint32_t edge = kSkyCubeSize >> mip;
        regions[mip].bufferOffset = offset;
        regions[mip].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 6};
        regions[mip].imageExtent = {edge, edge, 1};
        offset += VkDeviceSize{6} * edge * edge * 4 * sizeof(std::uint16_t);
    }
    vkCmdCopyBufferToImage(cmd, frames_[frame_index_].sky_staging.buffer, sky_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           kSkyCubeMips, regions.data());
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    sky_image_initialised_ = true;
}

void VulkanRenderer::record_shadows(VkCommandBuffer cmd, FrameStats& stats) {
    constexpr VkPipelineStageFlags2 kDepthStages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    const auto barrier = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = src_stage;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stage;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = shadow_image_;
        b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    };
    // The previous frame's scene pass may still be sampling the cascades.
    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_NONE, kDepthStages,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // World-space bounding spheres of the casters, for per-cascade culling.
    struct Sphere {
        float x, y, radius;  // light-space centre and radius
    };
    std::vector<Sphere> spheres;
    if (sun_shadows_) {
        spheres.reserve(draws_.size());
        for (const Draw& d : draws_) {
            const Aabb& b = meshes_[d.instance.mesh].bounds;
            const math::Vec3 centre = d.model.transform_point((b.min + b.max) * 0.5f);
            float radius = 0.0f;
            for (int c = 0; c < 8; ++c)
                radius = std::max(radius, math::length(d.model.transform_point({(c & 1) ? b.max.x : b.min.x,
                                                                                (c & 2) ? b.max.y : b.min.y,
                                                                                (c & 4) ? b.max.z : b.min.z}) -
                                                       centre));
            spheres.push_back({math::dot(centre, light_right_), math::dot(centre, light_up_), radius});
        }
    }

    for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade) {
        VkRenderingAttachmentInfo depth_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth_att.imageView = shadow_layer_views_[cascade];
        depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_att.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, {kShadowMapSize, kShadowMapSize}};
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth_att;
        vkCmdBeginRendering(cmd, &rendering);
        if (sun_shadows_) {
            const VkViewport viewport{0, 0, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0, 1};
            const VkRect2D scissor{{0, 0}, {kShadowMapSize, kShadowMapSize}};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frames_[frame_index_].set, 0,
                                    nullptr);
            MeshId bound = static_cast<MeshId>(-1);
            const RenderOptions no_highlight;
            const auto& rect = cascade_rects_[cascade];
            for (std::size_t i = 0; i < draws_.size(); ++i) {
                const Sphere& sphere = spheres[i];
                if (std::abs(sphere.x - rect[0]) > rect[2] + sphere.radius ||
                    std::abs(sphere.y - rect[1]) > rect[2] + sphere.radius)
                    continue;  // cannot touch this cascade
                const Draw& d = draws_[i];
                const GpuMesh& mesh = meshes_[d.instance.mesh];
                if (d.instance.mesh != bound) {
                    const VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
                    vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                    bound = d.instance.mesh;
                }
                PushConstants pc = push_for(d, no_highlight);
                pc.material[0] = cascade;
                vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
                ++stats.shadow_draws;
            }
        }
        vkCmdEndRendering(cmd);
    }
    barrier(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, kDepthStages,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void VulkanRenderer::record_scene(VkCommandBuffer cmd, VkImage color, VkImageView color_view, VkExtent2D extent,
                                  const RenderOptions& options, FrameStats& stats) {
    constexpr VkPipelineStageFlags2 kDepthStages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    // The previous frame may still be writing (or, in UI mode, sampling) this
    // image; waiting on those stages makes reuse safe.
    image_barrier(cmd, color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color_att.imageView = color_view;
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue.color = {{desc_.clear_color.x, desc_.clear_color.y, desc_.clear_color.z, 1.0f}};
    VkRenderingAttachmentInfo depth_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth_att.imageView = depth_.view;
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_att;
    rendering.pDepthAttachment = &depth_att;
    vkCmdBeginRendering(cmd, &rendering);

    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frames_[frame_index_].set, 0, nullptr);
    MeshId bound = static_cast<MeshId>(-1);
    const auto texture_or_white = [&](TextureId t) { return t < textures_.size() ? t : kWhiteTexture; };
    for (const Draw& d : draws_) {
        const GpuMesh& mesh = meshes_[d.instance.mesh];
        if (d.instance.mesh != bound) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            bound = d.instance.mesh;
        }
        PushConstants pc = push_for(d, options);
        for (int i = 0; i < 3; ++i) pc.material[i] = texture_or_white(pc.material[i]);
        vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
        ++stats.draws;
    }

    // Sky behind everything drawn so far.
    if (draw_sky_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline_);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Grid last: it is blended over opaque geometry and the sky.
    if (options.grid) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipeline_);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &grid_.buffer, &offset);
        const PushConstants pc{};
        vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);
}

Result<FrameStats> VulkanRenderer::render(world::World& world, const Camera& camera, const RenderOptions& options) {
    FrameStats stats;
    Frame& frame = frames_[frame_index_];
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (headless()) {
        if (swapchain_dirty_) {  // resize() of the offscreen target
            vkDeviceWaitIdle(device_);
            destroy_targets();
            AETHER_TRY(create_targets());
            swapchain_dirty_ = false;
        }
        AETHER_VK_TRY(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        AETHER_VK_TRY(vkResetFences(device_, 1, &frame.fence));
        vkResetCommandBuffer(frame.cmd, 0);
        vkBeginCommandBuffer(frame.cmd, &begin);
        prepare_frame(world, camera, options, extent_, stats);
        record_sky_upload(frame.cmd);
        record_shadows(frame.cmd, stats);
        record_scene(frame.cmd, offscreen_.image, offscreen_.view, extent_, options, stats);
        image_barrier(frame.cmd, offscreen_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(frame.cmd, offscreen_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buffer, 1,
                               &region);
        VkBufferMemoryBarrier2 to_host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        to_host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        to_host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        to_host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_host.buffer = readback_.buffer;
        to_host.size = VK_WHOLE_SIZE;
        VkDependencyInfo host_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        host_dep.bufferMemoryBarrierCount = 1;
        host_dep.pBufferMemoryBarriers = &to_host;
        vkCmdPipelineBarrier2(frame.cmd, &host_dep);
        vkEndCommandBuffer(frame.cmd);
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = frame.cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        AETHER_VK_TRY(vkQueueSubmit2(queue_, 1, &submit, frame.fence));
        // Headless frames are synchronous so read_pixels() sees this frame.
        AETHER_VK_TRY(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        has_frame_ = true;
        stats.presented = true;
        return stats;
    }

    if (swapchain_dirty_ || swapchain_ == VK_NULL_HANDLE || swap_images_.empty()) {
        vkDeviceWaitIdle(device_);
        destroy_targets();
        AETHER_TRY(create_targets());
        if (ui_) ImGui_ImplVulkan_SetMinImageCount(2);
        if (swap_images_.empty()) return stats;  // still minimised
    }

    AETHER_VK_TRY(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX));
    std::uint32_t image = 0;
    const VkResult acquired =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty_ = true;
        return stats;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) return vk_error(acquired, "vkAcquireNextImageKHR");
    // Reset only once work is certain to be submitted, or the next wait deadlocks.
    AETHER_VK_TRY(vkResetFences(device_, 1, &frame.fence));
    vkResetCommandBuffer(frame.cmd, 0);
    vkBeginCommandBuffer(frame.cmd, &begin);

    const VkExtent2D scene_extent = ui_ ? viewport_extent_ : extent_;
    prepare_frame(world, camera, options, scene_extent, stats);
    record_sky_upload(frame.cmd);
    record_shadows(frame.cmd, stats);
    if (!ui_) {
        record_scene(frame.cmd, swap_images_[image], swap_views_[image], extent_, options, stats);
    } else {
        // 1) Scene into the viewport texture, then make it sampleable.
        record_scene(frame.cmd, viewport_.image, viewport_.view, viewport_extent_, options, stats);
        image_barrier(frame.cmd, viewport_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        // 2) ImGui (which samples the viewport texture) into the swapchain.
        image_barrier(frame.cmd, swap_images_[image], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att.imageView = swap_views_[image];
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.clearValue.color = {{0.08f, 0.08f, 0.09f, 1.0f}};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, extent_};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &att;
        vkCmdBeginRendering(frame.cmd, &rendering);
        if (ImDrawData* draw = ImGui::GetDrawData()) ImGui_ImplVulkan_RenderDrawData(draw, frame.cmd);
        vkCmdEndRendering(frame.cmd);
    }
    image_barrier(frame.cmd, swap_images_[image], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    vkEndCommandBuffer(frame.cmd);

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = frame.image_available;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = render_finished_[image];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = frame.cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    AETHER_VK_TRY(vkQueueSubmit2(queue_, 1, &submit, frame.fence));

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_[image];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image;
    const VkResult presented = vkQueuePresentKHR(queue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR)
        swapchain_dirty_ = true;
    else if (presented != VK_SUCCESS)
        return vk_error(presented, "vkQueuePresentKHR");

    frame_index_ = (frame_index_ + 1) % kFramesInFlight;
    stats.presented = true;

    // Apply a viewport resize only now: this frame's UI referenced the old
    // texture, and waiting for idle makes it safe to destroy.
    if (ui_ && (pending_viewport_.width != viewport_extent_.width ||
                pending_viewport_.height != viewport_extent_.height)) {
        vkDeviceWaitIdle(device_);
        destroy_viewport_target();
        AETHER_TRY(create_viewport_target());
    }
    return stats;
}

Result<std::vector<std::uint8_t>> VulkanRenderer::read_pixels() {
    if (!headless()) return Error{ErrorCode::Unsupported, "read_pixels() is only available for headless renderers"};
    if (!has_frame_) return Error{ErrorCode::InvalidState, "no frame has been rendered yet"};
    const std::size_t bytes = std::size_t{extent_.width} * extent_.height * 4;
    const auto* src = static_cast<const std::uint8_t*>(readback_.mapped);
    return std::vector<std::uint8_t>(src, src + bytes);
}

VulkanRenderer::~VulkanRenderer() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        if (ui_ && ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData) {
            destroy_viewport_target();
            ImGui_ImplVulkan_Shutdown();
        } else if (ui_) {
            // ImGui was shut down first; its descriptor pool is ours and is
            // destroyed below. Free our own viewport resources.
            if (viewport_sample_view_) vkDestroyImageView(device_, viewport_sample_view_, nullptr);
            destroy_image(viewport_);
            destroy_image(depth_);
        }
        if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
        if (ui_pool_) vkDestroyDescriptorPool(device_, ui_pool_, nullptr);
        for (GpuMesh& m : meshes_) {
            destroy_buffer(m.vertices);
            destroy_buffer(m.indices);
        }
        destroy_buffer(grid_);
        for (GpuTexture& t : textures_) {
            if (t.view) vkDestroyImageView(device_, t.view, nullptr);
            if (t.image) vkDestroyImage(device_, t.image, nullptr);
            if (t.memory) vkFreeMemory(device_, t.memory, nullptr);
        }
        for (VkImageView v : shadow_layer_views_)
            if (v) vkDestroyImageView(device_, v, nullptr);
        if (shadow_array_view_) vkDestroyImageView(device_, shadow_array_view_, nullptr);
        if (shadow_image_) vkDestroyImage(device_, shadow_image_, nullptr);
        if (shadow_memory_) vkFreeMemory(device_, shadow_memory_, nullptr);
        if (sky_view_) vkDestroyImageView(device_, sky_view_, nullptr);
        if (sky_image_) vkDestroyImage(device_, sky_image_, nullptr);
        if (sky_memory_) vkFreeMemory(device_, sky_memory_, nullptr);
        if (sky_sampler_) vkDestroySampler(device_, sky_sampler_, nullptr);
        for (Frame& f : frames_) {
            destroy_buffer(f.uniforms);
            destroy_buffer(f.sky_staging);
        }
        if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        if (texture_sampler_) vkDestroySampler(device_, texture_sampler_, nullptr);
        if (shadow_sampler_) vkDestroySampler(device_, shadow_sampler_, nullptr);
        if (set_layout_) vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
        destroy_targets();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (grid_pipeline_) vkDestroyPipeline(device_, grid_pipeline_, nullptr);
        if (shadow_pipeline_) vkDestroyPipeline(device_, shadow_pipeline_, nullptr);
        if (sky_pipeline_) vkDestroyPipeline(device_, sky_pipeline_, nullptr);
        if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
        for (Frame& f : frames_) {
            if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
            if (f.image_available) vkDestroySemaphore(device_, f.image_available, nullptr);
        }
        if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_) {
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        vkDestroyInstance(instance_, nullptr);
    }
}

}  // namespace

Result<std::unique_ptr<Renderer>> Renderer::create(const RendererDesc& desc) {
    auto renderer = std::make_unique<VulkanRenderer>();
    if (auto r = renderer->init(desc); !r) return r.error();
    return std::unique_ptr<Renderer>(std::move(renderer));
}

}  // namespace aether::render
