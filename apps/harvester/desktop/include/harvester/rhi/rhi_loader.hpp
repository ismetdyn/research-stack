#ifndef HARVESTER_RHI_LOADER_HPP
#define HARVESTER_RHI_LOADER_HPP

#include <cstdint>
#include <filesystem>
#include <string>

#include <harvester/rhi/rhi_api.h>

namespace harvester::rhi {

enum class RendererBackend : std::uint32_t {
    Null = 0U,
    OpenGL = 1U,
    Vulkan = 2U,
};

/*
 * A loaded module owns the lifetime of the plugin table returned by api(). The
 * host must destroy every runtime and stop calls through api() before reset()
 * or destruction. When load() fails and out_diagnostic is not null, it receives
 * a one-line message with the plugin path and expected vs actual values; it is
 * host-side text and not part of the plugin C ABI. The loader accepts a directory only; filenames are selected
 * from RendererBackend's fixed allowlist.
 */
class LoadedPlugin final {
public:
    LoadedPlugin() noexcept = default;
    ~LoadedPlugin();

    LoadedPlugin(const LoadedPlugin &) = delete;
    LoadedPlugin &operator=(const LoadedPlugin &) = delete;

    LoadedPlugin(LoadedPlugin &&other) noexcept;
    LoadedPlugin &operator=(LoadedPlugin &&other) noexcept;

    static RhiResult load(
        RendererBackend backend,
        const std::filesystem::path &trusted_directory,
        LoadedPlugin *out_plugin,
        std::string *out_diagnostic = nullptr
    );

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const RhiPluginApiV1 *api() const noexcept;

private:
    void *module_handle_ = nullptr;
    const RhiPluginApiV1 *api_ = nullptr;
};

}  // namespace harvester::rhi

#endif
