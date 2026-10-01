#ifndef HARVESTER_PLATFORM_SDL3_IMGUI_PLATFORM_HPP
#define HARVESTER_PLATFORM_SDL3_IMGUI_PLATFORM_HPP

#include <cstdint>
#include <string>

struct ImDrawData;

namespace harvester::platform {

/* The composition root selects this; RHI and UI code never see an SDL type. */
enum class Sdl3PresentationBackend : std::uint32_t {
    OpenGL,
    Vulkan,
};

class Sdl3ImGuiPlatform final {
  public:
    Sdl3ImGuiPlatform() = default;
    ~Sdl3ImGuiPlatform();

    Sdl3ImGuiPlatform(const Sdl3ImGuiPlatform &) = delete;
    Sdl3ImGuiPlatform &operator=(const Sdl3ImGuiPlatform &) = delete;

    [[nodiscard]] bool initialize(
        const char *title,
        std::uint32_t width,
        std::uint32_t height,
        Sdl3PresentationBackend backend = Sdl3PresentationBackend::OpenGL
    );
    [[nodiscard]] bool poll_events();
    void begin_frame();
    [[nodiscard]] ImDrawData *end_frame();
    void swap();
    void shutdown() noexcept;

    [[nodiscard]] std::uint32_t drawable_width() const noexcept;
    [[nodiscard]] std::uint32_t drawable_height() const noexcept;
    [[nodiscard]] void *native_window_for_wsi() const noexcept;
    [[nodiscard]] const std::string &last_error() const noexcept;

  private:
    void *window_ = nullptr;
    void *context_ = nullptr;
    std::uint32_t drawable_width_ = 0U;
    std::uint32_t drawable_height_ = 0U;
    bool imgui_initialized_ = false;
    bool sdl_initialized_ = false;
    Sdl3PresentationBackend presentation_backend_ =
        Sdl3PresentationBackend::OpenGL;
    std::string last_error_{};
};

}  // namespace harvester::platform

#endif
