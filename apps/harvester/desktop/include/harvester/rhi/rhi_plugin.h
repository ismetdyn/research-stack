#ifndef HARVESTER_RHI_PLUGIN_H
#define HARVESTER_RHI_PLUGIN_H

#include <stddef.h>

#include <harvester/rhi/rhi_types.h>
#include <harvester/rhi/rhi_ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HARVESTER_RHI_ABI_VERSION UINT32_C(1)
#define HARVESTER_RHI_PLUGIN_ENTRYPOINT_NAME "harvester_rhi_get_plugin_api"
#define HARVESTER_RHI_BACKEND_NAME_CAPACITY UINT32_C(48)
#define HARVESTER_RHI_BACKEND_VERSION_CAPACITY UINT32_C(32)
#define HARVESTER_RHI_DIAGNOSTIC_CAPACITY UINT32_C(256)

/*
 * All output structures use struct_size as caller-provided capacity. Callers
 * zero the full allocation and set struct_size to sizeof(their version). A
 * plugin must write only fields present in that capacity and leave reserved
 * fields zero. Unknown input flags and non-zero reserved fields are rejected.
 */
typedef struct RhiPluginInfo {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t pointer_width;
    RhiThreadingModel threading_model;
    RhiFeatureFlags supported_features;
    uint64_t supported_profiles;
    char backend_name[HARVESTER_RHI_BACKEND_NAME_CAPACITY];
    char backend_version[HARVESTER_RHI_BACKEND_VERSION_CAPACITY];
} RhiPluginInfo;

typedef struct RhiRuntimeCreateInfo {
    uint32_t struct_size;
    RhiCapabilityProfile requested_profile;
    RhiFeatureFlags required_features;
    RhiFeatureFlags optional_features;
    uint32_t flags;
    uint32_t reserved;
    /*
     * Optional trailing WSI extension. Older v1 callers omit these fields by
     * setting struct_size to the preceding size. A backend reads this pointer
     * only when the complete extension is present and binding_kind is known.
     */
    RhiPresentationBindingKind presentation_binding_kind;
    uint32_t reserved2;
    const void *presentation_binding;
} RhiRuntimeCreateInfo;

typedef struct RhiLimits {
    uint32_t struct_size;
    uint32_t max_texture_dimension_2d;
    uint32_t max_texture_array_layers;
    uint32_t max_bindings_per_group;
    uint32_t uniform_buffer_offset_alignment;
    uint32_t max_color_attachments;
    uint32_t reserved;
} RhiLimits;

typedef struct RhiRuntimeCapabilities {
    uint32_t struct_size;
    RhiCapabilityProfile enabled_profile;
    RhiFeatureFlags enabled_features;
    RhiLimits limits;
} RhiRuntimeCapabilities;

typedef struct RhiDiagnostic {
    uint32_t struct_size;
    RhiResult result;
    uint32_t message_length;
    uint32_t reserved;
    char message[HARVESTER_RHI_DIAGNOSTIC_CAPACITY];
} RhiDiagnostic;

typedef RhiResult(RHI_API_CALL *RhiGetPluginInfoFn)(RhiPluginInfo *out_info);
typedef RhiResult(RHI_API_CALL *RhiCreateRuntimeFn)(
    const RhiRuntimeCreateInfo *create_info,
    RhiRuntime *out_runtime
);
typedef RhiResult(RHI_API_CALL *RhiDestroyRuntimeFn)(RhiRuntime runtime);
typedef RhiResult(RHI_API_CALL *RhiGetRuntimeCapabilitiesFn)(
    RhiRuntime runtime,
    RhiRuntimeCapabilities *out_capabilities
);
typedef RhiResult(RHI_API_CALL *RhiCopyDiagnosticFn)(
    RhiRuntime runtime,
    RhiDiagnostic *out_diagnostic
);

/*
 * This table is immutable static storage owned by the loaded plugin. It remains
 * valid until every runtime created through it is destroyed and the host has
 * stopped all calls into the plugin. Calls for one runtime are externally
 * synchronized and thread-affine to the thread that created that runtime.
 */
typedef struct RhiPluginApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    RhiGetPluginInfoFn get_plugin_info;
    RhiCreateRuntimeFn create_runtime;
    RhiDestroyRuntimeFn destroy_runtime;
    RhiGetRuntimeCapabilitiesFn get_runtime_capabilities;
    RhiCopyDiagnosticFn copy_diagnostic;
    /* Optional trailing extension; absent in plugins built before UI API v1. */
    RhiGetUiApiFn get_ui_api;
} RhiPluginApiV1;

typedef const RhiPluginApiV1 *(RHI_API_CALL *RhiGetPluginApiFn)(
    uint32_t requested_abi_version
);

RHI_PLUGIN_EXPORT const RhiPluginApiV1 *RHI_API_CALL
harvester_rhi_get_plugin_api(uint32_t requested_abi_version);

#ifdef __cplusplus
}
#endif

#endif
