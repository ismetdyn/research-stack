#include <cassert>
#include <filesystem>

#include <harvester/platform/sdl3_imgui_platform.hpp>
#include <harvester/platform/sdl3_vulkan_presentation.hpp>
#include <harvester/rhi/rhi_loader.hpp>

int main() {
    harvester::platform::Sdl3ImGuiPlatform platform;
    assert(platform.initialize(
        "Harvester Vulkan runtime smoke",
        64U,
        64U,
        harvester::platform::Sdl3PresentationBackend::Vulkan
    ));

    harvester::platform::Sdl3VulkanPresentation presentation;
    assert(presentation.initialize(platform.native_window_for_wsi()));

    harvester::rhi::LoadedPlugin plugin;
    assert(
        harvester::rhi::LoadedPlugin::load(
            harvester::rhi::RendererBackend::Vulkan,
            std::filesystem::path{RHI_TEST_VULKAN_PLUGIN_DIR},
            &plugin
        ) == RHI_RESULT_SUCCESS
    );

    RhiRuntimeCreateInfo create_info{};
    create_info.struct_size = sizeof(create_info);
    create_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    create_info.required_features = RHI_FEATURE_DYNAMIC_TEXTURES;
    create_info.presentation_binding_kind = RHI_PRESENTATION_BINDING_VULKAN_V1;
    create_info.presentation_binding = presentation.binding();
    RhiRuntime runtime = RHI_RUNTIME_INVALID;
    assert(plugin.api()->create_runtime(&create_info, &runtime) == RHI_RESULT_SUCCESS);

    RhiRuntimeCapabilities capabilities{};
    capabilities.struct_size = sizeof(capabilities);
    assert(
        plugin.api()->get_runtime_capabilities(runtime, &capabilities) ==
        RHI_RESULT_SUCCESS
    );
    assert(capabilities.enabled_profile == RHI_CAPABILITY_PROFILE_UI_BASELINE);
    assert(
        (capabilities.enabled_features & RHI_FEATURE_DYNAMIC_TEXTURES) != 0U
    );
    assert(capabilities.limits.max_texture_dimension_2d > 0U);

    assert(plugin.api()->destroy_runtime(runtime) == RHI_RESULT_SUCCESS);
    presentation.shutdown();
    platform.shutdown();
    return 0;
}
