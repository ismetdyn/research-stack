#ifndef HARVESTER_RHI_TYPES_H
#define HARVESTER_RHI_TYPES_H

#include <stdint.h>

#if defined(_WIN32)
#define RHI_API_CALL __cdecl
#elif defined(__GNUC__) || defined(__clang__)
#define RHI_API_CALL
#else
#define RHI_API_CALL
#endif

#if defined(HARVESTER_RHI_PLUGIN_BUILD)
#if defined(_WIN32)
#define RHI_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define RHI_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define RHI_PLUGIN_EXPORT
#endif
#else
#define RHI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
#define RHI_INVALID_HANDLE(type_name) type_name{UINT64_C(0)}
extern "C" {
#else
#define RHI_INVALID_HANDLE(type_name) ((type_name){UINT64_C(0)})
#endif

/*
 * Handles are opaque, backend-owned values. Zero is always invalid. A backend
 * must reject a non-zero handle belonging to another runtime or device without
 * modifying state. The bit layout and generation scheme are private to a
 * backend, so callers must never inspect or persist the value.
 */
#define RHI_DECLARE_HANDLE(name) \
    typedef struct name {          \
        uint64_t value;            \
    } name

RHI_DECLARE_HANDLE(RhiRuntime);
RHI_DECLARE_HANDLE(RhiAdapter);
RHI_DECLARE_HANDLE(RhiDevice);
RHI_DECLARE_HANDLE(RhiQueue);
RHI_DECLARE_HANDLE(RhiSurface);
RHI_DECLARE_HANDLE(RhiSwapchain);
RHI_DECLARE_HANDLE(RhiBuffer);
RHI_DECLARE_HANDLE(RhiTexture);
RHI_DECLARE_HANDLE(RhiSampler);
RHI_DECLARE_HANDLE(RhiShaderModule);
RHI_DECLARE_HANDLE(RhiPipeline);
RHI_DECLARE_HANDLE(RhiBindGroup);
RHI_DECLARE_HANDLE(RhiCommandEncoder);
RHI_DECLARE_HANDLE(RhiCommandBuffer);
RHI_DECLARE_HANDLE(RhiFence);

#undef RHI_DECLARE_HANDLE

#define RHI_RUNTIME_INVALID RHI_INVALID_HANDLE(RhiRuntime)
#define RHI_ADAPTER_INVALID RHI_INVALID_HANDLE(RhiAdapter)
#define RHI_DEVICE_INVALID RHI_INVALID_HANDLE(RhiDevice)
#define RHI_QUEUE_INVALID RHI_INVALID_HANDLE(RhiQueue)
#define RHI_SURFACE_INVALID RHI_INVALID_HANDLE(RhiSurface)
#define RHI_SWAPCHAIN_INVALID RHI_INVALID_HANDLE(RhiSwapchain)
#define RHI_BUFFER_INVALID RHI_INVALID_HANDLE(RhiBuffer)
#define RHI_TEXTURE_INVALID RHI_INVALID_HANDLE(RhiTexture)
#define RHI_SAMPLER_INVALID RHI_INVALID_HANDLE(RhiSampler)
#define RHI_SHADER_MODULE_INVALID RHI_INVALID_HANDLE(RhiShaderModule)
#define RHI_PIPELINE_INVALID RHI_INVALID_HANDLE(RhiPipeline)
#define RHI_BIND_GROUP_INVALID RHI_INVALID_HANDLE(RhiBindGroup)
#define RHI_COMMAND_ENCODER_INVALID RHI_INVALID_HANDLE(RhiCommandEncoder)
#define RHI_COMMAND_BUFFER_INVALID RHI_INVALID_HANDLE(RhiCommandBuffer)
#define RHI_FENCE_INVALID RHI_INVALID_HANDLE(RhiFence)

/* Numeric result values are part of the stable plugin ABI. */
typedef uint32_t RhiResult;

#define RHI_RESULT_SUCCESS UINT32_C(0)
#define RHI_RESULT_INVALID_ARGUMENT UINT32_C(1)
#define RHI_RESULT_ABI_MISMATCH UINT32_C(2)
#define RHI_RESULT_UNSUPPORTED UINT32_C(3)
#define RHI_RESULT_OUT_OF_MEMORY UINT32_C(4)
#define RHI_RESULT_DEVICE_LOST UINT32_C(5)
#define RHI_RESULT_SURFACE_OUT_OF_DATE UINT32_C(6)
#define RHI_RESULT_TIMEOUT UINT32_C(7)
#define RHI_RESULT_VALIDATION_FAILED UINT32_C(8)
#define RHI_RESULT_INTERNAL_ERROR UINT32_C(9)

typedef uint32_t RhiCapabilityProfile;

#define RHI_CAPABILITY_PROFILE_NONE UINT32_C(0)
#define RHI_CAPABILITY_PROFILE_UI_BASELINE UINT32_C(1)
#define RHI_CAPABILITY_PROFILE_ENGINE_BASELINE UINT32_C(2)

typedef uint64_t RhiFeatureFlags;

#define RHI_FEATURE_DYNAMIC_TEXTURES (UINT64_C(1) << 0U)
#define RHI_FEATURE_SRGB_SWAPCHAIN (UINT64_C(1) << 1U)
#define RHI_FEATURE_TIMESTAMP_QUERIES (UINT64_C(1) << 2U)
#define RHI_FEATURE_COMPUTE (UINT64_C(1) << 3U)

typedef uint32_t RhiThreadingModel;

#define RHI_THREADING_EXTERNAL_SYNCHRONIZATION UINT32_C(0)

/* Optional platform bindings remain opaque to the base RHI. */
typedef uint32_t RhiPresentationBindingKind;

#define RHI_PRESENTATION_BINDING_NONE UINT32_C(0)
#define RHI_PRESENTATION_BINDING_VULKAN_V1 UINT32_C(1)

#ifdef __cplusplus
}
#endif

#endif
