#include "vulkan_backend.hpp"

#include "ge.hpp"
#include "gl_backend.hpp"

#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <psprecomp/guest_memory.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef MHF_PSP_VULKAN_SHADER_DIR
#define MHF_PSP_VULKAN_SHADER_DIR "shaders"
#endif

namespace mhf::psp::gpu {
namespace {

constexpr std::size_t kMaxFramesInFlight = 2;
constexpr std::size_t kMaxCachedTextures = 256;
constexpr VkDeviceSize kTextureBudget = 64u * 1024u * 1024u;
constexpr VkDeviceSize kMaxVertexBufferSize = 32u * 1024u * 1024u;
constexpr float kPspWidth = 480.0f;
constexpr float kPspHeight = 272.0f;
constexpr std::size_t kMaxRenderTargets = 48;
constexpr VkDeviceSize kRenderTargetBudget = 256u * 1024u * 1024u;
constexpr VkFormat kRenderTargetFormat = VK_FORMAT_R8G8B8A8_UNORM;

enum GeRegister : std::uint32_t {
    kVertexType = 0x12,
    kTextureMapEnable = 0x1E,
    kFogEnable = 0x1F,
    kAlphaBlendEnable = 0x21,
    kAlphaTestEnable = 0x22,
    kZTestEnable = 0x23,
    kStencilTestEnable = 0x24,
    kShadeMode = 0x50,
    kTexAddr0 = 0xA0,
    kTexBufWidth0 = 0xA8,
    kTexSize0 = 0xB8,
    kTexFormat = 0xC3,
    kClutFormat = 0xC5,
    kTexFilter = 0xC6,
    kTexWrap = 0xC7,
    kTexFunc = 0xC9,
    kTexEnvColor = 0xCA,
    kFogColor = 0xCF,
    kFramebufferPtr = 0x9C,
    kFramebufferWidth = 0x9D,
    kFramebufferFormat = 0xD2,
    kClearMode = 0xD3,
    kScissor1 = 0xD4,
    kScissor2 = 0xD5,
    kMinZ = 0xD6,
    kMaxZ = 0xD7,
    kAlphaTest = 0xDB,
    kZTest = 0xDE,
    kBlendMode = 0xDF,
    kBlendFixedA = 0xE0,
    kBlendFixedB = 0xE1,
    kZWriteDisable = 0xE7,
    kMaskRgb = 0xE8,
    kMaskAlpha = 0xE9,
};

struct GpuVertex {
    float position[4];
    float uv[2];
    float color[4];
    float specular[4];
    float fog;
};

struct GpuSpriteVertex {
    float position[4];
    float uv[2];
    float color[4];
    float specular[4];
    float fog;
};

struct alignas(16) DrawPushConstants {
    float target_uv[4]{};
    float uv_env[4]{};
    float env_fog[4]{};
    float fog_zrange[4]{};
    std::uint32_t flags[4]{};
    std::uint32_t alpha[4]{};
};

static_assert(sizeof(DrawPushConstants) <= 128);
static_assert(sizeof(GpuVertex) == sizeof(GpuSpriteVertex));

struct PipelineKey {
    VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkBlendFactor source_blend{VK_BLEND_FACTOR_ONE};
    VkBlendFactor destination_blend{VK_BLEND_FACTOR_ZERO};
    VkBlendOp blend_op{VK_BLEND_OP_ADD};
    VkCompareOp depth_compare{VK_COMPARE_OP_ALWAYS};
    VkCullModeFlags cull_mode{VK_CULL_MODE_NONE};
    VkColorComponentFlags color_write_mask{VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    std::uint8_t shader_variant{};
    bool blend_enable{};
    bool depth_test{};
    bool depth_write{};

    bool operator==(const PipelineKey &) const = default;
};

struct PipelineKeyHash {
    std::size_t operator()(const PipelineKey &key) const noexcept {
        std::size_t value = static_cast<std::size_t>(key.topology);
        const auto append = [&value](std::size_t item) {
            value ^= item + 0x9E3779B97F4A7C15ull + (value << 6u) + (value >> 2u);
        };
        append(static_cast<std::size_t>(key.source_blend));
        append(static_cast<std::size_t>(key.destination_blend));
        append(static_cast<std::size_t>(key.blend_op));
        append(static_cast<std::size_t>(key.depth_compare));
        append(static_cast<std::size_t>(key.cull_mode));
        append(static_cast<std::size_t>(key.color_write_mask));
        append(key.shader_variant);
        append(key.blend_enable ? 1u : 0u);
        append(key.depth_test ? 1u : 0u);
        append(key.depth_write ? 1u : 0u);
        return value;
    }
};

struct TextureKey {
    std::uint64_t content_hash{};
    std::uint32_t wrap{};
    std::uint32_t filter{};
    bool operator==(const TextureKey &) const = default;
};

struct TextureKeyHash {
    std::size_t operator()(const TextureKey &key) const noexcept {
        std::size_t value = static_cast<std::size_t>(key.content_hash);
        value ^= static_cast<std::size_t>(key.wrap) + 0x9E3779B97F4A7C15ull + (value << 6u) + (value >> 2u);
        value ^= static_cast<std::size_t>(key.filter) + 0x9E3779B97F4A7C15ull + (value << 6u) + (value >> 2u);
        return value;
    }
};

void log_vk_failure(const char *operation, VkResult result) {
    std::fprintf(stderr, "[Vulkan] %s failed: VkResult=%d\n", operation, static_cast<int>(result));
}

bool has_name(const std::vector<VkLayerProperties> &layers, const char *name) {
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties &layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool has_name(const std::vector<VkExtensionProperties> &extensions, const char *name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties &extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

VkBool32 VKAPI_CALL validation_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                       VkDebugUtilsMessageTypeFlagsEXT,
                                       const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if ((severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) != 0) {
        std::fprintf(stderr, "[Vulkan] validation: %s\n", data != nullptr && data->pMessage != nullptr
                                                                 ? data->pMessage
                                                                 : "no diagnostic text");
    }
    return VK_FALSE;
}

std::uint8_t byte_channel(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}

float normalized_channel(std::uint32_t color, unsigned shift) {
    return static_cast<float>((color >> shift) & 0xFFu) / 255.0f;
}

std::uint32_t expand_channel(std::uint32_t value, unsigned bits) {
    if (bits == 4u) return (value << 4u) | value;
    if (bits == 5u) return (value << 3u) | (value >> 2u);
    return (value << 2u) | (value >> 4u);
}

std::uint32_t decode_framebuffer_pixel(const std::uint8_t *bytes, std::uint32_t format) {
    if (format == 3u) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, bytes, sizeof(pixel));
        return pixel;
    }
    std::uint16_t pixel = 0;
    std::memcpy(&pixel, bytes, sizeof(pixel));
    std::uint32_t red = 0;
    std::uint32_t green = 0;
    std::uint32_t blue = 0;
    std::uint32_t alpha = 255u;
    if (format == 0u) {
        red = expand_channel(pixel & 31u, 5u);
        green = expand_channel((pixel >> 5u) & 63u, 6u);
        blue = expand_channel((pixel >> 11u) & 31u, 5u);
    } else if (format == 1u) {
        red = expand_channel(pixel & 31u, 5u);
        green = expand_channel((pixel >> 5u) & 31u, 5u);
        blue = expand_channel((pixel >> 10u) & 31u, 5u);
        alpha = (pixel & 0x8000u) != 0 ? 255u : 0u;
    } else {
        red = expand_channel(pixel & 15u, 4u);
        green = expand_channel((pixel >> 4u) & 15u, 4u);
        blue = expand_channel((pixel >> 8u) & 15u, 4u);
        alpha = expand_channel((pixel >> 12u) & 15u, 4u);
    }
    return red | (green << 8u) | (blue << 16u) | (alpha << 24u);
}

void encode_framebuffer_pixel(const std::uint8_t *rgba, std::uint32_t format, std::uint8_t *destination) {
    if (format == 3u) {
        std::memcpy(destination, rgba, 4u);
        return;
    }
    const std::uint32_t red = rgba[0];
    const std::uint32_t green = rgba[1];
    const std::uint32_t blue = rgba[2];
    const std::uint32_t alpha = rgba[3];
    std::uint16_t pixel = 0;
    if (format == 0u) {
        pixel = static_cast<std::uint16_t>((red >> 3u) | ((green >> 2u) << 5u) | ((blue >> 3u) << 11u));
    } else if (format == 1u) {
        pixel = static_cast<std::uint16_t>((red >> 3u) | ((green >> 3u) << 5u) |
                                           ((blue >> 3u) << 10u) | (alpha >= 128u ? 0x8000u : 0u));
    } else {
        pixel = static_cast<std::uint16_t>((red >> 4u) | ((green >> 4u) << 4u) |
                                           ((blue >> 4u) << 8u) | ((alpha >> 4u) << 12u));
    }
    std::memcpy(destination, &pixel, sizeof(pixel));
}

VkBlendFactor blend_factor(std::uint32_t factor, bool source_side, std::uint32_t fixed, bool &uses_constant) {
    switch (factor) {
    case 0:
        return source_side ? VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_SRC_COLOR;
    case 1:
        return source_side ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 2:
    case 6:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case 3:
    case 7:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 4:
    case 8:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case 5:
    case 9:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default:
        if ((fixed & 0xFFFFFFu) == 0xFFFFFFu) return VK_BLEND_FACTOR_ONE;
        if ((fixed & 0xFFFFFFu) == 0u) return VK_BLEND_FACTOR_ZERO;
        uses_constant = true;
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    }
}

VkCompareOp depth_compare(std::uint32_t compare) {
    constexpr std::array<VkCompareOp, 8> values{
        VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_NOT_EQUAL,
        VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_GREATER_OR_EQUAL};
    return values[compare & 7u];
}

} // namespace

struct VulkanBackend::Impl {
    struct Buffer {
        VkBuffer handle{};
        VkDeviceMemory memory{};
        VkDeviceSize size{};
        void *mapped{};
    };

    struct Texture {
        VkImage image{};
        VkDeviceMemory memory{};
        VkImageView view{};
        VkSampler sampler{};
        VkDescriptorSet descriptor{};
        std::vector<std::uint8_t> pending_pixels;
        std::uint64_t last_used{};
        VkDeviceSize byte_size{};
        std::uint32_t width{};
        std::uint32_t height{};
        bool uploaded{};
    };

    struct DepthTarget {
        VkImage image{};
        VkDeviceMemory memory{};
        VkImageView view{};
        VkFramebuffer framebuffer{};
    };

    struct Frame {
        VkCommandPool command_pool{};
        VkCommandBuffer command_buffer{};
        VkSemaphore image_available{};
        VkFence in_flight{};
        Buffer vertices;
        Buffer sprites;
        std::vector<Buffer> staging;
    };

    enum class Stream : std::uint8_t { Vertex, Sprite };

