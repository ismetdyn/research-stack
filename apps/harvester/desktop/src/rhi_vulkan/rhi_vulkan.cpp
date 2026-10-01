#include <harvester/rhi/rhi_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>

#include <volk.h>

#include "imgui_shaders_spv.hpp"

namespace {

constexpr RhiFeatureFlags kSupportedFeatures = RHI_FEATURE_DYNAMIC_TEXTURES;
constexpr std::uint32_t kMaxBuffers = 64U;
constexpr std::uint32_t kMaxTextures = 64U;
constexpr std::uint32_t kMaxPipelines = 16U;
constexpr std::uint32_t kMaxSwapchainImages = 8U;
constexpr std::uint32_t kFrameSlotCount = 2U;
constexpr std::uint32_t kMaxDrawsPerSubmit = 4096U;

static_assert(std::size(harvester::rhi_vulkan::shaders::kVertexSpirv) == 324U);
static_assert(std::size(harvester::rhi_vulkan::shaders::kFragmentSpirv) == 225U);

struct BufferSlot final {
    bool active = false;
    std::uint32_t generation = 0U;
    RhiBufferUsageFlags usage = 0U;
    std::uint64_t byte_size = 0U;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    bool memory_is_coherent = false;
};

struct TextureSlot final {
    bool active = false;
    std::uint32_t generation = 0U;
    RhiTextureUsageFlags usage = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    RhiTextureFormat format = 0U;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct PipelineSlot final {
    bool active = false;
    std::uint32_t generation = 0U;
    RhiUiPipelineCreateInfo create_info{};
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct FrameSlot final {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::uint32_t generation = 0U;
    std::uint32_t image_index = 0U;
    std::uint32_t target_width = 0U;
    std::uint32_t target_height = 0U;
    bool recording = false;
    bool fence_active = false;
    bool completed = false;
};

struct VulkanRuntime final {
    bool active = false;
    std::uint32_t generation = 0U;
    RhiFeatureFlags enabled_features = 0U;
    RhiDiagnostic diagnostic{};
    RhiVulkanPresentationBindingV1 presentation{};
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    bool validation_enabled = false;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0U;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::array<FrameSlot, kFrameSlotCount> frame_slots{};
    std::array<BufferSlot, kMaxBuffers> buffers{};
    std::array<TextureSlot, kMaxTextures> textures{};
    std::array<PipelineSlot, kMaxPipelines> pipelines{};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::array<VkImage, kMaxSwapchainImages> swapchain_images{};
    // Present does not make a frame fence prove that its wait semaphore is
    // reusable, so these semaphores remain associated with swapchain images.
    std::array<VkSemaphore, kMaxSwapchainImages> render_finished{};
    std::array<VkImageView, kMaxSwapchainImages> swapchain_views{};
    std::array<VkFramebuffer, kMaxSwapchainImages> framebuffers{};
    std::uint32_t swapchain_image_count = 0U;
    bool recreate_swapchain = false;
    VkDescriptorSetLayout image_set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout sampler_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet sampler_descriptor_set = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
};

VulkanRuntime g_runtime{};

void set_diagnostic(RhiResult result, const char *message) noexcept;

VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    const VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *const callback_data,
    void *
) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U) {
        set_diagnostic(
            RHI_RESULT_VALIDATION_FAILED,
            callback_data != nullptr && callback_data->pMessageIdName != nullptr
                ? callback_data->pMessageIdName
                : "Vulkan validation layer reported an error."
        );
    }
    return VK_FALSE;
}

[[nodiscard]] constexpr std::size_t runtime_create_info_base_size() noexcept {
    return offsetof(RhiRuntimeCreateInfo, presentation_binding_kind);
}

[[nodiscard]] constexpr bool has_capacity(
    const std::uint32_t capacity,
    const std::size_t required
) noexcept {
    return capacity >= required;
}

[[nodiscard]] RhiRuntime runtime_handle() noexcept {
    return RhiRuntime{
        (static_cast<std::uint64_t>(g_runtime.generation) << 32U) | UINT64_C(1),
    };
}

[[nodiscard]] bool is_runtime(const RhiRuntime runtime) noexcept {
    return g_runtime.active && runtime.value == runtime_handle().value;
}

void set_diagnostic(const RhiResult result, const char *const message) noexcept {
    g_runtime.diagnostic = {};
    g_runtime.diagnostic.struct_size = sizeof(g_runtime.diagnostic);
    g_runtime.diagnostic.result = result;
    const std::size_t length = std::min(
        std::strlen(message),
        sizeof(g_runtime.diagnostic.message) - 1U
    );
    std::memcpy(g_runtime.diagnostic.message, message, length);
    g_runtime.diagnostic.message[length] = '\0';
    g_runtime.diagnostic.message_length = static_cast<std::uint32_t>(length);
}

[[nodiscard]] RhiResult result_from_vk(const VkResult result) noexcept {
    switch (result) {
        case VK_SUCCESS:
            return RHI_RESULT_SUCCESS;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return RHI_RESULT_OUT_OF_MEMORY;
        case VK_ERROR_DEVICE_LOST:
            return RHI_RESULT_DEVICE_LOST;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return RHI_RESULT_SURFACE_OUT_OF_DATE;
        case VK_ERROR_EXTENSION_NOT_PRESENT:
        case VK_ERROR_LAYER_NOT_PRESENT:
        case VK_ERROR_FEATURE_NOT_PRESENT:
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return RHI_RESULT_UNSUPPORTED;
        default:
            return RHI_RESULT_INTERNAL_ERROR;
    }
}

template <typename Handle>
[[nodiscard]] std::uint64_t native_handle_to_u64(const Handle handle) noexcept {
    static_assert(sizeof(Handle) <= sizeof(std::uint64_t));
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(handle)
        );
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

template <typename Handle>
[[nodiscard]] Handle native_handle_from_u64(const std::uint64_t handle) noexcept {
    static_assert(sizeof(Handle) <= sizeof(std::uint64_t));
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(static_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<Handle>(handle);
    }
}

[[nodiscard]] constexpr std::uint64_t resource_handle_value(
    const std::uint32_t index,
    const std::uint32_t generation
) noexcept {
    return (static_cast<std::uint64_t>(generation) << 32U) |
           static_cast<std::uint64_t>(index + 1U);
}

[[nodiscard]] constexpr std::uint32_t resource_index(
    const std::uint64_t value
) noexcept {
    return static_cast<std::uint32_t>(value) - 1U;
}

[[nodiscard]] constexpr std::uint32_t resource_generation(
    const std::uint64_t value
) noexcept {
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] bool is_valid_texture_format(const RhiTextureFormat format) noexcept {
    return format == RHI_TEXTURE_FORMAT_RGBA8_UNORM ||
           format == RHI_TEXTURE_FORMAT_RGBA8_SRGB;
}

[[nodiscard]] VkFormat vk_texture_format(const RhiTextureFormat format) noexcept {
    return format == RHI_TEXTURE_FORMAT_RGBA8_SRGB ? VK_FORMAT_R8G8B8A8_SRGB :
                                                       VK_FORMAT_R8G8B8A8_UNORM;
}

[[nodiscard]] bool find_memory_type(
    const std::uint32_t memory_type_bits,
    const VkMemoryPropertyFlags required,
    std::uint32_t *const out_index,
    VkMemoryPropertyFlags *const out_properties
) noexcept {
    if (out_index == nullptr || out_properties == nullptr) {
        return false;
    }
    for (std::uint32_t index = 0U;
         index < g_runtime.memory_properties.memoryTypeCount;
         ++index) {
        const VkMemoryPropertyFlags properties =
            g_runtime.memory_properties.memoryTypes[index].propertyFlags;
        if ((memory_type_bits & (UINT32_C(1) << index)) != 0U &&
            (properties & required) == required) {
            *out_index = index;
            *out_properties = properties;
            return true;
        }
    }
    return false;
}

void destroy_buffer_slot(BufferSlot *const slot) noexcept {
    if (slot == nullptr) {
        return;
    }
    if (slot->mapped != nullptr && slot->memory != VK_NULL_HANDLE) {
        vkUnmapMemory(g_runtime.device, slot->memory);
    }
    if (slot->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_runtime.device, slot->buffer, nullptr);
    }
    if (slot->memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_runtime.device, slot->memory, nullptr);
    }
    const std::uint32_t generation = slot->generation;
    *slot = {};
    slot->generation = generation;
}

void destroy_texture_slot(TextureSlot *const slot) noexcept {
    if (slot == nullptr) {
        return;
    }
    if (slot->descriptor_set != VK_NULL_HANDLE &&
        g_runtime.descriptor_pool != VK_NULL_HANDLE) {
        (void)vkFreeDescriptorSets(
            g_runtime.device, g_runtime.descriptor_pool, 1U, &slot->descriptor_set
        );
    }
    if (slot->view != VK_NULL_HANDLE) {
        vkDestroyImageView(g_runtime.device, slot->view, nullptr);
    }
    if (slot->image != VK_NULL_HANDLE) {
        vkDestroyImage(g_runtime.device, slot->image, nullptr);
    }
    if (slot->memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_runtime.device, slot->memory, nullptr);
    }
    const std::uint32_t generation = slot->generation;
    *slot = {};
    slot->generation = generation;
}

