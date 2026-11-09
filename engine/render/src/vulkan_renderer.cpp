// Vulkan 1.3 backend for aether::render::Renderer.
//
// Deliberately small: one graphics pipeline, dynamic rendering, two frames
// in flight, host-visible mesh buffers. It is the first working slice of
// the renderer, not the final architecture (render graph, GPU-driven
// culling and a real memory allocator are on the roadmap).
#include <volk.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "aether/render/renderer.hpp"
#include "aether/world/hierarchy.hpp"
#include "generated/shaders.inl"

namespace aether::render {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat kOffscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

struct PushConstants {
    float mvp[16];
    float normal0[4];
    float normal1[4];
    float normal2[4];
    float color[4];
};
static_assert(sizeof(PushConstants) == 128, "must fit the guaranteed push constant size");

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

struct GpuMesh {
    Buffer vertices;
    Buffer indices;
    std::uint32_t index_count = 0;
};

struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
};

class VulkanRenderer final : public Renderer {
public:
    ~VulkanRenderer() override;

    Result<void> init(const RendererDesc& desc);

    Result<MeshId> upload_mesh(const MeshData& mesh) override;
    void resize(std::uint32_t width, std::uint32_t height) override;
    Result<FrameStats> render(world::World& world, const Camera& camera) override;
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

    std::optional<std::uint32_t> find_memory(std::uint32_t type_bits, VkMemoryPropertyFlags props) const;
    Result<Buffer> create_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void destroy_buffer(Buffer& b);
    Result<Image> create_image(std::uint32_t w, std::uint32_t h, VkFormat format, VkImageUsageFlags usage,
                               VkImageAspectFlags aspect);
    void destroy_image(Image& img);

    void record(VkCommandBuffer cmd, VkImage color, VkImageView color_view, world::World& world,
                const Camera& camera, FrameStats& stats);

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
    VkFormat color_format_ = kOffscreenFormat;
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
    if (headless()) {
        color_format_ = kOffscreenFormat;
    } else {
        // Choose the surface format once; the pipeline is built against it.
        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
        if (formats.empty()) return Error{ErrorCode::Unsupported, "surface reports no formats"};
        color_format_ = formats[0].format;
        color_space_ = formats[0].colorSpace;
        for (const auto& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                color_format_ = f.format;
                color_space_ = f.colorSpace;
                break;
            }
    }
    AETHER_TRY(create_pipeline());
    AETHER_TRY(create_targets());
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

    const char* swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &f13;
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
        VkVertexInputAttributeDescription attrs[2] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        };
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 2;
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
        rendering.pColorAttachmentFormats = &color_format_;
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
    auto depth = create_image(extent_.width, extent_.height, kDepthFormat,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depth) return depth.error();
    depth_ = depth.value();
    return ok();
}

void VulkanRenderer::destroy_targets() {
    destroy_image(depth_);
    destroy_image(offscreen_);
    destroy_buffer(readback_);
    for (VkImageView v : swap_views_) vkDestroyImageView(device_, v, nullptr);
    for (VkSemaphore s : render_finished_) vkDestroySemaphore(device_, s, nullptr);
    swap_views_.clear();
    render_finished_.clear();
    swap_images_.clear();
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
    meshes_.push_back(gpu);
    return static_cast<MeshId>(meshes_.size() - 1);
}

void VulkanRenderer::resize(std::uint32_t w, std::uint32_t h) {
    if (w == requested_extent_.width && h == requested_extent_.height) return;
    requested_extent_ = {w, h};
    swapchain_dirty_ = true;
}

// ---- Frame -----------------------------------------------------------------