    struct DrawBatch {
        Stream stream{};
        std::uint32_t first{};
        std::uint32_t count{};
        std::uint32_t target_address{};
        std::uint32_t target_stride{};
        std::uint32_t target_format{};
        PipelineKey pipeline{};
        DrawPushConstants push{};
        VkRect2D scissor{};
        VkDescriptorSet texture{};
        TextureKey texture_key{};
        bool uses_cached_texture{};
        bool through{};
        std::array<float, 4> blend_constants{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct RenderTarget {
        std::uint32_t address{};
        std::uint32_t stride{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t format{};
        VkImage color{};
        VkDeviceMemory color_memory{};
        VkImageView color_view{};
        VkImageLayout color_layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImage depth{};
        VkDeviceMemory depth_memory{};
        VkImageView depth_view{};
        VkImageLayout depth_layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkFramebuffer framebuffer{};
        std::uint64_t last_draw{};
        std::uint64_t last_use{};
        std::uint64_t copied{};
        VkFence last_fence{};
        bool initialized{};

        [[nodiscard]] std::uint32_t bytes_per_pixel() const { return format == 3u ? 4u : 2u; }
        [[nodiscard]] std::uint32_t end() const { return address + stride * height * bytes_per_pixel(); }
    };

    struct TextureRegisterState {
        std::array<std::uint32_t, 9> registers{};
        std::uint64_t clut_generation{};
        bool operator==(const TextureRegisterState &) const = default;
    };

    SDL_Window *window{};
    bool vsync = true;
    bool sdl_vulkan_loaded{};
    bool validation_enabled{};
    bool failed{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debug_messenger{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    VkQueue graphics_queue{};
    VkQueue present_queue{};
    std::uint32_t graphics_family{};
    std::uint32_t present_family{};
    VkSwapchainKHR swapchain{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    int drawable_width{};
    int drawable_height{};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> image_views;
    std::vector<VkSemaphore> render_finished;
    std::vector<DepthTarget> depth_targets;
    std::vector<VkImageLayout> swapchain_layouts;
    std::array<Frame, kMaxFramesInFlight> frames{};
    std::size_t frame_index{};

    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool descriptor_pool{};
    VkPipelineLayout pipeline_layout{};
    VkRenderPass render_pass{};
    VkRenderPass target_render_pass{};
    VkRenderPass target_clear_pass{};
    VkFormat render_pass_format = VK_FORMAT_UNDEFINED;
    VkShaderModule draw_vertex_shader{};
    VkShaderModule sprite_vertex_shader{};
    VkShaderModule draw_fragment_shader{};
    VkShaderModule present_vertex_shader{};
    VkShaderModule present_fragment_shader{};
    Texture fallback_texture{};
    VkImage display_image{};
    VkDeviceMemory display_image_memory{};
    VkImageLayout display_image_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    psprecomp::GuestMemory *guest_memory{};
    LiveSettings *live_settings{};
    OverlayImage *overlay{};
    int render_scale{1};
    std::unordered_map<std::uint32_t, RenderTarget> render_targets;
    VkDeviceSize render_target_bytes{};
    std::vector<std::uint32_t> shown_targets;
    std::uint32_t shown_next{};
    std::vector<std::uint32_t> cpu_frame;
    bool frame_has_3d{};
    float picture_aspect{kPspWidth / kPspHeight};

    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines;
    std::unordered_map<TextureKey, Texture, TextureKeyHash> textures;
    VkDeviceSize texture_bytes{};
    std::uint64_t frame_serial{};
    std::vector<GpuVertex> vertex_data;
    std::vector<GpuSpriteVertex> sprite_data;
    std::vector<DrawBatch> batches;
    bool force_new_batch{};
    TextureRegisterState last_texture_registers{};
    bool have_texture_registers{};
    TextureKey last_texture_key{};
    bool last_texture_valid{};
    Texture *last_texture{};

    bool initialize(SDL_Window *target_window, bool use_vsync, int scale, psprecomp::GuestMemory *memory,
                    LiveSettings *live, OverlayImage *overlay_image) {
        window = target_window;
        vsync = use_vsync;
        render_scale = std::clamp(scale, 1, 10);
        guest_memory = memory;
        live_settings = live;
        overlay = overlay_image;
        if (window == nullptr) {
            std::fprintf(stderr, "[Vulkan] initialization failed: SDL window is null\n");
            return false;
        }
        if (guest_memory == nullptr) {
            std::fprintf(stderr, "[Vulkan] initialization failed: guest memory is null\n");
            return false;
        }
        if (!SDL_Vulkan_LoadLibrary(nullptr)) {
            std::fprintf(stderr, "[Vulkan] cannot load the Vulkan loader: %s\n", SDL_GetError());
            return false;
        }
        sdl_vulkan_loaded = true;

        const auto initialize_step = [](bool succeeded, const char *name) {
            if (!succeeded) std::fprintf(stderr, "[Vulkan] initialization failed during %s\n", name);
            return succeeded;
        };
        if (!initialize_step(create_instance(), "instance creation") ||
            !initialize_step(create_surface(), "surface creation") ||
            !initialize_step(select_physical_device(), "physical-device selection") ||
            !initialize_step(create_device(), "logical-device creation") ||
            !initialize_step(create_target_render_passes(), "offscreen render-pass creation") ||
            !initialize_step(create_descriptor_resources(), "descriptor-resource creation") ||
            !initialize_step(create_pipeline_layout(), "pipeline-layout creation") ||
            !initialize_step(create_shader_modules(), "shader-module creation") ||
            !initialize_step(create_frame_resources(), "frame-resource creation") ||
            !initialize_step(create_fallback_texture(), "fallback-texture creation") ||
            !initialize_step(create_swapchain(), "swapchain creation") ||
            !initialize_step(create_display_resources(), "display staging creation")) return false;
        vertex_data.reserve(4096);
        sprite_data.reserve(1024);
        batches.reserve(512);
        return true;
    }

    bool create_instance() {
        Uint32 extension_count = 0;
        const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (sdl_extensions == nullptr || extension_count == 0) {
            std::fprintf(stderr, "[Vulkan] cannot query SDL instance extensions: %s\n", SDL_GetError());
            return false;
        }
        std::vector<const char *> extensions(sdl_extensions, sdl_extensions + extension_count);

        std::uint32_t available_extension_count = 0;
        VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &available_extension_count, nullptr);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkEnumerateInstanceExtensionProperties", result);
            return false;
        }
        std::vector<VkExtensionProperties> available_extensions(available_extension_count);
        result = vkEnumerateInstanceExtensionProperties(nullptr, &available_extension_count, available_extensions.data());
        if (result != VK_SUCCESS) {
            log_vk_failure("vkEnumerateInstanceExtensionProperties", result);
            return false;
        }
        for (const char *extension : extensions) {
            if (!has_name(available_extensions, extension)) {
                std::fprintf(stderr, "[Vulkan] required SDL instance extension is unavailable: %s\n", extension);
                return false;
            }
        }

        std::uint32_t available_layer_count = 0;
        result = vkEnumerateInstanceLayerProperties(&available_layer_count, nullptr);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkEnumerateInstanceLayerProperties", result);
            return false;
        }
        std::vector<VkLayerProperties> available_layers(available_layer_count);
        result = vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers.data());
        if (result != VK_SUCCESS) {
            log_vk_failure("vkEnumerateInstanceLayerProperties", result);
            return false;
        }

        std::vector<const char *> layers;
#ifndef NDEBUG
        validation_enabled = has_name(available_layers, "VK_LAYER_KHRONOS_validation");
        if (validation_enabled) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            if (has_name(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        }
#endif

        VkApplicationInfo application_info{};
        application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application_info.pApplicationName = "MHForever PSP GPU";
        application_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        application_info.pEngineName = "MHForever";
        application_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        application_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &application_info;
        create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        create_info.ppEnabledLayerNames = layers.data();
        result = vkCreateInstance(&create_info, nullptr, &instance);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateInstance", result);
            return false;
        }

        if (validation_enabled && has_name(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (create_messenger != nullptr) {
                VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
                messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                messenger_info.pfnUserCallback = validation_callback;
                result = create_messenger(instance, &messenger_info, nullptr, &debug_messenger);
                if (result != VK_SUCCESS) log_vk_failure("vkCreateDebugUtilsMessengerEXT", result);
            }
        }
        return true;
    }

    bool create_surface() {
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            std::fprintf(stderr, "[Vulkan] cannot create an SDL window surface: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }

    bool supports_swapchain(VkPhysicalDevice candidate) const {
        std::uint32_t count = 0;
        VkResult result = vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr);
        if (result != VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> extensions(count);
        result = vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data());
        if (result != VK_SUCCESS || !has_name(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return false;

        VkSurfaceCapabilitiesKHR capabilities{};
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &capabilities);
        if (result != VK_SUCCESS) return false;
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            std::fprintf(stderr, "[Vulkan] physical device surface does not support transfer-destination swapchain images\n");
            return false;
        }

        VkFormatProperties depth_properties{};
        vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_D32_SFLOAT, &depth_properties);
        if ((depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) return false;

        std::uint32_t format_count = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &format_count, nullptr);
        if (result != VK_SUCCESS || format_count == 0) return false;
        std::uint32_t mode_count = 0;
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &mode_count, nullptr);
        return result == VK_SUCCESS && mode_count != 0;
    }

    bool select_physical_device() {
        std::uint32_t device_count = 0;
        VkResult result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
        if (result != VK_SUCCESS || device_count == 0) {
            if (result != VK_SUCCESS) log_vk_failure("vkEnumeratePhysicalDevices", result);
            else std::fprintf(stderr, "[Vulkan] no physical devices are available\n");
            return false;
        }
        std::vector<VkPhysicalDevice> devices(device_count);
        result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
        if (result != VK_SUCCESS) {
            log_vk_failure("vkEnumeratePhysicalDevices", result);
            return false;
        }

        int best_score = std::numeric_limits<int>::min();
        for (VkPhysicalDevice candidate : devices) {
            if (!supports_swapchain(candidate)) continue;

            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

            std::uint32_t candidate_graphics = std::numeric_limits<std::uint32_t>::max();
            std::uint32_t candidate_present = std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t i = 0; i < family_count; ++i) {
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                    candidate_graphics == std::numeric_limits<std::uint32_t>::max()) {
                    candidate_graphics = i;
                }
                VkBool32 can_present = VK_FALSE;
                const VkResult present_result =
                    vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &can_present);
                if (present_result == VK_SUCCESS && can_present == VK_TRUE &&
                    candidate_present == std::numeric_limits<std::uint32_t>::max()) {
                    candidate_present = i;
                }
            }
            if (candidate_graphics == std::numeric_limits<std::uint32_t>::max() ||
                candidate_present == std::numeric_limits<std::uint32_t>::max()) continue;

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                                  ? 3
                                  : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > best_score) {
                physical_device = candidate;
                graphics_family = candidate_graphics;
                present_family = candidate_present;
                best_score = score;
            }
        }

