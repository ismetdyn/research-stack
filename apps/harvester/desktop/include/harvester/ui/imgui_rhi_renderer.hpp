#ifndef HARVESTER_UI_IMGUI_RHI_RENDERER_HPP
#define HARVESTER_UI_IMGUI_RHI_RENDERER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <harvester/rhi/rhi_api.h>

struct ImDrawData;

namespace harvester::ui {

/*
 * Dear ImGui renderer implemented solely against the versioned UI RHI table.
 * It deliberately knows neither SDL nor OpenGL/Vulkan, so application screens
 * remain portable across renderer and platform implementations.
 */
class ImGuiRhiRenderer final {
  public:
    ImGuiRhiRenderer(const RhiPluginApiV1 *plugin_api, RhiRuntime runtime);
    ~ImGuiRhiRenderer();

    ImGuiRhiRenderer(const ImGuiRhiRenderer &) = delete;
    ImGuiRhiRenderer &operator=(const ImGuiRhiRenderer &) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool render(
        ImDrawData *draw_data,
        std::uint32_t target_width,
        std::uint32_t target_height
    );
    void shutdown() noexcept;

    [[nodiscard]] const std::string &last_error() const noexcept;

  private:
    struct TextureBinding;

    [[nodiscard]] bool update_imgui_textures(ImDrawData *draw_data);
    [[nodiscard]] bool ensure_buffer_capacity(
        RhiBuffer *buffer,
        std::uint64_t *capacity,
        std::uint64_t required_size,
        RhiBufferUsageFlags usage
    );
    [[nodiscard]] bool wait_for_previous_frame();
    [[nodiscard]] bool clear_target(
        std::uint32_t target_width,
        std::uint32_t target_height
    );
    void set_error(const char *message);
    void destroy_buffer(RhiBuffer *buffer, std::uint64_t *capacity) noexcept;
    void destroy_texture_bindings() noexcept;

    const RhiPluginApiV1 *plugin_api_ = nullptr;
    RhiRuntime runtime_ = RHI_RUNTIME_INVALID;
    RhiUiApiV1 ui_api_{};
    RhiBuffer vertex_buffer_ = RHI_BUFFER_INVALID;
    RhiBuffer index_buffer_ = RHI_BUFFER_INVALID;
    std::uint64_t vertex_capacity_ = 0U;
    std::uint64_t index_capacity_ = 0U;
    RhiPipeline pipeline_ = RHI_PIPELINE_INVALID;
    RhiFence previous_frame_fence_ = RHI_FENCE_INVALID;
    std::vector<TextureBinding *> texture_bindings_;
    std::vector<std::byte> vertex_staging_;
    std::vector<std::byte> index_staging_;
    std::vector<RhiUiDraw> draws_;
    std::string last_error_{};
    bool initialized_ = false;
};

}  // namespace harvester::ui

#endif
