#include <harvester/rhi/rhi_plugin.h>

#include <stdbool.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>

enum {
    RHI_NULL_MAX_RUNTIMES = 8,
    RHI_NULL_MAX_BUFFERS = 64,
    RHI_NULL_MAX_TEXTURES = 64,
    RHI_NULL_MAX_PIPELINES = 16,
    RHI_NULL_MAX_FENCES = 64,
    RHI_NULL_MAX_DRAWS_PER_SUBMIT = 256,
};

static const RhiFeatureFlags RHI_NULL_SUPPORTED_FEATURES =
    RHI_FEATURE_DYNAMIC_TEXTURES | RHI_FEATURE_SRGB_SWAPCHAIN;

typedef struct RhiNullRuntimeSlot {
    bool active;
    uint32_t generation;
    RhiFeatureFlags enabled_features;
    RhiDiagnostic diagnostic;
} RhiNullRuntimeSlot;

typedef struct RhiNullResourceSlot {
    bool active;
    uint32_t generation;
    uint64_t runtime_value;
    uint32_t usage;
    uint64_t byte_size;
    uint32_t width;
    uint32_t height;
} RhiNullResourceSlot;

static RhiNullRuntimeSlot rhi_null_runtime_slots[RHI_NULL_MAX_RUNTIMES];
static RhiNullResourceSlot rhi_null_buffer_slots[RHI_NULL_MAX_BUFFERS];
static RhiNullResourceSlot rhi_null_texture_slots[RHI_NULL_MAX_TEXTURES];
static RhiNullResourceSlot rhi_null_pipeline_slots[RHI_NULL_MAX_PIPELINES];
static RhiNullResourceSlot rhi_null_fence_slots[RHI_NULL_MAX_FENCES];
static atomic_flag rhi_null_lock_flag = ATOMIC_FLAG_INIT;

static RhiResult RHI_API_CALL rhi_null_get_plugin_info(RhiPluginInfo *out_info);
static RhiResult RHI_API_CALL rhi_null_create_runtime(
    const RhiRuntimeCreateInfo *create_info,
    RhiRuntime *out_runtime
);
static RhiResult RHI_API_CALL rhi_null_destroy_runtime(RhiRuntime runtime);
static RhiResult RHI_API_CALL rhi_null_get_runtime_capabilities(
    RhiRuntime runtime,
    RhiRuntimeCapabilities *out_capabilities
);
static RhiResult RHI_API_CALL rhi_null_copy_diagnostic(
    RhiRuntime runtime,
    RhiDiagnostic *out_diagnostic
);
static RhiResult RHI_API_CALL rhi_null_get_ui_api(
    RhiRuntime runtime,
    RhiUiApiV1 *out_ui_api
);
static RhiResult RHI_API_CALL rhi_null_create_buffer(
    RhiRuntime runtime,
    const RhiBufferCreateInfo *create_info,
    RhiBuffer *out_buffer
);
static RhiResult RHI_API_CALL rhi_null_destroy_buffer(
    RhiRuntime runtime,
    RhiBuffer buffer
);
static RhiResult RHI_API_CALL rhi_null_create_texture(
    RhiRuntime runtime,
    const RhiTextureCreateInfo *create_info,
    RhiTexture *out_texture
);
static RhiResult RHI_API_CALL rhi_null_destroy_texture(
    RhiRuntime runtime,
    RhiTexture texture
);
static RhiResult RHI_API_CALL rhi_null_create_pipeline(
    RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *create_info,
    RhiPipeline *out_pipeline
);
static RhiResult RHI_API_CALL rhi_null_destroy_pipeline(
    RhiRuntime runtime,
    RhiPipeline pipeline
);
static RhiResult RHI_API_CALL rhi_null_write_buffer(
    RhiRuntime runtime,
    const RhiBufferWriteInfo *write_info
);
static RhiResult RHI_API_CALL rhi_null_write_texture(
    RhiRuntime runtime,
    const RhiTextureWriteInfo *write_info
);
static RhiResult RHI_API_CALL rhi_null_submit(
    RhiRuntime runtime,
    const RhiUiSubmitInfo *submit_info,
    RhiFence *out_fence
);
static RhiResult RHI_API_CALL rhi_null_wait_fence(
    RhiRuntime runtime,
    RhiFence fence,
    uint64_t timeout_nanoseconds
);
static RhiResult RHI_API_CALL rhi_null_destroy_fence(
    RhiRuntime runtime,
    RhiFence fence
);
static RhiResult RHI_API_CALL rhi_null_clear_target(
    RhiRuntime runtime,
    const RhiUiClearInfo *clear_info
);

