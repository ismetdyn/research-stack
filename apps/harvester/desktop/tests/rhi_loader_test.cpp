#include <cassert>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <filesystem>
#include <string>

#include <harvester/rhi/rhi_loader.hpp>

namespace {

// Tanı mesajında beklenen parçaların hepsi yoksa "yalnız kod" regresyonudur.
bool contains_all(
    const std::string &text,
    std::initializer_list<const char *> parts
) {
    for (const char *part : parts) {
        if (text.find(part) == std::string::npos) {
            std::fprintf(stderr, "missing [%s] in [%s]\n", part, text.c_str());
            return false;
        }
    }
    return true;
}

RhiResult load_null(
    const std::filesystem::path &directory,
    std::string *diagnostic
) {
    harvester::rhi::LoadedPlugin plugin{};
    const RhiResult result = harvester::rhi::LoadedPlugin::load(
        harvester::rhi::RendererBackend::Null,
        directory,
        &plugin,
        diagnostic
    );
    assert(plugin.valid() == (result == RHI_RESULT_SUCCESS));
    return result;
}

void check_actionable_diagnostics() {
    const std::filesystem::path root{RHI_TEST_MALFORMED_ROOT};
    const std::string abi_now = std::to_string(HARVESTER_RHI_ABI_VERSION);
    const std::string abi_bad = std::to_string(HARVESTER_RHI_ABI_VERSION + 1U);
    std::string diagnostic;

    // Başarıda tanı boş kalmalı.
    assert(
        load_null(std::filesystem::path{RHI_TEST_NULL_PLUGIN_DIR}, &diagnostic) ==
        RHI_RESULT_SUCCESS
    );
    assert(diagnostic.empty());

    // ABI uyuşmazlığı: beklenen/gerçek sürüm ve eklenti yolu.
    assert(load_null(root / "malformed_abi", &diagnostic) == RHI_RESULT_ABI_MISMATCH);
    assert(contains_all(diagnostic, {"ABI", abi_now.c_str(), abi_bad.c_str(), "librhi_null.so"}));

    // Kırpılmış tablo: gerçek ve gereken struct boyutu.
    const std::string cut = std::to_string(offsetof(RhiPluginApiV1, copy_diagnostic));
    assert(load_null(root / "malformed_truncated", &diagnostic) == RHI_RESULT_INVALID_ARGUMENT);
    assert(contains_all(diagnostic, {"struct_size", cut.c_str(), "librhi_null.so"}));

    // NULL giriş noktası: hangi alanın eksik olduğu.
    assert(load_null(root / "malformed_null_entry", &diagnostic) == RHI_RESULT_INVALID_ARGUMENT);
    assert(contains_all(diagnostic, {"create_runtime", "NULL", "librhi_null.so"}));

    // Giriş noktası sembolü yok: aranan sembol adı.
    assert(load_null(root / "malformed_no_symbol", &diagnostic) == RHI_RESULT_ABI_MISMATCH);
    assert(contains_all(diagnostic, {HARVESTER_RHI_PLUGIN_ENTRYPOINT_NAME, "librhi_null.so"}));

    // Backend adı uyuşmazlığı: beklenen "null", gerçek "vulkan".
    assert(
        load_null(std::filesystem::path{RHI_TEST_MALFORMED_PLUGIN_DIR}, &diagnostic) ==
        RHI_RESULT_VALIDATION_FAILED
    );
    assert(contains_all(diagnostic, {"backend", "null", "vulkan", "librhi_null.so"}));

    // Allowlist dışı: dizinde eklenti yok, aranan dosya adı ve dizin görünmeli.
    assert(load_null(root / "no_such_dir", &diagnostic) == RHI_RESULT_INVALID_ARGUMENT);
    assert(contains_all(diagnostic, {"no_such_dir"}));
    // CMakeFiles var ama içinde librhi_null.so yok.
    assert(load_null(root / "CMakeFiles", &diagnostic) == RHI_RESULT_INVALID_ARGUMENT);
    assert(contains_all(diagnostic, {"librhi_null.so", "CMakeFiles"}));

    // Bilinmeyen backend değeri.
    harvester::rhi::LoadedPlugin plugin{};
    assert(
        harvester::rhi::LoadedPlugin::load(
            static_cast<harvester::rhi::RendererBackend>(999U),
            root / "CMakeFiles",
            &plugin,
            &diagnostic
        ) == RHI_RESULT_INVALID_ARGUMENT
    );
    assert(contains_all(diagnostic, {"999"}));
}

}  // namespace

