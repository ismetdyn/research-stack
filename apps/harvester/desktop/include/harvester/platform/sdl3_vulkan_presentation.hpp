#ifndef HARVESTER_PLATFORM_SDL3_VULKAN_PRESENTATION_HPP
#define HARVESTER_PLATFORM_SDL3_VULKAN_PRESENTATION_HPP

#include <cstdint>
#include <string>

#include <harvester/rhi/rhi_wsi.h>

namespace harvester::platform {

/*
 * SDL-specific implementation of the opaque Vulkan WSI binding. The native
 * window is deliberately accepted as void*: SDL and Vulkan types stay out of
 * the platform/RHI public boundary used by the desktop composition root.
 */
class Sdl3VulkanPresentation final {
  public:
    Sdl3VulkanPresentation() = default;
    ~Sdl3VulkanPresentation();

    Sdl3VulkanPresentation(const Sdl3VulkanPresentation &) = delete;
    Sdl3VulkanPresentation &operator=(const Sdl3VulkanPresentation &) = delete;

    [[nodiscard]] bool initialize(void *native_window);
    void shutdown() noexcept;

    [[nodiscard]] const RhiVulkanPresentationBindingV1 *binding() const noexcept;
    [[nodiscard]] const std::string &last_error() const noexcept;

  private:
    static RhiResult RHI_API_CALL get_instance_proc_addr(
        void *context,
        RhiVulkanProcAddress *out_get_instance_proc_addr
    );
    static RhiResult RHI_API_CALL get_instance_extensions(
        void *context,
        const char *const **out_extensions,
        std::uint32_t *out_extension_count
    );
    static RhiResult RHI_API_CALL create_surface(
        void *context,
        RhiVulkanInstanceHandle vulkan_instance,
        RhiVulkanSurfaceHandle *out_vulkan_surface
    );
    static RhiResult RHI_API_CALL destroy_surface(
        void *context,
        RhiVulkanInstanceHandle vulkan_instance,
        RhiVulkanSurfaceHandle vulkan_surface
    );

    void *native_window_ = nullptr;
    RhiVulkanPresentationBindingV1 binding_{};
    bool vulkan_loader_loaded_ = false;
    std::string last_error_{};
};

}  // namespace harvester::platform

#endif
