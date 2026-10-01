#include <cassert>

#include <harvester/rhi/rhi_cpp.hpp>

static RhiResult RHI_API_CALL valid_get_plugin_info(RhiPluginInfo *out_info) {
    (void)out_info;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_create_runtime(
    const RhiRuntimeCreateInfo *create_info,
    RhiRuntime *out_runtime
) {
    (void)create_info;
    if (out_runtime == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    *out_runtime = RHI_RUNTIME_INVALID;
    return RHI_RESULT_UNSUPPORTED;
}

static RhiResult RHI_API_CALL valid_destroy_runtime(RhiRuntime runtime) {
    (void)runtime;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_get_runtime_capabilities(
    RhiRuntime runtime,
    RhiRuntimeCapabilities *out_capabilities
) {
    (void)runtime;
    (void)out_capabilities;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_copy_diagnostic(
    RhiRuntime runtime,
    RhiDiagnostic *out_diagnostic
) {
    (void)runtime;
    (void)out_diagnostic;
    return RHI_RESULT_SUCCESS;
}

static RhiPluginApiV1 valid_plugin_api() {
    RhiPluginApiV1 api{};

    api.struct_size = sizeof(api);
    api.abi_version = HARVESTER_RHI_ABI_VERSION;
    api.get_plugin_info = valid_get_plugin_info;
    api.create_runtime = valid_create_runtime;
    api.destroy_runtime = valid_destroy_runtime;
    api.get_runtime_capabilities = valid_get_runtime_capabilities;
    api.copy_diagnostic = valid_copy_diagnostic;

    return api;
}

int main() {
    harvester::rhi::PluginApiView view{};
    RhiPluginApiV1 api = valid_plugin_api();
    RhiPluginApiV1 invalid_api = valid_plugin_api();

    assert(view.valid() == false);
    assert(view.abi_version() == 0U);

    assert(
        harvester::rhi::PluginApiView::from(&api, &view) ==
        RHI_RESULT_SUCCESS
    );
    assert(view.valid());
    assert(view.abi_version() == HARVESTER_RHI_ABI_VERSION);
    assert(view.get() == &api);

    invalid_api.copy_diagnostic = nullptr;
    assert(
        harvester::rhi::PluginApiView::from(&invalid_api, &view) ==
        RHI_RESULT_INVALID_ARGUMENT
    );
    assert(view.valid() == false);

    return 0;
}