static const RhiUiApiV1 RHI_NULL_UI_API = {
    sizeof(RHI_NULL_UI_API),
    HARVESTER_RHI_UI_API_VERSION,
    rhi_null_create_buffer,
    rhi_null_destroy_buffer,
    rhi_null_create_texture,
    rhi_null_destroy_texture,
    rhi_null_create_pipeline,
    rhi_null_destroy_pipeline,
    rhi_null_submit,
    rhi_null_wait_fence,
    rhi_null_destroy_fence,
    rhi_null_write_buffer,
    rhi_null_write_texture,
    rhi_null_clear_target,
};

static const RhiPluginApiV1 RHI_NULL_PLUGIN_API = {
    sizeof(RHI_NULL_PLUGIN_API),
    HARVESTER_RHI_ABI_VERSION,
    rhi_null_get_plugin_info,
    rhi_null_create_runtime,
    rhi_null_destroy_runtime,
    rhi_null_get_runtime_capabilities,
    rhi_null_copy_diagnostic,
    rhi_null_get_ui_api,
};

static void rhi_null_lock(void) {
    while (atomic_flag_test_and_set_explicit(
        &rhi_null_lock_flag,
        memory_order_acquire
    )) {
    }
}

static void rhi_null_unlock(void) {
    atomic_flag_clear_explicit(&rhi_null_lock_flag, memory_order_release);
}

static bool rhi_null_has_capacity(const uint32_t capacity, const size_t size) {
    return capacity >= size;
}

static size_t rhi_null_runtime_create_info_base_size(void) {
    return offsetof(RhiRuntimeCreateInfo, presentation_binding_kind);
}

static bool rhi_null_has_unsupported_presentation_binding(
    const RhiRuntimeCreateInfo *const create_info
) {
    if (create_info->struct_size == rhi_null_runtime_create_info_base_size()) {
        return false;
    }
    if (!rhi_null_has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return true;
    }
    return create_info->presentation_binding_kind != RHI_PRESENTATION_BINDING_NONE ||
           create_info->reserved2 != 0U || create_info->presentation_binding != NULL;
}

static uint64_t rhi_null_handle_value(
    const uint32_t index,
    const uint32_t generation
) {
    return ((uint64_t)generation << 32U) | (uint64_t)(index + 1U);
}

static RhiRuntime rhi_null_runtime_for_slot(
    const uint32_t index,
    const uint32_t generation
) {
    return (RhiRuntime){rhi_null_handle_value(index, generation)};
}

static RhiNullRuntimeSlot *rhi_null_find_runtime(const RhiRuntime runtime) {
    const uint32_t index_plus_one = (uint32_t)runtime.value;
    const uint32_t generation = (uint32_t)(runtime.value >> 32U);
    if (index_plus_one == 0U || index_plus_one > RHI_NULL_MAX_RUNTIMES ||
        generation == 0U) {
        return NULL;
    }

    RhiNullRuntimeSlot *const slot =
        &rhi_null_runtime_slots[index_plus_one - 1U];
    return slot->active && slot->generation == generation ? slot : NULL;
}

static RhiNullResourceSlot *rhi_null_find_resource(
    RhiNullResourceSlot *const slots,
    const uint32_t slot_count,
    const uint64_t resource_value,
    const RhiRuntime runtime
) {
    const uint32_t index_plus_one = (uint32_t)resource_value;
    const uint32_t generation = (uint32_t)(resource_value >> 32U);
    if (index_plus_one == 0U || index_plus_one > slot_count ||
        generation == 0U) {
        return NULL;
    }

    RhiNullResourceSlot *const slot = &slots[index_plus_one - 1U];
    return slot->active && slot->generation == generation &&
                   slot->runtime_value == runtime.value
               ? slot
               : NULL;
}

