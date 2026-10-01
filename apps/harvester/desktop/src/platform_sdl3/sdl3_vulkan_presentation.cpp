#include <harvester/platform/sdl3_vulkan_presentation.hpp>

#include <cstdint>
#include <type_traits>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

namespace harvester::platform {

namespace {

template <typename Handle>
std::uint64_t native_handle_to_u64(const Handle handle) noexcept {
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
Handle native_handle_from_u64(const std::uint64_t handle) noexcept {
    static_assert(sizeof(Handle) <= sizeof(std::uint64_t));
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(static_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<Handle>(handle);
    }
}

std::string sdl_failure(const char *const operation) {
    return std::string{operation} + ": " + SDL_GetError();
}

}  // namespace

Sdl3VulkanPresentation::~Sdl3VulkanPresentation() {
    shutdown();
}

bool Sdl3VulkanPresentation::initialize(void *const native_window) {
    shutdown();
    last_error_.clear();
    if (native_window == nullptr) {
        last_error_ = "A Vulkan presentation binding requires an SDL window.";
        return false;
    }

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        last_error_ = sdl_failure("Vulkan loader initialization failed");
        return false;
    }
    vulkan_loader_loaded_ = true;
    native_window_ = native_window;
    binding_.struct_size = sizeof(binding_);
    binding_.context = this;
    binding_.get_instance_proc_addr =
        &Sdl3VulkanPresentation::get_instance_proc_addr;
    binding_.get_instance_extensions = &Sdl3VulkanPresentation::get_instance_extensions;
    binding_.create_surface = &Sdl3VulkanPresentation::create_surface;
    binding_.destroy_surface = &Sdl3VulkanPresentation::destroy_surface;
    return true;
}

void Sdl3VulkanPresentation::shutdown() noexcept {
    binding_ = {};
    native_window_ = nullptr;
    if (vulkan_loader_loaded_) {
        SDL_Vulkan_UnloadLibrary();
        vulkan_loader_loaded_ = false;
    }
}

const RhiVulkanPresentationBindingV1 *
Sdl3VulkanPresentation::binding() const noexcept {
    return binding_.struct_size == sizeof(binding_) ? &binding_ : nullptr;
}

const std::string &Sdl3VulkanPresentation::last_error() const noexcept {
    return last_error_;
}

RhiResult RHI_API_CALL Sdl3VulkanPresentation::get_instance_proc_addr(
    void *const context,
    RhiVulkanProcAddress *const out_get_instance_proc_addr
) {
    const auto *const presentation =
        static_cast<const Sdl3VulkanPresentation *>(context);
    if (presentation == nullptr || presentation->native_window_ == nullptr ||
        out_get_instance_proc_addr == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const SDL_FunctionPointer proc_address =
        SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (proc_address == nullptr) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    *out_get_instance_proc_addr =
        reinterpret_cast<RhiVulkanProcAddress>(proc_address);
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL Sdl3VulkanPresentation::get_instance_extensions(
    void *const context,
    const char *const **const out_extensions,
    std::uint32_t *const out_extension_count
) {
    const auto *const presentation =
        static_cast<const Sdl3VulkanPresentation *>(context);
    if (presentation == nullptr || presentation->native_window_ == nullptr ||
        out_extensions == nullptr || out_extension_count == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    std::uint32_t extension_count = 0U;
    const char *const *const extensions =
        SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (extensions == nullptr || extension_count == 0U) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    *out_extensions = extensions;
    *out_extension_count = extension_count;
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL Sdl3VulkanPresentation::create_surface(
    void *const context,
    const RhiVulkanInstanceHandle vulkan_instance,
    RhiVulkanSurfaceHandle *const out_vulkan_surface
) {
    const auto *const presentation =
        static_cast<const Sdl3VulkanPresentation *>(context);
    if (presentation == nullptr || presentation->native_window_ == nullptr ||
        vulkan_instance == 0U || out_vulkan_surface == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const VkInstance instance = native_handle_from_u64<VkInstance>(vulkan_instance);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(
            static_cast<SDL_Window *>(presentation->native_window_),
            instance,
            nullptr,
            &surface
        )) {
        return RHI_RESULT_INTERNAL_ERROR;
    }
    *out_vulkan_surface = native_handle_to_u64(surface);
    return RHI_RESULT_SUCCESS;
}

RhiResult RHI_API_CALL Sdl3VulkanPresentation::destroy_surface(
    void *const context,
    const RhiVulkanInstanceHandle vulkan_instance,
    const RhiVulkanSurfaceHandle vulkan_surface
) {
    const auto *const presentation =
        static_cast<const Sdl3VulkanPresentation *>(context);
    if (presentation == nullptr || presentation->native_window_ == nullptr ||
        vulkan_instance == 0U || vulkan_surface == 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    SDL_Vulkan_DestroySurface(
        native_handle_from_u64<VkInstance>(vulkan_instance),
        native_handle_from_u64<VkSurfaceKHR>(vulkan_surface),
        nullptr
    );
    return RHI_RESULT_SUCCESS;
}

}  // namespace harvester::platform