void VulkanRenderer::record(VkCommandBuffer cmd, VkImage color, VkImageView color_view, world::World& world,
                            const Camera& camera, FrameStats& stats) {
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier2 to_render[2]{};
    to_render[0] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    to_render[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_render[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_render[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_render[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_render[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_render[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_render[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_render[0].image = color;
    to_render[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_render[1] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    to_render[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    to_render[1].srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    to_render[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    to_render[1].dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    to_render[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_render[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    to_render[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_render[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_render[1].image = depth_.image;
    to_render[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 2;
    dep.pImageMemoryBarriers = to_render;
    vkCmdPipelineBarrier2(cmd, &dep);

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
    rendering.renderArea = {{0, 0}, extent_};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_att;
    rendering.pDepthAttachment = &depth_att;
    vkCmdBeginRendering(cmd, &rendering);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkViewport viewport{0, 0, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const math::Mat4 view_proj =
        view_projection(camera, static_cast<float>(extent_.width) / static_cast<float>(extent_.height));
    MeshId bound = static_cast<MeshId>(-1);
    world.each<const world::GlobalTransform, const MeshInstance>(
        [&](const world::GlobalTransform& g, const MeshInstance& inst) {
            if (inst.mesh >= meshes_.size()) return;
            const GpuMesh& mesh = meshes_[inst.mesh];
            if (inst.mesh != bound) {
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
                vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                bound = inst.mesh;
            }
            PushConstants pc{};
            const math::Mat4 mvp = view_proj * g.matrix;
            std::memcpy(pc.mvp, mvp.m, sizeof(pc.mvp));
            // Normal matrix = inverse-transpose of the upper 3x3, up to a
            // positive scale the shader normalises away: the columns are
            // the cross products of the other two columns.
            const math::Vec3 c0{g.matrix.m[0], g.matrix.m[1], g.matrix.m[2]};
            const math::Vec3 c1{g.matrix.m[4], g.matrix.m[5], g.matrix.m[6]};
            const math::Vec3 c2{g.matrix.m[8], g.matrix.m[9], g.matrix.m[10]};
            const float sign = math::dot(c0, math::cross(c1, c2)) < 0 ? -1.0f : 1.0f;
            const math::Vec3 n0 = math::cross(c1, c2) * sign;
            const math::Vec3 n1 = math::cross(c2, c0) * sign;
            const math::Vec3 n2 = math::cross(c0, c1) * sign;
            const float n[3][4] = {{n0.x, n0.y, n0.z, 0}, {n1.x, n1.y, n1.z, 0}, {n2.x, n2.y, n2.z, 0}};
            std::memcpy(pc.normal0, n[0], sizeof(pc.normal0));
            std::memcpy(pc.normal1, n[1], sizeof(pc.normal1));
            std::memcpy(pc.normal2, n[2], sizeof(pc.normal2));
            pc.color[0] = inst.color.x;
            pc.color[1] = inst.color.y;
            pc.color[2] = inst.color.z;
            pc.color[3] = 1.0f;
            vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc), &pc);
            vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
            ++stats.draws;
        });
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 after{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    after.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    after.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    after.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after.image = color;
    after.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo after_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    after_dep.imageMemoryBarrierCount = 1;
    after_dep.pImageMemoryBarriers = &after;

    if (headless()) {
        after.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        after.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        after.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier2(cmd, &after_dep);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buffer, 1, &region);
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
        vkCmdPipelineBarrier2(cmd, &host_dep);
    } else {
        after.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        after.dstAccessMask = VK_ACCESS_2_NONE;
        after.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(cmd, &after_dep);
    }
    vkEndCommandBuffer(cmd);
}

Result<FrameStats> VulkanRenderer::render(world::World& world, const Camera& camera) {
    FrameStats stats;
    Frame& frame = frames_[frame_index_];

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
        record(frame.cmd, offscreen_.image, offscreen_.view, world, camera, stats);
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
    record(frame.cmd, swap_images_[image], swap_views_[image], world, camera, stats);

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
        for (GpuMesh& m : meshes_) {
            destroy_buffer(m.vertices);
            destroy_buffer(m.indices);
        }
        destroy_targets();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
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
