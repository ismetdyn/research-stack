#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include <harvester/app/research_dashboard.hpp>
#include <harvester/app/python_research_service.hpp>
#include <harvester/platform/sdl3_imgui_platform.hpp>
#include <harvester/platform/sdl3_vulkan_presentation.hpp>
#include <harvester/rhi/rhi_loader.hpp>
#include <harvester/ui/imgui_rhi_renderer.hpp>

namespace {

struct DesktopOptions final {
    std::uint32_t frame_limit = 0U;
    harvester::rhi::RendererBackend backend =
        harvester::rhi::RendererBackend::OpenGL;
};

bool parse_renderer(
    const std::string_view value,
    harvester::rhi::RendererBackend *const out_backend
) {
    if (out_backend == nullptr) {
        return false;
    }
    if (value == "opengl") {
        *out_backend = harvester::rhi::RendererBackend::OpenGL;
        return true;
    }
    if (value == "vulkan") {
        *out_backend = harvester::rhi::RendererBackend::Vulkan;
        return true;
    }
    return false;
}

bool parse_options(
    const int argc,
    char *const *const argv,
    DesktopOptions *const out_options
) {
    if (out_options == nullptr) {
        return false;
    }
    *out_options = {};
    bool has_frame_limit = false;
    bool has_renderer = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--frames" && !has_frame_limit && index + 1 < argc) {
            errno = 0;
            char *end = nullptr;
            const unsigned long value = std::strtoul(argv[++index], &end, 10);
            if (errno != 0 || end == argv[index] || *end != '\0' || value == 0UL ||
                value > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            out_options->frame_limit = static_cast<std::uint32_t>(value);
            has_frame_limit = true;
            continue;
        }
        if (argument == "--renderer" && !has_renderer && index + 1 < argc) {
            if (!parse_renderer(argv[++index], &out_options->backend)) {
                return false;
            }
            has_renderer = true;
            continue;
        }
        constexpr std::string_view renderer_prefix = "--renderer=";
        if (!has_renderer && argument.starts_with(renderer_prefix)) {
            if (!parse_renderer(
                    argument.substr(renderer_prefix.size()), &out_options->backend
                )) {
                return false;
            }
            has_renderer = true;
            continue;
        }
        return false;
    }
    return true;
}

int fail(const std::string_view message) {
    std::cerr << "harvester_desktop: " << message << '\n';
    return 1;
}

std::string bounded_rhi_string(
    const char *const value,
    const std::size_t capacity
) {
    std::size_t length = 0U;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return {value, length};
}

std::string diagnostic_message(const RhiDiagnostic &diagnostic) {
    const std::string message = bounded_rhi_string(
        diagnostic.message,
        HARVESTER_RHI_DIAGNOSTIC_CAPACITY
    );
    if (!message.empty()) {
        return message;
    }
    return "RHI result " + std::to_string(diagnostic.result) + ".";
}

}  // namespace

