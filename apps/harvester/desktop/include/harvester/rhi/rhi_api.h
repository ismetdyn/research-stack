#ifndef HARVESTER_RHI_API_H
#define HARVESTER_RHI_API_H

#include <stdint.h>

#include <harvester/rhi/rhi_plugin.h>
#include <harvester/rhi/rhi_wsi.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t harvester_rhi_abi_version(void);

/*
 * Validates tables returned by a cooperative plugin after the dynamic loader
 * resolved the entry point. It cannot make arbitrary native plugin code safe;
 * the host must load plugins only from trusted, allowlisted locations.
 */
RhiResult harvester_rhi_validate_plugin_api_v1(const RhiPluginApiV1 *api);

/* Validates the fixed Vulkan WSI callback table before a backend uses it. */
RhiResult harvester_rhi_validate_vulkan_presentation_binding_v1(
    const RhiVulkanPresentationBindingV1 *binding
);

/*
 * Resolves and validates the optional UI-baseline extension for one runtime.
 * The caller owns out_ui_api and must set its struct_size to sizeof its
 * allocation. An older base plugin reports RHI_RESULT_UNSUPPORTED here.
 */
RhiResult harvester_rhi_get_ui_api_v1(
    const RhiPluginApiV1 *api,
    RhiRuntime runtime,
    RhiUiApiV1 *out_ui_api
);

#ifdef __cplusplus
}
#endif

#endif
