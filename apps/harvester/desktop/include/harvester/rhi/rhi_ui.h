#ifndef HARVESTER_RHI_UI_H
#define HARVESTER_RHI_UI_H

#include <stdint.h>

#include <harvester/rhi/rhi_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HARVESTER_RHI_UI_API_VERSION UINT32_C(1)

typedef uint32_t RhiBufferUsageFlags;

#define RHI_BUFFER_USAGE_VERTEX (UINT32_C(1) << 0U)
#define RHI_BUFFER_USAGE_INDEX (UINT32_C(1) << 1U)
#define RHI_BUFFER_USAGE_COPY_DST (UINT32_C(1) << 2U)

typedef uint32_t RhiTextureUsageFlags;

#define RHI_TEXTURE_USAGE_SAMPLED (UINT32_C(1) << 0U)
#define RHI_TEXTURE_USAGE_COPY_DST (UINT32_C(1) << 1U)

typedef uint32_t RhiTextureFormat;

#define RHI_TEXTURE_FORMAT_RGBA8_UNORM UINT32_C(1)
#define RHI_TEXTURE_FORMAT_RGBA8_SRGB UINT32_C(2)

typedef uint32_t RhiUiBlendMode;

#define RHI_UI_BLEND_ALPHA UINT32_C(1)

typedef uint32_t RhiIndexFormat;

#define RHI_INDEX_FORMAT_UINT16 UINT32_C(1)
#define RHI_INDEX_FORMAT_UINT32 UINT32_C(2)

/* Descriptors contain no owned pointers; they are consumed before return. */
typedef struct RhiBufferCreateInfo {
    uint32_t struct_size;
    RhiBufferUsageFlags usage;
    uint64_t byte_size;
    uint32_t reserved;
    uint32_t reserved2;
} RhiBufferCreateInfo;

typedef struct RhiTextureCreateInfo {
    uint32_t struct_size;
    RhiTextureUsageFlags usage;
    uint32_t width;
    uint32_t height;
    RhiTextureFormat format;
    uint32_t reserved;
} RhiTextureCreateInfo;

typedef struct RhiUiPipelineCreateInfo {
    uint32_t struct_size;
    RhiTextureFormat color_format;
    RhiUiBlendMode blend_mode;
    uint32_t reserved;
} RhiUiPipelineCreateInfo;

/*
 * Host data is borrowed only for the call. On success a backend has copied it
 * into backend-owned memory or queued an equivalent copy that no longer reads
 * from data after this function returns.
 */
typedef struct RhiBufferWriteInfo {
    uint32_t struct_size;
    uint32_t reserved;
    RhiBuffer buffer;
    uint64_t offset;
    const void *data;
    uint64_t data_size;
} RhiBufferWriteInfo;

typedef struct RhiTextureWriteInfo {
    uint32_t struct_size;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_row;
    uint32_t reserved;
    RhiTexture texture;
    const void *data;
    uint64_t data_size;
} RhiTextureWriteInfo;

typedef struct RhiRect {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} RhiRect;

/* Matches the fixed UI vertex ABI consumed by the built-in UI pipeline. */
typedef struct RhiUiVertex {
    float position[2];
    float uv[2];
    uint32_t color_rgba8;
} RhiUiVertex;

/*
 * Draws are borrowed by submit and may be reused once it returns. All resource
 * handles must be live and owned by the supplied runtime. A zero-draw submit
 * is valid and completes a clear/presentation frame; its draws pointer is
 * ignored and may be null.
 */
typedef struct RhiUiDraw {
    RhiPipeline pipeline;
    RhiBuffer vertex_buffer;
    RhiBuffer index_buffer;
    RhiTexture texture;
    RhiRect scissor;
    uint32_t index_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t reserved;
} RhiUiDraw;