int main() {
    check_actionable_diagnostics();

    harvester::rhi::LoadedPlugin plugin{};
    const std::filesystem::path null_plugin_directory{
        RHI_TEST_NULL_PLUGIN_DIR
    };

    assert(
        harvester::rhi::LoadedPlugin::load(
            harvester::rhi::RendererBackend::Null,
            null_plugin_directory,
            &plugin
        ) == RHI_RESULT_SUCCESS
    );
    assert(plugin.valid());
    assert(plugin.api() != nullptr);

    RhiPluginInfo info{};
    info.struct_size = sizeof(info);
    assert(plugin.api()->get_plugin_info(&info) == RHI_RESULT_SUCCESS);
    assert(info.abi_version == HARVESTER_RHI_ABI_VERSION);
    assert(info.threading_model == RHI_THREADING_EXTERNAL_SYNCHRONIZATION);

    RhiRuntimeCreateInfo create_info{};
    create_info.struct_size = sizeof(create_info);
    create_info.requested_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;

    RhiRuntimeCreateInfo unsupported_create_info = create_info;
    unsupported_create_info.required_features = RHI_FEATURE_COMPUTE;
    RhiRuntime unsupported_runtime = RHI_RUNTIME_INVALID;
    assert(
        plugin.api()->create_runtime(
            &unsupported_create_info,
            &unsupported_runtime
        ) == RHI_RESULT_UNSUPPORTED
    );
    assert(unsupported_runtime.value == 0U);

    RhiRuntime runtime = RHI_RUNTIME_INVALID;
    assert(
        plugin.api()->create_runtime(&create_info, &runtime) ==
        RHI_RESULT_SUCCESS
    );
    assert(runtime.value != 0U);

    RhiRuntimeCapabilities capabilities{};
    capabilities.struct_size = sizeof(capabilities);
    capabilities.limits.struct_size = sizeof(capabilities.limits);
    assert(
        plugin.api()->get_runtime_capabilities(runtime, &capabilities) ==
        RHI_RESULT_SUCCESS
    );
    assert(capabilities.enabled_profile == RHI_CAPABILITY_PROFILE_UI_BASELINE);

    assert(plugin.api()->destroy_runtime(runtime) == RHI_RESULT_SUCCESS);
    assert(plugin.api()->destroy_runtime(runtime) == RHI_RESULT_INVALID_ARGUMENT);
    assert(
        plugin.api()->get_runtime_capabilities(runtime, &capabilities) ==
        RHI_RESULT_INVALID_ARGUMENT
    );

    harvester::rhi::LoadedPlugin malformed{};
    const std::filesystem::path malformed_plugin_directory{
        RHI_TEST_MALFORMED_PLUGIN_DIR
    };
    assert(
        harvester::rhi::LoadedPlugin::load(
            harvester::rhi::RendererBackend::Null,
            malformed_plugin_directory,
            &malformed
        ) == RHI_RESULT_VALIDATION_FAILED
    );
    assert(malformed.valid() == false);

    harvester::rhi::LoadedPlugin rejected{};
    assert(
        harvester::rhi::LoadedPlugin::load(
            static_cast<harvester::rhi::RendererBackend>(999U),
            null_plugin_directory,
            &rejected
        ) == RHI_RESULT_INVALID_ARGUMENT
    );
    assert(rejected.valid() == false);

    return 0;
}
