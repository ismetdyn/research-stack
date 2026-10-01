#ifndef HARVESTER_RHI_WSI_H
#define HARVESTER_RHI_WSI_H

#include <stdint.h>

#include <harvester/rhi/rhi_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-owned bridge used only with RHI_PRESENTATION_BINDING_VULKAN_V1. Vulkan
 * handles travel as fixed-width values so the base RHI, UI, and application
 * headers do not depend on Vulkan or a windowing-library type. `VkSurfaceKHR`
 * may be 64-bit even on a 32-bit target, so uintptr_t is not a valid transport
 * type. The binding and its context must remain valid from create_runtime
 * through destroy_runtime.
 */
typedef uint64_t RhiVulkanInstanceHandle;
typedef uint64_t RhiVulkanSurfaceHandle;
typedef void(RHI_API_CALL *RhiVulkanProcAddress)(void);

typedef RhiResult(RHI_API_CALL *RhiVulkanGetInstanceProcAddrFn)(
    void *context,
    RhiVulkanProcAddress *out_get_instance_proc_addr
);

typedef RhiResult(RHI_API_CALL *RhiVulkanGetInstanceExtensionsFn)(
    void *context,
    const char *const **out_extensions,
    uint32_t *out_extension_count
);
typedef RhiResult(RHI_API_CALL *RhiVulkanCreateSurfaceFn)(
    void *context,
    RhiVulkanInstanceHandle vulkan_instance,
    RhiVulkanSurfaceHandle *out_vulkan_surface
);
typedef RhiResult(RHI_API_CALL *RhiVulkanDestroySurfaceFn)(
    void *context,
    RhiVulkanInstanceHandle vulkan_instance,
    RhiVulkanSurfaceHandle vulkan_surface
);

typedef struct RhiVulkanPresentationBindingV1 {
    uint32_t struct_size;
    uint32_t reserved;
    void *context;
    RhiVulkanGetInstanceProcAddrFn get_instance_proc_addr;
    RhiVulkanGetInstanceExtensionsFn get_instance_extensions;
    RhiVulkanCreateSurfaceFn create_surface;
    RhiVulkanDestroySurfaceFn destroy_surface;
} RhiVulkanPresentationBindingV1;

#ifdef __cplusplus
}
#endif

#endif
