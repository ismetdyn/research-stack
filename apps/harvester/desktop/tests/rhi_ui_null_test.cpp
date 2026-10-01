#include <cassert>
#include <cstddef>
#include <limits>
#include <filesystem>

#include <harvester/rhi/rhi_loader.hpp>
#include <harvester/rhi/rhi_ui.h>

int main() {
    harvester::rhi::LoadedPlugin plugin{};
    assert(
        harvester::rhi::LoadedPlugin::load(
            harvester::rhi::RendererBackend::Null,
            std::filesystem::path{RHI_TEST_NULL_PLUGIN_DIR},
            &plugin
        ) == RHI_RESULT_SUCCESS
    );

    RhiRuntimeCreateInfo legacy_runtime_info{};
    legacy_runtime_info.struct_size = static_cast<std::uint32_t>(
        offsetof(RhiRuntimeCreateInfo, presentation_binding_kind)
    );
    legacy_runtime_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    RhiRuntime legacy_runtime = RHI_RUNTIME_INVALID;
    assert(
        plugin.api()->create_runtime(&legacy_runtime_info, &legacy_runtime) ==
        RHI_RESULT_SUCCESS
    );
    assert(plugin.api()->destroy_runtime(legacy_runtime) == RHI_RESULT_SUCCESS);

    RhiRuntimeCreateInfo presentation_runtime_info{};
    presentation_runtime_info.struct_size = sizeof(presentation_runtime_info);
    presentation_runtime_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    presentation_runtime_info.presentation_binding_kind =
        RHI_PRESENTATION_BINDING_VULKAN_V1;
    presentation_runtime_info.presentation_binding =
        reinterpret_cast<const void *>(static_cast<std::uintptr_t>(1U));
    RhiRuntime presentation_runtime = RHI_RUNTIME_INVALID;
    assert(
        plugin.api()->create_runtime(
            &presentation_runtime_info,
            &presentation_runtime
        ) == RHI_RESULT_UNSUPPORTED
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

    RhiUiApiV1 ui{};
    ui.struct_size = sizeof(ui);
    assert(harvester_rhi_get_ui_api_v1(plugin.api(), runtime, &ui) == RHI_RESULT_SUCCESS);
    assert(ui.clear_target != nullptr);
    RhiUiClearInfo clear_info{};
    clear_info.struct_size = sizeof(clear_info);
    clear_info.target_width = 1U;
    clear_info.target_height = 1U;
    clear_info.color_rgba[3] = 1.0F;
    assert(ui.clear_target(runtime, &clear_info) == RHI_RESULT_SUCCESS);
    clear_info.color_rgba[0] = std::numeric_limits<float>::infinity();
    assert(
        ui.clear_target(runtime, &clear_info) == RHI_RESULT_VALIDATION_FAILED
    );

    RhiUiSubmitInfo empty_submit{};
    empty_submit.struct_size = sizeof(empty_submit);
    empty_submit.target_width = 1U;
    empty_submit.target_height = 1U;
    empty_submit.index_format = RHI_INDEX_FORMAT_UINT16;
    RhiFence empty_fence = RHI_FENCE_INVALID;
    assert(
        ui.submit(runtime, &empty_submit, &empty_fence) == RHI_RESULT_SUCCESS
    );
    assert(empty_fence.value != 0U);
    assert(
        ui.wait_fence(
            runtime,
            empty_fence,
            std::numeric_limits<std::uint64_t>::max()
        ) == RHI_RESULT_SUCCESS
    );
    assert(ui.destroy_fence(runtime, empty_fence) == RHI_RESULT_SUCCESS);

    RhiBufferCreateInfo buffer_info{};
    buffer_info.struct_size = sizeof(buffer_info);
    buffer_info.byte_size = 1024U;
    buffer_info.usage = RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_INDEX |
                        RHI_BUFFER_USAGE_COPY_DST;
    RhiBuffer vertex_buffer = RHI_BUFFER_INVALID;
    RhiBuffer index_buffer = RHI_BUFFER_INVALID;
    assert(ui.create_buffer(runtime, &buffer_info, &vertex_buffer) == RHI_RESULT_SUCCESS);
    assert(ui.create_buffer(runtime, &buffer_info, &index_buffer) == RHI_RESULT_SUCCESS);

    unsigned char buffer_data[1024]{};
    RhiBufferWriteInfo buffer_write{};
    buffer_write.struct_size = sizeof(buffer_write);
    buffer_write.buffer = vertex_buffer;
    buffer_write.data = buffer_data;
    buffer_write.data_size = sizeof(buffer_data);
    assert(ui.write_buffer(runtime, &buffer_write) == RHI_RESULT_SUCCESS);
    buffer_write.buffer = index_buffer;
    assert(ui.write_buffer(runtime, &buffer_write) == RHI_RESULT_SUCCESS);

    RhiTextureCreateInfo texture_info{};
    texture_info.struct_size = sizeof(texture_info);
    texture_info.width = 1U;
    texture_info.height = 1U;
    texture_info.format = RHI_TEXTURE_FORMAT_RGBA8_SRGB;
    texture_info.usage = RHI_TEXTURE_USAGE_SAMPLED | RHI_TEXTURE_USAGE_COPY_DST;
    RhiTexture texture = RHI_TEXTURE_INVALID;
    assert(ui.create_texture(runtime, &texture_info, &texture) == RHI_RESULT_SUCCESS);

    unsigned char texture_data[4]{};
    RhiTextureWriteInfo texture_write{};
    texture_write.struct_size = sizeof(texture_write);
    texture_write.texture = texture;
    texture_write.width = 1U;
    texture_write.height = 1U;
    texture_write.bytes_per_row = sizeof(texture_data);
    texture_write.data = texture_data;
    texture_write.data_size = sizeof(texture_data);
    assert(ui.write_texture(runtime, &texture_write) == RHI_RESULT_SUCCESS);

    RhiUiPipelineCreateInfo pipeline_info{};
    pipeline_info.struct_size = sizeof(pipeline_info);
    pipeline_info.color_format = RHI_TEXTURE_FORMAT_RGBA8_SRGB;
    pipeline_info.blend_mode = RHI_UI_BLEND_ALPHA;
    RhiPipeline pipeline = RHI_PIPELINE_INVALID;
    assert(ui.create_pipeline(runtime, &pipeline_info, &pipeline) == RHI_RESULT_SUCCESS);

    RhiUiDraw invalid_draw{};
    invalid_draw.vertex_buffer = vertex_buffer;
    invalid_draw.index_buffer = index_buffer;
    invalid_draw.texture = texture;
    invalid_draw.index_count = 3U;
    invalid_draw.scissor.width = 1U;
    invalid_draw.scissor.height = 1U;
    RhiUiSubmitInfo invalid_submit{};
    invalid_submit.struct_size = sizeof(invalid_submit);
    invalid_submit.draws = &invalid_draw;
    invalid_submit.draw_count = 1U;
    invalid_submit.target_width = 1U;
    invalid_submit.target_height = 1U;
    invalid_submit.index_format = RHI_INDEX_FORMAT_UINT16;
    RhiFence invalid_fence = RHI_FENCE_INVALID;
    assert(
        ui.submit(runtime, &invalid_submit, &invalid_fence) ==
        RHI_RESULT_VALIDATION_FAILED
    );
    assert(invalid_fence.value == 0U);

    invalid_draw.pipeline = pipeline;
    RhiRuntime other_runtime = RHI_RUNTIME_INVALID;
    assert(
        plugin.api()->create_runtime(&runtime_info, &other_runtime) ==
        RHI_RESULT_SUCCESS
    );
    RhiBuffer foreign_buffer = RHI_BUFFER_INVALID;
    assert(
        ui.create_buffer(other_runtime, &buffer_info, &foreign_buffer) ==
        RHI_RESULT_SUCCESS
    );
    RhiUiDraw cross_runtime_draw = invalid_draw;
    cross_runtime_draw.vertex_buffer = foreign_buffer;
    RhiUiSubmitInfo cross_runtime_submit = invalid_submit;
    cross_runtime_submit.draws = &cross_runtime_draw;
    RhiFence cross_runtime_fence = RHI_FENCE_INVALID;
    assert(
        ui.submit(runtime, &cross_runtime_submit, &cross_runtime_fence) ==
        RHI_RESULT_VALIDATION_FAILED
    );
    assert(cross_runtime_fence.value == 0U);
    assert(ui.destroy_buffer(other_runtime, foreign_buffer) == RHI_RESULT_SUCCESS);
    assert(plugin.api()->destroy_runtime(other_runtime) == RHI_RESULT_SUCCESS);

    RhiFence fence = RHI_FENCE_INVALID;
    assert(ui.submit(runtime, &invalid_submit, &fence) == RHI_RESULT_SUCCESS);
    assert(ui.wait_fence(runtime, fence, 0U) == RHI_RESULT_SUCCESS);

    assert(ui.destroy_fence(runtime, fence) == RHI_RESULT_SUCCESS);
    assert(ui.destroy_buffer(runtime, vertex_buffer) == RHI_RESULT_SUCCESS);
    buffer_write.buffer = vertex_buffer;
    assert(
        ui.write_buffer(runtime, &buffer_write) == RHI_RESULT_INVALID_ARGUMENT
    );
    RhiFence stale_fence = RHI_FENCE_INVALID;
    assert(
        ui.submit(runtime, &invalid_submit, &stale_fence) ==
        RHI_RESULT_VALIDATION_FAILED
    );
    assert(stale_fence.value == 0U);
    assert(ui.destroy_pipeline(runtime, pipeline) == RHI_RESULT_SUCCESS);
    assert(ui.destroy_texture(runtime, texture) == RHI_RESULT_SUCCESS);
    assert(ui.destroy_buffer(runtime, index_buffer) == RHI_RESULT_SUCCESS);
    assert(plugin.api()->destroy_runtime(runtime) == RHI_RESULT_SUCCESS);
    return 0;
}
