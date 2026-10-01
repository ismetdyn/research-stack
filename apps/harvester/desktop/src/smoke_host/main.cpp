#include <filesystem>

#include <harvester/rhi/rhi_loader.hpp>

int main() {
    harvester::rhi::LoadedPlugin plugin{};
    if (
        harvester::rhi::LoadedPlugin::load(
            harvester::rhi::RendererBackend::Null,
            std::filesystem::path{RHI_SMOKE_NULL_PLUGIN_DIR},
            &plugin
        ) != RHI_RESULT_SUCCESS
    ) {
        return 1;
    }

    RhiRuntimeCreateInfo create_info{};
    create_info.struct_size = sizeof(create_info);
    create_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;

    RhiRuntime runtime = RHI_RUNTIME_INVALID;
    if (plugin.api()->create_runtime(&create_info, &runtime) != RHI_RESULT_SUCCESS) {
        return 1;
    }

    return plugin.api()->destroy_runtime(runtime) == RHI_RESULT_SUCCESS ? 0 : 1;
}
