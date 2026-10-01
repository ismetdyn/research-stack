#include <assert.h>
#include <stddef.h>

#include <harvester/rhi/rhi_api.h>
#include <harvester/rhi/rhi_wsi.h>

static RhiResult RHI_API_CALL valid_get_vulkan_instance_extensions(
    void *context,
    const char *const **out_extensions,
    uint32_t *out_extension_count
) {
    static const char *const extensions[] = {"VK_KHR_surface"};
    (void)context;
    if (out_extensions == NULL || out_extension_count == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_extensions = extensions;
    *out_extension_count = 1U;
    return RHI_RESULT_SUCCESS;
}

static void RHI_API_CALL valid_vulkan_get_instance_proc_addr(void) {}

static RhiResult RHI_API_CALL valid_get_vulkan_instance_proc_addr(
    void *context,
    RhiVulkanProcAddress *out_get_instance_proc_addr
) {
    (void)context;
    if (out_get_instance_proc_addr == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_get_instance_proc_addr = valid_vulkan_get_instance_proc_addr;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_create_vulkan_surface(
    void *context,
    RhiVulkanInstanceHandle vulkan_instance,
    RhiVulkanSurfaceHandle *out_vulkan_surface
) {
    (void)context;
    if (vulkan_instance == 0U || out_vulkan_surface == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_vulkan_surface = UINT64_MAX;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_destroy_vulkan_surface(
    void *context,
    RhiVulkanInstanceHandle vulkan_instance,
    RhiVulkanSurfaceHandle vulkan_surface
) {
    (void)context;
    return vulkan_instance != 0U && vulkan_surface != 0U ?
        RHI_RESULT_SUCCESS : RHI_RESULT_INVALID_ARGUMENT;
}

static RhiResult RHI_API_CALL valid_get_plugin_info(
    RhiPluginInfo *out_info
) {
    if (out_info == NULL || out_info->struct_size < sizeof(*out_info)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    out_info->abi_version = HARVESTER_RHI_ABI_VERSION;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_create_runtime(
    const RhiRuntimeCreateInfo *create_info,
    RhiRuntime *out_runtime
) {
    (void)create_info;
    if (out_runtime == NULL) {
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
    if (out_capabilities == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    out_capabilities->enabled_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL valid_copy_diagnostic(
    RhiRuntime runtime,
    RhiDiagnostic *out_diagnostic
) {
    (void)runtime;
    if (out_diagnostic == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    out_diagnostic->result = RHI_RESULT_SUCCESS;
    return RHI_RESULT_SUCCESS;
}

static RhiPluginApiV1 valid_plugin_api(void) {
    RhiPluginApiV1 api = {0};

    api.struct_size = sizeof(api);
    api.abi_version = HARVESTER_RHI_ABI_VERSION;
    api.get_plugin_info = valid_get_plugin_info;
    api.create_runtime = valid_create_runtime;
    api.destroy_runtime = valid_destroy_runtime;
    api.get_runtime_capabilities = valid_get_runtime_capabilities;
    api.copy_diagnostic = valid_copy_diagnostic;

    return api;
}

int main(void) {
    RhiPluginApiV1 api = valid_plugin_api();
    RhiPluginApiV1 wrong_version = valid_plugin_api();
    RhiPluginApiV1 truncated = valid_plugin_api();
    RhiPluginApiV1 missing_entry = valid_plugin_api();
    RhiVulkanPresentationBindingV1 presentation_binding = {0};
    RhiVulkanPresentationBindingV1 truncated_presentation_binding = {0};
    RhiVulkanPresentationBindingV1 incomplete_presentation_binding = {0};

    assert(harvester_rhi_validate_plugin_api_v1(&api) == RHI_RESULT_SUCCESS);

    wrong_version.abi_version = HARVESTER_RHI_ABI_VERSION + 1U;
    assert(
        harvester_rhi_validate_plugin_api_v1(&wrong_version) ==
        RHI_RESULT_ABI_MISMATCH
    );

    truncated.struct_size = offsetof(RhiPluginApiV1, copy_diagnostic);
    assert(
        harvester_rhi_validate_plugin_api_v1(&truncated) ==
        RHI_RESULT_INVALID_ARGUMENT
    );

    missing_entry.create_runtime = NULL;
    assert(
        harvester_rhi_validate_plugin_api_v1(&missing_entry) ==
        RHI_RESULT_INVALID_ARGUMENT
    );

    RhiPluginApiV1 legacy_base = valid_plugin_api();
    RhiUiApiV1 ui_api = {0};
    legacy_base.struct_size = offsetof(RhiPluginApiV1, get_ui_api);
    ui_api.struct_size = sizeof(ui_api);
    assert(
        harvester_rhi_get_ui_api_v1(
            &legacy_base,
            RHI_RUNTIME_INVALID,
            &ui_api
        ) == RHI_RESULT_UNSUPPORTED
    );

    presentation_binding.struct_size = sizeof(presentation_binding);
    presentation_binding.get_instance_proc_addr =
        valid_get_vulkan_instance_proc_addr;
    presentation_binding.get_instance_extensions =
        valid_get_vulkan_instance_extensions;
    presentation_binding.create_surface = valid_create_vulkan_surface;
    presentation_binding.destroy_surface = valid_destroy_vulkan_surface;
    assert(
        harvester_rhi_validate_vulkan_presentation_binding_v1(
            &presentation_binding
        ) == RHI_RESULT_SUCCESS
    );

    truncated_presentation_binding = presentation_binding;
    truncated_presentation_binding.struct_size =
        offsetof(RhiVulkanPresentationBindingV1, destroy_surface);
    assert(
        harvester_rhi_validate_vulkan_presentation_binding_v1(
            &truncated_presentation_binding
        ) == RHI_RESULT_INVALID_ARGUMENT
    );

    incomplete_presentation_binding = presentation_binding;
    incomplete_presentation_binding.get_instance_proc_addr = NULL;
    assert(
        harvester_rhi_validate_vulkan_presentation_binding_v1(
            &incomplete_presentation_binding
        ) == RHI_RESULT_INVALID_ARGUMENT
    );

    incomplete_presentation_binding = presentation_binding;
    incomplete_presentation_binding.create_surface = NULL;
    assert(
        harvester_rhi_validate_vulkan_presentation_binding_v1(
            &incomplete_presentation_binding
        ) == RHI_RESULT_INVALID_ARGUMENT
    );

    return 0;
}
