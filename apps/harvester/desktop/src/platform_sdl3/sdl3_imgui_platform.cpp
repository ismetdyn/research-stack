#include <harvester/platform/sdl3_imgui_platform.hpp>

#include <limits>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <imgui_impl_sdl3.h>

namespace harvester::platform {

namespace {

std::uint32_t dimension_or_zero(const int value) noexcept {
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}

std::string sdl_failure(const char *const operation) {
    return std::string{operation} + ": " + SDL_GetError();
}

bool is_supported_backend(const Sdl3PresentationBackend backend) noexcept {
    return backend == Sdl3PresentationBackend::OpenGL ||
           backend == Sdl3PresentationBackend::Vulkan;
}

void configure_desktop_style() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(18.0F, 16.0F);
    style.FramePadding = ImVec2(11.0F, 7.0F);
    style.CellPadding = ImVec2(10.0F, 9.0F);
    style.ItemSpacing = ImVec2(10.0F, 10.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.IndentSpacing = 18.0F;
    style.WindowRounding = 0.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 5.0F;
    style.GrabRounding = 5.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 6.0F;
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;

    ImVec4 *const colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.88F, 0.92F, 0.98F, 1.0F);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.49F, 0.56F, 0.67F, 1.0F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.043F, 0.055F, 0.078F, 1.0F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.071F, 0.090F, 0.125F, 1.0F);
    colors[ImGuiCol_Border] = ImVec4(0.15F, 0.19F, 0.27F, 1.0F);
    colors[ImGuiCol_Separator] = ImVec4(0.15F, 0.19F, 0.27F, 1.0F);
    colors[ImGuiCol_FrameBg] = ImVec4(0.102F, 0.133F, 0.188F, 1.0F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.135F, 0.179F, 0.251F, 1.0F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.165F, 0.218F, 0.306F, 1.0F);
    colors[ImGuiCol_Button] = ImVec4(0.102F, 0.335F, 0.76F, 1.0F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.14F, 0.41F, 0.90F, 1.0F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.08F, 0.26F, 0.62F, 1.0F);
    colors[ImGuiCol_Header] = ImVec4(0.10F, 0.27F, 0.52F, 0.78F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.13F, 0.35F, 0.67F, 0.86F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.16F, 0.42F, 0.79F, 1.0F);
    colors[ImGuiCol_CheckMark] = ImVec4(0.46F, 0.73F, 1.0F, 1.0F);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.35F, 0.63F, 1.0F, 1.0F);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.52F, 0.76F, 1.0F, 1.0F);
}

}  // namespace

Sdl3ImGuiPlatform::~Sdl3ImGuiPlatform() {
    shutdown();
}

