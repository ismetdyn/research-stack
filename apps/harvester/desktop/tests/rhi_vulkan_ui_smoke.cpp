#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <limits>

#include <SDL3/SDL.h>

#include <harvester/platform/sdl3_imgui_platform.hpp>
#include <harvester/platform/sdl3_vulkan_presentation.hpp>
#include <harvester/rhi/rhi_loader.hpp>

namespace {

struct Vertex final {
    float position[2];
    float uv[2];
    std::uint32_t color;
};

}  // namespace

int main() {
    harvester::platform::Sdl3ImGuiPlatform platform;
    assert(platform.initialize(
        "Harvester Vulkan UI smoke",
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

    RhiUiApiV1 ui{};
    ui.struct_size = sizeof(ui);
    assert(harvester_rhi_get_ui_api_v1(plugin.api(), runtime, &ui) == RHI_RESULT_SUCCESS);

    RhiTextureCreateInfo texture_info{};
    texture_info.struct_size = sizeof(texture_info);
    texture_info.usage = RHI_TEXTURE_USAGE_SAMPLED | RHI_TEXTURE_USAGE_COPY_DST;
    texture_info.width = 1U;
    texture_info.height = 1U;
    texture_info.format = RHI_TEXTURE_FORMAT_RGBA8_UNORM;
    RhiTexture texture = RHI_TEXTURE_INVALID;
    assert(ui.create_texture(runtime, &texture_info, &texture) == RHI_RESULT_SUCCESS);
    constexpr std::array<std::uint8_t, 4> pixel{UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX};
    RhiTextureWriteInfo texture_write{};
    texture_write.struct_size = sizeof(texture_write);
    texture_write.texture = texture;
    texture_write.width = 1U;
    texture_write.height = 1U;
    texture_write.bytes_per_row = 4U;
    texture_write.data = pixel.data();
    texture_write.data_size = pixel.size();
    assert(ui.write_texture(runtime, &texture_write) == RHI_RESULT_SUCCESS);
    // Descriptor sets must return to the fixed pool when dynamic textures die.
    // This exceeds the remaining descriptor capacity if a destroyed texture leaks.
    for (std::uint32_t index = 0U; index < 64U; ++index) {
        RhiTexture transient_texture = RHI_TEXTURE_INVALID;
        assert(
            ui.create_texture(runtime, &texture_info, &transient_texture) ==
            RHI_RESULT_SUCCESS
        );
        assert(ui.destroy_texture(runtime, transient_texture) == RHI_RESULT_SUCCESS);
    }

    constexpr std::array<Vertex, 3> vertices{{
        {{-0.75F, -0.75F}, {0.0F, 0.0F}, UINT32_MAX},
        {{0.75F, -0.75F}, {1.0F, 0.0F}, UINT32_MAX},
        {{0.0F, 0.75F}, {0.5F, 1.0F}, UINT32_MAX},
    }};
    constexpr std::array<std::uint16_t, 3> indices{0U, 1U, 2U};
    RhiBufferCreateInfo vertex_info{};
    vertex_info.struct_size = sizeof(vertex_info);
    vertex_info.usage = RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_COPY_DST;
    vertex_info.byte_size = sizeof(vertices);
    RhiBuffer vertex_buffer = RHI_BUFFER_INVALID;
    assert(ui.create_buffer(runtime, &vertex_info, &vertex_buffer) == RHI_RESULT_SUCCESS);
    RhiBufferWriteInfo vertex_write{};
    vertex_write.struct_size = sizeof(vertex_write);
    vertex_write.buffer = vertex_buffer;
    vertex_write.data = vertices.data();
    vertex_write.data_size = sizeof(vertices);
    assert(ui.write_buffer(runtime, &vertex_write) == RHI_RESULT_SUCCESS);

    RhiBufferCreateInfo index_info{};
    index_info.struct_size = sizeof(index_info);
    index_info.usage = RHI_BUFFER_USAGE_INDEX | RHI_BUFFER_USAGE_COPY_DST;
    index_info.byte_size = sizeof(indices);
    RhiBuffer index_buffer = RHI_BUFFER_INVALID;
    assert(ui.create_buffer(runtime, &index_info, &index_buffer) == RHI_RESULT_SUCCESS);
    RhiBufferWriteInfo index_write{};
    index_write.struct_size = sizeof(index_write);
    index_write.buffer = index_buffer;
    index_write.data = indices.data();
    index_write.data_size = sizeof(indices);
    assert(ui.write_buffer(runtime, &index_write) == RHI_RESULT_SUCCESS);

    RhiUiPipelineCreateInfo pipeline_info{};
    pipeline_info.struct_size = sizeof(pipeline_info);
    pipeline_info.color_format = RHI_TEXTURE_FORMAT_RGBA8_UNORM;
    pipeline_info.blend_mode = RHI_UI_BLEND_ALPHA;
    RhiPipeline pipeline = RHI_PIPELINE_INVALID;
    assert(ui.create_pipeline(runtime, &pipeline_info, &pipeline) == RHI_RESULT_SUCCESS);

    RhiUiClearInfo clear{};
    clear.struct_size = sizeof(clear);
    clear.target_width = 64U;
    clear.target_height = 64U;
    clear.color_rgba[3] = 1.0F;
    assert(ui.clear_target(runtime, &clear) == RHI_RESULT_SUCCESS);
    RhiUiDraw draw{};
    draw.pipeline = pipeline;
    draw.vertex_buffer = vertex_buffer;
    draw.index_buffer = index_buffer;
    draw.texture = texture;
    draw.scissor = {0, 0, 64U, 64U};
    draw.index_count = 3U;
    RhiUiSubmitInfo submit{};
    submit.struct_size = sizeof(submit);
    submit.draw_count = 1U;
    submit.target_width = 64U;
    submit.target_height = 64U;
    submit.index_format = RHI_INDEX_FORMAT_UINT16;
    submit.draws = &draw;
    RhiFence fence = RHI_FENCE_INVALID;
    assert(ui.submit(runtime, &submit, &fence) == RHI_RESULT_SUCCESS);
    assert(
        ui.wait_fence(runtime, fence, std::numeric_limits<std::uint64_t>::max()) ==
        RHI_RESULT_SUCCESS
    );
    assert(ui.destroy_fence(runtime, fence) == RHI_RESULT_SUCCESS);

    assert(SDL_SetWindowSize(
        static_cast<SDL_Window *>(platform.native_window_for_wsi()), 96, 80
    ));
    // SDL window-size requests may be asynchronous.  Synchronize the native
    // request so this smoke test exercises the resized swapchain deterministically.
    assert(SDL_SyncWindow(static_cast<SDL_Window *>(platform.native_window_for_wsi())));
    assert(platform.poll_events());
    assert(platform.drawable_width() == 96U);
    assert(platform.drawable_height() == 80U);
    clear.target_width = platform.drawable_width();
    clear.target_height = platform.drawable_height();
    submit.draw_count = 0U;
    submit.draws = nullptr;
    submit.target_width = platform.drawable_width();
    submit.target_height = platform.drawable_height();
    for (std::uint32_t frame_index = 0U; frame_index < 4U; ++frame_index) {
        assert(ui.clear_target(runtime, &clear) == RHI_RESULT_SUCCESS);
        assert(ui.submit(runtime, &submit, &fence) == RHI_RESULT_SUCCESS);
        assert(
            ui.wait_fence(runtime, fence, std::numeric_limits<std::uint64_t>::max()) ==
            RHI_RESULT_SUCCESS
        );
        assert(ui.destroy_fence(runtime, fence) == RHI_RESULT_SUCCESS);
    }

    assert(ui.destroy_pipeline(runtime, pipeline) == RHI_RESULT_SUCCESS);
    assert(ui.destroy_buffer(runtime, index_buffer) == RHI_RESULT_SUCCESS);
    assert(ui.destroy_buffer(runtime, vertex_buffer) == RHI_RESULT_SUCCESS);
    assert(ui.destroy_texture(runtime, texture) == RHI_RESULT_SUCCESS);
    RhiDiagnostic diagnostic{};
    diagnostic.struct_size = sizeof(diagnostic);
    assert(plugin.api()->copy_diagnostic(runtime, &diagnostic) == RHI_RESULT_SUCCESS);
    assert(diagnostic.result == RHI_RESULT_SUCCESS);
    assert(plugin.api()->destroy_runtime(runtime) == RHI_RESULT_SUCCESS);
    presentation.shutdown();
    platform.shutdown();
    return 0;
}