void destroy_materialized_pipelines() noexcept {
    for (PipelineSlot &slot : g_runtime.pipelines) {
        if (slot.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(g_runtime.device, slot.pipeline, nullptr);
            slot.pipeline = VK_NULL_HANDLE;
        }
    }
}

void destroy_swapchain() noexcept {
    destroy_materialized_pipelines();
    for (VkSemaphore &semaphore : g_runtime.render_finished) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_runtime.device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    for (VkFramebuffer &framebuffer : g_runtime.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(g_runtime.device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }
    for (VkImageView &view : g_runtime.swapchain_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(g_runtime.device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    if (g_runtime.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(g_runtime.device, g_runtime.render_pass, nullptr);
        g_runtime.render_pass = VK_NULL_HANDLE;
    }
    if (g_runtime.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(g_runtime.device, g_runtime.swapchain, nullptr);
        g_runtime.swapchain = VK_NULL_HANDLE;
    }
    g_runtime.swapchain_images = {};
    g_runtime.swapchain_image_count = 0U;
    g_runtime.swapchain_format = VK_FORMAT_UNDEFINED;
    g_runtime.swapchain_extent = {};
    g_runtime.recreate_swapchain = false;
}

void destroy_ui_objects() noexcept {
    if (g_runtime.device == VK_NULL_HANDLE) {
        return;
    }
    destroy_swapchain();
    for (BufferSlot &slot : g_runtime.buffers) {
        if (slot.active) {
            destroy_buffer_slot(&slot);
        }
    }
    for (TextureSlot &slot : g_runtime.textures) {
        if (slot.active) {
            destroy_texture_slot(&slot);
        }
    }
    for (PipelineSlot &slot : g_runtime.pipelines) {
        const std::uint32_t generation = slot.generation;
        slot = {};
        slot.generation = generation;
    }
    for (FrameSlot &slot : g_runtime.frame_slots) {
        if (slot.fence != VK_NULL_HANDLE) {
            vkDestroyFence(g_runtime.device, slot.fence, nullptr);
        }
        if (slot.image_available != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_runtime.device, slot.image_available, nullptr);
        }
        slot = {};
    }
    if (g_runtime.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(g_runtime.device, g_runtime.command_pool, nullptr);
        g_runtime.command_pool = VK_NULL_HANDLE;
    }
    if (g_runtime.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(g_runtime.device, g_runtime.sampler, nullptr);
        g_runtime.sampler = VK_NULL_HANDLE;
    }
    if (g_runtime.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(g_runtime.device, g_runtime.pipeline_layout, nullptr);
        g_runtime.pipeline_layout = VK_NULL_HANDLE;
    }
    if (g_runtime.descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g_runtime.device, g_runtime.descriptor_pool, nullptr);
        g_runtime.descriptor_pool = VK_NULL_HANDLE;
    }
    if (g_runtime.image_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            g_runtime.device, g_runtime.image_set_layout, nullptr
        );
        g_runtime.image_set_layout = VK_NULL_HANDLE;
    }
    if (g_runtime.sampler_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            g_runtime.device, g_runtime.sampler_set_layout, nullptr
        );
        g_runtime.sampler_set_layout = VK_NULL_HANDLE;
    }
    g_runtime.sampler_descriptor_set = VK_NULL_HANDLE;
}