bool Sdl3ImGuiPlatform::initialize(
    const char *const title,
    const std::uint32_t width,
    const std::uint32_t height,
    const Sdl3PresentationBackend backend
) {
    shutdown();
    last_error_.clear();
    if (title == nullptr || width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        !is_supported_backend(backend)) {
        last_error_ = "Invalid window configuration.";
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        last_error_ = sdl_failure("SDL video initialization failed");
        return false;
    }
    sdl_initialized_ = true;

    if (backend == Sdl3PresentationBackend::OpenGL) {
        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) ||
            !SDL_GL_SetAttribute(
                SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_CORE
            ) ||
            !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)) {
            last_error_ = sdl_failure("OpenGL context configuration failed");
            shutdown();
            return false;
        }
    }

    const SDL_WindowFlags graphics_window_flag =
        backend == Sdl3PresentationBackend::OpenGL ? SDL_WINDOW_OPENGL :
                                                     SDL_WINDOW_VULKAN;
    SDL_Window *const window = SDL_CreateWindow(
        title,
        static_cast<int>(width),
        static_cast<int>(height),
        graphics_window_flag | SDL_WINDOW_RESIZABLE |
            SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (window == nullptr) {
        last_error_ = sdl_failure("Graphics window creation failed");
        shutdown();
        return false;
    }
    window_ = window;

    if (backend == Sdl3PresentationBackend::OpenGL) {
        SDL_GLContext context = SDL_GL_CreateContext(window);
        if (context == nullptr || !SDL_GL_MakeCurrent(window, context)) {
            last_error_ = sdl_failure("OpenGL context creation failed");
            shutdown();
            return false;
        }
        context_ = context;
        // VSync is preferred for an interactive desktop window, but compositors
        // and software GLX implementations are allowed not to expose swap-control
        // at all. Presentation remains valid without it, so this is deliberately
        // best-effort rather than an initialization requirement.
        if (!SDL_GL_SetSwapInterval(1)) {
            (void)SDL_GL_SetSwapInterval(0);
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImFontConfig font_config{};
    font_config.SizePixels = 16.0F;
    ImGui::GetIO().Fonts->AddFontDefault(&font_config);
    ImGui::StyleColorsDark();
    configure_desktop_style();
    const bool imgui_initialized =
        backend == Sdl3PresentationBackend::OpenGL ?
            ImGui_ImplSDL3_InitForOpenGL(window, context_) :
            ImGui_ImplSDL3_InitForVulkan(window);
    if (!imgui_initialized) {
        last_error_ = "Dear ImGui SDL3 platform initialization failed.";
        shutdown();
        return false;
    }
    imgui_initialized_ = true;

    int drawable_width = 0;
    int drawable_height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height)) {
        last_error_ = sdl_failure("Initial drawable-size query failed");
        shutdown();
        return false;
    }
    drawable_width_ = dimension_or_zero(drawable_width);
    drawable_height_ = dimension_or_zero(drawable_height);
    presentation_backend_ = backend;
    return drawable_width_ != 0U && drawable_height_ != 0U;
}

bool Sdl3ImGuiPlatform::poll_events() {
    bool keep_running = true;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            keep_running = false;
        }
    }

    int drawable_width = 0;
    int drawable_height = 0;
    if (!SDL_GetWindowSizeInPixels(
            static_cast<SDL_Window *>(window_),
            &drawable_width,
            &drawable_height
        )) {
        last_error_ = sdl_failure("Drawable-size query failed");
        return false;
    }
    drawable_width_ = dimension_or_zero(drawable_width);
    drawable_height_ = dimension_or_zero(drawable_height);
    return keep_running;
}

void Sdl3ImGuiPlatform::begin_frame() {
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

ImDrawData *Sdl3ImGuiPlatform::end_frame() {
    ImGui::Render();
    return ImGui::GetDrawData();
}

void Sdl3ImGuiPlatform::swap() {
    if (presentation_backend_ != Sdl3PresentationBackend::OpenGL) {
        return;
    }
    if (!SDL_GL_SwapWindow(static_cast<SDL_Window *>(window_))) {
        last_error_ = sdl_failure("OpenGL buffer swap failed");
    }
}

void Sdl3ImGuiPlatform::shutdown() noexcept {
    if (imgui_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imgui_initialized_ = false;
    }
    if (context_ != nullptr) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context_));
        context_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
        window_ = nullptr;
    }
    if (sdl_initialized_) {
        SDL_Quit();
        sdl_initialized_ = false;
    }
    drawable_width_ = 0U;
    drawable_height_ = 0U;
    presentation_backend_ = Sdl3PresentationBackend::OpenGL;
}

std::uint32_t Sdl3ImGuiPlatform::drawable_width() const noexcept {
    return drawable_width_;
}

std::uint32_t Sdl3ImGuiPlatform::drawable_height() const noexcept {
    return drawable_height_;
}

void *Sdl3ImGuiPlatform::native_window_for_wsi() const noexcept {
    return presentation_backend_ == Sdl3PresentationBackend::Vulkan ? window_ :
                                                                        nullptr;
}

const std::string &Sdl3ImGuiPlatform::last_error() const noexcept {
    return last_error_;
}

}  // namespace harvester::platform
