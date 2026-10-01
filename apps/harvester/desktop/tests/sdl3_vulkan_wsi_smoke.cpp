#include <cassert>
#include <cstdint>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

#include <harvester/platform/sdl3_imgui_platform.hpp>
#include <harvester/platform/sdl3_vulkan_presentation.hpp>

int main() {
    harvester::platform::Sdl3ImGuiPlatform platform;
    assert(platform.initialize(
        "Harvester Vulkan WSI smoke",
        64U,
        64U,
        harvester::platform::Sdl3PresentationBackend::Vulkan
    ));

    harvester::platform::Sdl3VulkanPresentation presentation;
    assert(presentation.initialize(platform.native_window_for_wsi()));
    const RhiVulkanPresentationBindingV1 *const binding = presentation.binding();
    assert(binding != nullptr);

    const char *const *extensions = nullptr;
    std::uint32_t extension_count = 0U;
    assert(
        binding->get_instance_extensions(
            binding->context,
            &extensions,
            &extension_count
        ) == RHI_RESULT_SUCCESS
    );
    assert(extensions != nullptr);
    assert(extension_count > 0U);

    RhiVulkanProcAddress raw_get_instance_proc_address = nullptr;
    assert(
        binding->get_instance_proc_addr(
            binding->context,
            &raw_get_instance_proc_address
        ) == RHI_RESULT_SUCCESS
    );
    const auto get_instance_proc_address =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            raw_get_instance_proc_address
        );
    assert(get_instance_proc_address != nullptr);
    const auto create_instance =
        reinterpret_cast<PFN_vkCreateInstance>(
            get_instance_proc_address(VK_NULL_HANDLE, "vkCreateInstance")
        );
    assert(create_instance != nullptr);

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = extensions;
    VkInstance instance = VK_NULL_HANDLE;
    assert(create_instance(&instance_info, nullptr, &instance) == VK_SUCCESS);

    RhiVulkanSurfaceHandle surface = 0U;
    assert(
        binding->create_surface(
            binding->context,
            static_cast<RhiVulkanInstanceHandle>(
                reinterpret_cast<std::uintptr_t>(instance)
            ),
            &surface
        ) == RHI_RESULT_SUCCESS
    );
    assert(surface != 0U);
    assert(
        binding->destroy_surface(
            binding->context,
            static_cast<RhiVulkanInstanceHandle>(
                reinterpret_cast<std::uintptr_t>(instance)
            ),
            surface
        ) == RHI_RESULT_SUCCESS
    );

    const auto destroy_instance =
        reinterpret_cast<PFN_vkDestroyInstance>(
            get_instance_proc_address(instance, "vkDestroyInstance")
        );
    assert(destroy_instance != nullptr);
    destroy_instance(instance, nullptr);

    presentation.shutdown();
    platform.shutdown();
    return 0;
}