static RhiResult rhi_null_allocate_resource(
    RhiNullResourceSlot *const slots,
    const uint32_t slot_count,
    const RhiRuntime runtime,
    uint64_t *const out_value
) {
    for (uint32_t index = 0U; index < slot_count; ++index) {
        RhiNullResourceSlot *const slot = &slots[index];
        if (slot->active) {
            continue;
        }

        ++slot->generation;
        if (slot->generation == 0U) {
            ++slot->generation;
        }
        slot->active = true;
        slot->runtime_value = runtime.value;
        slot->usage = 0U;
        slot->byte_size = UINT64_C(0);
        slot->width = 0U;
        slot->height = 0U;
        *out_value = rhi_null_handle_value(index, slot->generation);
        return RHI_RESULT_SUCCESS;
    }
    return RHI_RESULT_OUT_OF_MEMORY;
}

static void rhi_null_release_resources(
    RhiNullResourceSlot *const slots,
    const uint32_t slot_count,
    const RhiRuntime runtime
) {
    for (uint32_t index = 0U; index < slot_count; ++index) {
        RhiNullResourceSlot *const slot = &slots[index];
        if (slot->active && slot->runtime_value == runtime.value) {
            slot->active = false;
            slot->runtime_value = UINT64_C(0);
            slot->usage = 0U;
            slot->byte_size = UINT64_C(0);
            slot->width = 0U;
            slot->height = 0U;
        }
    }
}

static void rhi_null_release_runtime_resources(const RhiRuntime runtime) {
    rhi_null_release_resources(rhi_null_buffer_slots, RHI_NULL_MAX_BUFFERS, runtime);
    rhi_null_release_resources(rhi_null_texture_slots, RHI_NULL_MAX_TEXTURES, runtime);
    rhi_null_release_resources(
        rhi_null_pipeline_slots,
        RHI_NULL_MAX_PIPELINES,
        runtime
    );
    rhi_null_release_resources(rhi_null_fence_slots, RHI_NULL_MAX_FENCES, runtime);
}

static void rhi_null_reset_diagnostic(
    RhiNullRuntimeSlot *const slot,
    const RhiResult result
) {
    memset(&slot->diagnostic, 0, sizeof(slot->diagnostic));
    slot->diagnostic.struct_size = sizeof(slot->diagnostic);
    slot->diagnostic.result = result;
}

static bool rhi_null_is_valid_texture_format(const RhiTextureFormat format) {
    return format == RHI_TEXTURE_FORMAT_RGBA8_UNORM ||
           format == RHI_TEXTURE_FORMAT_RGBA8_SRGB;
}