int main(const int argc, char *const *const argv) {
    DesktopOptions options{};
    if (!parse_options(argc, argv, &options)) {
        return fail(
            "usage: harvester_desktop [--renderer opengl|vulkan] "
            "[--frames positive-count]"
        );
    }
    const bool is_vulkan = options.backend == harvester::rhi::RendererBackend::Vulkan;

    harvester::platform::Sdl3ImGuiPlatform platform{};
    if (!platform.initialize(
            "Research Harvester",
            1280U,
            800U,
            is_vulkan ? harvester::platform::Sdl3PresentationBackend::Vulkan :
                        harvester::platform::Sdl3PresentationBackend::OpenGL
        )) {
        return fail(platform.last_error());
    }

    harvester::platform::Sdl3VulkanPresentation vulkan_presentation{};
    if (is_vulkan &&
        !vulkan_presentation.initialize(platform.native_window_for_wsi())) {
        return fail(vulkan_presentation.last_error());
    }

    harvester::rhi::LoadedPlugin plugin{};
    std::string plugin_diagnostic{};
    if (
        harvester::rhi::LoadedPlugin::load(
            options.backend,
            std::filesystem::path{
                is_vulkan ? HARVESTER_VULKAN_PLUGIN_DIR :
                            HARVESTER_OPENGL_PLUGIN_DIR
            },
            &plugin,
            &plugin_diagnostic
        ) != RHI_RESULT_SUCCESS
    ) {
        return fail(
            "could not load the allowlisted RHI plugin: " + plugin_diagnostic
        );
    }

    RhiRuntimeCreateInfo runtime_info{};
    runtime_info.struct_size = sizeof(runtime_info);
    runtime_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    runtime_info.required_features = RHI_FEATURE_DYNAMIC_TEXTURES;
    if (is_vulkan) {
        runtime_info.presentation_binding_kind = RHI_PRESENTATION_BINDING_VULKAN_V1;
        runtime_info.presentation_binding = vulkan_presentation.binding();
    }
    RhiRuntime runtime = RHI_RUNTIME_INVALID;
    if (plugin.api()->create_runtime(&runtime_info, &runtime) != RHI_RESULT_SUCCESS) {
        return fail("could not create the selected RHI runtime");
    }

    RhiPluginInfo plugin_info{};
    plugin_info.struct_size = sizeof(plugin_info);
    RhiRuntimeCapabilities runtime_capabilities{};
    runtime_capabilities.struct_size = sizeof(runtime_capabilities);
    RhiDiagnostic runtime_diagnostic{};
    runtime_diagnostic.struct_size = sizeof(runtime_diagnostic);
    if (plugin.api()->get_plugin_info(&plugin_info) != RHI_RESULT_SUCCESS ||
        plugin.api()->get_runtime_capabilities(runtime, &runtime_capabilities) !=
            RHI_RESULT_SUCCESS ||
        plugin.api()->copy_diagnostic(runtime, &runtime_diagnostic) !=
            RHI_RESULT_SUCCESS) {
        (void)plugin.api()->destroy_runtime(runtime);
        return fail("could not read active renderer capabilities");
    }
    harvester::app::RendererDiagnostics renderer_diagnostics{
        bounded_rhi_string(
            plugin_info.backend_name,
            HARVESTER_RHI_BACKEND_NAME_CAPACITY
        ),
        bounded_rhi_string(
            plugin_info.backend_version,
            HARVESTER_RHI_BACKEND_VERSION_CAPACITY
        ),
        runtime_capabilities.limits.max_texture_dimension_2d,
        (runtime_capabilities.enabled_features & RHI_FEATURE_DYNAMIC_TEXTURES) != 0U,
    };
    std::clog << "harvester_desktop renderer"
              << " backend=" << renderer_diagnostics.backend_name
              << " version=" << renderer_diagnostics.backend_version
              << " max_texture_dimension="
              << renderer_diagnostics.maximum_texture_dimension
              << " dynamic_textures="
              << (renderer_diagnostics.dynamic_textures_enabled ? "true" : "false")
              << " rhi_result=" << runtime_diagnostic.result << '\n';

    harvester::ui::ImGuiRhiRenderer renderer{plugin.api(), runtime};
    if (!renderer.initialize()) {
        (void)plugin.api()->destroy_runtime(runtime);
        return fail(renderer.last_error());
    }

    harvester::app::ResearchDashboardState dashboard{};
    harvester::app::PythonResearchService service{
        {
            std::filesystem::path{HARVESTER_PYTHON_PROJECT_DIR},
            {"uv", "run", "python", "-m", "research_harvester.desktop_bridge"},
        },
    };
    std::uint32_t rendered_frames = 0U;
    bool keep_running = true;
    while (keep_running &&
           (options.frame_limit == 0U || rendered_frames < options.frame_limit)) {
        keep_running = platform.poll_events();
        if (!keep_running) {
            break;
        }
        if (platform.drawable_width() == 0U || platform.drawable_height() == 0U) {
            continue;
        }
        platform.begin_frame();
        harvester::app::draw_research_dashboard(
            dashboard,
            service,
            service,
            renderer_diagnostics
        );
        if (!renderer.render(
                platform.end_frame(),
                platform.drawable_width(),
                platform.drawable_height()
            )) {
            renderer.shutdown();
            (void)plugin.api()->destroy_runtime(runtime);
            return fail(renderer.last_error());
        }
        runtime_diagnostic = {};
        runtime_diagnostic.struct_size = sizeof(runtime_diagnostic);
        if (plugin.api()->copy_diagnostic(runtime, &runtime_diagnostic) !=
            RHI_RESULT_SUCCESS) {
            renderer.shutdown();
            (void)plugin.api()->destroy_runtime(runtime);
            return fail("could not read the active renderer diagnostic");
        }
        if (runtime_diagnostic.result != RHI_RESULT_SUCCESS) {
            renderer.shutdown();
            (void)plugin.api()->destroy_runtime(runtime);
            return fail(diagnostic_message(runtime_diagnostic));
        }
        platform.swap();
        ++rendered_frames;
    }

    renderer.shutdown();
    if (plugin.api()->destroy_runtime(runtime) != RHI_RESULT_SUCCESS) {
        return fail("could not destroy the selected RHI runtime");
    }
    return 0;
}