[[nodiscard]] RhiResult create_ui_objects() noexcept {
    VkDescriptorSetLayoutBinding image_binding{};
    image_binding.binding = 0U;
    image_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    image_binding.descriptorCount = 1U;
    image_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo image_layout_info{};
    image_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    image_layout_info.bindingCount = 1U;
    image_layout_info.pBindings = &image_binding;
    if (vkCreateDescriptorSetLayout(
            g_runtime.device, &image_layout_info, nullptr,
            &g_runtime.image_set_layout
        ) != VK_SUCCESS) {
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkDescriptorSetLayoutBinding sampler_binding = image_binding;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    VkDescriptorSetLayoutCreateInfo sampler_layout_info = image_layout_info;
    sampler_layout_info.pBindings = &sampler_binding;
    if (vkCreateDescriptorSetLayout(
            g_runtime.device, &sampler_layout_info, nullptr,
            &g_runtime.sampler_set_layout
        ) != VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1U},
    };
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = kMaxTextures + 1U;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;
    if (vkCreateDescriptorPool(
            g_runtime.device, &pool_info, nullptr, &g_runtime.descriptor_pool
        ) != VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    const VkDescriptorSetLayout layouts[] = {
        g_runtime.image_set_layout,
        g_runtime.sampler_set_layout,
    };
    VkPushConstantRange push_constants{};
    push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constants.offset = 0U;
    push_constants.size = 4U * sizeof(float);
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = static_cast<std::uint32_t>(std::size(layouts));
    pipeline_layout_info.pSetLayouts = layouts;
    pipeline_layout_info.pushConstantRangeCount = 1U;
    pipeline_layout_info.pPushConstantRanges = &push_constants;
    if (vkCreatePipelineLayout(
            g_runtime.device, &pipeline_layout_info, nullptr,
            &g_runtime.pipeline_layout
        ) != VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    if (vkCreateSampler(g_runtime.device, &sampler_info, nullptr, &g_runtime.sampler) !=
        VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkDescriptorSetAllocateInfo sampler_set_info{};
    sampler_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sampler_set_info.descriptorPool = g_runtime.descriptor_pool;
    sampler_set_info.descriptorSetCount = 1U;
    sampler_set_info.pSetLayouts = &g_runtime.sampler_set_layout;
    if (vkAllocateDescriptorSets(
            g_runtime.device, &sampler_set_info, &g_runtime.sampler_descriptor_set
        ) != VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkDescriptorImageInfo sampler_descriptor{};
    sampler_descriptor.sampler = g_runtime.sampler;
    VkWriteDescriptorSet sampler_write{};
    sampler_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sampler_write.dstSet = g_runtime.sampler_descriptor_set;
    sampler_write.dstBinding = 0U;
    sampler_write.descriptorCount = 1U;
    sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_write.pImageInfo = &sampler_descriptor;
    vkUpdateDescriptorSets(g_runtime.device, 1U, &sampler_write, 0U, nullptr);

    VkCommandPoolCreateInfo pool_create_info{};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create_info.queueFamilyIndex = g_runtime.queue_family;
    if (vkCreateCommandPool(
            g_runtime.device, &pool_create_info, nullptr, &g_runtime.command_pool
        ) != VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    std::array<VkCommandBuffer, kFrameSlotCount> command_buffers{};
    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = g_runtime.command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = kFrameSlotCount;
    if (vkAllocateCommandBuffers(
            g_runtime.device, &command_buffer_info, command_buffers.data()
        ) != VK_SUCCESS) {
        destroy_ui_objects();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (std::uint32_t index = 0U; index < kFrameSlotCount; ++index) {
        FrameSlot &slot = g_runtime.frame_slots[index];
        slot.command_buffer = command_buffers[index];
        if (vkCreateSemaphore(
                g_runtime.device, &semaphore_info, nullptr, &slot.image_available
            ) != VK_SUCCESS ||
            vkCreateFence(g_runtime.device, &fence_info, nullptr, &slot.fence) !=
                VK_SUCCESS) {
            destroy_ui_objects();
            return RHI_RESULT_OUT_OF_MEMORY;
        }
    }
    return RHI_RESULT_SUCCESS;
}

void destroy_native_runtime() noexcept {
    if (g_runtime.device != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(g_runtime.device);
        destroy_ui_objects();
        vkDestroyDevice(g_runtime.device, nullptr);
        g_runtime.device = VK_NULL_HANDLE;
    }
    if (g_runtime.surface != VK_NULL_HANDLE &&
        g_runtime.presentation.destroy_surface != nullptr) {
        (void)g_runtime.presentation.destroy_surface(
            g_runtime.presentation.context,
            native_handle_to_u64(g_runtime.instance),
            native_handle_to_u64(g_runtime.surface)
        );
        g_runtime.surface = VK_NULL_HANDLE;
    }
    if (g_runtime.debug_messenger != VK_NULL_HANDLE &&
        vkDestroyDebugUtilsMessengerEXT != nullptr) {
        vkDestroyDebugUtilsMessengerEXT(
            g_runtime.instance, g_runtime.debug_messenger, nullptr
        );
        g_runtime.debug_messenger = VK_NULL_HANDLE;
    }
    if (g_runtime.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(g_runtime.instance, nullptr);
        g_runtime.instance = VK_NULL_HANDLE;
    }
    volkFinalize();
    g_runtime.presentation = {};
    g_runtime.physical_device = VK_NULL_HANDLE;
    g_runtime.queue = VK_NULL_HANDLE;
    g_runtime.queue_family = 0U;
    g_runtime.properties = {};
    g_runtime.memory_properties = {};
    g_runtime.validation_enabled = false;
}

[[nodiscard]] bool has_swapchain_extension(
    const VkPhysicalDevice physical_device
) noexcept {
    std::uint32_t extension_count = 0U;
    if (vkEnumerateDeviceExtensionProperties(
            physical_device, nullptr, &extension_count, nullptr
        ) != VK_SUCCESS || extension_count == 0U || extension_count > 256U) {
        return false;
    }
    VkExtensionProperties extensions[256]{};
    if (vkEnumerateDeviceExtensionProperties(
            physical_device, nullptr, &extension_count, extensions
        ) != VK_SUCCESS) {
        return false;
    }
    for (std::uint32_t index = 0U; index < extension_count; ++index) {
        if (std::strcmp(
                extensions[index].extensionName,
                VK_KHR_SWAPCHAIN_EXTENSION_NAME
            ) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_instance_extension(const char *const name) noexcept {
    std::uint32_t count = 0U;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS ||
        count == 0U || count > 256U) {
        return false;
    }
    std::array<VkExtensionProperties, 256U> extensions{};
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &count, extensions.data()
        ) != VK_SUCCESS) {
        return false;
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (std::strcmp(extensions[index].extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_validation_layer() noexcept {
    std::uint32_t count = 0U;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS ||
        count == 0U || count > 64U) {
        return false;
    }
    std::array<VkLayerProperties, 64U> layers{};
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (std::strcmp(layers[index].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool select_presentation_device() noexcept {
    std::uint32_t device_count = 0U;
    if (vkEnumeratePhysicalDevices(g_runtime.instance, &device_count, nullptr) !=
            VK_SUCCESS ||
        device_count == 0U || device_count > 32U) {
        return false;
    }
    VkPhysicalDevice devices[32]{};
    if (vkEnumeratePhysicalDevices(
            g_runtime.instance, &device_count, devices
        ) != VK_SUCCESS) {
        return false;
    }
    for (std::uint32_t device_index = 0U; device_index < device_count;
         ++device_index) {
        const VkPhysicalDevice device = devices[device_index];
        if (!has_swapchain_extension(device)) {
            continue;
        }
        std::uint32_t queue_family_count = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(
            device, &queue_family_count, nullptr
        );
        if (queue_family_count == 0U || queue_family_count > 64U) {
            continue;
        }
        VkQueueFamilyProperties queue_families[64]{};
        vkGetPhysicalDeviceQueueFamilyProperties(
            device, &queue_family_count, queue_families
        );
        for (std::uint32_t queue_index = 0U; queue_index < queue_family_count;
             ++queue_index) {
            VkBool32 supports_presentation = VK_FALSE;
            if ((queue_families[queue_index].queueFlags &
                 VK_QUEUE_GRAPHICS_BIT) == 0U ||
                vkGetPhysicalDeviceSurfaceSupportKHR(
                    device, queue_index, g_runtime.surface, &supports_presentation
                ) != VK_SUCCESS || supports_presentation == VK_FALSE) {
                continue;
            }
            g_runtime.physical_device = device;
            g_runtime.queue_family = queue_index;
            vkGetPhysicalDeviceProperties(device, &g_runtime.properties);
            return true;
        }
    }
    return false;
}

[[nodiscard]] RhiResult create_device() noexcept {
    constexpr float queue_priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = g_runtime.queue_family;
    queue_info.queueCount = 1U;
    queue_info.pQueuePriorities = &queue_priority;
    const char *const extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1U;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1U;
    device_info.ppEnabledExtensionNames = extensions;
    const VkResult create_result = vkCreateDevice(
        g_runtime.physical_device, &device_info, nullptr, &g_runtime.device
    );
    if (create_result != VK_SUCCESS) {
        return result_from_vk(create_result);
    }
    volkLoadDevice(g_runtime.device);
    vkGetDeviceQueue(g_runtime.device, g_runtime.queue_family, 0U, &g_runtime.queue);
    return g_runtime.queue == VK_NULL_HANDLE ? RHI_RESULT_DEVICE_LOST :
                                                RHI_RESULT_SUCCESS;
}

[[nodiscard]] RhiResult validate_create_info(
    const RhiRuntimeCreateInfo *const create_info,
    RhiVulkanPresentationBindingV1 *const out_presentation
) noexcept {
    if (create_info == nullptr || out_presentation == nullptr ||
        !has_capacity(create_info->struct_size, runtime_create_info_base_size()) ||
        create_info->requested_profile != RHI_CAPABILITY_PROFILE_UI_BASELINE ||
        create_info->flags != 0U || create_info->reserved != 0U ||
        (create_info->required_features & ~kSupportedFeatures) != 0U ||
        (create_info->optional_features & ~kSupportedFeatures) != 0U ||
        !has_capacity(create_info->struct_size, sizeof(*create_info)) ||
        create_info->presentation_binding_kind !=
            RHI_PRESENTATION_BINDING_VULKAN_V1 ||
        create_info->reserved2 != 0U || create_info->presentation_binding == nullptr) {
        return RHI_RESULT_UNSUPPORTED;
    }
    const auto *const presentation = static_cast<
        const RhiVulkanPresentationBindingV1 *>(create_info->presentation_binding);
    if (harvester_rhi_validate_vulkan_presentation_binding_v1(presentation) !=
        RHI_RESULT_SUCCESS) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_presentation = *presentation;
    return RHI_RESULT_SUCCESS;
}

[[nodiscard]] RhiResult create_swapchain(
    const std::uint32_t target_width,
    const std::uint32_t target_height
) noexcept {
    VkSurfaceCapabilitiesKHR capabilities{};
    const VkResult capabilities_result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        g_runtime.physical_device, g_runtime.surface, &capabilities
    );
    if (capabilities_result != VK_SUCCESS) {
        return result_from_vk(capabilities_result);
    }
    std::uint32_t format_count = 0U;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(
            g_runtime.physical_device, g_runtime.surface, &format_count, nullptr
        ) != VK_SUCCESS ||
        format_count == 0U || format_count > 64U) {
        return RHI_RESULT_UNSUPPORTED;
    }
    std::array<VkSurfaceFormatKHR, 64U> formats{};
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(
            g_runtime.physical_device, g_runtime.surface, &format_count, formats.data()
        ) != VK_SUCCESS) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    VkSurfaceFormatKHR surface_format = formats[0];
    for (std::uint32_t index = 0U; index < format_count; ++index) {
        if (formats[index].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = formats[index];
            break;
        }
        if (formats[index].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = formats[index];
        }
    }
    std::uint32_t present_mode_count = 0U;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(
            g_runtime.physical_device,
            g_runtime.surface,
            &present_mode_count,
            nullptr
        ) != VK_SUCCESS ||
        present_mode_count == 0U || present_mode_count > 32U) {
        return RHI_RESULT_UNSUPPORTED;
    }
    std::array<VkPresentModeKHR, 32U> present_modes{};
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(
            g_runtime.physical_device,
            g_runtime.surface,
            &present_mode_count,
            present_modes.data()
        ) != VK_SUCCESS) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    bool has_fifo = false;
    for (std::uint32_t index = 0U; index < present_mode_count; ++index) {
        has_fifo = has_fifo || present_modes[index] == VK_PRESENT_MODE_FIFO_KHR;
    }
    if (!has_fifo ||
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0U) {
        return RHI_RESULT_UNSUPPORTED;
    }
    VkExtent2D extent{};
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = std::clamp(
            target_width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        );
        extent.height = std::clamp(
            target_height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        );
    }
    if (extent.width == 0U || extent.height == 0U) {
        return RHI_RESULT_SURFACE_OUT_OF_DATE;
    }
    std::uint32_t image_count = capabilities.minImageCount + 1U;
    if (capabilities.maxImageCount != 0U && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & composite_alpha) == 0U) {
        constexpr VkCompositeAlphaFlagBitsKHR candidates[] = {
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        bool found = false;
        for (const VkCompositeAlphaFlagBitsKHR candidate : candidates) {
            if ((capabilities.supportedCompositeAlpha & candidate) != 0U) {
                composite_alpha = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            return RHI_RESULT_UNSUPPORTED;
        }
    }
    VkSwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = g_runtime.surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1U;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = composite_alpha;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    const VkResult swapchain_result = vkCreateSwapchainKHR(
        g_runtime.device, &swapchain_info, nullptr, &g_runtime.swapchain
    );
    if (swapchain_result != VK_SUCCESS) {
        return result_from_vk(swapchain_result);
    }
    if (vkGetSwapchainImagesKHR(
            g_runtime.device,
            g_runtime.swapchain,
            &g_runtime.swapchain_image_count,
            nullptr
        ) != VK_SUCCESS ||
        g_runtime.swapchain_image_count == 0U ||
        g_runtime.swapchain_image_count > kMaxSwapchainImages ||
        vkGetSwapchainImagesKHR(
            g_runtime.device,
            g_runtime.swapchain,
            &g_runtime.swapchain_image_count,
            g_runtime.swapchain_images.data()
        ) != VK_SUCCESS) {
        destroy_swapchain();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    g_runtime.swapchain_format = surface_format.format;
    g_runtime.swapchain_extent = extent;
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (std::uint32_t index = 0U; index < g_runtime.swapchain_image_count;
         ++index) {
        if (vkCreateSemaphore(
                g_runtime.device,
                &semaphore_info,
                nullptr,
                &g_runtime.render_finished[index]
            ) != VK_SUCCESS) {
            destroy_swapchain();
            return RHI_RESULT_OUT_OF_MEMORY;
        }
    }
    VkAttachmentDescription attachment{};
    attachment.format = g_runtime.swapchain_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color_reference{};
    color_reference.attachment = 0U;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1U;
    subpass.pColorAttachments = &color_reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0U;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1U;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1U;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1U;
    render_pass_info.pDependencies = &dependency;
    if (vkCreateRenderPass(
            g_runtime.device, &render_pass_info, nullptr, &g_runtime.render_pass
        ) != VK_SUCCESS) {
        destroy_swapchain();
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    for (std::uint32_t index = 0U; index < g_runtime.swapchain_image_count;
         ++index) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = g_runtime.swapchain_images[index];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = g_runtime.swapchain_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1U;
        view_info.subresourceRange.layerCount = 1U;
        if (vkCreateImageView(
                g_runtime.device, &view_info, nullptr, &g_runtime.swapchain_views[index]
            ) != VK_SUCCESS) {
            destroy_swapchain();
            return RHI_RESULT_OUT_OF_MEMORY;
        }
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = g_runtime.render_pass;
        framebuffer_info.attachmentCount = 1U;
        framebuffer_info.pAttachments = &g_runtime.swapchain_views[index];
        framebuffer_info.width = g_runtime.swapchain_extent.width;
        framebuffer_info.height = g_runtime.swapchain_extent.height;
        framebuffer_info.layers = 1U;
        if (vkCreateFramebuffer(
                g_runtime.device,
                &framebuffer_info,
                nullptr,
                &g_runtime.framebuffers[index]
            ) != VK_SUCCESS) {
            destroy_swapchain();
            return RHI_RESULT_OUT_OF_MEMORY;
        }
    }
    return RHI_RESULT_SUCCESS;
}

[[nodiscard]] RhiResult ensure_swapchain(
    const std::uint32_t target_width,
    const std::uint32_t target_height
) noexcept {
    if (g_runtime.swapchain != VK_NULL_HANDLE && !g_runtime.recreate_swapchain &&
        g_runtime.swapchain_extent.width == target_width &&
        g_runtime.swapchain_extent.height == target_height) {
        return RHI_RESULT_SUCCESS;
    }
    if (vkDeviceWaitIdle(g_runtime.device) != VK_SUCCESS) {
        return RHI_RESULT_DEVICE_LOST;
    }
    destroy_swapchain();
    return create_swapchain(target_width, target_height);
}

[[nodiscard]] RhiResult materialize_pipeline(PipelineSlot *const slot) noexcept {
    if (slot == nullptr || !slot->active || g_runtime.render_pass == VK_NULL_HANDLE) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (slot->pipeline != VK_NULL_HANDLE) {
        return RHI_RESULT_SUCCESS;
    }
    VkShaderModuleCreateInfo vertex_module_info{};
    vertex_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_module_info.codeSize = sizeof(harvester::rhi_vulkan::shaders::kVertexSpirv);
    vertex_module_info.pCode = harvester::rhi_vulkan::shaders::kVertexSpirv;
    VkShaderModule vertex_module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(
            g_runtime.device, &vertex_module_info, nullptr, &vertex_module
        ) != VK_SUCCESS) {
        return RHI_RESULT_UNSUPPORTED;
    }
    VkShaderModuleCreateInfo fragment_module_info{};
    fragment_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_module_info.codeSize = sizeof(harvester::rhi_vulkan::shaders::kFragmentSpirv);
    fragment_module_info.pCode = harvester::rhi_vulkan::shaders::kFragmentSpirv;
    VkShaderModule fragment_module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(
            g_runtime.device, &fragment_module_info, nullptr, &fragment_module
        ) != VK_SUCCESS) {
        vkDestroyShaderModule(g_runtime.device, vertex_module, nullptr);
        return RHI_RESULT_UNSUPPORTED;
    }
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         nullptr,
         0U,
         VK_SHADER_STAGE_VERTEX_BIT,
         vertex_module,
         "main",
         nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         nullptr,
         0U,
         VK_SHADER_STAGE_FRAGMENT_BIT,
         fragment_module,
         "main",
         nullptr},
    };
    VkVertexInputBindingDescription binding{};
    binding.binding = 0U;
    binding.stride = sizeof(RhiUiVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    const VkVertexInputAttributeDescription attributes[] = {
        {0U, 0U, VK_FORMAT_R32G32_SFLOAT, offsetof(RhiUiVertex, position)},
        {1U, 0U, VK_FORMAT_R32G32_SFLOAT, offsetof(RhiUiVertex, uv)},
        {2U, 0U, VK_FORMAT_R8G8B8A8_UNORM, offsetof(RhiUiVertex, color_rgba8)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1U;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(std::size(attributes));
    vertex_input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1U;
    viewport_state.scissorCount = 1U;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1U;
    color_blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
    dynamic_state.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<std::uint32_t>(std::size(stages));
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = g_runtime.pipeline_layout;
    pipeline_info.renderPass = g_runtime.render_pass;
    const VkResult pipeline_result = vkCreateGraphicsPipelines(
        g_runtime.device, VK_NULL_HANDLE, 1U, &pipeline_info, nullptr, &slot->pipeline
    );
    vkDestroyShaderModule(g_runtime.device, fragment_module, nullptr);
    vkDestroyShaderModule(g_runtime.device, vertex_module, nullptr);
    return result_from_vk(pipeline_result);
}

RhiResult RHI_API_CALL get_plugin_info(RhiPluginInfo *out_info);
RhiResult RHI_API_CALL create_runtime(
    const RhiRuntimeCreateInfo *create_info, RhiRuntime *out_runtime
);
RhiResult RHI_API_CALL destroy_runtime(RhiRuntime runtime);
RhiResult RHI_API_CALL get_runtime_capabilities(
    RhiRuntime runtime, RhiRuntimeCapabilities *out_capabilities
);
RhiResult RHI_API_CALL copy_diagnostic(
    RhiRuntime runtime, RhiDiagnostic *out_diagnostic
);
RhiResult RHI_API_CALL get_ui_api(RhiRuntime runtime, RhiUiApiV1 *out_ui_api);
RhiResult RHI_API_CALL create_buffer(
    RhiRuntime runtime,
    const RhiBufferCreateInfo *create_info,
    RhiBuffer *out_buffer
);
RhiResult RHI_API_CALL destroy_buffer(RhiRuntime runtime, RhiBuffer buffer);
RhiResult RHI_API_CALL create_texture(
    RhiRuntime runtime,
    const RhiTextureCreateInfo *create_info,
    RhiTexture *out_texture
);
RhiResult RHI_API_CALL destroy_texture(RhiRuntime runtime, RhiTexture texture);
RhiResult RHI_API_CALL create_pipeline(
    RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *create_info,
    RhiPipeline *out_pipeline
);
RhiResult RHI_API_CALL destroy_pipeline(RhiRuntime runtime, RhiPipeline pipeline);
RhiResult RHI_API_CALL write_buffer(
    RhiRuntime runtime,
    const RhiBufferWriteInfo *write_info
);
RhiResult RHI_API_CALL write_texture(
    RhiRuntime runtime,
    const RhiTextureWriteInfo *write_info
);
RhiResult RHI_API_CALL submit(
    RhiRuntime runtime,
    const RhiUiSubmitInfo *submit_info,
    RhiFence *out_fence
);
RhiResult RHI_API_CALL wait_fence(
    RhiRuntime runtime, RhiFence fence, std::uint64_t timeout_nanoseconds
);
RhiResult RHI_API_CALL destroy_fence(RhiRuntime runtime, RhiFence fence);
RhiResult RHI_API_CALL clear_target(
    RhiRuntime runtime, const RhiUiClearInfo *clear_info
);

const RhiUiApiV1 kUiApi = {
    sizeof(kUiApi),
    HARVESTER_RHI_UI_API_VERSION,
    create_buffer,
    destroy_buffer,
    create_texture,
    destroy_texture,
    create_pipeline,
    destroy_pipeline,
    submit,
    wait_fence,
    destroy_fence,
    write_buffer,
    write_texture,
    clear_target,
};

const RhiPluginApiV1 kPluginApi = {
    sizeof(kPluginApi), HARVESTER_RHI_ABI_VERSION, get_plugin_info,
    create_runtime, destroy_runtime, get_runtime_capabilities, copy_diagnostic,
    get_ui_api,
};

RhiResult RHI_API_CALL get_plugin_info(RhiPluginInfo *const out_info) {
    if (out_info == nullptr || !has_capacity(out_info->struct_size, sizeof(*out_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t capacity = out_info->struct_size;
    *out_info = {};
    out_info->struct_size = capacity;
    out_info->abi_version = HARVESTER_RHI_ABI_VERSION;
    out_info->pointer_width = static_cast<std::uint32_t>(sizeof(void *) * 8U);
    out_info->threading_model = RHI_THREADING_EXTERNAL_SYNCHRONIZATION;
    out_info->supported_features = kSupportedFeatures;
    out_info->supported_profiles = UINT64_C(1) << RHI_CAPABILITY_PROFILE_UI_BASELINE;
    std::memcpy(out_info->backend_name, "vulkan", sizeof("vulkan"));
    std::memcpy(out_info->backend_version, "1.0", sizeof("1.0"));
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL create_runtime(
    const RhiRuntimeCreateInfo *const create_info, RhiRuntime *const out_runtime
) {
    if (out_runtime == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_runtime = RHI_RUNTIME_INVALID;
    if (g_runtime.active) {
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    RhiVulkanPresentationBindingV1 presentation{};
    const RhiResult validation = validate_create_info(create_info, &presentation);
    if (validation != RHI_RESULT_SUCCESS) {
        return validation;
    }
    RhiVulkanProcAddress raw_proc_address = nullptr;
    if (presentation.get_instance_proc_addr(
            presentation.context, &raw_proc_address
        ) != RHI_RESULT_SUCCESS || raw_proc_address == nullptr) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    volkInitializeCustom(reinterpret_cast<PFN_vkGetInstanceProcAddr>(raw_proc_address));
    if (vkCreateInstance == nullptr) {
        volkFinalize();
        return RHI_RESULT_INTERNAL_ERROR;
    }
    set_diagnostic(RHI_RESULT_SUCCESS, "");
    const char *const *extensions = nullptr;
    std::uint32_t extension_count = 0U;
    if (presentation.get_instance_extensions(
            presentation.context, &extensions, &extension_count
        ) != RHI_RESULT_SUCCESS || extensions == nullptr || extension_count == 0U ||
        extension_count > 64U) {
        volkFinalize();
        return RHI_RESULT_INTERNAL_ERROR;
    }
    std::array<const char *, 65U> instance_extensions{};
    std::copy_n(extensions, extension_count, instance_extensions.begin());
    bool enable_validation = false;
#if !defined(NDEBUG)
    enable_validation = has_validation_layer() &&
                        has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    if (enable_validation) {
        bool has_debug_utils = false;
        for (std::uint32_t index = 0U; index < extension_count; ++index) {
            has_debug_utils = has_debug_utils ||
                std::strcmp(
                    instance_extensions[index], VK_EXT_DEBUG_UTILS_EXTENSION_NAME
                ) == 0;
        }
        if (!has_debug_utils) {
            instance_extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
    }
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Research Harvester";
    app_info.applicationVersion = 1U;
    app_info.pEngineName = "Harvester RHI";
    app_info.engineVersion = 1U;
    app_info.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = instance_extensions.data();
    const char *const validation_layer = "VK_LAYER_KHRONOS_validation";
    if (enable_validation) {
        instance_info.enabledLayerCount = 1U;
        instance_info.ppEnabledLayerNames = &validation_layer;
    }
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info{};
    if (enable_validation) {
        debug_messenger_info.sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_messenger_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_messenger_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_messenger_info.pfnUserCallback = validation_callback;
        instance_info.pNext = &debug_messenger_info;
    }
    const VkResult instance_result = vkCreateInstance(
        &instance_info, nullptr, &g_runtime.instance
    );
    if (instance_result != VK_SUCCESS) {
        volkFinalize();
        return result_from_vk(instance_result);
    }
    volkLoadInstance(g_runtime.instance);
    if (enable_validation &&
        (vkCreateDebugUtilsMessengerEXT == nullptr ||
         vkCreateDebugUtilsMessengerEXT(
             g_runtime.instance,
             &debug_messenger_info,
             nullptr,
             &g_runtime.debug_messenger
         ) != VK_SUCCESS)) {
        destroy_native_runtime();
        return RHI_RESULT_UNSUPPORTED;
    }
    g_runtime.validation_enabled = enable_validation;
    g_runtime.presentation = presentation;
    RhiVulkanSurfaceHandle surface = 0U;
    if (presentation.create_surface(
            presentation.context, native_handle_to_u64(g_runtime.instance), &surface
        ) != RHI_RESULT_SUCCESS || surface == 0U) {
        destroy_native_runtime();
        return RHI_RESULT_INTERNAL_ERROR;
    }
    g_runtime.surface = native_handle_from_u64<VkSurfaceKHR>(surface);
    if (!select_presentation_device()) {
        destroy_native_runtime();
        return RHI_RESULT_UNSUPPORTED;
    }
    const RhiResult device_result = create_device();
    if (device_result != RHI_RESULT_SUCCESS) {
        destroy_native_runtime();
        return device_result;
    }
    vkGetPhysicalDeviceMemoryProperties(
        g_runtime.physical_device, &g_runtime.memory_properties
    );
    const RhiResult ui_result = create_ui_objects();
    if (ui_result != RHI_RESULT_SUCCESS) {
        destroy_native_runtime();
        return ui_result;
    }
    if (g_runtime.diagnostic.result == RHI_RESULT_VALIDATION_FAILED) {
        destroy_native_runtime();
        return RHI_RESULT_VALIDATION_FAILED;
    }
    ++g_runtime.generation;
    if (g_runtime.generation == 0U) {
        ++g_runtime.generation;
    }
    g_runtime.enabled_features =
        create_info->required_features | create_info->optional_features;
    g_runtime.active = true;
    set_diagnostic(RHI_RESULT_SUCCESS, "");
    *out_runtime = runtime_handle();
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL destroy_runtime(const RhiRuntime runtime) {
    if (!is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    destroy_native_runtime();
    g_runtime.active = false;
    g_runtime.enabled_features = 0U;
    set_diagnostic(RHI_RESULT_SUCCESS, "");
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL get_runtime_capabilities(
    const RhiRuntime runtime, RhiRuntimeCapabilities *const out_capabilities
) {
    if (!is_runtime(runtime) || out_capabilities == nullptr ||
        !has_capacity(out_capabilities->struct_size, sizeof(*out_capabilities))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t capacity = out_capabilities->struct_size;
    *out_capabilities = {};
    out_capabilities->struct_size = capacity;
    out_capabilities->enabled_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    out_capabilities->enabled_features = g_runtime.enabled_features;
    out_capabilities->limits.struct_size = sizeof(out_capabilities->limits);
    out_capabilities->limits.max_texture_dimension_2d =
        g_runtime.properties.limits.maxImageDimension2D;
    out_capabilities->limits.max_texture_array_layers =
        g_runtime.properties.limits.maxImageArrayLayers;
    out_capabilities->limits.max_bindings_per_group =
        g_runtime.properties.limits.maxBoundDescriptorSets;
    out_capabilities->limits.uniform_buffer_offset_alignment =
        g_runtime.properties.limits.minUniformBufferOffsetAlignment >
                std::numeric_limits<std::uint32_t>::max() ?
            std::numeric_limits<std::uint32_t>::max() :
            static_cast<std::uint32_t>(
                g_runtime.properties.limits.minUniformBufferOffsetAlignment
            );
    out_capabilities->limits.max_color_attachments =
        g_runtime.properties.limits.maxColorAttachments;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL copy_diagnostic(
    const RhiRuntime runtime, RhiDiagnostic *const out_diagnostic
) {
    if (!is_runtime(runtime) || out_diagnostic == nullptr ||
        !has_capacity(out_diagnostic->struct_size, sizeof(*out_diagnostic))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t capacity = out_diagnostic->struct_size;
    *out_diagnostic = g_runtime.diagnostic;
    out_diagnostic->struct_size = capacity;
    return RHI_RESULT_SUCCESS;
}

[[nodiscard]] BufferSlot *find_buffer(const RhiBuffer handle) noexcept {
    if (handle.value == 0U) {
        return nullptr;
    }
    const std::uint32_t index = resource_index(handle.value);
    if (index >= g_runtime.buffers.size()) {
        return nullptr;
    }
    BufferSlot &slot = g_runtime.buffers[index];
    return slot.active && slot.generation == resource_generation(handle.value) ?
        &slot : nullptr;
}

[[nodiscard]] TextureSlot *find_texture(const RhiTexture handle) noexcept {
    if (handle.value == 0U) {
        return nullptr;
    }
    const std::uint32_t index = resource_index(handle.value);
    if (index >= g_runtime.textures.size()) {
        return nullptr;
    }
    TextureSlot &slot = g_runtime.textures[index];
    return slot.active && slot.generation == resource_generation(handle.value) ?
        &slot : nullptr;
}

[[nodiscard]] PipelineSlot *find_pipeline(const RhiPipeline handle) noexcept {
    if (handle.value == 0U) {
        return nullptr;
    }
    const std::uint32_t index = resource_index(handle.value);
    if (index >= g_runtime.pipelines.size()) {
        return nullptr;
    }
    PipelineSlot &slot = g_runtime.pipelines[index];
    return slot.active && slot.generation == resource_generation(handle.value) ?
        &slot : nullptr;
}

[[nodiscard]] FrameSlot *find_fence(const RhiFence handle) noexcept {
    if (handle.value == 0U) {
        return nullptr;
    }
    const std::uint32_t index = resource_index(handle.value);
    if (index >= g_runtime.frame_slots.size()) {
        return nullptr;
    }
    FrameSlot &slot = g_runtime.frame_slots[index];
    return slot.fence_active && slot.generation == resource_generation(handle.value) ?
        &slot : nullptr;
}

[[nodiscard]] RhiResult create_mapped_buffer(
    const VkDeviceSize byte_size,
    const VkBufferUsageFlags usage,
    BufferSlot *const out_slot
) noexcept {
    if (out_slot == nullptr || byte_size == 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = byte_size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_runtime.device, &buffer_info, nullptr, &out_slot->buffer) !=
        VK_SUCCESS) {
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(g_runtime.device, out_slot->buffer, &requirements);
    std::uint32_t memory_type = 0U;
    VkMemoryPropertyFlags memory_properties = 0U;
    if (!find_memory_type(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memory_type,
            &memory_properties
        ) &&
        !find_memory_type(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &memory_type,
            &memory_properties
        )) {
        destroy_buffer_slot(out_slot);
        return RHI_RESULT_UNSUPPORTED;
    }
    VkMemoryAllocateInfo allocation_info{};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.allocationSize = requirements.size;
    allocation_info.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(
            g_runtime.device, &allocation_info, nullptr, &out_slot->memory
        ) != VK_SUCCESS ||
        vkBindBufferMemory(
            g_runtime.device, out_slot->buffer, out_slot->memory, 0U
        ) != VK_SUCCESS ||
        vkMapMemory(
            g_runtime.device,
            out_slot->memory,
            0U,
            VK_WHOLE_SIZE,
            0U,
            &out_slot->mapped
        ) != VK_SUCCESS) {
        destroy_buffer_slot(out_slot);
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    out_slot->memory_is_coherent =
        (memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;
    return RHI_RESULT_SUCCESS;
}

void flush_buffer_range(
    const BufferSlot &slot,
    const VkDeviceSize offset,
    const VkDeviceSize size
) noexcept {
    if (slot.memory_is_coherent || size == 0U) {
        return;
    }
    const VkDeviceSize atom_size =
        g_runtime.properties.limits.nonCoherentAtomSize;
    const VkDeviceSize aligned_offset = offset - (offset % atom_size);
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = slot.memory;
    range.offset = aligned_offset;
    range.size = VK_WHOLE_SIZE;
    (void)vkFlushMappedMemoryRanges(g_runtime.device, 1U, &range);
}

[[nodiscard]] RhiResult upload_texture_data(
    TextureSlot *const texture,
    const RhiTextureWriteInfo *const write_info
) noexcept {
    const VkDeviceSize staging_size = static_cast<VkDeviceSize>(write_info->data_size);
    BufferSlot staging{};
    RhiResult result = create_mapped_buffer(
        staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging
    );
    if (result != RHI_RESULT_SUCCESS) {
        return result;
    }
    std::memcpy(staging.mapped, write_info->data, write_info->data_size);
    flush_buffer_range(staging, 0U, staging_size);
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = g_runtime.command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1U;
    if (vkAllocateCommandBuffers(
            g_runtime.device, &command_buffer_info, &command_buffer
        ) != VK_SUCCESS) {
        destroy_buffer_slot(&staging);
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        vkFreeCommandBuffers(g_runtime.device, g_runtime.command_pool, 1U, &command_buffer);
        destroy_buffer_slot(&staging);
        return RHI_RESULT_INTERNAL_ERROR;
    }
    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.oldLayout = texture->layout;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = texture->image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1U;
    to_transfer.subresourceRange.layerCount = 1U;
    to_transfer.srcAccessMask = static_cast<VkAccessFlags>(0U);
    if (texture->layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
        command_buffer,
        texture->layout == VK_IMAGE_LAYOUT_UNDEFINED ?
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &to_transfer
    );
    VkBufferImageCopy copy{};
    copy.bufferRowLength = write_info->bytes_per_row / 4U;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1U;
    copy.imageOffset = {
        static_cast<std::int32_t>(write_info->x),
        static_cast<std::int32_t>(write_info->y),
        0,
    };
    copy.imageExtent = {write_info->width, write_info->height, 1U};
    vkCmdCopyBufferToImage(
        command_buffer,
        staging.buffer,
        texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1U,
        &copy
    );
    VkImageMemoryBarrier to_sampled = to_transfer;
    to_sampled.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_sampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_sampled.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_sampled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &to_sampled
    );
    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(g_runtime.device, g_runtime.command_pool, 1U, &command_buffer);
        destroy_buffer_slot(&staging);
        return RHI_RESULT_INTERNAL_ERROR;
    }
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1U;
    submit_info.pCommandBuffers = &command_buffer;
    const VkResult submit_result = vkQueueSubmit(
        g_runtime.queue, 1U, &submit_info, VK_NULL_HANDLE
    );
    const VkResult idle_result = submit_result == VK_SUCCESS ?
        vkQueueWaitIdle(g_runtime.queue) : submit_result;
    vkFreeCommandBuffers(g_runtime.device, g_runtime.command_pool, 1U, &command_buffer);
    destroy_buffer_slot(&staging);
    if (idle_result != VK_SUCCESS) {
        return result_from_vk(idle_result);
    }
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL get_ui_api(
    const RhiRuntime runtime,
    RhiUiApiV1 *const out_ui_api
) {
    if (!is_runtime(runtime) || out_ui_api == nullptr ||
        !has_capacity(
            out_ui_api->struct_size,
            offsetof(RhiUiApiV1, write_texture) + sizeof(out_ui_api->write_texture)
        )) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t capacity = out_ui_api->struct_size;
    std::memset(out_ui_api, 0, capacity);
    std::memcpy(out_ui_api, &kUiApi, std::min<std::size_t>(capacity, sizeof(kUiApi)));
    out_ui_api->struct_size = capacity;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL create_buffer(
    const RhiRuntime runtime,
    const RhiBufferCreateInfo *const create_info,
    RhiBuffer *const out_buffer
) {
    if (out_buffer == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_buffer = RHI_BUFFER_INVALID;
    if (!is_runtime(runtime) || create_info == nullptr ||
        !has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    constexpr RhiBufferUsageFlags allowed_usage =
        RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_INDEX | RHI_BUFFER_USAGE_COPY_DST;
    if (create_info->byte_size == 0U || create_info->usage == 0U ||
        (create_info->usage & ~allowed_usage) != 0U ||
        create_info->reserved != 0U || create_info->reserved2 != 0U) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    for (std::uint32_t index = 0U; index < g_runtime.buffers.size(); ++index) {
        BufferSlot &slot = g_runtime.buffers[index];
        if (slot.active) {
            continue;
        }
        ++slot.generation;
        if (slot.generation == 0U) {
            ++slot.generation;
        }
        slot.active = true;
        slot.usage = create_info->usage;
        slot.byte_size = create_info->byte_size;
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if ((slot.usage & RHI_BUFFER_USAGE_VERTEX) != 0U) {
            usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        }
        if ((slot.usage & RHI_BUFFER_USAGE_INDEX) != 0U) {
            usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }
        const RhiResult result = create_mapped_buffer(
            static_cast<VkDeviceSize>(slot.byte_size), usage, &slot
        );
        if (result != RHI_RESULT_SUCCESS) {
            destroy_buffer_slot(&slot);
            return result;
        }
        out_buffer->value = resource_handle_value(index, slot.generation);
        return RHI_RESULT_SUCCESS;
    }
    return RHI_RESULT_OUT_OF_MEMORY;
}

RhiResult RHI_API_CALL destroy_buffer(
    const RhiRuntime runtime,
    const RhiBuffer buffer
) {
    if (!is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    BufferSlot *const slot = find_buffer(buffer);
    if (slot == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (vkDeviceWaitIdle(g_runtime.device) != VK_SUCCESS) {
        return RHI_RESULT_DEVICE_LOST;
    }
    destroy_buffer_slot(slot);
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL create_texture(
    const RhiRuntime runtime,
    const RhiTextureCreateInfo *const create_info,
    RhiTexture *const out_texture
) {
    if (out_texture == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_texture = RHI_TEXTURE_INVALID;
    if (!is_runtime(runtime) || create_info == nullptr ||
        !has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    constexpr RhiTextureUsageFlags allowed_usage =
        RHI_TEXTURE_USAGE_SAMPLED | RHI_TEXTURE_USAGE_COPY_DST;
    if (create_info->width == 0U || create_info->height == 0U ||
        create_info->reserved != 0U || !is_valid_texture_format(create_info->format) ||
        (create_info->usage & RHI_TEXTURE_USAGE_SAMPLED) == 0U ||
        (create_info->usage & ~allowed_usage) != 0U ||
        create_info->width > g_runtime.properties.limits.maxImageDimension2D ||
        create_info->height > g_runtime.properties.limits.maxImageDimension2D) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    for (std::uint32_t index = 0U; index < g_runtime.textures.size(); ++index) {
        TextureSlot &slot = g_runtime.textures[index];
        if (slot.active) {
            continue;
        }
        ++slot.generation;
        if (slot.generation == 0U) {
            ++slot.generation;
        }
        slot.active = true;
        slot.usage = create_info->usage;
        slot.width = create_info->width;
        slot.height = create_info->height;
        slot.format = create_info->format;
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = vk_texture_format(slot.format);
        image_info.extent = {slot.width, slot.height, 1U};
        image_info.mipLevels = 1U;
        image_info.arrayLayers = 1U;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_runtime.device, &image_info, nullptr, &slot.image) !=
            VK_SUCCESS) {
            destroy_texture_slot(&slot);
            return RHI_RESULT_OUT_OF_MEMORY;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(g_runtime.device, slot.image, &requirements);
        std::uint32_t memory_type = 0U;
        VkMemoryPropertyFlags memory_properties = 0U;
        if (!find_memory_type(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &memory_type,
                &memory_properties
            ) &&
            !find_memory_type(
                requirements.memoryTypeBits, 0U, &memory_type, &memory_properties
            )) {
            destroy_texture_slot(&slot);
            return RHI_RESULT_UNSUPPORTED;
        }
        VkMemoryAllocateInfo allocation_info{};
        allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation_info.allocationSize = requirements.size;
        allocation_info.memoryTypeIndex = memory_type;
        if (vkAllocateMemory(
                g_runtime.device, &allocation_info, nullptr, &slot.memory
            ) != VK_SUCCESS ||
            vkBindImageMemory(g_runtime.device, slot.image, slot.memory, 0U) !=
                VK_SUCCESS) {
            destroy_texture_slot(&slot);
            return RHI_RESULT_OUT_OF_MEMORY;
        }
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = slot.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = vk_texture_format(slot.format);
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1U;
        view_info.subresourceRange.layerCount = 1U;
        if (vkCreateImageView(g_runtime.device, &view_info, nullptr, &slot.view) !=
            VK_SUCCESS) {
            destroy_texture_slot(&slot);
            return RHI_RESULT_OUT_OF_MEMORY;
        }
        VkDescriptorSetAllocateInfo descriptor_info{};
        descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_info.descriptorPool = g_runtime.descriptor_pool;
        descriptor_info.descriptorSetCount = 1U;
        descriptor_info.pSetLayouts = &g_runtime.image_set_layout;
        if (vkAllocateDescriptorSets(
                g_runtime.device, &descriptor_info, &slot.descriptor_set
            ) != VK_SUCCESS) {
            destroy_texture_slot(&slot);
            return RHI_RESULT_OUT_OF_MEMORY;
        }
        VkDescriptorImageInfo image_descriptor{};
        image_descriptor.imageView = slot.view;
        image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet image_write{};
        image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        image_write.dstSet = slot.descriptor_set;
        image_write.dstBinding = 0U;
        image_write.descriptorCount = 1U;
        image_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        image_write.pImageInfo = &image_descriptor;
        vkUpdateDescriptorSets(g_runtime.device, 1U, &image_write, 0U, nullptr);
        out_texture->value = resource_handle_value(index, slot.generation);
        return RHI_RESULT_SUCCESS;
    }
    return RHI_RESULT_OUT_OF_MEMORY;
}

RhiResult RHI_API_CALL destroy_texture(
    const RhiRuntime runtime,
    const RhiTexture texture
) {
    if (!is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    TextureSlot *const slot = find_texture(texture);
    if (slot == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (vkDeviceWaitIdle(g_runtime.device) != VK_SUCCESS) {
        return RHI_RESULT_DEVICE_LOST;
    }
    destroy_texture_slot(slot);
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL create_pipeline(
    const RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *const create_info,
    RhiPipeline *const out_pipeline
) {
    if (out_pipeline == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_pipeline = RHI_PIPELINE_INVALID;
    if (!is_runtime(runtime) || create_info == nullptr ||
        !has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (create_info->reserved != 0U ||
        create_info->blend_mode != RHI_UI_BLEND_ALPHA ||
        !is_valid_texture_format(create_info->color_format)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    for (std::uint32_t index = 0U; index < g_runtime.pipelines.size(); ++index) {
        PipelineSlot &slot = g_runtime.pipelines[index];
        if (slot.active) {
            continue;
        }
        ++slot.generation;
        if (slot.generation == 0U) {
            ++slot.generation;
        }
        slot.active = true;
        slot.create_info = *create_info;
        out_pipeline->value = resource_handle_value(index, slot.generation);
        return RHI_RESULT_SUCCESS;
    }
    return RHI_RESULT_OUT_OF_MEMORY;
}

RhiResult RHI_API_CALL destroy_pipeline(
    const RhiRuntime runtime,
    const RhiPipeline pipeline
) {
    if (!is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    PipelineSlot *const slot = find_pipeline(pipeline);
    if (slot == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (vkDeviceWaitIdle(g_runtime.device) != VK_SUCCESS) {
        return RHI_RESULT_DEVICE_LOST;
    }
    if (slot->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(g_runtime.device, slot->pipeline, nullptr);
    }
    const std::uint32_t generation = slot->generation;
    *slot = {};
    slot->generation = generation;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL write_buffer(
    const RhiRuntime runtime,
    const RhiBufferWriteInfo *const write_info
) {
    if (!is_runtime(runtime) || write_info == nullptr ||
        !has_capacity(write_info->struct_size, sizeof(*write_info)) ||
        write_info->reserved != 0U || write_info->data == nullptr ||
        write_info->data_size == 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    BufferSlot *const slot = find_buffer(write_info->buffer);
    if (slot == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if ((slot->usage & RHI_BUFFER_USAGE_COPY_DST) == 0U ||
        write_info->offset > slot->byte_size ||
        write_info->data_size > slot->byte_size - write_info->offset ||
        write_info->data_size > std::numeric_limits<std::size_t>::max()) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    std::memcpy(
        static_cast<std::byte *>(slot->mapped) + write_info->offset,
        write_info->data,
        static_cast<std::size_t>(write_info->data_size)
    );
    flush_buffer_range(
        *slot,
        static_cast<VkDeviceSize>(write_info->offset),
        static_cast<VkDeviceSize>(write_info->data_size)
    );
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL write_texture(
    const RhiRuntime runtime,
    const RhiTextureWriteInfo *const write_info
) {
    if (!is_runtime(runtime) || write_info == nullptr ||
        !has_capacity(write_info->struct_size, sizeof(*write_info)) ||
        write_info->reserved != 0U || write_info->data == nullptr ||
        write_info->width == 0U || write_info->height == 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    TextureSlot *const slot = find_texture(write_info->texture);
    if (slot == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(write_info->width) * 4U;
    if ((slot->usage & RHI_TEXTURE_USAGE_COPY_DST) == 0U ||
        write_info->bytes_per_row < row_bytes ||
        (write_info->bytes_per_row % 4U) != 0U ||
        write_info->x > slot->width || write_info->y > slot->height ||
        write_info->width > slot->width - write_info->x ||
        write_info->height > slot->height - write_info->y ||
        write_info->x > static_cast<std::uint32_t>(INT32_MAX) ||
        write_info->y > static_cast<std::uint32_t>(INT32_MAX) ||
        (write_info->height > 1U &&
         write_info->bytes_per_row >
             (std::numeric_limits<std::uint64_t>::max() - row_bytes) /
                 static_cast<std::uint64_t>(write_info->height - 1U))) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const std::uint64_t minimum_size =
        static_cast<std::uint64_t>(write_info->height - 1U) *
            write_info->bytes_per_row +
        row_bytes;
    if (write_info->data_size < minimum_size ||
        write_info->data_size > std::numeric_limits<std::size_t>::max()) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    for (const FrameSlot &frame : g_runtime.frame_slots) {
        if (frame.recording) {
            return RHI_RESULT_VALIDATION_FAILED;
        }
    }
    return upload_texture_data(slot, write_info);
}

[[nodiscard]] RhiResult validate_draw(
    const RhiUiSubmitInfo *const submit_info,
    const RhiUiDraw *const draw
) noexcept {
    if (draw == nullptr || draw->reserved != 0U || draw->index_count == 0U ||
        draw->scissor.x < 0 || draw->scissor.y < 0 ||
        draw->scissor.width == 0U || draw->scissor.height == 0U ||
        static_cast<std::uint32_t>(draw->scissor.x) > submit_info->target_width ||
        static_cast<std::uint32_t>(draw->scissor.y) > submit_info->target_height ||
        draw->scissor.width >
            submit_info->target_width - static_cast<std::uint32_t>(draw->scissor.x) ||
        draw->scissor.height >
            submit_info->target_height - static_cast<std::uint32_t>(draw->scissor.y)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    BufferSlot *const vertex_buffer = find_buffer(draw->vertex_buffer);
    BufferSlot *const index_buffer = find_buffer(draw->index_buffer);
    TextureSlot *const texture = find_texture(draw->texture);
    PipelineSlot *const pipeline = find_pipeline(draw->pipeline);
    if (vertex_buffer == nullptr || index_buffer == nullptr || texture == nullptr ||
        pipeline == nullptr ||
        (vertex_buffer->usage & RHI_BUFFER_USAGE_VERTEX) == 0U ||
        (index_buffer->usage & RHI_BUFFER_USAGE_INDEX) == 0U ||
        texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const std::uint64_t index_size = submit_info->index_format == RHI_INDEX_FORMAT_UINT16 ?
        2U : 4U;
    if (draw->first_index > std::numeric_limits<std::uint64_t>::max() / index_size) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const std::uint64_t first_byte =
        static_cast<std::uint64_t>(draw->first_index) * index_size;
    if (first_byte > index_buffer->byte_size ||
        draw->index_count > (index_buffer->byte_size - first_byte) / index_size) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL clear_target(
    const RhiRuntime runtime,
    const RhiUiClearInfo *const clear_info
) {
    if (!is_runtime(runtime) || clear_info == nullptr ||
        !has_capacity(clear_info->struct_size, sizeof(*clear_info)) ||
        clear_info->target_width == 0U || clear_info->target_height == 0U ||
        clear_info->reserved != 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    for (const float component : clear_info->color_rgba) {
        if (!std::isfinite(component)) {
            return RHI_RESULT_VALIDATION_FAILED;
        }
    }
    FrameSlot *frame = nullptr;
    for (FrameSlot &candidate : g_runtime.frame_slots) {
        if (!candidate.recording && !candidate.fence_active) {
            frame = &candidate;
            break;
        }
    }
    if (frame == nullptr) {
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    RhiResult result = ensure_swapchain(
        clear_info->target_width, clear_info->target_height
    );
    if (result != RHI_RESULT_SUCCESS) {
        return result;
    }
    VkResult acquire_result = vkAcquireNextImageKHR(
        g_runtime.device,
        g_runtime.swapchain,
        std::numeric_limits<std::uint64_t>::max(),
        frame->image_available,
        VK_NULL_HANDLE,
        &frame->image_index
    );
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        g_runtime.recreate_swapchain = true;
        result = ensure_swapchain(clear_info->target_width, clear_info->target_height);
        if (result != RHI_RESULT_SUCCESS) {
            return result;
        }
        acquire_result = vkAcquireNextImageKHR(
            g_runtime.device,
            g_runtime.swapchain,
            std::numeric_limits<std::uint64_t>::max(),
            frame->image_available,
            VK_NULL_HANDLE,
            &frame->image_index
        );
    }
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
        return result_from_vk(acquire_result);
    }
    g_runtime.recreate_swapchain = acquire_result == VK_SUBOPTIMAL_KHR;
    if (frame->image_index >= g_runtime.swapchain_image_count ||
        vkResetCommandBuffer(frame->command_buffer, 0U) != VK_SUCCESS) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    VkCommandBufferBeginInfo command_begin{};
    command_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(frame->command_buffer, &command_begin) != VK_SUCCESS) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    VkClearValue clear_value{};
    std::memcpy(clear_value.color.float32, clear_info->color_rgba, sizeof(clear_info->color_rgba));
    VkRenderPassBeginInfo pass_begin{};
    pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass_begin.renderPass = g_runtime.render_pass;
    pass_begin.framebuffer = g_runtime.framebuffers[frame->image_index];
    pass_begin.renderArea.extent = g_runtime.swapchain_extent;
    pass_begin.clearValueCount = 1U;
    pass_begin.pClearValues = &clear_value;
    vkCmdBeginRenderPass(
        frame->command_buffer, &pass_begin, VK_SUBPASS_CONTENTS_INLINE
    );
    frame->recording = true;
    frame->target_width = clear_info->target_width;
    frame->target_height = clear_info->target_height;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL submit(
    const RhiRuntime runtime,
    const RhiUiSubmitInfo *const submit_info,
    RhiFence *const out_fence
) {
    if (out_fence == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_fence = RHI_FENCE_INVALID;
    if (!is_runtime(runtime) || submit_info == nullptr ||
        !has_capacity(submit_info->struct_size, sizeof(*submit_info)) ||
        (submit_info->draw_count != 0U && submit_info->draws == nullptr)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (submit_info->reserved != 0U || submit_info->reserved2 != 0U ||
        submit_info->draw_count > kMaxDrawsPerSubmit ||
        submit_info->target_width == 0U || submit_info->target_height == 0U ||
        (submit_info->index_format != RHI_INDEX_FORMAT_UINT16 &&
         submit_info->index_format != RHI_INDEX_FORMAT_UINT32)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    FrameSlot *frame = nullptr;
    for (FrameSlot &candidate : g_runtime.frame_slots) {
        if (candidate.recording) {
            frame = &candidate;
            break;
        }
    }
    if (frame == nullptr || frame->target_width != submit_info->target_width ||
        frame->target_height != submit_info->target_height) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    for (std::uint32_t index = 0U; index < submit_info->draw_count; ++index) {
        RhiResult result = validate_draw(submit_info, &submit_info->draws[index]);
        if (result != RHI_RESULT_SUCCESS) {
            return result;
        }
        result = materialize_pipeline(find_pipeline(submit_info->draws[index].pipeline));
        if (result != RHI_RESULT_SUCCESS) {
            return result;
        }
    }
    VkViewport viewport{};
    viewport.width = static_cast<float>(g_runtime.swapchain_extent.width);
    viewport.height = static_cast<float>(g_runtime.swapchain_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(frame->command_buffer, 0U, 1U, &viewport);
    const float push_constants[] = {
        2.0F / static_cast<float>(submit_info->target_width),
        2.0F / static_cast<float>(submit_info->target_height),
        -1.0F,
        -1.0F,
    };
    for (std::uint32_t index = 0U; index < submit_info->draw_count; ++index) {
        const RhiUiDraw &draw = submit_info->draws[index];
        const BufferSlot *const vertex_buffer = find_buffer(draw.vertex_buffer);
        const BufferSlot *const index_buffer = find_buffer(draw.index_buffer);
        const TextureSlot *const texture = find_texture(draw.texture);
        const PipelineSlot *const pipeline = find_pipeline(draw.pipeline);
        const VkDeviceSize vertex_offset = 0U;
        const VkDescriptorSet descriptor_sets[] = {
            texture->descriptor_set,
            g_runtime.sampler_descriptor_set,
        };
        const VkRect2D scissor{
            {draw.scissor.x, draw.scissor.y},
            {draw.scissor.width, draw.scissor.height},
        };
        vkCmdBindPipeline(
            frame->command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline->pipeline
        );
        vkCmdBindVertexBuffers(
            frame->command_buffer, 0U, 1U, &vertex_buffer->buffer, &vertex_offset
        );
        vkCmdBindIndexBuffer(
            frame->command_buffer,
            index_buffer->buffer,
            0U,
            submit_info->index_format == RHI_INDEX_FORMAT_UINT16 ?
                VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32
        );
        vkCmdBindDescriptorSets(
            frame->command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            g_runtime.pipeline_layout,
            0U,
            static_cast<std::uint32_t>(std::size(descriptor_sets)),
            descriptor_sets,
            0U,
            nullptr
        );
        vkCmdPushConstants(
            frame->command_buffer,
            g_runtime.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0U,
            sizeof(push_constants),
            push_constants
        );
        vkCmdSetScissor(frame->command_buffer, 0U, 1U, &scissor);
        vkCmdDrawIndexed(
            frame->command_buffer,
            draw.index_count,
            1U,
            draw.first_index,
            draw.vertex_offset,
            0U
        );
    }
    vkCmdEndRenderPass(frame->command_buffer);
    if (vkEndCommandBuffer(frame->command_buffer) != VK_SUCCESS ||
        vkResetFences(g_runtime.device, 1U, &frame->fence) != VK_SUCCESS) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore &render_finished = g_runtime.render_finished[frame->image_index];
    VkSubmitInfo queue_submit{};
    queue_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    queue_submit.waitSemaphoreCount = 1U;
    queue_submit.pWaitSemaphores = &frame->image_available;
    queue_submit.pWaitDstStageMask = &wait_stage;
    queue_submit.commandBufferCount = 1U;
    queue_submit.pCommandBuffers = &frame->command_buffer;
    queue_submit.signalSemaphoreCount = 1U;
    queue_submit.pSignalSemaphores = &render_finished;
    const VkResult queue_result = vkQueueSubmit(
        g_runtime.queue, 1U, &queue_submit, frame->fence
    );
    if (queue_result != VK_SUCCESS) {
        frame->recording = false;
        return result_from_vk(queue_result);
    }
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1U;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1U;
    present_info.pSwapchains = &g_runtime.swapchain;
    present_info.pImageIndices = &frame->image_index;
    const VkResult present_result = vkQueuePresentKHR(g_runtime.queue, &present_info);
    frame->recording = false;
    frame->fence_active = true;
    frame->completed = false;
    ++frame->generation;
    if (frame->generation == 0U) {
        ++frame->generation;
    }
    out_fence->value = resource_handle_value(
        static_cast<std::uint32_t>(frame - g_runtime.frame_slots.data()),
        frame->generation
    );
    if (present_result == VK_SUBOPTIMAL_KHR || present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        g_runtime.recreate_swapchain = true;
        return RHI_RESULT_SUCCESS;
    }
    if (present_result != VK_SUCCESS) {
        *out_fence = RHI_FENCE_INVALID;
        return result_from_vk(present_result);
    }
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL wait_fence(
    const RhiRuntime runtime,
    const RhiFence fence,
    const std::uint64_t timeout_nanoseconds
) {
    if (!is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    FrameSlot *const frame = find_fence(fence);
    if (frame == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const VkResult wait_result = vkWaitForFences(
        g_runtime.device, 1U, &frame->fence, VK_TRUE, timeout_nanoseconds
    );
    if (wait_result == VK_TIMEOUT) {
        return RHI_RESULT_TIMEOUT;
    }
    if (wait_result != VK_SUCCESS) {
        return result_from_vk(wait_result);
    }
    frame->completed = true;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL destroy_fence(
    const RhiRuntime runtime,
    const RhiFence fence
) {
    if (!is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    FrameSlot *const frame = find_fence(fence);
    if (frame == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (!frame->completed) {
        const VkResult wait_result = vkWaitForFences(
            g_runtime.device,
            1U,
            &frame->fence,
            VK_TRUE,
            std::numeric_limits<std::uint64_t>::max()
        );
        if (wait_result != VK_SUCCESS) {
            return result_from_vk(wait_result);
        }
    }
    frame->fence_active = false;
    frame->completed = false;
    frame->target_width = 0U;
    frame->target_height = 0U;
    return RHI_RESULT_SUCCESS;
}

}  // namespace

extern "C" RHI_PLUGIN_EXPORT const RhiPluginApiV1 *RHI_API_CALL
harvester_rhi_get_plugin_api(const std::uint32_t requested_abi_version) {
    return requested_abi_version == HARVESTER_RHI_ABI_VERSION ? &kPluginApi :
                                                                 nullptr;
}
