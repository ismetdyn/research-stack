#include <harvester/rhi/rhi_api.h>

#include <stddef.h>
#include <string.h>

static size_t rhi_plugin_api_v1_required_size(void) {
    return offsetof(RhiPluginApiV1, copy_diagnostic) +
           sizeof(((RhiPluginApiV1 *)0)->copy_diagnostic);
}

static size_t rhi_plugin_api_v1_ui_extension_size(void) {
    return offsetof(RhiPluginApiV1, get_ui_api) +
           sizeof(((RhiPluginApiV1 *)0)->get_ui_api);
}

static size_t rhi_ui_api_v1_required_size(void) {
    return offsetof(RhiUiApiV1, write_texture) +
           sizeof(((RhiUiApiV1 *)0)->write_texture);
}

static size_t rhi_vulkan_presentation_binding_v1_required_size(void) {
    return offsetof(RhiVulkanPresentationBindingV1, destroy_surface) +
           sizeof(((RhiVulkanPresentationBindingV1 *)0)->destroy_surface);
}

static RhiResult rhi_validate_ui_api_v1(const RhiUiApiV1 *const api) {
    if (api == NULL || api->abi_version != HARVESTER_RHI_UI_API_VERSION ||
        api->struct_size < rhi_ui_api_v1_required_size()) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    if (api->create_buffer == NULL || api->destroy_buffer == NULL ||
        api->create_texture == NULL || api->destroy_texture == NULL ||
        api->create_pipeline == NULL || api->destroy_pipeline == NULL ||
        api->submit == NULL || api->wait_fence == NULL ||
        api->destroy_fence == NULL || api->write_buffer == NULL ||
        api->write_texture == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    return RHI_RESULT_SUCCESS;
}

uint32_t harvester_rhi_abi_version(void) {
    return HARVESTER_RHI_ABI_VERSION;
}

RhiResult harvester_rhi_validate_plugin_api_v1(const RhiPluginApiV1 *api) {
    if (api == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    if (api->abi_version != HARVESTER_RHI_ABI_VERSION) {
        return RHI_RESULT_ABI_MISMATCH;
    }

    if (api->struct_size < rhi_plugin_api_v1_required_size()) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    if (api->get_plugin_info == NULL || api->create_runtime == NULL ||
        api->destroy_runtime == NULL || api->get_runtime_capabilities == NULL ||
        api->copy_diagnostic == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    return RHI_RESULT_SUCCESS;
}

RhiResult harvester_rhi_validate_vulkan_presentation_binding_v1(
    const RhiVulkanPresentationBindingV1 *const binding
) {
    if (binding == NULL ||
        binding->struct_size < rhi_vulkan_presentation_binding_v1_required_size() ||
        binding->reserved != 0U || binding->get_instance_proc_addr == NULL ||
        binding->get_instance_extensions == NULL ||
        binding->create_surface == NULL || binding->destroy_surface == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    return RHI_RESULT_SUCCESS;
}

RhiResult harvester_rhi_get_ui_api_v1(
    const RhiPluginApiV1 *const api,
    const RhiRuntime runtime,
    RhiUiApiV1 *const out_ui_api
) {
    const RhiResult base_validation = harvester_rhi_validate_plugin_api_v1(api);
    if (base_validation != RHI_RESULT_SUCCESS) {
        return base_validation;
    }

    if (out_ui_api == NULL ||
        out_ui_api->struct_size < rhi_ui_api_v1_required_size()) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    if (api->struct_size < rhi_plugin_api_v1_ui_extension_size() ||
        api->get_ui_api == NULL) {
        return RHI_RESULT_UNSUPPORTED;
    }

    const uint32_t capacity = out_ui_api->struct_size;
    memset(out_ui_api, 0, capacity);
    out_ui_api->struct_size = capacity;
    const RhiResult result = api->get_ui_api(runtime, out_ui_api);
    if (result != RHI_RESULT_SUCCESS) {
        return result;
    }

    return rhi_validate_ui_api_v1(out_ui_api);
}
