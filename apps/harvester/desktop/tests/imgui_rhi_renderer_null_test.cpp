#include <cassert>
#include <filesystem>

#include <imgui.h>

#include <harvester/rhi/rhi_loader.hpp>
#include <harvester/ui/imgui_rhi_renderer.hpp>

int main() {
    harvester::rhi::LoadedPlugin plugin{};
    assert(
        harvester::rhi::LoadedPlugin::load(
            harvester::rhi::RendererBackend::Null,
            std::filesystem::path{RHI_TEST_NULL_PLUGIN_DIR},
            &plugin
        ) == RHI_RESULT_SUCCESS
    );

    RhiRuntimeCreateInfo runtime_info{};
    runtime_info.struct_size = sizeof(runtime_info);
    runtime_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    runtime_info.required_features = RHI_FEATURE_DYNAMIC_TEXTURES;
    RhiRuntime runtime = RHI_RUNTIME_INVALID;
    assert(
        plugin.api()->create_runtime(&runtime_info, &runtime) ==
        RHI_RESULT_SUCCESS
    );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0F, 600.0F);
    io.DeltaTime = 1.0F / 60.0F;

    harvester::ui::ImGuiRhiRenderer renderer{plugin.api(), runtime};
    assert(renderer.initialize());
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::Begin("RHI adapter test");
        ImGui::Text("Frame %d", frame);
        ImGui::Button("Native UI");
        ImGui::End();
        ImGui::Render();
        assert(renderer.render(ImGui::GetDrawData(), 800U, 600U));
    }
    renderer.shutdown();
    ImGui::DestroyContext();

    assert(plugin.api()->destroy_runtime(runtime) == RHI_RESULT_SUCCESS);
    return 0;
}