        if (physical_device == VK_NULL_HANDLE) {
            std::fprintf(stderr, "[Vulkan] no device supports graphics, presentation, color attachments, and depth testing\n");
            return false;
        }
        return true;
    }

    bool create_device() {
        const float priority = 1.0f;
        const std::array<std::uint32_t, 2> requested_families{graphics_family, present_family};
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (const std::uint32_t family : requested_families) {
            if (std::any_of(queues.begin(), queues.end(), [family](const VkDeviceQueueCreateInfo &queue) {
                    return queue.queueFamilyIndex == family;
                })) continue;
            VkDeviceQueueCreateInfo queue{};
            queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            queues.push_back(queue);
        }
        constexpr const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
        create_info.pQueueCreateInfos = queues.data();
        create_info.enabledExtensionCount = 1;
        create_info.ppEnabledExtensionNames = extensions;

        const VkResult result = vkCreateDevice(physical_device, &create_info, nullptr, &device);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateDevice", result);
            return false;
        }
        vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
        vkGetDeviceQueue(device, present_family, 0, &present_queue);
        return true;
    }

    std::uint32_t memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties available{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &available);
        for (std::uint32_t i = 0; i < available.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) != 0 &&
                (available.memoryTypes[i].propertyFlags & properties) == properties) return i;
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, Buffer &buffer,
                       bool map) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, &buffer.handle);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateBuffer", result);
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        const std::uint32_t type = memory_type(requirements.memoryTypeBits, properties);
        if (type == std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr, "[Vulkan] no compatible memory type for buffer allocation (%llu bytes)\n",
                         static_cast<unsigned long long>(size));
            destroy_buffer(buffer);
            return false;
        }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        result = vkAllocateMemory(device, &allocation, nullptr, &buffer.memory);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkAllocateMemory(buffer)", result);
            destroy_buffer(buffer);
            return false;
        }
        result = vkBindBufferMemory(device, buffer.handle, buffer.memory, 0);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkBindBufferMemory", result);
            destroy_buffer(buffer);
            return false;
        }
        if (map) {
            result = vkMapMemory(device, buffer.memory, 0, size, 0, &buffer.mapped);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkMapMemory(buffer)", result);
                destroy_buffer(buffer);
                return false;
            }
        }
        buffer.size = size;
        return true;
    }

    void destroy_buffer(Buffer &buffer) noexcept {
        if (device != VK_NULL_HANDLE) {
            if (buffer.mapped != nullptr && buffer.memory != VK_NULL_HANDLE) vkUnmapMemory(device, buffer.memory);
            if (buffer.handle != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer.handle, nullptr);
            if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(device, buffer.memory, nullptr);
        }
        buffer = {};
    }

    bool ensure_vertex_buffer(Buffer &buffer, VkDeviceSize needed) {
        if (needed == 0 || needed <= buffer.size) return true;
        if (needed > kMaxVertexBufferSize) {
            std::fprintf(stderr, "[Vulkan] GE frame exceeds the per-stream vertex ring limit (%llu bytes)\n",
                         static_cast<unsigned long long>(needed));
            return false;
        }
        VkDeviceSize capacity = std::max<VkDeviceSize>(buffer.size, 64u * 1024u);
        while (capacity < needed) capacity = std::min(capacity * 2u, kMaxVertexBufferSize);
        destroy_buffer(buffer);
        return create_buffer(capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, true);
    }

    bool create_image(std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage,
                      VkImage &image, VkDeviceMemory &memory) {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent = {width, height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = usage;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = vkCreateImage(device, &image_info, nullptr, &image);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateImage", result);
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image, &requirements);
        const std::uint32_t type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr, "[Vulkan] no device-local memory type for image allocation\n");
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        result = vkAllocateMemory(device, &allocation, nullptr, &memory);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkAllocateMemory(image)", result);
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }
        result = vkBindImageMemory(device, image, memory, 0);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkBindImageMemory", result);
            vkDestroyImage(device, image, nullptr);
            vkFreeMemory(device, memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    bool create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect, VkImageView &view) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = aspect;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        const VkResult result = vkCreateImageView(device, &view_info, nullptr, &view);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateImageView", result);
            return false;
        }
        return true;
    }

    bool create_display_resources() {
        if (!create_image(480u, 272u, kRenderTargetFormat,
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          display_image, display_image_memory)) return false;
        cpu_frame.resize(480u * 272u);
        return true;
    }

    void destroy_render_target(RenderTarget &target) noexcept {
        if (device != VK_NULL_HANDLE) {
            if (target.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, target.framebuffer, nullptr);
            if (target.color_view != VK_NULL_HANDLE) vkDestroyImageView(device, target.color_view, nullptr);
            if (target.color != VK_NULL_HANDLE) vkDestroyImage(device, target.color, nullptr);
            if (target.color_memory != VK_NULL_HANDLE) vkFreeMemory(device, target.color_memory, nullptr);
            if (target.depth_view != VK_NULL_HANDLE) vkDestroyImageView(device, target.depth_view, nullptr);
            if (target.depth != VK_NULL_HANDLE) vkDestroyImage(device, target.depth, nullptr);
            if (target.depth_memory != VK_NULL_HANDLE) vkFreeMemory(device, target.depth_memory, nullptr);
        }
        target = {};
    }

    bool immediate_submit(const std::function<void(VkCommandBuffer)> &record) {
        VkCommandPool pool{};
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = graphics_family;
        VkResult result = vkCreateCommandPool(device, &pool_info, nullptr, &pool);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateCommandPool(immediate)", result);
            return false;
        }
        VkCommandBuffer command{};
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(device, &allocation, &command);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkAllocateCommandBuffers(immediate)", result);
            vkDestroyCommandPool(device, pool, nullptr);
            return false;
        }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(command, &begin);
        if (result == VK_SUCCESS) record(command);
        if (result == VK_SUCCESS) result = vkEndCommandBuffer(command);
        VkFence fence{};
        if (result == VK_SUCCESS) {
            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            result = vkCreateFence(device, &fence_info, nullptr, &fence);
        }
        if (result == VK_SUCCESS) {
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            result = vkQueueSubmit(graphics_queue, 1, &submit, fence);
        }
        if (result == VK_SUCCESS) result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) log_vk_failure("immediate Vulkan submission", result);
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
        return result == VK_SUCCESS;
    }

    bool create_render_target(RenderTarget &target, std::uint32_t address, std::uint32_t stride,
                              std::uint32_t height, std::uint32_t format) {
        target.address = address;
        target.stride = stride;
        target.width = stride;
        target.height = height;
        target.format = format;
        const std::uint32_t width_px = target.width * static_cast<std::uint32_t>(render_scale);
        const std::uint32_t height_px = target.height * static_cast<std::uint32_t>(render_scale);
        if (!create_image(width_px, height_px, kRenderTargetFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          target.color, target.color_memory) ||
            !create_image_view(target.color, kRenderTargetFormat, VK_IMAGE_ASPECT_COLOR_BIT, target.color_view) ||
            !create_image(width_px, height_px, VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          target.depth, target.depth_memory) ||
            !create_image_view(target.depth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, target.depth_view)) {
            destroy_render_target(target);
            return false;
        }
        const std::array<VkImageView, 2> attachments{target.color_view, target.depth_view};
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = target_render_pass;
        framebuffer_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = width_px;
        framebuffer_info.height = height_px;
        framebuffer_info.layers = 1;
        const VkResult result = vkCreateFramebuffer(device, &framebuffer_info, nullptr, &target.framebuffer);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateFramebuffer(offscreen)", result);
            destroy_render_target(target);
            return false;
        }
        return true;
    }

    bool preserve_render_target(RenderTarget &old_target, RenderTarget &new_target) {
        if (!old_target.initialized) return true;
        const std::uint32_t old_width = old_target.width * static_cast<std::uint32_t>(render_scale);
        const std::uint32_t old_height = old_target.height * static_cast<std::uint32_t>(render_scale);
        const std::uint32_t new_width = new_target.width * static_cast<std::uint32_t>(render_scale);
        const std::uint32_t new_height = new_target.height * static_cast<std::uint32_t>(render_scale);
        const bool copied = immediate_submit([&](VkCommandBuffer command) {
            std::array<VkImageMemoryBarrier, 4> barriers{};
            const auto fill_barrier = [](VkImageMemoryBarrier &barrier, VkImage image, VkImageAspectFlags aspect,
                                         VkImageLayout old_layout, VkImageLayout new_layout,
                                         VkAccessFlags src_access, VkAccessFlags dst_access) {
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = src_access;
                barrier.dstAccessMask = dst_access;
                barrier.oldLayout = old_layout;
                barrier.newLayout = new_layout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image;
                barrier.subresourceRange.aspectMask = aspect;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.layerCount = 1;
            };
            fill_barrier(barriers[0], old_target.color, VK_IMAGE_ASPECT_COLOR_BIT, old_target.color_layout,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT);
            fill_barrier(barriers[1], new_target.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
            fill_barrier(barriers[2], old_target.depth, VK_IMAGE_ASPECT_DEPTH_BIT, old_target.depth_layout,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT);
            fill_barrier(barriers[3], new_target.depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdPipelineBarrier(command,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                 static_cast<std::uint32_t>(barriers.size()), barriers.data());
            VkImageBlit color_copy{};
            color_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            color_copy.srcSubresource.layerCount = 1;
            color_copy.srcOffsets[1] = {static_cast<std::int32_t>(std::min(old_width, new_width)),
                                         static_cast<std::int32_t>(std::min(old_height, new_height)), 1};
            color_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            color_copy.dstSubresource.layerCount = 1;
            color_copy.dstOffsets[1] = color_copy.srcOffsets[1];
            vkCmdBlitImage(command, old_target.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           new_target.color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &color_copy, VK_FILTER_NEAREST);
            VkImageCopy depth_copy{};
            depth_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depth_copy.srcSubresource.layerCount = 1;
            depth_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depth_copy.dstSubresource.layerCount = 1;
            depth_copy.extent = {std::min(old_width, new_width), std::min(old_height, new_height), 1};
            vkCmdCopyImage(command, old_target.depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           new_target.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &depth_copy);
            std::array<VkImageMemoryBarrier, 2> restore{};
            fill_barrier(restore[0], new_target.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            fill_barrier(restore[1], new_target.depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(restore.size()), restore.data());
        });
        if (copied) {
            new_target.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            new_target.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            new_target.initialized = true;
        }
        return copied;
    }

    RenderTarget *target_for(std::uint32_t address, std::uint32_t stride, std::uint32_t format,
                             std::uint32_t needed_height) {
        address &= 0x001FFFFFu;
        stride = std::clamp(stride, 1u, 1024u);
        needed_height = std::clamp(std::max(needed_height, 272u), 1u, 512u);
        auto found = render_targets.find(address);
        if (found != render_targets.end() &&
            (found->second.stride != stride || found->second.format != format || found->second.height < needed_height)) {
            const VkResult idle_result = vkDeviceWaitIdle(device);
            if (idle_result != VK_SUCCESS) {
                log_vk_failure("vkDeviceWaitIdle(render-target resize)", idle_result);
                failed = true;
                return nullptr;
            }
            RenderTarget replacement;
            if (!create_render_target(replacement, address, stride, needed_height, format)) {
                failed = true;
                return nullptr;
            }
            if (found->second.stride == stride && found->second.format == format &&
                !preserve_render_target(found->second, replacement)) {
                destroy_render_target(replacement);
                failed = true;
                return nullptr;
            }
            render_target_bytes -= static_cast<VkDeviceSize>(found->second.width) * found->second.height *
                                  static_cast<VkDeviceSize>(render_scale * render_scale) * 8u;
            destroy_render_target(found->second);
            found->second = std::move(replacement);
            render_target_bytes += static_cast<VkDeviceSize>(stride) * needed_height *
                                   static_cast<VkDeviceSize>(render_scale * render_scale) * 8u;
            return &found->second;
        }
        if (found != render_targets.end()) {
            found->second.last_use = frame_serial;
            return &found->second;
        }

        const VkDeviceSize required = static_cast<VkDeviceSize>(stride) * needed_height *
                                      static_cast<VkDeviceSize>(render_scale * render_scale) * 8u;
        if (required > kRenderTargetBudget) {
            std::fprintf(stderr, "[Vulkan] render target exceeds the memory budget: address=%08X size=%ux%u scale=%d\n",
                         address, stride, needed_height, render_scale);
            failed = true;
            return nullptr;
        }
        while (render_targets.size() >= kMaxRenderTargets || render_target_bytes + required > kRenderTargetBudget) {
            auto victim = render_targets.end();
            for (auto it = render_targets.begin(); it != render_targets.end(); ++it) {
                if (victim == render_targets.end() || it->second.last_use < victim->second.last_use) victim = it;
            }
            if (victim == render_targets.end()) return nullptr;
            const VkResult idle_result = vkDeviceWaitIdle(device);
            if (idle_result != VK_SUCCESS) {
                log_vk_failure("vkDeviceWaitIdle(render-target eviction)", idle_result);
                failed = true;
                return nullptr;
            }
            render_target_bytes -= static_cast<VkDeviceSize>(victim->second.width) * victim->second.height *
                                   static_cast<VkDeviceSize>(render_scale * render_scale) * 8u;
            destroy_render_target(victim->second);
            render_targets.erase(victim);
        }
        RenderTarget target;
        if (!create_render_target(target, address, stride, needed_height, format)) {
            failed = true;
            return nullptr;
        }
        target.last_use = frame_serial;
        auto [inserted, was_inserted] = render_targets.emplace(address, std::move(target));
        if (!was_inserted) {
            destroy_render_target(target);
            return &inserted->second;
        }
        render_target_bytes += required;
        return &inserted->second;
    }

    RenderTarget *find_target(std::uint32_t address, bool drawn_only) {
        address &= 0x001FFFFFu;
        RenderTarget *best = nullptr;
        for (auto &[key, target] : render_targets) {
            static_cast<void>(key);
            if (address >= target.address && address < target.end() && (!drawn_only || target.last_draw != 0) &&
                (best == nullptr || target.last_draw > best->last_draw)) {
                best = &target;
            }
        }
        return best;
    }

    bool create_descriptor_resources() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        VkResult result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_layout);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateDescriptorSetLayout", result);
            return false;
        }

        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        size.descriptorCount = static_cast<std::uint32_t>(kMaxCachedTextures + 1u);
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = static_cast<std::uint32_t>(kMaxCachedTextures + 1u);
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &size;
        result = vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateDescriptorPool", result);
            return false;
        }
        return true;
    }

    bool create_pipeline_layout() {
        VkPushConstantRange range{};
        range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        range.offset = 0;
        range.size = sizeof(DrawPushConstants);
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &range;
        const VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreatePipelineLayout", result);
            return false;
        }
        return true;
    }

    bool load_shader_module(std::string_view name, VkShaderModule &module) {
        const std::string path = std::string(MHF_PSP_VULKAN_SHADER_DIR) + "/" + std::string(name) + ".spv";
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            std::fprintf(stderr, "[Vulkan] cannot open compiled shader: path=%s\n", path.c_str());
            return false;
        }
        const std::streamsize size = input.tellg();
        if (size <= 0 || (size % 4) != 0) {
            std::fprintf(stderr, "[Vulkan] compiled shader has invalid size: path=%s size=%lld\n", path.c_str(),
                         static_cast<long long>(size));
            return false;
        }
        std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char *>(code.data()), size)) {
            std::fprintf(stderr, "[Vulkan] cannot read compiled shader: path=%s\n", path.c_str());
            return false;
        }
        VkShaderModuleCreateInfo module_info{};
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = static_cast<std::size_t>(size);
        module_info.pCode = code.data();
        const VkResult result = vkCreateShaderModule(device, &module_info, nullptr, &module);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateShaderModule", result);
            return false;
        }
        return true;
    }

    bool create_shader_modules() {
        return load_shader_module("draw.vert", draw_vertex_shader) &&
               load_shader_module("sprite.vert", sprite_vertex_shader) &&
               load_shader_module("draw.frag", draw_fragment_shader) &&
               load_shader_module("present.vert", present_vertex_shader) &&
               load_shader_module("present.frag", present_fragment_shader);
    }

    bool create_frame_resources() {
        for (Frame &frame : frames) {
            VkCommandPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = graphics_family;
            VkResult result = vkCreateCommandPool(device, &pool_info, nullptr, &frame.command_pool);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkCreateCommandPool", result);
                return false;
            }
            VkCommandBufferAllocateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            buffer_info.commandPool = frame.command_pool;
            buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            buffer_info.commandBufferCount = 1;
            result = vkAllocateCommandBuffers(device, &buffer_info, &frame.command_buffer);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkAllocateCommandBuffers", result);
                return false;
            }
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            result = vkCreateSemaphore(device, &semaphore_info, nullptr, &frame.image_available);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkCreateSemaphore(image available)", result);
                return false;
            }
            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            result = vkCreateFence(device, &fence_info, nullptr, &frame.in_flight);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkCreateFence", result);
                return false;
            }
        }
        return true;
    }

    bool allocate_texture_descriptor(Texture &texture) {
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = descriptor_pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptor_layout;
        VkResult result = vkAllocateDescriptorSets(device, &allocation, &texture.descriptor);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkAllocateDescriptorSets(texture)", result);
            return false;
        }
        VkDescriptorImageInfo image_info{};
        image_info.sampler = texture.sampler;
        image_info.imageView = texture.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return true;
    }

    bool create_sampler(std::uint32_t wrap, std::uint32_t filter, VkSampler &sampler) {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        const bool linear = (filter & 0x100u) != 0;
        sampler_info.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler_info.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = (wrap & 1u) != 0 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = (wrap & 0x100u) != 0 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0f;
        const VkResult result = vkCreateSampler(device, &sampler_info, nullptr, &sampler);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateSampler", result);
            return false;
        }
        return true;
    }

    bool create_texture_resource(std::uint32_t width, std::uint32_t height, std::uint32_t wrap, std::uint32_t filter,
                                 std::vector<std::uint8_t> pixels, Texture &texture) {
        if (width == 0 || height == 0 || width > 4096u || height > 4096u ||
            pixels.size() != static_cast<std::size_t>(width) * height * 4u) {
            std::fprintf(stderr, "[Vulkan] decoded texture has invalid dimensions or payload: %ux%u bytes=%zu\n",
                         width, height, pixels.size());
            return false;
        }
        texture.byte_size = static_cast<VkDeviceSize>(pixels.size());
        texture.width = width;
        texture.height = height;
        texture.pending_pixels = std::move(pixels);
        if (!create_image(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture.image, texture.memory) ||
            !create_image_view(texture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, texture.view) ||
            !create_sampler(wrap, filter, texture.sampler) || !allocate_texture_descriptor(texture)) {
            destroy_texture(texture);
            return false;
        }
        return true;
    }

    bool create_fallback_texture() {
        std::vector<std::uint8_t> white{255u, 255u, 255u, 255u};
        if (!create_texture_resource(1, 1, 1u | 0x100u, 0x100u, std::move(white), fallback_texture)) return false;
        return true;
    }

    void destroy_texture(Texture &texture) noexcept {
        if (device != VK_NULL_HANDLE) {
            if (texture.descriptor != VK_NULL_HANDLE && descriptor_pool != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(device, descriptor_pool, 1, &texture.descriptor);
            }
            if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture.sampler, nullptr);
            if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(device, texture.view, nullptr);
            if (texture.image != VK_NULL_HANDLE) vkDestroyImage(device, texture.image, nullptr);
            if (texture.memory != VK_NULL_HANDLE) vkFreeMemory(device, texture.memory, nullptr);
        }
        texture = {};
    }

    bool batch_uses_texture(const TextureKey &key) const {
        return std::any_of(batches.begin(), batches.end(), [&key](const DrawBatch &batch) {
            return batch.uses_cached_texture && batch.texture_key == key;
        });
    }

    bool evict_texture_if_needed(VkDeviceSize new_size) {
        while (textures.size() >= kMaxCachedTextures || texture_bytes + new_size > kTextureBudget) {
            auto candidate = textures.end();
            for (auto it = textures.begin(); it != textures.end(); ++it) {
                if (batch_uses_texture(it->first)) continue;
                if (candidate == textures.end() || it->second.last_used < candidate->second.last_used) candidate = it;
            }
            if (candidate == textures.end()) {
                std::fprintf(stderr, "[Vulkan] texture cache is at capacity while pending draws retain every entry\n");
                return false;
            }
            const VkResult idle_result = vkDeviceWaitIdle(device);
            if (idle_result != VK_SUCCESS) {
                log_vk_failure("vkDeviceWaitIdle(texture eviction)", idle_result);
                return false;
            }
            if (have_texture_registers && last_texture_valid && candidate->first == last_texture_key) {
                have_texture_registers = false;
                last_texture_valid = false;
                last_texture = nullptr;
            }
            texture_bytes -= candidate->second.byte_size;
            destroy_texture(candidate->second);
            textures.erase(candidate);
        }
        return true;
    }

    Texture *find_or_create_texture(const Ge &ge) {
        TextureRegisterState state{{
            ge.reg(kTexAddr0), ge.reg(kTexBufWidth0), ge.reg(kTexSize0), ge.reg(kTexFormat),
            ge.reg(kClutFormat), ge.reg(kTexWrap), ge.reg(kTexFilter), ge.reg(kTextureMapEnable),
            ge.reg(kVertexType)}, ge.clut_generation()};
        if (have_texture_registers && state == last_texture_registers) return last_texture;
        have_texture_registers = true;
        last_texture_registers = state;
        last_texture = nullptr;
        last_texture_valid = false;

        const Ge::TextureInfo info = ge.texture_info();
        if (info.width == 0 || info.height == 0 || info.width > 4096u || info.height > 4096u) {
            std::fprintf(stderr, "[Vulkan] texture state has unsupported dimensions: %ux%u\n", info.width, info.height);
            return nullptr;
        }
        const TextureKey key{ge.texture_hash(), ge.reg(kTexWrap), ge.reg(kTexFilter)};
        if (const auto found = textures.find(key); found != textures.end()) {
            found->second.last_used = frame_serial;
            last_texture_key = key;
            last_texture = &found->second;
            last_texture_valid = true;
            return last_texture;
        }

        const VkDeviceSize bytes = static_cast<VkDeviceSize>(info.width) * info.height * 4u;
        if (bytes > kTextureBudget || !evict_texture_if_needed(bytes)) return nullptr;
        std::vector<std::uint32_t> decoded(static_cast<std::size_t>(info.width) * info.height);
        ge.decode_texture(decoded.data());
        std::vector<std::uint8_t> pixels(decoded.size() * sizeof(std::uint32_t));
        std::memcpy(pixels.data(), decoded.data(), pixels.size());

        Texture texture;
        texture.last_used = frame_serial;
        if (!create_texture_resource(info.width, info.height, key.wrap, key.filter, std::move(pixels), texture)) return nullptr;
        auto [inserted, was_inserted] = textures.emplace(key, std::move(texture));
        if (!was_inserted) {
            destroy_texture(texture);
            inserted->second.last_used = frame_serial;
        } else {
            texture_bytes += inserted->second.byte_size;
        }
        last_texture_key = key;
        last_texture = &inserted->second;
        last_texture_valid = true;
        return last_texture;
    }

    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR> &formats) const {
        constexpr std::array<VkFormat, 2> preferred{VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
        for (const VkFormat format : preferred) {
            const auto found = std::find_if(formats.begin(), formats.end(), [format](const VkSurfaceFormatKHR &entry) {
                return entry.format == format && entry.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
            if (found != formats.end()) return *found;
        }
        return formats.front();
    }

    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR> &modes) const {
        if (vsync) return VK_PRESENT_MODE_FIFO_KHR;
        constexpr std::array<VkPresentModeKHR, 2> preferred{VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
        for (const VkPresentModeKHR mode : preferred) {
            if (std::find(modes.begin(), modes.end(), mode) != modes.end()) return mode;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) const {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
        for (const VkCompositeAlphaFlagBitsKHR choice : choices) {
            if ((supported & choice) != 0) return choice;
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    bool create_target_render_passes() {
        const auto create_pass = [this](VkAttachmentLoadOp color_load, VkAttachmentLoadOp depth_load,
                                        VkRenderPass &pass) {
            std::array<VkAttachmentDescription, 2> attachments{};
            attachments[0].format = kRenderTargetFormat;
            attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[0].loadOp = color_load;
            attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[1].format = VK_FORMAT_D32_SFLOAT;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = depth_load;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference color_reference{};
            color_reference.attachment = 0;
            color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkAttachmentReference depth_reference{};
            depth_reference.attachment = 1;
            depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color_reference;
            subpass.pDepthStencilAttachment = &depth_reference;

            std::array<VkSubpassDependency, 2> dependencies{};
            dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass = 0;
            dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependencies[0].dstStageMask = dependencies[0].srcStageMask;
            dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies[1].srcSubpass = 0;
            dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask = dependencies[0].dstStageMask;
            dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            VkRenderPassCreateInfo pass_info{};
            pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            pass_info.pAttachments = attachments.data();
            pass_info.subpassCount = 1;
            pass_info.pSubpasses = &subpass;
            pass_info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
            pass_info.pDependencies = dependencies.data();
            const VkResult result = vkCreateRenderPass(device, &pass_info, nullptr, &pass);
            if (result != VK_SUCCESS) log_vk_failure("vkCreateRenderPass(offscreen)", result);
            return result == VK_SUCCESS;
        };

        return create_pass(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD, target_render_pass) &&
               create_pass(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR, target_clear_pass);
    }

    bool create_render_pass() {
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = swapchain_format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color_reference{};
        color_reference.attachment = 0;
        color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depth_reference{};
        depth_reference.attachment = 1;
        depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pDepthStencilAttachment = &depth_reference;
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = dependencies[0].srcStageMask;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass_info{};
        pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        pass_info.pAttachments = attachments.data();
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        pass_info.pDependencies = dependencies.data();
        const VkResult result = vkCreateRenderPass(device, &pass_info, nullptr, &render_pass);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateRenderPass", result);
            return false;
        }
        render_pass_format = swapchain_format;
        return true;
    }

    bool create_depth_targets() {
        depth_targets.resize(swapchain_images.size());
        for (std::size_t i = 0; i < depth_targets.size(); ++i) {
            DepthTarget &target = depth_targets[i];
            if (!create_image(swapchain_extent.width, swapchain_extent.height, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, target.image, target.memory) ||
                !create_image_view(target.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, target.view)) return false;
            const std::array<VkImageView, 2> attachments{image_views[i], target.view};
            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass;
            framebuffer_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = swapchain_extent.width;
            framebuffer_info.height = swapchain_extent.height;
            framebuffer_info.layers = 1;
            const VkResult result = vkCreateFramebuffer(device, &framebuffer_info, nullptr, &target.framebuffer);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkCreateFramebuffer", result);
                return false;
            }
        }
        return true;
    }

    bool create_swapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
            return false;
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            std::fprintf(stderr, "[Vulkan] surface images do not support transfer destinations\n");
            return false;
        }

        std::uint32_t format_count = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
        if (result != VK_SUCCESS || format_count == 0) {
            if (result != VK_SUCCESS) log_vk_failure("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
            else std::fprintf(stderr, "[Vulkan] surface has no supported formats\n");
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());
        if (result != VK_SUCCESS) {
            log_vk_failure("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
            return false;
        }
        std::uint32_t mode_count = 0;
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, nullptr);
        if (result != VK_SUCCESS || mode_count == 0) {
            if (result != VK_SUCCESS) log_vk_failure("vkGetPhysicalDeviceSurfacePresentModesKHR", result);
            else std::fprintf(stderr, "[Vulkan] surface has no present modes\n");
            return false;
        }
        std::vector<VkPresentModeKHR> modes(mode_count);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, modes.data());
        if (result != VK_SUCCESS) {
            log_vk_failure("vkGetPhysicalDeviceSurfacePresentModesKHR", result);
            return false;
        }

        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
            std::fprintf(stderr, "[Vulkan] cannot query drawable size: %s\n", SDL_GetError());
            return false;
        }
        if (width <= 0 || height <= 0) {
            std::fprintf(stderr, "[Vulkan] cannot create a swapchain for a zero-sized window\n");
            return false;
        }
        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            extent.width = std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width,
                                      capabilities.maxImageExtent.width);
            extent.height = std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height,
                                       capabilities.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0) return false;
        std::uint32_t image_count = capabilities.minImageCount + 1u;
        if (capabilities.maxImageCount != 0) image_count = std::min(image_count, capabilities.maxImageCount);

        const VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
        VkFormatProperties source_properties{};
        VkFormatProperties destination_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, kRenderTargetFormat, &source_properties);
        vkGetPhysicalDeviceFormatProperties(physical_device, surface_format.format, &destination_properties);
        constexpr VkFormatFeatureFlags kBlitSourceFeatures =
            VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((source_properties.optimalTilingFeatures & kBlitSourceFeatures) != kBlitSourceFeatures ||
            (destination_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
            std::fprintf(stderr, "[Vulkan] selected formats do not support the required linear image blit\n");
            return false;
        }
        const VkPresentModeKHR present_mode = choose_present_mode(modes);
        const std::array<std::uint32_t, 2> queue_families{graphics_family, present_family};
        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (graphics_family != present_family) {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = static_cast<std::uint32_t>(queue_families.size());
            create_info.pQueueFamilyIndices = queue_families.data();
        } else {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        result = vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateSwapchainKHR", result);
            return false;
        }

        std::uint32_t actual_image_count = 0;
        result = vkGetSwapchainImagesKHR(device, swapchain, &actual_image_count, nullptr);
        if (result != VK_SUCCESS || actual_image_count == 0) {
            if (result != VK_SUCCESS) log_vk_failure("vkGetSwapchainImagesKHR", result);
            else std::fprintf(stderr, "[Vulkan] swapchain has no images\n");
            return false;
        }
        swapchain_images.resize(actual_image_count);
        result = vkGetSwapchainImagesKHR(device, swapchain, &actual_image_count, swapchain_images.data());
        if (result != VK_SUCCESS) {
            log_vk_failure("vkGetSwapchainImagesKHR", result);
            return false;
        }

        image_views.reserve(actual_image_count);
        render_finished.reserve(actual_image_count);
        for (const VkImage image : swapchain_images) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = surface_format.format;
            view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            VkImageView view{};
            result = vkCreateImageView(device, &view_info, nullptr, &view);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkCreateImageView(swapchain)", result);
                return false;
            }
            image_views.push_back(view);
            VkSemaphore semaphore{};
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            result = vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore);
            if (result != VK_SUCCESS) {
                log_vk_failure("vkCreateSemaphore(render finished)", result);
                return false;
            }
            render_finished.push_back(semaphore);
        }

        swapchain_format = surface_format.format;
        swapchain_extent = extent;
        drawable_width = width;
        drawable_height = height;
        swapchain_layouts.assign(actual_image_count, VK_IMAGE_LAYOUT_UNDEFINED);
        if (render_pass == VK_NULL_HANDLE || render_pass_format != swapchain_format) {
            destroy_pipeline_cache();
            if (render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, render_pass, nullptr);
            render_pass = VK_NULL_HANDLE;
            if (!create_render_pass()) return false;
        }
        return true;
    }

    void destroy_pipeline_cache() noexcept {
        if (device == VK_NULL_HANDLE) return;
        for (const auto &[key, pipeline] : pipelines) {
            static_cast<void>(key);
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        }
        pipelines.clear();
    }

    void destroy_swapchain_resources() noexcept {
        if (device != VK_NULL_HANDLE) {
            for (DepthTarget &target : depth_targets) {
                if (target.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, target.framebuffer, nullptr);
                if (target.view != VK_NULL_HANDLE) vkDestroyImageView(device, target.view, nullptr);
                if (target.image != VK_NULL_HANDLE) vkDestroyImage(device, target.image, nullptr);
                if (target.memory != VK_NULL_HANDLE) vkFreeMemory(device, target.memory, nullptr);
                target = {};
            }
            depth_targets.clear();
            for (const VkSemaphore semaphore : render_finished) {
                if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, semaphore, nullptr);
            }
            for (const VkImageView view : image_views) {
                if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
            }
            if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        render_finished.clear();
        image_views.clear();
        swapchain_images.clear();
        swapchain = VK_NULL_HANDLE;
        swapchain_format = VK_FORMAT_UNDEFINED;
        swapchain_extent = {};
        drawable_width = 0;
        drawable_height = 0;
    }

    bool recreate_swapchain() {
        const VkResult result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkDeviceWaitIdle(swapchain recreation)", result);
            failed = true;
            return false;
        }
        destroy_swapchain_resources();
        if (!create_swapchain()) {
            std::fprintf(stderr, "[Vulkan] swapchain recreation failed\n");
            failed = true;
            return false;
        }
        return true;
    }

    bool ensure_texture_uploaded(Frame &frame, VkCommandBuffer command_buffer, Texture &texture) {
        if (texture.uploaded || texture.pending_pixels.empty()) return true;
        Buffer staging_buffer;
        if (!create_buffer(texture.byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, true)) {
            return false;
        }
        std::memcpy(staging_buffer.mapped, texture.pending_pixels.data(), texture.pending_pixels.size());
        frame.staging.push_back(staging_buffer);

        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = texture.image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &to_transfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        return record_texture_copy(command_buffer, staging_buffer.handle, texture, copy);
    }

    bool record_texture_copy(VkCommandBuffer command_buffer, VkBuffer staging_buffer, Texture &texture,
                             VkBufferImageCopy copy) {
        std::uint32_t width = 0, height = 0;
        width = texture_width(texture);
        height = texture_height(texture);
        if (width == 0 || height == 0) {
            std::fprintf(stderr, "[Vulkan] texture upload has no stored image dimensions\n");
            return false;
        }
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(command_buffer, staging_buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier to_shader{};
        to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader.image = texture.image;
        to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_shader.subresourceRange.levelCount = 1;
        to_shader.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &to_shader);
        return true;
    }

    std::uint32_t texture_width(const Texture &texture) const {
        return texture.width;
    }

    std::uint32_t texture_height(const Texture &texture) const {
        return texture.height;
    }

    bool record_pending_texture_uploads(Frame &frame, VkCommandBuffer command_buffer) {
        if (!fallback_texture.uploaded &&
            !ensure_texture_uploaded(frame, command_buffer, fallback_texture)) return false;
        for (auto &[key, texture] : textures) {
            static_cast<void>(key);
            if (!texture.uploaded && !ensure_texture_uploaded(frame, command_buffer, texture)) return false;
        }
        return true;
    }

    void cleanup_frame_staging(Frame &frame) noexcept {
        for (Buffer &buffer : frame.staging) destroy_buffer(buffer);
        frame.staging.clear();
    }

    bool create_pipeline(const PipelineKey &key, VkPipeline &pipeline) {
        const bool sprite = (key.shader_variant & 2u) != 0;
        const VkShaderModule vertex_shader = sprite ? sprite_vertex_shader : draw_vertex_shader;
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex_shader;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = draw_fragment_shader;
        stages[1].pName = "main";
        const VkBool32 texture_variant = (key.shader_variant & 1u) != 0 ? VK_TRUE : VK_FALSE;
        VkSpecializationMapEntry specialization_entry{};
        specialization_entry.constantID = 0;
        specialization_entry.offset = 0;
        specialization_entry.size = sizeof(texture_variant);
        VkSpecializationInfo specialization{};
        specialization.mapEntryCount = 1;
        specialization.pMapEntries = &specialization_entry;
        specialization.dataSize = sizeof(texture_variant);
        specialization.pData = &texture_variant;
        stages[1].pSpecializationInfo = &specialization;

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sprite ? sizeof(GpuSpriteVertex) : sizeof(GpuVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        const std::array<VkVertexInputAttributeDescription, 5> attributes{{
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, color)},
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, specular)},
            {4, 0, VK_FORMAT_R32_SFLOAT, offsetof(GpuVertex, fog)},
        }};
        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = key.topology;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = key.cull_mode;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = key.depth_test ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = key.depth_write ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = key.depth_compare;
        depth.maxDepthBounds = 1.0f;
        VkPipelineColorBlendAttachmentState color_attachment{};
        color_attachment.blendEnable = key.blend_enable ? VK_TRUE : VK_FALSE;
        color_attachment.srcColorBlendFactor = key.source_blend;
        color_attachment.dstColorBlendFactor = key.destination_blend;
        color_attachment.colorBlendOp = key.blend_op;
        color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        color_attachment.colorWriteMask = key.color_write_mask;
        VkPipelineColorBlendStateCreateInfo color_blend{};
        color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.attachmentCount = 1;
        color_blend.pAttachments = &color_attachment;
        constexpr std::array<VkDynamicState, 3> dynamics{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamics.size());
        dynamic.pDynamicStates = dynamics.data();
        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = static_cast<std::uint32_t>(stages.size());
        pipeline_info.pStages = stages.data();
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &assembly;
        pipeline_info.pViewportState = &viewport;
        pipeline_info.pRasterizationState = &rasterization;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = &depth;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = target_render_pass;
        pipeline_info.subpass = 0;
        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkCreateGraphicsPipelines", result);
            return false;
        }
        return true;
    }

    VkPipeline pipeline_for(const PipelineKey &key) {
        if (const auto found = pipelines.find(key); found != pipelines.end()) return found->second;
        VkPipeline pipeline{};
        if (!create_pipeline(key, pipeline)) return VK_NULL_HANDLE;
        pipelines.emplace(key, pipeline);
        return pipeline;
    }

    DrawBatch make_draw_batch(const Ge &ge, bool sprite) {
        DrawBatch batch{};
        const bool clear = (ge.reg(kClearMode) & 1u) != 0;
        const std::uint32_t framebuffer_address = ge.reg(kFramebufferPtr) & 0x001FFFF0u;
        const std::uint32_t framebuffer_stride = std::max(ge.reg(kFramebufferWidth) & 0x7FCu, 1u);
        const std::uint32_t framebuffer_format = ge.reg(kFramebufferFormat) & 3u;
        const std::uint32_t scissor2_for_height = ge.reg(kScissor2);
        const std::uint32_t required_height = std::min(((scissor2_for_height >> 10u) & 0x3FFu) + 1u, 512u);
        RenderTarget *target = target_for(framebuffer_address, framebuffer_stride, framebuffer_format, required_height);
        if (target == nullptr) return batch;
        batch.target_address = target->address;
        batch.target_stride = target->stride;
        batch.target_format = target->format;
        target->last_draw = frame_serial + 1u;
        target->last_use = frame_serial;
        frame_has_3d = frame_has_3d || (!clear && (ge.reg(kVertexType) & (1u << 23u)) == 0);
        bool textured = !clear && (ge.reg(kTextureMapEnable) & 1u) != 0;
        Texture *texture = textured ? find_or_create_texture(ge) : nullptr;
        if (textured && texture == nullptr) {
            std::fprintf(stderr, "[Vulkan] using untextured GE fallback after texture decode/cache failure\n");
            textured = false;
        }

        const std::uint32_t blend_mode = ge.reg(kBlendMode);
        bool source_constant = false;
        bool destination_constant = false;
        batch.pipeline.source_blend = blend_factor(blend_mode & 0xFu, true, ge.reg(kBlendFixedA), source_constant);
        batch.pipeline.destination_blend =
            blend_factor((blend_mode >> 4u) & 0xFu, false, ge.reg(kBlendFixedB), destination_constant);
        std::uint32_t fixed_color = source_constant ? ge.reg(kBlendFixedA) : ge.reg(kBlendFixedB);
        if (source_constant && destination_constant &&
            (ge.reg(kBlendFixedA) & 0xFFFFFFu) != (ge.reg(kBlendFixedB) & 0xFFFFFFu)) {
            batch.pipeline.destination_blend = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            fixed_color = ge.reg(kBlendFixedA);
        }
        if (clear) {
            batch.pipeline.blend_enable = false;
            batch.pipeline.depth_test = true;
            batch.pipeline.depth_write = (ge.reg(kClearMode) & 0x400u) != 0;
            batch.pipeline.depth_compare = VK_COMPARE_OP_ALWAYS;
            const std::uint32_t mode = ge.reg(kClearMode);
            const bool rgb_write = (mode & 0x100u) != 0;
            const bool alpha_write = (mode & 0x200u) != 0;
            batch.pipeline.color_write_mask =
                (rgb_write ? static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                               VK_COLOR_COMPONENT_B_BIT)
                           : 0u) |
                (alpha_write ? static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_A_BIT) : 0u);
        } else {
            batch.pipeline.blend_enable = (ge.reg(kAlphaBlendEnable) & 1u) != 0;
            batch.pipeline.depth_test = (ge.reg(kZTestEnable) & 1u) != 0;
            batch.pipeline.depth_write = batch.pipeline.depth_test && (ge.reg(kZWriteDisable) & 1u) == 0;
            batch.pipeline.depth_compare = batch.pipeline.depth_test ? depth_compare(ge.reg(kZTest)) : VK_COMPARE_OP_ALWAYS;
            const std::uint32_t mask = ge.reg(kMaskRgb);
            const bool keep_alpha = (ge.reg(kStencilTestEnable) & 1u) != 0 ||
                                    (ge.reg(kMaskAlpha) & 0xFFu) == 0xFFu;
            batch.pipeline.color_write_mask =
                ((mask & 0xFFu) != 0xFFu ? static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_R_BIT) : 0u) |
                (((mask >> 8u) & 0xFFu) != 0xFFu ? static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_G_BIT) : 0u) |
                (((mask >> 16u) & 0xFFu) != 0xFFu ? static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_B_BIT) : 0u) |
                (!keep_alpha ? static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_A_BIT) : 0u);
        }
        batch.pipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        batch.pipeline.cull_mode = VK_CULL_MODE_NONE;
        batch.pipeline.shader_variant = static_cast<std::uint8_t>((sprite ? 2u : 0u) | (textured ? 1u : 0u));
        batch.stream = sprite ? Stream::Sprite : Stream::Vertex;
        batch.through = (ge.reg(kVertexType) & (1u << 23u)) != 0;
        batch.texture = texture != nullptr ? texture->descriptor : fallback_texture.descriptor;
        batch.uses_cached_texture = texture != nullptr;
        if (texture != nullptr) batch.texture_key = last_texture_key;
        batch.blend_constants = {normalized_channel(fixed_color, 0), normalized_channel(fixed_color, 8),
                                 normalized_channel(fixed_color, 16), 1.0f};

        const bool through = batch.through;
        const Ge::TextureInfo texture_info = textured ? ge.texture_info() : Ge::TextureInfo{};
        batch.push.target_uv[0] = static_cast<float>(target->width);
        batch.push.target_uv[1] = static_cast<float>(target->height);
        batch.push.target_uv[2] = through && texture_info.width != 0
                                      ? 1.0f / static_cast<float>(texture_info.width)
                                      : 1.0f;
        batch.push.target_uv[3] = through && texture_info.height != 0
                                      ? 1.0f / static_cast<float>(texture_info.height)
                                      : 1.0f;
        const std::uint32_t environment = ge.reg(kTexEnvColor);
        batch.push.uv_env[2] = normalized_channel(environment, 0);
        batch.push.uv_env[3] = normalized_channel(environment, 8);
        batch.push.env_fog[0] = normalized_channel(environment, 16);
        const std::uint32_t fog = ge.reg(kFogColor);
        batch.push.env_fog[1] = normalized_channel(fog, 0);
        batch.push.env_fog[2] = normalized_channel(fog, 8);
        batch.push.env_fog[3] = normalized_channel(fog, 16);
        batch.push.flags[0] = textured ? 1u : 0u;
        batch.push.flags[1] = clear ? 1u : 0u;
        const std::uint32_t texture_function = ge.reg(kTexFunc);
        batch.push.flags[2] = texture_function & 7u;
        batch.push.flags[3] = ((texture_function & 0x100u) != 0 ? 1u : 0u) |
                              ((texture_function & 0x10000u) != 0 ? 2u : 0u);
        batch.push.alpha[0] = 1u;
        if (!clear && (ge.reg(kAlphaTestEnable) & 1u) != 0) {
            const std::uint32_t alpha = ge.reg(kAlphaTest);
            batch.push.alpha[0] = alpha & 7u;
            batch.push.alpha[1] = (alpha >> 8u) & 0xFFu;
            batch.push.alpha[2] = (alpha >> 16u) & 0xFFu;
        }
        batch.push.alpha[3] = !clear && !through && (ge.reg(kFogEnable) & 1u) != 0 ? 1u : 0u;
        const std::uint32_t min_z = ge.reg(kMinZ) & 0xFFFFu;
        const std::uint32_t max_z = ge.reg(kMaxZ) & 0xFFFFu;
        if (!clear && !through && max_z != 0u && (min_z != 0u || max_z != 0xFFFFu)) {
            batch.push.flags[3] |= 4u;
            batch.push.fog_zrange[1] = static_cast<float>(min_z);
            batch.push.fog_zrange[2] = static_cast<float>(max_z);
        }

        const std::uint32_t scissor_1 = ge.reg(kScissor1);
        const std::uint32_t scissor_2 = ge.reg(kScissor2);
        const std::uint32_t x1 = std::min(scissor_1 & 0x3FFu, std::min(479u, target->width - 1u));
        const std::uint32_t y1 = std::min((scissor_1 >> 10u) & 0x3FFu, std::min(271u, target->height - 1u));
        const std::uint32_t x2 = std::min(scissor_2 & 0x3FFu, std::min(479u, target->width - 1u));
        const std::uint32_t y2 = std::min((scissor_2 >> 10u) & 0x3FFu, std::min(271u, target->height - 1u));
        if (x1 > x2 || y1 > y2) {
            batch.scissor = {{0, 0}, {0, 0}};
        } else {
            const std::uint32_t scale = static_cast<std::uint32_t>(render_scale);
            const std::uint32_t left = x1 * scale;
            const std::uint32_t top = y1 * scale;
            const std::uint32_t right = std::min((x2 + 1u) * scale, target->width * scale);
            const std::uint32_t bottom = std::min((y2 + 1u) * scale, target->height * scale);
            batch.scissor.offset = {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top)};
            batch.scissor.extent = {right > left ? right - left : 0u, bottom > top ? bottom - top : 0u};
        }
        return batch;
    }

    template <typename OutputVertex>
    OutputVertex pack_vertex(const Vertex &vertex, const Vertex &color, bool through) const {
        OutputVertex packed{};
        packed.position[0] = vertex.x;
        packed.position[1] = vertex.y;
        packed.position[2] = vertex.z;
        packed.position[3] = through ? 1.0f : (vertex.w > 0.0f ? vertex.w : 1.0f);
        packed.uv[0] = vertex.u;
        packed.uv[1] = vertex.v;
        packed.color[0] = byte_channel(color.r) / 255.0f;
        packed.color[1] = byte_channel(color.g) / 255.0f;
        packed.color[2] = byte_channel(color.b) / 255.0f;
        packed.color[3] = byte_channel(color.a) / 255.0f;
        packed.specular[0] = byte_channel(color.sr) / 255.0f;
        packed.specular[1] = byte_channel(color.sg) / 255.0f;
        packed.specular[2] = byte_channel(color.sb) / 255.0f;
        packed.specular[3] = 0.0f;
        packed.fog = vertex.fog;
        return packed;
    }

    bool same_batch_state(const DrawBatch &left, const DrawBatch &right) const {
        return left.stream == right.stream && left.target_address == right.target_address &&
               left.target_stride == right.target_stride && left.target_format == right.target_format &&
               left.pipeline == right.pipeline && left.scissor.offset.x == right.scissor.offset.x &&
               left.scissor.offset.y == right.scissor.offset.y && left.scissor.extent.width == right.scissor.extent.width &&
               left.scissor.extent.height == right.scissor.extent.height && left.texture == right.texture &&
               left.texture_key == right.texture_key && left.uses_cached_texture == right.uses_cached_texture &&
               left.through == right.through &&
               std::memcmp(&left.push, &right.push, sizeof(DrawPushConstants)) == 0 &&
               left.blend_constants == right.blend_constants;
    }

    void append_batch(DrawBatch batch, std::uint32_t first, std::uint32_t count) {
        batch.first = first;
        batch.count = count;
        if (!force_new_batch && !batches.empty()) {
            DrawBatch &previous = batches.back();
            if (same_batch_state(previous, batch) && previous.first + previous.count == first) {
                previous.count += count;
                return;
            }
        }
        force_new_batch = false;
        batches.push_back(std::move(batch));
    }

    void append_triangle(const Ge &ge, const Vertex &a, const Vertex &b, const Vertex &c) {
        constexpr std::size_t kMaxVertices = static_cast<std::size_t>(kMaxVertexBufferSize / sizeof(GpuVertex));
        if (vertex_data.size() > kMaxVertices - 3u) {
            std::fprintf(stderr, "[Vulkan] GE frame exceeds the per-frame triangle vertex limit\n");
            failed = true;
            return;
        }
        DrawBatch batch = make_draw_batch(ge, false);
        if (batch.scissor.extent.width == 0 || batch.scissor.extent.height == 0) return;
        const bool through = (ge.reg(kVertexType) & (1u << 23u)) != 0;
        const bool flat = (ge.reg(kShadeMode) & 1u) == 0;
        const std::uint32_t first = static_cast<std::uint32_t>(vertex_data.size());
        vertex_data.push_back(pack_vertex<GpuVertex>(a, flat ? c : a, through));
        vertex_data.push_back(pack_vertex<GpuVertex>(b, flat ? c : b, through));
        vertex_data.push_back(pack_vertex<GpuVertex>(c, c, through));
        append_batch(std::move(batch), first, 3u);
    }

    void append_sprite(const Ge &ge, const Vertex &a, const Vertex &b) {
        constexpr std::size_t kMaxVertices = static_cast<std::size_t>(kMaxVertexBufferSize / sizeof(GpuSpriteVertex));
        if (sprite_data.size() > kMaxVertices - 6u) {
            std::fprintf(stderr, "[Vulkan] GE frame exceeds the per-frame sprite vertex limit\n");
            failed = true;
            return;
        }
        DrawBatch batch = make_draw_batch(ge, true);
        if (batch.scissor.extent.width == 0 || batch.scissor.extent.height == 0) return;
        Vertex top_left = b, top_right = b, bottom_left = b, bottom_right = b;
        top_left.x = a.x;
        top_left.y = a.y;
        top_left.u = a.u;
        top_left.v = a.v;
        top_right.x = b.x;
        top_right.y = a.y;
        top_right.u = b.u;
        top_right.v = a.v;
        bottom_left.x = a.x;
        bottom_left.y = b.y;
        bottom_left.u = a.u;
        bottom_left.v = b.v;
        if ((ge.reg(kVertexType) & (1u << 23u)) != 0 &&
            ((a.x < b.x && a.y > b.y) || (a.x > b.x && a.y < b.y))) {
            std::swap(top_right.u, bottom_left.u);
            std::swap(top_right.v, bottom_left.v);
        }
        for (Vertex *vertex : {&top_left, &top_right, &bottom_left, &bottom_right}) vertex->w = 1.0f;
        const std::uint32_t first = static_cast<std::uint32_t>(sprite_data.size());
        sprite_data.push_back(pack_vertex<GpuSpriteVertex>(top_left, b, true));
        sprite_data.push_back(pack_vertex<GpuSpriteVertex>(top_right, b, true));
        sprite_data.push_back(pack_vertex<GpuSpriteVertex>(bottom_left, b, true));
        sprite_data.push_back(pack_vertex<GpuSpriteVertex>(top_right, b, true));
        sprite_data.push_back(pack_vertex<GpuSpriteVertex>(bottom_right, b, true));
        sprite_data.push_back(pack_vertex<GpuSpriteVertex>(bottom_left, b, true));
        append_batch(std::move(batch), first, 6u);
    }

    void draw_batches(VkCommandBuffer command_buffer, Frame &frame, const RenderTarget &target) {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(target.width * static_cast<std::uint32_t>(render_scale));
        viewport.height = static_cast<float>(target.height * static_cast<std::uint32_t>(render_scale));
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkBuffer bound_buffer{};
        for (const DrawBatch &batch : batches) {
            if (batch.target_address != target.address) continue;
            const VkPipeline pipeline = pipeline_for(batch.pipeline);
            if (pipeline == VK_NULL_HANDLE) {
                failed = true;
                return;
            }
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdSetViewport(command_buffer, 0, 1, &viewport);
            vkCmdSetScissor(command_buffer, 0, 1, &batch.scissor);
            vkCmdSetBlendConstants(command_buffer, batch.blend_constants.data());
            const Buffer &buffer = batch.stream == Stream::Sprite ? frame.sprites : frame.vertices;
            if (buffer.handle != bound_buffer) {
                constexpr VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command_buffer, 0, 1, &buffer.handle, &offset);
                bound_buffer = buffer.handle;
            }
            if (batch.texture != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
                                        &batch.texture, 0, nullptr);
            }
            vkCmdPushConstants(command_buffer, pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(DrawPushConstants), &batch.push);
            vkCmdDraw(command_buffer, batch.count, 1, batch.first, 0);
        }
    }

    void prepare_render_target(VkCommandBuffer command_buffer, RenderTarget &target) {
        std::array<VkImageMemoryBarrier, 2> barriers{};
        const auto configure = [](VkImageMemoryBarrier &barrier, VkImage image, VkImageAspectFlags aspect,
                                  VkImageLayout layout, VkAccessFlags src_access, VkAccessFlags dst_access) {
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = src_access;
            barrier.dstAccessMask = dst_access;
            barrier.oldLayout = layout;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
                barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = aspect;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
        };
        configure(barriers[0], target.color, VK_IMAGE_ASPECT_COLOR_BIT, target.color_layout,
                  target.color_layout == VK_IMAGE_LAYOUT_UNDEFINED
                      ? VkAccessFlags{0}
                      : VkAccessFlags{VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        configure(barriers[1], target.depth, VK_IMAGE_ASPECT_DEPTH_BIT, target.depth_layout,
                  target.depth_layout == VK_IMAGE_LAYOUT_UNDEFINED
                      ? VkAccessFlags{0}
                      : VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        const VkPipelineStageFlags source_stage =
            target.color_layout == VK_IMAGE_LAYOUT_UNDEFINED || target.depth_layout == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        vkCmdPipelineBarrier(command_buffer, source_stage,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(barriers.size()), barriers.data());
        target.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        target.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if (!target.initialized) {
            std::array<VkClearValue, 2> clear_values{};
            clear_values[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            clear_values[1].depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo clear_begin{};
            clear_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            clear_begin.renderPass = target_clear_pass;
            clear_begin.framebuffer = target.framebuffer;
            clear_begin.renderArea.extent = {
                target.width * static_cast<std::uint32_t>(render_scale),
                target.height * static_cast<std::uint32_t>(render_scale)};
            clear_begin.clearValueCount = static_cast<std::uint32_t>(clear_values.size());
            clear_begin.pClearValues = clear_values.data();
            vkCmdBeginRenderPass(command_buffer, &clear_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(command_buffer);
            target.initialized = true;
        }
    }

    void record_pending_batches(VkCommandBuffer command_buffer, Frame &frame) {
        std::unordered_set<std::uint32_t> visited;
        for (const DrawBatch &batch : batches) {
            if (!visited.insert(batch.target_address).second) continue;
            const auto found = render_targets.find(batch.target_address);
            if (found == render_targets.end()) {
                std::fprintf(stderr, "[Vulkan] draw batch references a missing render target: address=%08X\n",
                             batch.target_address);
                failed = true;
                return;
            }
            RenderTarget &target = found->second;
            prepare_render_target(command_buffer, target);
            VkRenderPassBeginInfo render_begin{};
            render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_begin.renderPass = target_render_pass;
            render_begin.framebuffer = target.framebuffer;
            render_begin.renderArea.extent = {
                target.width * static_cast<std::uint32_t>(render_scale),
                target.height * static_cast<std::uint32_t>(render_scale)};
            vkCmdBeginRenderPass(command_buffer, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            draw_batches(command_buffer, frame, target);
            vkCmdEndRenderPass(command_buffer);
            if (failed) return;
            target.last_draw = frame_serial + 1u;
            target.last_use = frame_serial;
        }
    }

    bool prepare_frame_buffers(Frame &frame) {
        const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vertex_data.size()) * sizeof(GpuVertex);
        const VkDeviceSize sprite_bytes = static_cast<VkDeviceSize>(sprite_data.size()) * sizeof(GpuSpriteVertex);
        if (!ensure_vertex_buffer(frame.vertices, vertex_bytes) || !ensure_vertex_buffer(frame.sprites, sprite_bytes)) {
            return false;
        }
        if (vertex_bytes != 0) std::memcpy(frame.vertices.mapped, vertex_data.data(), static_cast<std::size_t>(vertex_bytes));
        if (sprite_bytes != 0) std::memcpy(frame.sprites.mapped, sprite_data.data(), static_cast<std::size_t>(sprite_bytes));
        return true;
    }

    void mark_textures_uploaded() {
        fallback_texture.uploaded = true;
        std::vector<std::uint8_t>().swap(fallback_texture.pending_pixels);
        for (auto &[key, texture] : textures) {
            static_cast<void>(key);
            if (!texture.pending_pixels.empty()) {
                texture.uploaded = true;
                std::vector<std::uint8_t>().swap(texture.pending_pixels);
            }
        }
    }

    void clear_pending_batches() {
        vertex_data.clear();
        sprite_data.clear();
        batches.clear();
        force_new_batch = false;
        have_texture_registers = false;
        last_texture_valid = false;
        last_texture = nullptr;
    }

    bool submit_pending_batches_sync() {
        if (batches.empty()) return true;
        Frame &frame = frames[frame_index];
        VkResult result = vkWaitForFences(device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkWaitForFences(block transfer)", result);
            return false;
        }
        cleanup_frame_staging(frame);
        if (!prepare_frame_buffers(frame)) return false;
        const bool submitted = immediate_submit([&](VkCommandBuffer command) {
            if (!record_pending_texture_uploads(frame, command)) {
                failed = true;
                return;
            }
            record_pending_batches(command, frame);
        });
        if (!submitted || failed) return false;
        mark_textures_uploaded();
        cleanup_frame_staging(frame);
        for (const DrawBatch &batch : batches) {
            const auto target = render_targets.find(batch.target_address);
            if (target != render_targets.end()) target->second.last_fence = VK_NULL_HANDLE;
        }
        clear_pending_batches();
        return true;
    }

    void set_image_layout(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect,
                         VkImageLayout old_layout, VkImageLayout new_layout,
                         VkAccessFlags src_access, VkAccessFlags dst_access,
                         VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void record_swapchain_layout(VkCommandBuffer command, std::uint32_t image_index, VkImageLayout new_layout) {
        const VkImageLayout old_layout = swapchain_layouts[image_index];
        set_image_layout(command, swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                         old_layout, new_layout, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                         old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                                  : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
        swapchain_layouts[image_index] = new_layout;
    }

    VkRect2D presentation_rect() const {
        const float window_aspect = static_cast<float>(swapchain_extent.width) /
                                    static_cast<float>(swapchain_extent.height);
        float aspect = picture_aspect;
        if (aspect <= 0.0f) aspect = kPspWidth / kPspHeight;
        VkRect2D rect{};
        if (window_aspect > aspect) {
            rect.extent.height = swapchain_extent.height;
            rect.extent.width = static_cast<std::uint32_t>(static_cast<float>(rect.extent.height) * aspect);
            rect.offset.x = static_cast<std::int32_t>((swapchain_extent.width - rect.extent.width) / 2u);
        } else {
            rect.extent.width = swapchain_extent.width;
            rect.extent.height = static_cast<std::uint32_t>(static_cast<float>(rect.extent.width) / aspect);
            rect.offset.y = static_cast<std::int32_t>((swapchain_extent.height - rect.extent.height) / 2u);
        }
        return rect;
    }

    bool overlay_is_visible() const {
        if (overlay == nullptr) return false;
        std::lock_guard lock(overlay->mutex);
        return overlay->visible && overlay->width > 0 && overlay->height > 0;
    }

    bool composite_overlay() {
        if (overlay == nullptr) return true;
        std::vector<std::uint32_t> overlay_pixels;
        int overlay_width = 0;
        int overlay_height = 0;
        float overlay_aspect = 0.0f;
        {
            std::lock_guard lock(overlay->mutex);
            if (!overlay->visible) return true;
            overlay_width = overlay->width;
            overlay_height = overlay->height;
            overlay_aspect = overlay->aspect;
            if (overlay_width <= 0 || overlay_height <= 0 ||
                static_cast<std::uint64_t>(overlay_width) * static_cast<std::uint64_t>(overlay_height) >
                    overlay->pixels.size()) {
                std::fprintf(stderr, "[Vulkan] visible overlay has invalid dimensions or pixel data\n");
                return false;
            }
            overlay_pixels.assign(overlay->pixels.begin(),
                                  overlay->pixels.begin() + static_cast<std::ptrdiff_t>(
                                      static_cast<std::size_t>(overlay_width) * overlay_height));
        }
        float box_width = 480.0f;
        float box_height = 272.0f;
        if (overlay_aspect > 0.0f) {
            if (overlay_aspect > box_width / box_height) {
                box_height = box_width / overlay_aspect;
            } else {
                box_width = box_height * overlay_aspect;
            }
        }
        const float left = (480.0f - box_width) * 0.5f;
        const float top = (272.0f - box_height) * 0.5f;
        for (std::uint32_t y = 0; y < 272u; ++y) {
            const float sample_y = (static_cast<float>(y) + 0.5f - top) / box_height;
            if (sample_y < 0.0f || sample_y >= 1.0f) continue;
            const int source_y = std::clamp(static_cast<int>(sample_y * overlay_height), 0, overlay_height - 1);
            for (std::uint32_t x = 0; x < 480u; ++x) {
                const float sample_x = (static_cast<float>(x) + 0.5f - left) / box_width;
                if (sample_x < 0.0f || sample_x >= 1.0f) continue;
                const int source_x = std::clamp(static_cast<int>(sample_x * overlay_width), 0, overlay_width - 1);
                const std::uint32_t overlay_pixel =
                    overlay_pixels[static_cast<std::size_t>(source_y) * overlay_width + source_x];
                auto *base = reinterpret_cast<std::uint8_t *>(
                    &cpu_frame[static_cast<std::size_t>(y) * 480u + x]);
                const auto *over = reinterpret_cast<const std::uint8_t *>(&overlay_pixel);
                const std::uint32_t alpha = over[3];
                const std::uint32_t inverse_alpha = 255u - alpha;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    base[channel] = static_cast<std::uint8_t>(
                        std::min(255u, static_cast<std::uint32_t>(over[channel]) +
                                          (static_cast<std::uint32_t>(base[channel]) * inverse_alpha + 127u) / 255u));
                }
                base[3] = static_cast<std::uint8_t>(
                    std::min(255u, alpha + (static_cast<std::uint32_t>(base[3]) * inverse_alpha + 127u) / 255u));
            }
        }
        return true;
    }

    bool read_target_for_overlay(RenderTarget &target) {
        const std::uint32_t width = std::min(target.width, 480u);
        const std::uint32_t height = std::min(target.height, 272u);
        if (!copy_target_to_guest(target, target.address, 0u, 0u,
                                  0x04000000u + target.address, target.stride,
                                  0u, 0u, width, height)) return false;
        std::fill(cpu_frame.begin(), cpu_frame.end(), 0xFF000000u);
        const std::uint32_t bpp = target.bytes_per_pixel();
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint8_t *row = guest_memory->raw_pointer(
                0x04000000u + target.address + y * target.stride * bpp, width * bpp);
            if (row == nullptr) return false;
            for (std::uint32_t x = 0; x < width; ++x) {
                cpu_frame[static_cast<std::size_t>(y) * 480u + x] =
                    decode_framebuffer_pixel(row + x * bpp, target.format);
            }
        }
        return true;
    }

    bool upload_composited_frame() {
        Buffer staging;
        constexpr VkDeviceSize payload_size = 480u * 272u * 4u;
        if (!create_buffer(payload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, true)) return false;
        std::memcpy(staging.mapped, cpu_frame.data(), static_cast<std::size_t>(payload_size));
        const VkImageLayout old_layout = display_image_layout;
        const bool uploaded = immediate_submit([&](VkCommandBuffer command) {
            set_image_layout(command, display_image, VK_IMAGE_ASPECT_COLOR_BIT, old_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                 ? VkAccessFlags{VK_ACCESS_TRANSFER_READ_BIT}
                                 : VkAccessFlags{0},
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                                 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {480u, 272u, 1u};
            vkCmdCopyBufferToImage(command, staging.handle, display_image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            set_image_layout(command, display_image, VK_IMAGE_ASPECT_COLOR_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        });
        destroy_buffer(staging);
        if (uploaded) display_image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        return uploaded;
    }

    bool load_guest_framebuffer(Frame &frame, VkCommandBuffer command, std::uint32_t address,
                                std::uint32_t stride, std::uint32_t format) {
        const std::uint32_t bytes_per_pixel = format == 3u ? 4u : 2u;
        const std::uint32_t minimum_stride =
            (address & 0x0F000000u) == 0x04000000u || (address & 0x0F000000u) == 0x08000000u ? 480u : 512u;
        stride = std::max(stride, minimum_stride);
        const std::size_t needed = static_cast<std::size_t>(stride) * 271u * bytes_per_pixel +
                                   480u * bytes_per_pixel;
        const std::uint8_t *base = guest_memory->raw_pointer(address, needed);
        cpu_frame.resize(480u * 272u);
        if (base == nullptr) {
            std::fill(cpu_frame.begin(), cpu_frame.end(), 0xFF000000u);
        } else {
            for (std::uint32_t y = 0; y < 272u; ++y) {
                for (std::uint32_t x = 0; x < 480u; ++x) {
                    cpu_frame[static_cast<std::size_t>(y) * 480u + x] =
                        decode_framebuffer_pixel(base + (static_cast<std::size_t>(y) * stride + x) * bytes_per_pixel,
                                                 format);
                }
            }
        }
        if (!composite_overlay()) return false;
        Buffer staging;
        constexpr VkDeviceSize payload_size = 480u * 272u * 4u;
        if (!create_buffer(payload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, true)) return false;
        std::memcpy(staging.mapped, cpu_frame.data(), static_cast<std::size_t>(payload_size));
        frame.staging.push_back(staging);

        set_image_layout(command, display_image, VK_IMAGE_ASPECT_COLOR_BIT, display_image_layout,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         display_image_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                             ? VkAccessFlags{VK_ACCESS_TRANSFER_READ_BIT}
                             : VkAccessFlags{0},
                         VK_ACCESS_TRANSFER_WRITE_BIT,
                         display_image_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                                           : VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
        display_image_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {480u, 272u, 1u};
        vkCmdCopyBufferToImage(command, staging.handle, display_image, display_image_layout, 1, &copy);
        set_image_layout(command, display_image, VK_IMAGE_ASPECT_COLOR_BIT, display_image_layout,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        display_image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        return true;
    }

    void record_target_blit(VkCommandBuffer command, std::uint32_t image_index, RenderTarget &target) {
        if (target.color_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            set_image_layout(command, target.color, VK_IMAGE_ASPECT_COLOR_BIT, target.color_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            target.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        record_swapchain_layout(command, image_index, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkRect2D destination = presentation_rect();
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = {
            static_cast<std::int32_t>(std::min(target.width, 480u) * static_cast<std::uint32_t>(render_scale)),
            static_cast<std::int32_t>(std::min(target.height, 272u) * static_cast<std::uint32_t>(render_scale)), 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = {destination.offset.x, destination.offset.y, 0};
        blit.dstOffsets[1] = {
            destination.offset.x + static_cast<std::int32_t>(destination.extent.width),
            destination.offset.y + static_cast<std::int32_t>(destination.extent.height), 1};
        vkCmdBlitImage(command, target.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        set_image_layout(command, target.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        target.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    void record_display_blit(VkCommandBuffer command, std::uint32_t image_index) {
        record_swapchain_layout(command, image_index, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkRect2D destination = presentation_rect();
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = {480, 272, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = {destination.offset.x, destination.offset.y, 0};
        blit.dstOffsets[1] = {
            destination.offset.x + static_cast<std::int32_t>(destination.extent.width),
            destination.offset.y + static_cast<std::int32_t>(destination.extent.height), 1};
        vkCmdBlitImage(command, display_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }

    bool copy_target_to_guest(RenderTarget &source, std::uint32_t source_address,
                              std::uint32_t sx, std::uint32_t sy, std::uint32_t destination_address,
                              std::uint32_t destination_stride, std::uint32_t dx, std::uint32_t dy,
                              std::uint32_t width, std::uint32_t height) {
        const std::uint32_t bytes_per_pixel = source.bytes_per_pixel();
        const std::uint32_t pixel_offset = ((source_address & 0x001FFFFFu) - source.address) / bytes_per_pixel;
        const std::uint32_t source_x = pixel_offset % source.stride + sx;
        const std::uint32_t source_y = pixel_offset / source.stride + sy;
        if (source_x >= source.width || source_y >= source.height) return true;
        width = std::min(width, source.width - source_x);
        height = std::min(height, source.height - source_y);
        if (width == 0 || height == 0) return true;

        const std::uint32_t scale = static_cast<std::uint32_t>(render_scale);
        const std::uint32_t scaled_width = width * scale;
        const std::uint32_t scaled_height = height * scale;
        const VkDeviceSize byte_count = static_cast<VkDeviceSize>(scaled_width) * scaled_height * 4u;
        Buffer staging;
        if (!create_buffer(byte_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, true)) return false;
        const VkImageLayout old_layout = source.color_layout;
        const bool copied = immediate_submit([&](VkCommandBuffer command) {
            set_image_layout(command, source.color, VK_IMAGE_ASPECT_COLOR_BIT, old_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {static_cast<std::int32_t>(source_x * scale),
                                static_cast<std::int32_t>(source_y * scale), 0};
            copy.imageExtent = {scaled_width, scaled_height, 1};
            vkCmdCopyImageToBuffer(command, source.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   staging.handle, 1, &copy);
            set_image_layout(command, source.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             old_layout, VK_ACCESS_TRANSFER_READ_BIT,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT);
        });
        if (!copied) {
            destroy_buffer(staging);
            return false;
        }
        source.color_layout = old_layout;
        const auto *pixels = static_cast<const std::uint8_t *>(staging.mapped);
        const std::uint32_t sample_count = scale * scale;
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint64_t row_address =
                static_cast<std::uint64_t>(destination_address) +
                (static_cast<std::uint64_t>(dy + y) * destination_stride + dx) * bytes_per_pixel;
            if (row_address > std::numeric_limits<std::uint32_t>::max()) continue;
            std::uint8_t *destination = guest_memory->raw_pointer(static_cast<std::uint32_t>(row_address),
                                                                  static_cast<std::size_t>(width) * bytes_per_pixel);
            if (destination == nullptr) continue;
            for (std::uint32_t x = 0; x < width; ++x) {
                std::array<std::uint32_t, 4> sum{};
                for (std::uint32_t iy = 0; iy < scale; ++iy) {
                    for (std::uint32_t ix = 0; ix < scale; ++ix) {
                        const std::size_t index =
                            (static_cast<std::size_t>(y * scale + iy) * scaled_width + x * scale + ix) * 4u;
                        for (std::size_t channel = 0; channel < sum.size(); ++channel) sum[channel] += pixels[index + channel];
                    }
                }
                std::array<std::uint8_t, 4> rgba{};
                for (std::size_t channel = 0; channel < rgba.size(); ++channel) {
                    rgba[channel] = static_cast<std::uint8_t>(sum[channel] / sample_count);
                }
                encode_framebuffer_pixel(rgba.data(), source.format, destination + x * bytes_per_pixel);
            }
        }
        destroy_buffer(staging);
        return true;
    }

    bool upload_guest_rect_to_target(RenderTarget &target, std::uint32_t destination_address,
                                    std::uint32_t dx, std::uint32_t dy, std::uint32_t width,
                                    std::uint32_t height) {
        const std::uint32_t bytes_per_pixel = target.bytes_per_pixel();
        const std::uint32_t pixel_offset =
            ((destination_address & 0x001FFFFFu) - target.address) / bytes_per_pixel;
        const std::uint32_t target_x = pixel_offset % target.stride + dx;
        const std::uint32_t target_y = pixel_offset / target.stride + dy;
        if (target_x >= target.width || target_y >= target.height) return true;
        width = std::min(width, target.width - target_x);
        height = std::min(height, target.height - target_y);
        if (width == 0 || height == 0) return true;
        const std::uint32_t scale = static_cast<std::uint32_t>(render_scale);
        const std::uint32_t scaled_width = width * scale;
        const std::uint32_t scaled_height = height * scale;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(scaled_width) * scaled_height);
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint64_t row_address =
                static_cast<std::uint64_t>(destination_address) +
                (static_cast<std::uint64_t>(dy + y) * target.stride + dx) * bytes_per_pixel;
            if (row_address > std::numeric_limits<std::uint32_t>::max()) return false;
            const std::uint8_t *row = guest_memory->raw_pointer(static_cast<std::uint32_t>(row_address),
                                                                static_cast<std::size_t>(width) * bytes_per_pixel);
            if (row == nullptr) return false;
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint32_t pixel = decode_framebuffer_pixel(row + x * bytes_per_pixel, target.format);
                for (std::uint32_t iy = 0; iy < scale; ++iy) {
                    std::fill_n(pixels.begin() + static_cast<std::ptrdiff_t>((y * scale + iy) * scaled_width + x * scale),
                                scale, pixel);
                }
            }
        }
        Buffer staging;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(pixels.size()) * sizeof(std::uint32_t);
        if (!create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, true)) return false;
        std::memcpy(staging.mapped, pixels.data(), static_cast<std::size_t>(bytes));
        const VkImageLayout old_layout = target.color_layout;
        const bool uploaded = immediate_submit([&](VkCommandBuffer command) {
            set_image_layout(command, target.color, VK_IMAGE_ASPECT_COLOR_BIT, old_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {static_cast<std::int32_t>(target_x * scale),
                                static_cast<std::int32_t>(target_y * scale), 0};
            copy.imageExtent = {scaled_width, scaled_height, 1};
            vkCmdCopyBufferToImage(command, staging.handle, target.color,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            set_image_layout(command, target.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             old_layout, VK_ACCESS_TRANSFER_WRITE_BIT,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT);
        });
        destroy_buffer(staging);
        return uploaded;
    }

    bool copy_target_to_target(RenderTarget &source, std::uint32_t source_address,
                               std::uint32_t sx, std::uint32_t sy, RenderTarget &destination,
                               std::uint32_t destination_address, std::uint32_t dx, std::uint32_t dy,
                               std::uint32_t width, std::uint32_t height) {
        if (&source == &destination || source.format != destination.format) return false;
        const std::uint32_t bpp = source.bytes_per_pixel();
        const std::uint32_t source_offset = ((source_address & 0x001FFFFFu) - source.address) / bpp;
        const std::uint32_t destination_offset =
            ((destination_address & 0x001FFFFFu) - destination.address) / bpp;
        const std::uint32_t source_x = source_offset % source.stride + sx;
        const std::uint32_t source_y = source_offset / source.stride + sy;
        const std::uint32_t destination_x = destination_offset % destination.stride + dx;
        const std::uint32_t destination_y = destination_offset / destination.stride + dy;
        if (source_x >= source.width || source_y >= source.height ||
            destination_x >= destination.width || destination_y >= destination.height) return true;
        width = std::min({width, source.width - source_x, destination.width - destination_x});
        height = std::min({height, source.height - source_y, destination.height - destination_y});
        if (width == 0 || height == 0) return true;
        const std::uint32_t scale = static_cast<std::uint32_t>(render_scale);
        const VkImageLayout source_layout = source.color_layout;
        const VkImageLayout destination_layout = destination.color_layout;
        return immediate_submit([&](VkCommandBuffer command) {
            set_image_layout(command, source.color, VK_IMAGE_ASPECT_COLOR_BIT, source_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             source_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT,
                             source_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            set_image_layout(command, destination.color, VK_IMAGE_ASPECT_COLOR_BIT, destination_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             destination_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             destination_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkImageCopy copy{};
            copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.srcSubresource.layerCount = 1;
            copy.srcOffset = {static_cast<std::int32_t>(source_x * scale),
                              static_cast<std::int32_t>(source_y * scale), 0};
            copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.dstSubresource.layerCount = 1;
            copy.dstOffset = {static_cast<std::int32_t>(destination_x * scale),
                              static_cast<std::int32_t>(destination_y * scale), 0};
            copy.extent = {width * scale, height * scale, 1};
            vkCmdCopyImage(command, source.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           destination.color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            set_image_layout(command, source.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             source_layout, VK_ACCESS_TRANSFER_READ_BIT,
                             source_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             source_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT);
            set_image_layout(command, destination.color, VK_IMAGE_ASPECT_COLOR_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, destination_layout,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             destination_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             destination_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 : VK_PIPELINE_STAGE_TRANSFER_BIT);
        });
    }

    bool present_frame(std::uint32_t address, std::uint32_t stride, std::uint32_t pixel_format) {
        if (failed || device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE) return false;
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
            std::fprintf(stderr, "[Vulkan] cannot query drawable size: %s\n", SDL_GetError());
            failed = true;
            return false;
        }
        if (width <= 0 || height <= 0) return true;
        if (width != drawable_width || height != drawable_height) {
            if (!recreate_swapchain()) return false;
        }

        bool overlay_frame_ready = false;
        const auto requested_target = render_targets.find(address & 0x001FFFFFu);
        if (overlay_is_visible() && requested_target != render_targets.end() &&
            requested_target->second.last_draw >= frame_serial) {
            if (!submit_pending_batches_sync()) {
                failed = true;
                return false;
            }
            const auto target_after_submit = render_targets.find(address & 0x001FFFFFu);
            if (target_after_submit != render_targets.end()) {
                RenderTarget &target = target_after_submit->second;
                if (!read_target_for_overlay(target) || !composite_overlay() || !upload_composited_frame()) {
                    failed = true;
                    return false;
                }
                target.copied = frame_serial + 1u;
                overlay_frame_ready = true;
            }
        }

        Frame &frame = frames[frame_index];
        VkResult result = vkWaitForFences(device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkWaitForFences", result);
            failed = true;
            return false;
        }
        cleanup_frame_staging(frame);
        if (!prepare_frame_buffers(frame)) {
            failed = true;
            return false;
        }

        std::uint32_t image_index = 0;
        result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) return recreate_swapchain();
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            log_vk_failure("vkAcquireNextImageKHR", result);
            failed = true;
            return false;
        }
        const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        if (image_index >= render_finished.size() || image_index >= swapchain_layouts.size()) {
            std::fprintf(stderr, "[Vulkan] acquired swapchain image index is out of range\n");
            failed = true;
            return false;
        }

        result = vkResetCommandPool(device, frame.command_pool, 0);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkResetCommandPool", result);
            failed = true;
            return false;
        }
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(frame.command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkBeginCommandBuffer", result);
            failed = true;
            return false;
        }
        if (!record_pending_texture_uploads(frame, frame.command_buffer)) {
            failed = true;
            return false;
        }
        record_pending_batches(frame.command_buffer, frame);
        if (failed) return false;

        RenderTarget *shown = nullptr;
        const std::uint32_t shown_address = address & 0x001FFFFFu;
        const auto shown_entry = render_targets.find(shown_address);
        if (shown_entry != render_targets.end() && shown_entry->second.last_draw >= frame_serial) {
            shown = &shown_entry->second;
            shown->last_use = frame_serial;
            picture_aspect = kPspWidth / kPspHeight;
            if (frame_has_3d && live_settings != nullptr) {
                const float wide_aspect = live_settings->wide_aspect.load(std::memory_order_relaxed);
                if (wide_aspect > picture_aspect) picture_aspect = wide_aspect;
            }
            if (std::find(shown_targets.begin(), shown_targets.end(), shown_address) == shown_targets.end()) {
                if (shown_targets.size() < 3u) shown_targets.push_back(shown_address);
                else {
                    shown_targets[shown_next] = shown_address;
                    shown_next = (shown_next + 1u) % static_cast<std::uint32_t>(shown_targets.size());
                }
            }
        } else {
            picture_aspect = kPspWidth / kPspHeight;
            if (!load_guest_framebuffer(frame, frame.command_buffer, address, stride, pixel_format)) {
                failed = true;
                return false;
            }
        }

        const bool copy_shown = !overlay_frame_ready && shown != nullptr &&
                                (shown->copied == 0 || frame_serial + 1u - shown->copied >= 4u);
        if (overlay_frame_ready) {
            record_display_blit(frame.command_buffer, image_index);
        } else if (shown != nullptr) {
            record_target_blit(frame.command_buffer, image_index, *shown);
        } else {
            record_display_blit(frame.command_buffer, image_index);
        }
        set_image_layout(frame.command_buffer, swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_ACCESS_TRANSFER_WRITE_BIT, 0u, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        swapchain_layouts[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        result = vkEndCommandBuffer(frame.command_buffer);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkEndCommandBuffer", result);
            failed = true;
            return false;
        }
        result = vkResetFences(device, 1, &frame.in_flight);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkResetFences", result);
            failed = true;
            return false;
        }
        constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &frame.image_available;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &frame.command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished[image_index];
        result = vkQueueSubmit(graphics_queue, 1, &submit_info, frame.in_flight);
        if (result != VK_SUCCESS) {
            log_vk_failure("vkQueueSubmit", result);
            failed = true;
            return false;
        }
        mark_textures_uploaded();
        for (const DrawBatch &batch : batches) {
            const auto target = render_targets.find(batch.target_address);
            if (target != render_targets.end()) target->second.last_fence = frame.in_flight;
        }
        if (copy_shown) {
            if (!copy_target_to_guest(*shown, shown->address, 0u, 0u,
                                      0x04000000u + shown->address, shown->stride,
                                      0u, 0u, std::min(shown->width, 480u),
                                      std::min(shown->height, 272u))) {
                failed = true;
                return false;
            }
            shown->copied = frame_serial + 1u;
        }

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished[image_index];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;
        result = vkQueuePresentKHR(present_queue, &present_info);
        frame_index = (frame_index + 1u) % kMaxFramesInFlight;
        ++frame_serial;
        clear_pending_batches();
        frame_has_3d = false;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal) {
            return recreate_swapchain();
        }
        if (result != VK_SUCCESS) {
            log_vk_failure("vkQueuePresentKHR", result);
            failed = true;
            return false;
        }
        return true;
    }

    void shutdown() noexcept {
        if (device != VK_NULL_HANDLE) {
            const VkResult result = vkDeviceWaitIdle(device);
            if (result != VK_SUCCESS) log_vk_failure("vkDeviceWaitIdle(teardown)", result);
            destroy_pipeline_cache();
            for (auto &[address, target] : render_targets) {
                static_cast<void>(address);
                destroy_render_target(target);
            }
            render_targets.clear();
            render_target_bytes = 0;
            destroy_swapchain_resources();
            if (render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, render_pass, nullptr);
            render_pass = VK_NULL_HANDLE;
            if (target_render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, target_render_pass, nullptr);
            target_render_pass = VK_NULL_HANDLE;
            if (target_clear_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, target_clear_pass, nullptr);
            target_clear_pass = VK_NULL_HANDLE;
            for (Frame &frame : frames) {
                cleanup_frame_staging(frame);
                destroy_buffer(frame.vertices);
                destroy_buffer(frame.sprites);
                if (frame.in_flight != VK_NULL_HANDLE) vkDestroyFence(device, frame.in_flight, nullptr);
                if (frame.image_available != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.image_available, nullptr);
                if (frame.command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, frame.command_pool, nullptr);
                frame = {};
            }
            destroy_texture(fallback_texture);
            for (auto &[key, texture] : textures) {
                static_cast<void>(key);
                destroy_texture(texture);
            }
            textures.clear();
            if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            descriptor_pool = VK_NULL_HANDLE;
            if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
            descriptor_layout = VK_NULL_HANDLE;
            if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            pipeline_layout = VK_NULL_HANDLE;
            if (display_image != VK_NULL_HANDLE) vkDestroyImage(device, display_image, nullptr);
            if (display_image_memory != VK_NULL_HANDLE) vkFreeMemory(device, display_image_memory, nullptr);
            display_image = VK_NULL_HANDLE;
            display_image_memory = VK_NULL_HANDLE;
            for (VkShaderModule *module : {&draw_vertex_shader, &sprite_vertex_shader, &draw_fragment_shader,
                                           &present_vertex_shader, &present_fragment_shader}) {
                if (*module != VK_NULL_HANDLE) vkDestroyShaderModule(device, *module, nullptr);
                *module = VK_NULL_HANDLE;
            }
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (debug_messenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            const auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy_messenger != nullptr) destroy_messenger(instance, debug_messenger, nullptr);
            debug_messenger = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        if (sdl_vulkan_loaded) {
            SDL_Vulkan_UnloadLibrary();
            sdl_vulkan_loaded = false;
        }
    }
};

VulkanBackend::VulkanBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<VulkanBackend> VulkanBackend::Create(SDL_Window *window, bool vsync, int scale,
                                                      psprecomp::GuestMemory *memory,
                                                      LiveSettings *live, OverlayImage *overlay) {
    auto impl = std::make_unique<Impl>();
    auto backend = std::unique_ptr<VulkanBackend>(new VulkanBackend(std::move(impl)));
    if (!backend->impl_->initialize(window, vsync, scale, memory, live, overlay)) return nullptr;
    return backend;
}

VulkanBackend::~VulkanBackend() {
    if (impl_ != nullptr) impl_->shutdown();
}

void VulkanBackend::triangle(const Ge &ge, const Vertex &a, const Vertex &b, const Vertex &c) {
    if (impl_ != nullptr && !impl_->failed) impl_->append_triangle(ge, a, b, c);
}

void VulkanBackend::triangle_unclipped(const Ge &, const Vertex &, const Vertex &, const Vertex &) {}

void VulkanBackend::sprite(const Ge &ge, const Vertex &a, const Vertex &b) {
    if (impl_ != nullptr && !impl_->failed) impl_->append_sprite(ge, a, b);
}

void VulkanBackend::block_transfer(std::uint32_t src, std::uint32_t, std::uint32_t sx, std::uint32_t sy,
                                   std::uint32_t dst, std::uint32_t dst_stride, std::uint32_t dx, std::uint32_t dy,
                                   std::uint32_t width, std::uint32_t height, std::uint32_t bytes_per_pixel) {
    if (impl_ == nullptr || impl_->failed) return;
    if (bytes_per_pixel != 2u && bytes_per_pixel != 4u) {
        std::fprintf(stderr, "[Vulkan] block transfer has unsupported pixel size: bytes_per_pixel=%u\n",
                     bytes_per_pixel);
        impl_->failed = true;
        return;
    }
    if (!impl_->submit_pending_batches_sync()) {
        impl_->failed = true;
        return;
    }
    const bool source_vram = (src & 0x0F000000u) == 0x04000000u;
    const bool destination_vram = (dst & 0x0F000000u) == 0x04000000u;
    VulkanBackend::Impl::RenderTarget *source =
        source_vram ? impl_->find_target(src & 0x001FFFFFu, true) : nullptr;
    if (source != nullptr) {
        if (source->bytes_per_pixel() != bytes_per_pixel) {
            std::fprintf(stderr, "[Vulkan] framebuffer transfer pixel size does not match source format\n");
            impl_->failed = true;
            return;
        }
        if (!impl_->copy_target_to_guest(*source, src, sx, sy, dst, dst_stride, dx, dy, width, height)) {
            impl_->failed = true;
            return;
        }
    }
    if (destination_vram) {
        VulkanBackend::Impl::RenderTarget *destination =
            impl_->find_target(dst & 0x001FFFFFu, true);
        if (destination != nullptr) {
            if (destination->bytes_per_pixel() != bytes_per_pixel) {
                std::fprintf(stderr, "[Vulkan] failed to synchronize framebuffer transfer destination\n");
                impl_->failed = true;
                return;
            }
            const bool copied_on_gpu =
                source != nullptr &&
                impl_->copy_target_to_target(*source, src, sx, sy, *destination, dst, dx, dy, width, height);
            if (!copied_on_gpu &&
                !impl_->upload_guest_rect_to_target(*destination, dst, dx, dy, width, height)) {
                std::fprintf(stderr, "[Vulkan] failed to synchronize framebuffer transfer destination\n");
                impl_->failed = true;
                return;
            }
        }
    }
    impl_->have_texture_registers = false;
    impl_->last_texture_valid = false;
    impl_->last_texture = nullptr;
}

void VulkanBackend::flush() {
    // Keep display-list boundaries while deferring command recording until present acquires a swapchain image.
    if (impl_ != nullptr) impl_->force_new_batch = true;
}

void VulkanBackend::present(std::uint32_t address, std::uint32_t stride, std::uint32_t pixel_format) {
    if (impl_ != nullptr) static_cast<void>(impl_->present_frame(address, stride, pixel_format));
}

} // namespace mhf::psp::gpu
