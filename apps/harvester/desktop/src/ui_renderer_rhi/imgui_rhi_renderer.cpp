#include <harvester/ui/imgui_rhi_renderer.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include <imgui.h>

namespace harvester::ui {

namespace {

constexpr std::size_t kClearTargetApiSize =
    offsetof(RhiUiApiV1, clear_target) + sizeof(RhiClearUiTargetFn);

static_assert(sizeof(ImDrawVert) == sizeof(RhiUiVertex));
static_assert(offsetof(ImDrawVert, pos) == offsetof(RhiUiVertex, position));
static_assert(offsetof(ImDrawVert, uv) == offsetof(RhiUiVertex, uv));
static_assert(offsetof(ImDrawVert, col) == offsetof(RhiUiVertex, color_rgba8));
static_assert(sizeof(ImDrawIdx) == sizeof(std::uint16_t) ||
              sizeof(ImDrawIdx) == sizeof(std::uint32_t));
static_assert(sizeof(ImTextureID) <= sizeof(std::uint64_t));

[[nodiscard]] bool fits_size_t(const std::uint64_t value) noexcept {
    return value <= std::numeric_limits<std::size_t>::max();
}

[[nodiscard]] bool is_texture_format_supported(
    const ImTextureData *const texture_data
) noexcept {
    return texture_data->Format == ImTextureFormat_RGBA32 ||
           texture_data->Format == ImTextureFormat_Alpha8;
}

}  // namespace

struct ImGuiRhiRenderer::TextureBinding {
    ImTextureData *source = nullptr;
    RhiTexture texture = RHI_TEXTURE_INVALID;
};

ImGuiRhiRenderer::ImGuiRhiRenderer(
    const RhiPluginApiV1 *const plugin_api,
    const RhiRuntime runtime
)
    : plugin_api_(plugin_api), runtime_(runtime) {}

ImGuiRhiRenderer::~ImGuiRhiRenderer() {
    shutdown();
}

bool ImGuiRhiRenderer::initialize() {
    shutdown();
    last_error_.clear();
    if (plugin_api_ == nullptr || runtime_.value == 0U) {
        set_error("Renderer requires a valid plugin API and runtime.");
        return false;
    }

    ui_api_.struct_size = sizeof(ui_api_);
    if (harvester_rhi_get_ui_api_v1(plugin_api_, runtime_, &ui_api_) !=
        RHI_RESULT_SUCCESS) {
        set_error("The selected renderer does not expose the UI RHI API.");
        return false;
    }

    RhiUiPipelineCreateInfo pipeline_info{};
    pipeline_info.struct_size = sizeof(pipeline_info);
    pipeline_info.color_format = RHI_TEXTURE_FORMAT_RGBA8_UNORM;
    pipeline_info.blend_mode = RHI_UI_BLEND_ALPHA;
    if (ui_api_.create_pipeline(runtime_, &pipeline_info, &pipeline_) !=
        RHI_RESULT_SUCCESS) {
        set_error("Could not create the UI RHI pipeline.");
        return false;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = "harvester_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    initialized_ = true;
    return true;
}

bool ImGuiRhiRenderer::wait_for_previous_frame() {
    if (previous_frame_fence_.value == 0U) {
        return true;
    }
    if (ui_api_.wait_fence(
            runtime_,
            previous_frame_fence_,
            std::numeric_limits<std::uint64_t>::max()
        ) != RHI_RESULT_SUCCESS ||
        ui_api_.destroy_fence(runtime_, previous_frame_fence_) !=
            RHI_RESULT_SUCCESS) {
        set_error("Could not retire the previous UI frame.");
        return false;
    }
    previous_frame_fence_ = RHI_FENCE_INVALID;
    return true;
}

bool ImGuiRhiRenderer::clear_target(
    const std::uint32_t target_width,
    const std::uint32_t target_height
) {
    if (ui_api_.struct_size < kClearTargetApiSize ||
        ui_api_.clear_target == nullptr) {
        return true;
    }

    RhiUiClearInfo clear_info{};
    clear_info.struct_size = sizeof(clear_info);
    clear_info.target_width = target_width;
    clear_info.target_height = target_height;
    clear_info.color_rgba[0] = 0.035F;
    clear_info.color_rgba[1] = 0.045F;
    clear_info.color_rgba[2] = 0.065F;
    clear_info.color_rgba[3] = 1.0F;
    if (ui_api_.clear_target(runtime_, &clear_info) != RHI_RESULT_SUCCESS) {
        set_error("Could not clear the UI presentation target.");
        return false;
    }
    return true;
}

bool ImGuiRhiRenderer::ensure_buffer_capacity(
    RhiBuffer *const buffer,
    std::uint64_t *const capacity,
    const std::uint64_t required_size,
    const RhiBufferUsageFlags usage
) {
    assert(buffer != nullptr);
    assert(capacity != nullptr);
    if (required_size == 0U || !fits_size_t(required_size)) {
        set_error("UI vertex or index data exceeds the supported address space.");
        return false;
    }
    if (buffer->value != 0U && *capacity >= required_size) {
        return true;
    }

    std::uint64_t replacement_capacity = required_size;
    if (*capacity <= std::numeric_limits<std::uint64_t>::max() / 2U) {
        replacement_capacity = std::max(required_size, *capacity * 2U);
    }
    RhiBufferCreateInfo create_info{};
    create_info.struct_size = sizeof(create_info);
    create_info.usage = usage | RHI_BUFFER_USAGE_COPY_DST;
    create_info.byte_size = replacement_capacity;
    RhiBuffer replacement = RHI_BUFFER_INVALID;
    if (ui_api_.create_buffer(runtime_, &create_info, &replacement) !=
        RHI_RESULT_SUCCESS) {
        set_error("Could not grow a UI RHI buffer.");
        return false;
    }

    destroy_buffer(buffer, capacity);
    *buffer = replacement;
    *capacity = replacement_capacity;
    return true;
}

bool ImGuiRhiRenderer::update_imgui_textures(ImDrawData *const draw_data) {
    if (draw_data->Textures == nullptr) {
        return true;
    }

    for (ImTextureData *const texture_data : *draw_data->Textures) {
        if (texture_data == nullptr) {
            set_error("ImGui submitted a null texture request.");
            return false;
        }
        const auto binding_it = std::find_if(
            texture_bindings_.begin(),
            texture_bindings_.end(),
            [texture_data](const TextureBinding *const binding) {
                return binding->source == texture_data;
            }
        );
        TextureBinding *binding =
            binding_it == texture_bindings_.end() ? nullptr : *binding_it;

        if (texture_data->Status == ImTextureStatus_WantDestroy) {
            if (binding != nullptr) {
                if (ui_api_.destroy_texture(runtime_, binding->texture) !=
                    RHI_RESULT_SUCCESS) {
                    set_error("Could not destroy an ImGui RHI texture.");
                    return false;
                }
                texture_data->BackendUserData = nullptr;
                texture_data->SetTexID(ImTextureID_Invalid);
                texture_data->SetStatus(ImTextureStatus_Destroyed);
                delete binding;
                texture_bindings_.erase(binding_it);
            }
            continue;
        }

        const bool creating_texture =
            texture_data->Status == ImTextureStatus_WantCreate;
        if (creating_texture) {
            if (binding != nullptr || texture_data->Width <= 0 ||
                texture_data->Height <= 0 ||
                texture_data->Width >
                    std::numeric_limits<unsigned short>::max() ||
                texture_data->Height >
                    std::numeric_limits<unsigned short>::max() ||
                texture_data->Pixels == nullptr ||
                !is_texture_format_supported(texture_data)) {
                set_error("ImGui supplied an invalid texture creation request.");
                return false;
            }
            RhiTextureCreateInfo create_info{};
            create_info.struct_size = sizeof(create_info);
            create_info.usage =
                RHI_TEXTURE_USAGE_SAMPLED | RHI_TEXTURE_USAGE_COPY_DST;
            create_info.width = static_cast<std::uint32_t>(texture_data->Width);
            create_info.height = static_cast<std::uint32_t>(texture_data->Height);
            create_info.format = RHI_TEXTURE_FORMAT_RGBA8_UNORM;
            RhiTexture texture = RHI_TEXTURE_INVALID;
            if (ui_api_.create_texture(runtime_, &create_info, &texture) !=
                RHI_RESULT_SUCCESS) {
                set_error("Could not create an ImGui RHI texture.");
                return false;
            }
            TextureBinding *const new_binding = new (std::nothrow) TextureBinding{
                texture_data,
                texture,
            };
            if (new_binding == nullptr) {
                (void)ui_api_.destroy_texture(runtime_, texture);
                set_error("Out of memory while tracking an ImGui RHI texture.");
                return false;
            }
            texture_data->BackendUserData = new_binding;
            texture_data->SetTexID(static_cast<ImTextureID>(texture.value));
            try {
                texture_bindings_.push_back(new_binding);
            } catch (const std::bad_alloc &) {
                texture_data->BackendUserData = nullptr;
                texture_data->SetTexID(ImTextureID_Invalid);
                (void)ui_api_.destroy_texture(runtime_, texture);
                delete new_binding;
                set_error("Out of memory while registering an ImGui RHI texture.");
                return false;
            }
            binding = new_binding;
        }

        if (texture_data->Status != ImTextureStatus_WantCreate &&
            texture_data->Status != ImTextureStatus_WantUpdates) {
            continue;
        }
        if (binding == nullptr || texture_data->Pixels == nullptr ||
            !is_texture_format_supported(texture_data)) {
            set_error("ImGui supplied an invalid texture update request.");
            return false;
        }

        std::vector<ImTextureRect> updates;
        if (creating_texture) {
            updates.push_back(ImTextureRect{
                0U,
                0U,
                static_cast<unsigned short>(texture_data->Width),
                static_cast<unsigned short>(texture_data->Height),
            });
        } else if (texture_data->Updates.Size > 0) {
            updates.assign(
                texture_data->Updates.begin(),
                texture_data->Updates.end()
            );
        } else {
            updates.push_back(texture_data->UpdateRect);
        }
        for (const ImTextureRect update : updates) {
            if (update.w == 0U || update.h == 0U ||
                static_cast<int>(update.x) + static_cast<int>(update.w) >
                    texture_data->Width ||
                static_cast<int>(update.y) + static_cast<int>(update.h) >
                    texture_data->Height) {
                set_error("ImGui supplied an out-of-bounds texture update.");
                return false;
            }

            RhiTextureWriteInfo write_info{};
            write_info.struct_size = sizeof(write_info);
            write_info.texture = binding->texture;
            write_info.x = update.x;
            write_info.y = update.y;
            write_info.width = update.w;
            write_info.height = update.h;

            std::vector<std::uint8_t> rgba_pixels;
            if (texture_data->Format == ImTextureFormat_RGBA32) {
                write_info.bytes_per_row = static_cast<std::uint32_t>(
                    texture_data->GetPitch()
                );
                write_info.data = texture_data->GetPixelsAt(update.x, update.y);
                write_info.data_size =
                    static_cast<std::uint64_t>(update.h - 1U) *
                        write_info.bytes_per_row +
                    static_cast<std::uint64_t>(update.w) * 4U;
            } else {
                const std::size_t pixel_count =
                    static_cast<std::size_t>(update.w) * update.h;
                rgba_pixels.resize(pixel_count * 4U);
                for (std::size_t index = 0U; index < pixel_count; ++index) {
                    const std::size_t source_x = index % update.w;
                    const std::size_t source_y = index / update.w;
                    const auto *const source = static_cast<const std::uint8_t *>(
                        texture_data->GetPixelsAt(
                            static_cast<int>(update.x + source_x),
                            static_cast<int>(update.y + source_y)
                        )
                    );
                    rgba_pixels[index * 4U] = UINT8_MAX;
                    rgba_pixels[index * 4U + 1U] = UINT8_MAX;
                    rgba_pixels[index * 4U + 2U] = UINT8_MAX;
                    rgba_pixels[index * 4U + 3U] = *source;
                }
                write_info.bytes_per_row = static_cast<std::uint32_t>(update.w) * 4U;
                write_info.data = rgba_pixels.data();
                write_info.data_size = rgba_pixels.size();
            }
            if (ui_api_.write_texture(runtime_, &write_info) != RHI_RESULT_SUCCESS) {
                set_error("Could not upload an ImGui RHI texture.");
                return false;
            }
        }
        texture_data->SetStatus(ImTextureStatus_OK);
    }
    return true;
}

bool ImGuiRhiRenderer::render(
    ImDrawData *const draw_data,
    const std::uint32_t target_width,
    const std::uint32_t target_height
) {
    last_error_.clear();
    if (!initialized_ || draw_data == nullptr || !draw_data->Valid ||
        target_width == 0U || target_height == 0U ||
        target_width > static_cast<std::uint32_t>(INT32_MAX) ||
        target_height > static_cast<std::uint32_t>(INT32_MAX)) {
        set_error("Invalid ImGui RHI render request.");
        return false;
    }
    if (!wait_for_previous_frame() || !update_imgui_textures(draw_data) ||
        !clear_target(target_width, target_height)) {
        return false;
    }
    if (draw_data->TotalVtxCount == 0 && draw_data->TotalIdxCount == 0) {
        RhiUiSubmitInfo empty_submit{};
        empty_submit.struct_size = sizeof(empty_submit);
        empty_submit.target_width = target_width;
        empty_submit.target_height = target_height;
        empty_submit.index_format = RHI_INDEX_FORMAT_UINT16;
        if (ui_api_.submit(runtime_, &empty_submit, &previous_frame_fence_) !=
            RHI_RESULT_SUCCESS) {
            previous_frame_fence_ = RHI_FENCE_INVALID;
            set_error("Could not submit an empty ImGui RHI frame.");
            return false;
        }
        return true;
    }
    if (draw_data->TotalVtxCount <= 0 || draw_data->TotalIdxCount <= 0 ||
        draw_data->DisplaySize.x <= 0.0F || draw_data->DisplaySize.y <= 0.0F) {
        set_error("ImGui supplied invalid draw totals or display dimensions.");
        return false;
    }

    const std::uint64_t vertex_bytes =
        static_cast<std::uint64_t>(draw_data->TotalVtxCount) * sizeof(ImDrawVert);
    const std::uint64_t index_bytes =
        static_cast<std::uint64_t>(draw_data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!fits_size_t(vertex_bytes) || !fits_size_t(index_bytes) ||
        !ensure_buffer_capacity(
            &vertex_buffer_,
            &vertex_capacity_,
            vertex_bytes,
            RHI_BUFFER_USAGE_VERTEX
        ) ||
        !ensure_buffer_capacity(
            &index_buffer_,
            &index_capacity_,
            index_bytes,
            RHI_BUFFER_USAGE_INDEX
        )) {
        return false;
    }

    vertex_staging_.resize(static_cast<std::size_t>(vertex_bytes));
    index_staging_.resize(static_cast<std::size_t>(index_bytes));
    std::size_t vertex_offset_bytes = 0U;
    std::size_t index_offset_bytes = 0U;
    for (ImDrawList *const list : draw_data->CmdLists) {
        if (list == nullptr || list->VtxBuffer.Size < 0 || list->IdxBuffer.Size < 0) {
            set_error("ImGui supplied an invalid draw list.");
            return false;
        }
        const std::size_t list_vertex_bytes =
            static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const std::size_t list_index_bytes =
            static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        if (vertex_offset_bytes + list_vertex_bytes > vertex_staging_.size() ||
            index_offset_bytes + list_index_bytes > index_staging_.size()) {
            set_error("ImGui draw-list totals do not match their buffers.");
            return false;
        }
        std::memcpy(
            vertex_staging_.data() + vertex_offset_bytes,
            list->VtxBuffer.Data,
            list_vertex_bytes
        );
        std::memcpy(
            index_staging_.data() + index_offset_bytes,
            list->IdxBuffer.Data,
            list_index_bytes
        );
        vertex_offset_bytes += list_vertex_bytes;
        index_offset_bytes += list_index_bytes;
    }

    RhiBufferWriteInfo vertex_write{};
    vertex_write.struct_size = sizeof(vertex_write);
    vertex_write.buffer = vertex_buffer_;
    vertex_write.data = vertex_staging_.data();
    vertex_write.data_size = vertex_staging_.size();
    RhiBufferWriteInfo index_write{};
    index_write.struct_size = sizeof(index_write);
    index_write.buffer = index_buffer_;
    index_write.data = index_staging_.data();
    index_write.data_size = index_staging_.size();
    if (ui_api_.write_buffer(runtime_, &vertex_write) != RHI_RESULT_SUCCESS ||
        ui_api_.write_buffer(runtime_, &index_write) != RHI_RESULT_SUCCESS) {
        set_error("Could not upload ImGui vertex or index data.");
        return false;
    }

    draws_.clear();
    const float scale_x =
        static_cast<float>(target_width) / draw_data->DisplaySize.x;
    const float scale_y =
        static_cast<float>(target_height) / draw_data->DisplaySize.y;
    std::uint64_t global_vertex_offset = 0U;
    std::uint64_t global_index_offset = 0U;
    for (ImDrawList *const list : draw_data->CmdLists) {
        for (const ImDrawCmd &command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                if (command.UserCallback == ImDrawCallback_ResetRenderState) {
                    continue;
                }
                set_error("Custom ImGui draw callbacks are not supported by this RHI adapter.");
                return false;
            }
            const float clipped_x1 = std::clamp(
                (command.ClipRect.x - draw_data->DisplayPos.x) * scale_x,
                0.0F,
                static_cast<float>(target_width)
            );
            const float clipped_y1 = std::clamp(
                (command.ClipRect.y - draw_data->DisplayPos.y) * scale_y,
                0.0F,
                static_cast<float>(target_height)
            );
            const float clipped_x2 = std::clamp(
                (command.ClipRect.z - draw_data->DisplayPos.x) * scale_x,
                0.0F,
                static_cast<float>(target_width)
            );
            const float clipped_y2 = std::clamp(
                (command.ClipRect.w - draw_data->DisplayPos.y) * scale_y,
                0.0F,
                static_cast<float>(target_height)
            );
            const int x1 = static_cast<int>(std::floor(clipped_x1));
            const int y1 = static_cast<int>(std::floor(clipped_y1));
            const int x2 = static_cast<int>(std::ceil(clipped_x2));
            const int y2 = static_cast<int>(std::ceil(clipped_y2));
            if (x2 <= x1 || y2 <= y1 || command.ElemCount == 0U) {
                continue;
            }
            const std::uint64_t first_index =
                global_index_offset + command.IdxOffset;
            const std::uint64_t vertex_offset =
                global_vertex_offset + command.VtxOffset;
            if (first_index > std::numeric_limits<std::uint32_t>::max() ||
                vertex_offset > static_cast<std::uint64_t>(INT32_MAX)) {
                set_error("ImGui draw offsets exceed the UI RHI contract.");
                return false;
            }
            const std::uint64_t texture_value =
                static_cast<std::uint64_t>(command.GetTexID());
            if (texture_value == 0U) {
                set_error("ImGui draw command references an unresolved texture.");
                return false;
            }
            RhiUiDraw draw{};
            draw.pipeline = pipeline_;
            draw.vertex_buffer = vertex_buffer_;
            draw.index_buffer = index_buffer_;
            draw.texture = RhiTexture{texture_value};
            draw.scissor.x = x1;
            draw.scissor.y = y1;
            draw.scissor.width = static_cast<std::uint32_t>(x2 - x1);
            draw.scissor.height = static_cast<std::uint32_t>(y2 - y1);
            draw.index_count = command.ElemCount;
            draw.first_index = static_cast<std::uint32_t>(first_index);
            draw.vertex_offset = static_cast<std::int32_t>(vertex_offset);
            draws_.push_back(draw);
        }
        global_vertex_offset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
        global_index_offset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
    }
    if (draws_.empty()) {
        RhiUiSubmitInfo empty_submit{};
        empty_submit.struct_size = sizeof(empty_submit);
        empty_submit.target_width = target_width;
        empty_submit.target_height = target_height;
        empty_submit.index_format = sizeof(ImDrawIdx) == sizeof(std::uint16_t) ?
            RHI_INDEX_FORMAT_UINT16 : RHI_INDEX_FORMAT_UINT32;
        if (ui_api_.submit(runtime_, &empty_submit, &previous_frame_fence_) !=
            RHI_RESULT_SUCCESS) {
            previous_frame_fence_ = RHI_FENCE_INVALID;
            set_error("Could not submit a clipped ImGui RHI frame.");
            return false;
        }
        return true;
    }

    RhiUiSubmitInfo submit_info{};
    submit_info.struct_size = sizeof(submit_info);
    submit_info.draw_count = static_cast<std::uint32_t>(draws_.size());
    submit_info.target_width = target_width;
    submit_info.target_height = target_height;
    submit_info.index_format = sizeof(ImDrawIdx) == sizeof(std::uint16_t) ?
        RHI_INDEX_FORMAT_UINT16 : RHI_INDEX_FORMAT_UINT32;
    submit_info.draws = draws_.data();
    if (ui_api_.submit(runtime_, &submit_info, &previous_frame_fence_) !=
        RHI_RESULT_SUCCESS) {
        previous_frame_fence_ = RHI_FENCE_INVALID;
        set_error("Could not submit the ImGui RHI frame.");
        return false;
    }
    return true;
}

void ImGuiRhiRenderer::destroy_buffer(
    RhiBuffer *const buffer,
    std::uint64_t *const capacity
) noexcept {
    if (buffer->value != 0U && ui_api_.destroy_buffer != nullptr) {
        (void)ui_api_.destroy_buffer(runtime_, *buffer);
    }
    *buffer = RHI_BUFFER_INVALID;
    *capacity = 0U;
}

void ImGuiRhiRenderer::destroy_texture_bindings() noexcept {
    for (TextureBinding *const binding : texture_bindings_) {
        if (binding->source != nullptr &&
            binding->source->BackendUserData == binding) {
            binding->source->BackendUserData = nullptr;
            binding->source->SetTexID(ImTextureID_Invalid);
        }
        if (binding->texture.value != 0U && ui_api_.destroy_texture != nullptr) {
            (void)ui_api_.destroy_texture(runtime_, binding->texture);
        }
        delete binding;
    }
    texture_bindings_.clear();
}

void ImGuiRhiRenderer::shutdown() noexcept {
    if (previous_frame_fence_.value != 0U && ui_api_.wait_fence != nullptr &&
        ui_api_.destroy_fence != nullptr) {
        (void)ui_api_.wait_fence(
            runtime_,
            previous_frame_fence_,
            std::numeric_limits<std::uint64_t>::max()
        );
        (void)ui_api_.destroy_fence(runtime_, previous_frame_fence_);
    }
    previous_frame_fence_ = RHI_FENCE_INVALID;
    destroy_texture_bindings();
    destroy_buffer(&vertex_buffer_, &vertex_capacity_);
    destroy_buffer(&index_buffer_, &index_capacity_);
    if (pipeline_.value != 0U && ui_api_.destroy_pipeline != nullptr) {
        (void)ui_api_.destroy_pipeline(runtime_, pipeline_);
    }
    pipeline_ = RHI_PIPELINE_INVALID;
    if (initialized_ && ImGui::GetCurrentContext() != nullptr) {
        ImGuiIO &io = ImGui::GetIO();
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
        if (io.BackendRendererName != nullptr &&
            std::strcmp(io.BackendRendererName, "harvester_rhi") == 0) {
            io.BackendRendererName = nullptr;
        }
    }
    ui_api_ = {};
    initialized_ = false;
}

const std::string &ImGuiRhiRenderer::last_error() const noexcept {
    return last_error_;
}

void ImGuiRhiRenderer::set_error(const char *const message) {
    last_error_ = message;
}

}  // namespace harvester::ui