static RhiResult RHI_API_CALL rhi_null_get_plugin_info(
    RhiPluginInfo *const out_info
) {
    if (out_info == NULL ||
        !rhi_null_has_capacity(out_info->struct_size, sizeof(*out_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint32_t capacity = out_info->struct_size;
    memset(out_info, 0, sizeof(*out_info));
    out_info->struct_size = capacity;
    out_info->abi_version = HARVESTER_RHI_ABI_VERSION;
    out_info->pointer_width = (uint32_t)(sizeof(void *) * 8U);
    out_info->threading_model = RHI_THREADING_EXTERNAL_SYNCHRONIZATION;
    out_info->supported_features = RHI_NULL_SUPPORTED_FEATURES;
    out_info->supported_profiles =
        UINT64_C(1) << RHI_CAPABILITY_PROFILE_UI_BASELINE;
    memcpy(out_info->backend_name, "null", sizeof("null"));
    memcpy(out_info->backend_version, "1", sizeof("1"));
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_create_runtime(
    const RhiRuntimeCreateInfo *const create_info,
    RhiRuntime *const out_runtime
) {
    if (out_runtime == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_runtime = RHI_RUNTIME_INVALID;

    if (create_info == NULL ||
        !rhi_null_has_capacity(
            create_info->struct_size,
            rhi_null_runtime_create_info_base_size()
        ) ||
        create_info->flags != 0U || create_info->reserved != 0U ||
        rhi_null_has_unsupported_presentation_binding(create_info) ||
        create_info->requested_profile != RHI_CAPABILITY_PROFILE_UI_BASELINE ||
        (create_info->required_features & ~RHI_NULL_SUPPORTED_FEATURES) != 0U ||
        (create_info->optional_features & ~RHI_NULL_SUPPORTED_FEATURES) != 0U) {
        return RHI_RESULT_UNSUPPORTED;
    }

    rhi_null_lock();
    for (uint32_t index = 0U; index < RHI_NULL_MAX_RUNTIMES; ++index) {
        RhiNullRuntimeSlot *const slot = &rhi_null_runtime_slots[index];
        if (slot->active) {
            continue;
        }

        ++slot->generation;
        if (slot->generation == 0U) {
            ++slot->generation;
        }
        slot->active = true;
        slot->enabled_features = create_info->required_features |
                                 (create_info->optional_features &
                                  RHI_NULL_SUPPORTED_FEATURES);
        rhi_null_reset_diagnostic(slot, RHI_RESULT_SUCCESS);
        *out_runtime = rhi_null_runtime_for_slot(index, slot->generation);
        rhi_null_unlock();
        return RHI_RESULT_SUCCESS;
    }
    rhi_null_unlock();
    return RHI_RESULT_OUT_OF_MEMORY;
}

static RhiResult RHI_API_CALL rhi_null_destroy_runtime(
    const RhiRuntime runtime
) {
    rhi_null_lock();
    RhiNullRuntimeSlot *const slot = rhi_null_find_runtime(runtime);
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    rhi_null_release_runtime_resources(runtime);
    slot->active = false;
    slot->enabled_features = 0U;
    rhi_null_reset_diagnostic(slot, RHI_RESULT_SUCCESS);
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_get_runtime_capabilities(
    const RhiRuntime runtime,
    RhiRuntimeCapabilities *const out_capabilities
) {
    if (out_capabilities == NULL ||
        !rhi_null_has_capacity(
            out_capabilities->struct_size,
            sizeof(*out_capabilities)
        ) ||
        !rhi_null_has_capacity(
            out_capabilities->limits.struct_size,
            sizeof(out_capabilities->limits)
        )) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint32_t capabilities_capacity = out_capabilities->struct_size;
    const uint32_t limits_capacity = out_capabilities->limits.struct_size;
    rhi_null_lock();
    RhiNullRuntimeSlot *const slot = rhi_null_find_runtime(runtime);
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->struct_size = capabilities_capacity;
    out_capabilities->enabled_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    out_capabilities->enabled_features = slot->enabled_features;
    out_capabilities->limits.struct_size = limits_capacity;
    out_capabilities->limits.max_texture_dimension_2d = 16384U;
    out_capabilities->limits.max_texture_array_layers = 256U;
    out_capabilities->limits.max_bindings_per_group = 8U;
    out_capabilities->limits.uniform_buffer_offset_alignment = 256U;
    out_capabilities->limits.max_color_attachments = 1U;
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_copy_diagnostic(
    const RhiRuntime runtime,
    RhiDiagnostic *const out_diagnostic
) {
    if (out_diagnostic == NULL ||
        !rhi_null_has_capacity(out_diagnostic->struct_size, sizeof(*out_diagnostic))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint32_t capacity = out_diagnostic->struct_size;
    rhi_null_lock();
    RhiNullRuntimeSlot *const slot = rhi_null_find_runtime(runtime);
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    memcpy(out_diagnostic, &slot->diagnostic, sizeof(*out_diagnostic));
    out_diagnostic->struct_size = capacity;
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_get_ui_api(
    const RhiRuntime runtime,
    RhiUiApiV1 *const out_ui_api
) {
    if (out_ui_api == NULL ||
        !rhi_null_has_capacity(out_ui_api->struct_size, sizeof(*out_ui_api))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    memcpy(out_ui_api, &RHI_NULL_UI_API, sizeof(RHI_NULL_UI_API));
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_create_buffer(
    const RhiRuntime runtime,
    const RhiBufferCreateInfo *const create_info,
    RhiBuffer *const out_buffer
) {
    if (out_buffer == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_buffer = RHI_BUFFER_INVALID;
    if (create_info == NULL ||
        !rhi_null_has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (create_info->byte_size == 0U || create_info->reserved != 0U ||
        create_info->reserved2 != 0U || create_info->usage == 0U ||
        (create_info->usage &
         ~(RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_INDEX |
           RHI_BUFFER_USAGE_COPY_DST)) != 0U) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    uint64_t resource_value = UINT64_C(0);
    const RhiResult result = rhi_null_allocate_resource(
        rhi_null_buffer_slots,
        RHI_NULL_MAX_BUFFERS,
        runtime,
        &resource_value
    );
    if (result == RHI_RESULT_SUCCESS) {
        *out_buffer = (RhiBuffer){resource_value};
        rhi_null_find_resource(
            rhi_null_buffer_slots,
            RHI_NULL_MAX_BUFFERS,
            resource_value,
            runtime
        )->usage = create_info->usage;
        rhi_null_find_resource(
            rhi_null_buffer_slots,
            RHI_NULL_MAX_BUFFERS,
            resource_value,
            runtime
        )->byte_size = create_info->byte_size;
    }
    rhi_null_unlock();
    return result;
}

static RhiResult RHI_API_CALL rhi_null_destroy_buffer(
    const RhiRuntime runtime,
    const RhiBuffer buffer
) {
    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiNullResourceSlot *const slot = rhi_null_find_resource(
        rhi_null_buffer_slots,
        RHI_NULL_MAX_BUFFERS,
        buffer.value,
        runtime
    );
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    slot->active = false;
    slot->runtime_value = UINT64_C(0);
    slot->usage = 0U;
    slot->byte_size = UINT64_C(0);
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_create_texture(
    const RhiRuntime runtime,
    const RhiTextureCreateInfo *const create_info,
    RhiTexture *const out_texture
) {
    if (out_texture == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_texture = RHI_TEXTURE_INVALID;
    if (create_info == NULL ||
        !rhi_null_has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (create_info->width == 0U || create_info->height == 0U ||
        create_info->reserved != 0U ||
        !rhi_null_is_valid_texture_format(create_info->format) ||
        (create_info->usage & RHI_TEXTURE_USAGE_SAMPLED) == 0U ||
        (create_info->usage &
         ~(RHI_TEXTURE_USAGE_SAMPLED | RHI_TEXTURE_USAGE_COPY_DST)) != 0U) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    uint64_t resource_value = UINT64_C(0);
    const RhiResult result = rhi_null_allocate_resource(
        rhi_null_texture_slots,
        RHI_NULL_MAX_TEXTURES,
        runtime,
        &resource_value
    );
    if (result == RHI_RESULT_SUCCESS) {
        *out_texture = (RhiTexture){resource_value};
        RhiNullResourceSlot *const slot = rhi_null_find_resource(
            rhi_null_texture_slots,
            RHI_NULL_MAX_TEXTURES,
            resource_value,
            runtime
        );
        slot->usage = create_info->usage;
        slot->width = create_info->width;
        slot->height = create_info->height;
    }
    rhi_null_unlock();
    return result;
}

static RhiResult RHI_API_CALL rhi_null_destroy_texture(
    const RhiRuntime runtime,
    const RhiTexture texture
) {
    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiNullResourceSlot *const slot = rhi_null_find_resource(
        rhi_null_texture_slots,
        RHI_NULL_MAX_TEXTURES,
        texture.value,
        runtime
    );
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    slot->active = false;
    slot->runtime_value = UINT64_C(0);
    slot->usage = 0U;
    slot->width = 0U;
    slot->height = 0U;
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_create_pipeline(
    const RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *const create_info,
    RhiPipeline *const out_pipeline
) {
    if (out_pipeline == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_pipeline = RHI_PIPELINE_INVALID;
    if (create_info == NULL ||
        !rhi_null_has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (create_info->reserved != 0U ||
        create_info->blend_mode != RHI_UI_BLEND_ALPHA ||
        !rhi_null_is_valid_texture_format(create_info->color_format)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    uint64_t resource_value = UINT64_C(0);
    const RhiResult result = rhi_null_allocate_resource(
        rhi_null_pipeline_slots,
        RHI_NULL_MAX_PIPELINES,
        runtime,
        &resource_value
    );
    if (result == RHI_RESULT_SUCCESS) {
        *out_pipeline = (RhiPipeline){resource_value};
    }
    rhi_null_unlock();
    return result;
}

static RhiResult RHI_API_CALL rhi_null_destroy_pipeline(
    const RhiRuntime runtime,
    const RhiPipeline pipeline
) {
    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiNullResourceSlot *const slot = rhi_null_find_resource(
        rhi_null_pipeline_slots,
        RHI_NULL_MAX_PIPELINES,
        pipeline.value,
        runtime
    );
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    slot->active = false;
    slot->runtime_value = UINT64_C(0);
    slot->usage = 0U;
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_write_buffer(
    const RhiRuntime runtime,
    const RhiBufferWriteInfo *const write_info
) {
    if (write_info == NULL ||
        !rhi_null_has_capacity(write_info->struct_size, sizeof(*write_info)) ||
        write_info->reserved != 0U || write_info->data == NULL ||
        write_info->data_size == 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiNullResourceSlot *const slot = rhi_null_find_resource(
        rhi_null_buffer_slots,
        RHI_NULL_MAX_BUFFERS,
        write_info->buffer.value,
        runtime
    );
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if ((slot->usage & RHI_BUFFER_USAGE_COPY_DST) == 0U ||
        write_info->offset > slot->byte_size ||
        write_info->data_size > slot->byte_size - write_info->offset) {
        rhi_null_unlock();
        return RHI_RESULT_VALIDATION_FAILED;
    }
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_write_texture(
    const RhiRuntime runtime,
    const RhiTextureWriteInfo *const write_info
) {
    if (write_info == NULL ||
        !rhi_null_has_capacity(write_info->struct_size, sizeof(*write_info)) ||
        write_info->reserved != 0U || write_info->data == NULL ||
        write_info->width == 0U || write_info->height == 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint64_t minimum_row_size = (uint64_t)write_info->width * 4U;
    if (write_info->bytes_per_row < minimum_row_size ||
        (write_info->height > 1U &&
         write_info->bytes_per_row >
             (UINT64_MAX - minimum_row_size) /
                 (uint64_t)(write_info->height - 1U))) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const uint64_t required_data_size =
        (uint64_t)(write_info->height - 1U) * write_info->bytes_per_row +
        minimum_row_size;
    if (write_info->data_size < required_data_size) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiNullResourceSlot *const slot = rhi_null_find_resource(
        rhi_null_texture_slots,
        RHI_NULL_MAX_TEXTURES,
        write_info->texture.value,
        runtime
    );
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if ((slot->usage & RHI_TEXTURE_USAGE_COPY_DST) == 0U ||
        write_info->x > slot->width || write_info->y > slot->height ||
        write_info->width > slot->width - write_info->x ||
        write_info->height > slot->height - write_info->y) {
        rhi_null_unlock();
        return RHI_RESULT_VALIDATION_FAILED;
    }
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult rhi_null_validate_ui_draw(
    const RhiRuntime runtime,
    const RhiUiSubmitInfo *const submit_info,
    const RhiUiDraw *const draw
) {
    if (draw->reserved != 0U || draw->index_count == 0U ||
        draw->scissor.x < 0 || draw->scissor.y < 0 ||
        draw->scissor.width == 0U || draw->scissor.height == 0U ||
        (uint32_t)draw->scissor.x > submit_info->target_width ||
        (uint32_t)draw->scissor.y > submit_info->target_height ||
        draw->scissor.width >
            submit_info->target_width - (uint32_t)draw->scissor.x ||
        draw->scissor.height >
            submit_info->target_height - (uint32_t)draw->scissor.y ||
        rhi_null_find_resource(
            rhi_null_pipeline_slots,
            RHI_NULL_MAX_PIPELINES,
            draw->pipeline.value,
            runtime
        ) == NULL ||
        rhi_null_find_resource(
            rhi_null_texture_slots,
            RHI_NULL_MAX_TEXTURES,
            draw->texture.value,
            runtime
        ) == NULL) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    RhiNullResourceSlot *const vertex_slot = rhi_null_find_resource(
        rhi_null_buffer_slots,
        RHI_NULL_MAX_BUFFERS,
        draw->vertex_buffer.value,
        runtime
    );
    RhiNullResourceSlot *const index_slot = rhi_null_find_resource(
        rhi_null_buffer_slots,
        RHI_NULL_MAX_BUFFERS,
        draw->index_buffer.value,
        runtime
    );
    if (vertex_slot == NULL || index_slot == NULL ||
        (vertex_slot->usage & RHI_BUFFER_USAGE_VERTEX) == 0U ||
        (index_slot->usage & RHI_BUFFER_USAGE_INDEX) == 0U) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const uint64_t index_size =
        submit_info->index_format == RHI_INDEX_FORMAT_UINT16 ? 2U : 4U;
    if ((uint64_t)draw->first_index > UINT64_MAX / index_size) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const uint64_t index_offset = (uint64_t)draw->first_index * index_size;
    if (index_offset > index_slot->byte_size ||
        (uint64_t)draw->index_count >
            (index_slot->byte_size - index_offset) / index_size) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_submit(
    const RhiRuntime runtime,
    const RhiUiSubmitInfo *const submit_info,
    RhiFence *const out_fence
) {
    if (out_fence == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_fence = RHI_FENCE_INVALID;
    if (submit_info == NULL ||
        !rhi_null_has_capacity(submit_info->struct_size, sizeof(*submit_info)) ||
        (submit_info->draw_count != 0U && submit_info->draws == NULL)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (submit_info->reserved != 0U || submit_info->reserved2 != 0U ||
        submit_info->draw_count > RHI_NULL_MAX_DRAWS_PER_SUBMIT ||
        submit_info->target_width == 0U || submit_info->target_height == 0U ||
        (submit_info->index_format != RHI_INDEX_FORMAT_UINT16 &&
         submit_info->index_format != RHI_INDEX_FORMAT_UINT32)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0U; index < submit_info->draw_count; ++index) {
        const RhiResult result = rhi_null_validate_ui_draw(
            runtime,
            submit_info,
            &submit_info->draws[index]
        );
        if (result != RHI_RESULT_SUCCESS) {
            rhi_null_unlock();
            return result;
        }
    }

    uint64_t resource_value = UINT64_C(0);
    const RhiResult result = rhi_null_allocate_resource(
        rhi_null_fence_slots,
        RHI_NULL_MAX_FENCES,
        runtime,
        &resource_value
    );
    if (result == RHI_RESULT_SUCCESS) {
        *out_fence = (RhiFence){resource_value};
    }
    rhi_null_unlock();
    return result;
}

static RhiResult RHI_API_CALL rhi_null_wait_fence(
    const RhiRuntime runtime,
    const RhiFence fence,
    const uint64_t timeout_nanoseconds
) {
    (void)timeout_nanoseconds;
    rhi_null_lock();
    const bool valid = rhi_null_find_runtime(runtime) != NULL &&
                       rhi_null_find_resource(
                           rhi_null_fence_slots,
                           RHI_NULL_MAX_FENCES,
                           fence.value,
                           runtime
                       ) != NULL;
    rhi_null_unlock();
    return valid ? RHI_RESULT_SUCCESS : RHI_RESULT_INVALID_ARGUMENT;
}

static RhiResult RHI_API_CALL rhi_null_destroy_fence(
    const RhiRuntime runtime,
    const RhiFence fence
) {
    rhi_null_lock();
    if (rhi_null_find_runtime(runtime) == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiNullResourceSlot *const slot = rhi_null_find_resource(
        rhi_null_fence_slots,
        RHI_NULL_MAX_FENCES,
        fence.value,
        runtime
    );
    if (slot == NULL) {
        rhi_null_unlock();
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    slot->active = false;
    slot->runtime_value = UINT64_C(0);
    slot->usage = 0U;
    rhi_null_unlock();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_null_clear_target(
    const RhiRuntime runtime,
    const RhiUiClearInfo *const clear_info
) {
    if (clear_info == NULL ||
        !rhi_null_has_capacity(clear_info->struct_size, sizeof(*clear_info)) ||
        clear_info->target_width == 0U || clear_info->target_height == 0U ||
        clear_info->reserved != 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0U; index < 4U; ++index) {
        if (!isfinite(clear_info->color_rgba[index])) {
            return RHI_RESULT_VALIDATION_FAILED;
        }
    }

    rhi_null_lock();
    const bool valid = rhi_null_find_runtime(runtime) != NULL;
    rhi_null_unlock();
    return valid ? RHI_RESULT_SUCCESS : RHI_RESULT_INVALID_ARGUMENT;
}

RHI_PLUGIN_EXPORT const RhiPluginApiV1 *RHI_API_CALL
harvester_rhi_get_plugin_api(const uint32_t requested_abi_version) {
    if (requested_abi_version != HARVESTER_RHI_ABI_VERSION) {
        return NULL;
    }
    return &RHI_NULL_PLUGIN_API;
}
