#include <harvester/rhi/rhi_plugin.h>

#include <stddef.h>
#include <string.h>

/*
 * Aynı kaynak, derleme tanımlarıyla farklı kasıtlı bozukluklar üretir; her
 * varyant ayrı dizinde librhi_null.so olarak yüklenir. Tanım yoksa backend adı
 * uyuşmazlığı (vulkan != null) davranışı korunur.
 *   RHI_MALFORMED_ABI_DELTA     : tablodaki abi_version'a eklenir
 *   RHI_MALFORMED_TRUNCATE      : struct_size copy_diagnostic öncesinde biter
 *   RHI_MALFORMED_NULL_CREATE   : create_runtime girişi NULL
 *   RHI_MALFORMED_NO_ENTRYPOINT : giriş noktası hiç export edilmez
 */
#ifndef RHI_MALFORMED_ABI_DELTA
#define RHI_MALFORMED_ABI_DELTA 0U
#endif

static RhiResult RHI_API_CALL malformed_get_plugin_info(
    RhiPluginInfo *const out_info
) {
    if (out_info == NULL || out_info->struct_size < sizeof(*out_info)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint32_t capacity = out_info->struct_size;
    memset(out_info, 0, sizeof(*out_info));
    out_info->struct_size = capacity;
    out_info->abi_version = HARVESTER_RHI_ABI_VERSION;
    out_info->pointer_width = (uint32_t)(sizeof(void *) * 8U);
    out_info->threading_model = RHI_THREADING_EXTERNAL_SYNCHRONIZATION;
    memcpy(out_info->backend_name, "vulkan", sizeof("vulkan"));
    return RHI_RESULT_SUCCESS;
}

#ifndef RHI_MALFORMED_NULL_CREATE
static RhiResult RHI_API_CALL malformed_create_runtime(
    const RhiRuntimeCreateInfo *const create_info,
    RhiRuntime *const out_runtime
) {
    (void)create_info;
    if (out_runtime != NULL) {
        *out_runtime = RHI_RUNTIME_INVALID;
    }
    return RHI_RESULT_UNSUPPORTED;
}
#endif

static RhiResult RHI_API_CALL malformed_destroy_runtime(
    const RhiRuntime runtime
) {
    (void)runtime;
    return RHI_RESULT_INVALID_ARGUMENT;
}

static RhiResult RHI_API_CALL malformed_get_runtime_capabilities(
    const RhiRuntime runtime,
    RhiRuntimeCapabilities *const out_capabilities
) {
    (void)runtime;
    (void)out_capabilities;
    return RHI_RESULT_UNSUPPORTED;
}

static RhiResult RHI_API_CALL malformed_copy_diagnostic(
    const RhiRuntime runtime,
    RhiDiagnostic *const out_diagnostic
) {
    (void)runtime;
    (void)out_diagnostic;
    return RHI_RESULT_UNSUPPORTED;
}

static const RhiPluginApiV1 RHI_MALFORMED_PLUGIN_API = {
#ifdef RHI_MALFORMED_TRUNCATE
    offsetof(RhiPluginApiV1, copy_diagnostic),
#else
    sizeof(RHI_MALFORMED_PLUGIN_API),
#endif
    HARVESTER_RHI_ABI_VERSION + RHI_MALFORMED_ABI_DELTA,
    malformed_get_plugin_info,
#ifdef RHI_MALFORMED_NULL_CREATE
    NULL,
#else
    malformed_create_runtime,
#endif
    malformed_destroy_runtime,
    malformed_get_runtime_capabilities,
    malformed_copy_diagnostic,
    NULL,
};

#ifndef RHI_MALFORMED_NO_ENTRYPOINT
RHI_PLUGIN_EXPORT const RhiPluginApiV1 *RHI_API_CALL
harvester_rhi_get_plugin_api(const uint32_t requested_abi_version) {
    (void)requested_abi_version;
    return &RHI_MALFORMED_PLUGIN_API;
}
#endif