typedef struct RhiUiSubmitInfo {
    uint32_t struct_size;
    uint32_t draw_count;
    uint32_t target_width;
    uint32_t target_height;
    RhiIndexFormat index_format;
    uint32_t reserved;
    const RhiUiDraw *draws;
    uint32_t reserved2;
} RhiUiSubmitInfo;

/*
 * Clears the presentation target currently associated with the runtime. This
 * is an optional trailing UI-table extension: older plugins can omit it and a
 * caller can still submit UI work. The target dimensions protect backends from
 * accidentally clearing a stale or minimized surface.
 */
typedef struct RhiUiClearInfo {
    uint32_t struct_size;
    uint32_t target_width;
    uint32_t target_height;
    uint32_t reserved;
    float color_rgba[4];
} RhiUiClearInfo;

typedef RhiResult(RHI_API_CALL *RhiCreateBufferFn)(
    RhiRuntime runtime,
    const RhiBufferCreateInfo *create_info,
    RhiBuffer *out_buffer
);
typedef RhiResult(RHI_API_CALL *RhiDestroyBufferFn)(
    RhiRuntime runtime,
    RhiBuffer buffer
);
typedef RhiResult(RHI_API_CALL *RhiCreateTextureFn)(
    RhiRuntime runtime,
    const RhiTextureCreateInfo *create_info,
    RhiTexture *out_texture
);
typedef RhiResult(RHI_API_CALL *RhiDestroyTextureFn)(
    RhiRuntime runtime,
    RhiTexture texture
);
typedef RhiResult(RHI_API_CALL *RhiCreateUiPipelineFn)(
    RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *create_info,
    RhiPipeline *out_pipeline
);
typedef RhiResult(RHI_API_CALL *RhiDestroyUiPipelineFn)(
    RhiRuntime runtime,
    RhiPipeline pipeline
);
typedef RhiResult(RHI_API_CALL *RhiWriteBufferFn)(
    RhiRuntime runtime,
    const RhiBufferWriteInfo *write_info
);
typedef RhiResult(RHI_API_CALL *RhiWriteTextureFn)(
    RhiRuntime runtime,
    const RhiTextureWriteInfo *write_info
);
typedef RhiResult(RHI_API_CALL *RhiSubmitUiFn)(
    RhiRuntime runtime,
    const RhiUiSubmitInfo *submit_info,
    RhiFence *out_fence
);
typedef RhiResult(RHI_API_CALL *RhiWaitFenceFn)(
    RhiRuntime runtime,
    RhiFence fence,
    uint64_t timeout_nanoseconds
);
typedef RhiResult(RHI_API_CALL *RhiDestroyFenceFn)(
    RhiRuntime runtime,
    RhiFence fence
);
typedef RhiResult(RHI_API_CALL *RhiClearUiTargetFn)(
    RhiRuntime runtime,
    const RhiUiClearInfo *clear_info
);

/*
 * This is a copied, versioned capability table. The caller sets struct_size to
 * its allocation size. A plugin writes no fields beyond it and does not retain
 * the output pointer.
 */
typedef struct RhiUiApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    RhiCreateBufferFn create_buffer;
    RhiDestroyBufferFn destroy_buffer;
    RhiCreateTextureFn create_texture;
    RhiDestroyTextureFn destroy_texture;
    RhiCreateUiPipelineFn create_pipeline;
    RhiDestroyUiPipelineFn destroy_pipeline;
    RhiSubmitUiFn submit;
    RhiWaitFenceFn wait_fence;
    RhiDestroyFenceFn destroy_fence;
    RhiWriteBufferFn write_buffer;
    RhiWriteTextureFn write_texture;
    /* Optional trailing extension; absent from older UI API v1 table layouts. */
    RhiClearUiTargetFn clear_target;
} RhiUiApiV1;

typedef RhiResult(RHI_API_CALL *RhiGetUiApiFn)(
    RhiRuntime runtime,
    RhiUiApiV1 *out_ui_api
);

#ifdef __cplusplus
}
#endif

#endif
