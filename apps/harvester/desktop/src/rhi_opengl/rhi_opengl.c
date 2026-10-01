#include <harvester/rhi/rhi_plugin.h>

#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

enum {
    RHI_GL_MAX_BUFFERS = 64,
    RHI_GL_MAX_TEXTURES = 64,
    RHI_GL_MAX_PIPELINES = 16,
    RHI_GL_MAX_FENCES = 64,
    RHI_GL_MAX_DRAWS_PER_SUBMIT = 1024,
};

static const RhiFeatureFlags RHI_GL_SUPPORTED_FEATURES =
    RHI_FEATURE_DYNAMIC_TEXTURES;

typedef struct RhiGlResourceSlot {
    bool active;
    uint32_t generation;
    uint64_t runtime_value;
    GLuint object;
    uint32_t usage;
    uint64_t byte_size;
    uint32_t width;
    uint32_t height;
} RhiGlResourceSlot;

typedef struct RhiGlFenceSlot {
    bool active;
    uint32_t generation;
    uint64_t runtime_value;
    GLsync sync;
} RhiGlFenceSlot;

typedef struct RhiGlFunctions {
    PFNGLGENBUFFERSPROC gen_buffers;
    PFNGLDELETEBUFFERSPROC delete_buffers;
    PFNGLBINDBUFFERPROC bind_buffer;
    PFNGLBUFFERDATAPROC buffer_data;
    PFNGLBUFFERSUBDATAPROC buffer_sub_data;
    PFNGLGENVERTEXARRAYSPROC gen_vertex_arrays;
    PFNGLDELETEVERTEXARRAYSPROC delete_vertex_arrays;
    PFNGLBINDVERTEXARRAYPROC bind_vertex_array;
    PFNGLCREATESHADERPROC create_shader;
    PFNGLSHADERSOURCEPROC shader_source;
    PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv;
    PFNGLGETSHADERINFOLOGPROC get_shader_info_log;
    PFNGLDELETESHADERPROC delete_shader;
    PFNGLCREATEPROGRAMPROC create_program;
    PFNGLATTACHSHADERPROC attach_shader;
    PFNGLLINKPROGRAMPROC link_program;
    PFNGLGETPROGRAMIVPROC get_program_iv;
    PFNGLGETPROGRAMINFOLOGPROC get_program_info_log;
    PFNGLDELETEPROGRAMPROC delete_program;
    PFNGLUSEPROGRAMPROC use_program;
    PFNGLGETUNIFORMLOCATIONPROC get_uniform_location;
    PFNGLUNIFORM1IPROC uniform_1i;
    PFNGLUNIFORM2FPROC uniform_2f;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enable_vertex_attrib_array;
    PFNGLVERTEXATTRIBPOINTERPROC vertex_attrib_pointer;
    PFNGLACTIVETEXTUREPROC active_texture;
    PFNGLDRAWELEMENTSBASEVERTEXPROC draw_elements_base_vertex;
    PFNGLFENCESYNCPROC fence_sync;
    PFNGLCLIENTWAITSYNCPROC client_wait_sync;
    PFNGLDELETESYNCPROC delete_sync;
    PFNGLBLENDEQUATIONPROC blend_equation;
    PFNGLBLENDFUNCSEPARATEPROC blend_func_separate;
    PFNGLDEBUGMESSAGECALLBACKPROC debug_message_callback;
} RhiGlFunctions;

typedef struct RhiGlRuntime {
    bool active;
    uint32_t generation;
    GLXContext context;
    RhiFeatureFlags enabled_features;
    RhiDiagnostic diagnostic;
    GLuint ui_program;
    GLuint ui_vertex_array;
    GLint display_size_location;
    GLint texture_location;
    bool debug_output_enabled;
} RhiGlRuntime;

static RhiGlFunctions rhi_gl;
static RhiGlRuntime rhi_gl_runtime;
static RhiGlResourceSlot rhi_gl_buffer_slots[RHI_GL_MAX_BUFFERS];
static RhiGlResourceSlot rhi_gl_texture_slots[RHI_GL_MAX_TEXTURES];
static RhiGlResourceSlot rhi_gl_pipeline_slots[RHI_GL_MAX_PIPELINES];
static RhiGlFenceSlot rhi_gl_fence_slots[RHI_GL_MAX_FENCES];

static RhiResult RHI_API_CALL rhi_gl_get_plugin_info(RhiPluginInfo *out_info);
static RhiResult RHI_API_CALL rhi_gl_create_runtime(
    const RhiRuntimeCreateInfo *create_info,
    RhiRuntime *out_runtime
);
static RhiResult RHI_API_CALL rhi_gl_destroy_runtime(RhiRuntime runtime);
static RhiResult RHI_API_CALL rhi_gl_get_runtime_capabilities(
    RhiRuntime runtime,
    RhiRuntimeCapabilities *out_capabilities
);
static RhiResult RHI_API_CALL rhi_gl_copy_diagnostic(
    RhiRuntime runtime,
    RhiDiagnostic *out_diagnostic
);
static RhiResult RHI_API_CALL rhi_gl_get_ui_api(
    RhiRuntime runtime,
    RhiUiApiV1 *out_ui_api
);
static RhiResult RHI_API_CALL rhi_gl_create_buffer(
    RhiRuntime runtime,
    const RhiBufferCreateInfo *create_info,
    RhiBuffer *out_buffer
);
static RhiResult RHI_API_CALL rhi_gl_destroy_buffer(
    RhiRuntime runtime,
    RhiBuffer buffer
);
static RhiResult RHI_API_CALL rhi_gl_create_texture(
    RhiRuntime runtime,
    const RhiTextureCreateInfo *create_info,
    RhiTexture *out_texture
);
static RhiResult RHI_API_CALL rhi_gl_destroy_texture(
    RhiRuntime runtime,
    RhiTexture texture
);
static RhiResult RHI_API_CALL rhi_gl_create_pipeline(
    RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *create_info,
    RhiPipeline *out_pipeline
);
static RhiResult RHI_API_CALL rhi_gl_destroy_pipeline(
    RhiRuntime runtime,
    RhiPipeline pipeline
);
static RhiResult RHI_API_CALL rhi_gl_write_buffer(
    RhiRuntime runtime,
    const RhiBufferWriteInfo *write_info
);
static RhiResult RHI_API_CALL rhi_gl_write_texture(
    RhiRuntime runtime,
    const RhiTextureWriteInfo *write_info
);
static RhiResult RHI_API_CALL rhi_gl_submit(
    RhiRuntime runtime,
    const RhiUiSubmitInfo *submit_info,
    RhiFence *out_fence
);
static RhiResult RHI_API_CALL rhi_gl_wait_fence(
    RhiRuntime runtime,
    RhiFence fence,
    uint64_t timeout_nanoseconds
);
static RhiResult RHI_API_CALL rhi_gl_destroy_fence(
    RhiRuntime runtime,
    RhiFence fence
);
static RhiResult RHI_API_CALL rhi_gl_clear_target(
    RhiRuntime runtime,
    const RhiUiClearInfo *clear_info
);

static const RhiUiApiV1 RHI_GL_UI_API = {
    sizeof(RHI_GL_UI_API),
    HARVESTER_RHI_UI_API_VERSION,
    rhi_gl_create_buffer,
    rhi_gl_destroy_buffer,
    rhi_gl_create_texture,
    rhi_gl_destroy_texture,
    rhi_gl_create_pipeline,
    rhi_gl_destroy_pipeline,
    rhi_gl_submit,
    rhi_gl_wait_fence,
    rhi_gl_destroy_fence,
    rhi_gl_write_buffer,
    rhi_gl_write_texture,
    rhi_gl_clear_target,
};

static const RhiPluginApiV1 RHI_GL_PLUGIN_API = {
    sizeof(RHI_GL_PLUGIN_API),
    HARVESTER_RHI_ABI_VERSION,
    rhi_gl_get_plugin_info,
    rhi_gl_create_runtime,
    rhi_gl_destroy_runtime,
    rhi_gl_get_runtime_capabilities,
    rhi_gl_copy_diagnostic,
    rhi_gl_get_ui_api,
};

static bool rhi_gl_has_capacity(const uint32_t capacity, const size_t size) {
    return capacity >= size;
}

static size_t rhi_gl_runtime_create_info_base_size(void) {
    return offsetof(RhiRuntimeCreateInfo, presentation_binding_kind);
}

static bool rhi_gl_has_unsupported_presentation_binding(
    const RhiRuntimeCreateInfo *const create_info
) {
    if (create_info->struct_size == rhi_gl_runtime_create_info_base_size()) {
        return false;
    }
    if (!rhi_gl_has_capacity(create_info->struct_size, sizeof(*create_info))) {
        return true;
    }
    return create_info->presentation_binding_kind != RHI_PRESENTATION_BINDING_NONE ||
           create_info->reserved2 != 0U || create_info->presentation_binding != NULL;
}

static uint64_t rhi_gl_handle_value(
    const uint32_t index,
    const uint32_t generation
) {
    return ((uint64_t)generation << 32U) | (uint64_t)(index + 1U);
}

static RhiRuntime rhi_gl_runtime_handle(void) {
    return (RhiRuntime){rhi_gl_handle_value(0U, rhi_gl_runtime.generation)};
}

static bool rhi_gl_is_runtime(const RhiRuntime runtime) {
    return rhi_gl_runtime.active &&
        rhi_gl_runtime.context != NULL &&
        glXGetCurrentContext() == rhi_gl_runtime.context &&
        runtime.value == rhi_gl_runtime_handle().value;
}

static void rhi_gl_set_diagnostic(
    const RhiResult result,
    const char *const message
) {
    memset(&rhi_gl_runtime.diagnostic, 0, sizeof(rhi_gl_runtime.diagnostic));
    rhi_gl_runtime.diagnostic.struct_size = sizeof(rhi_gl_runtime.diagnostic);
    rhi_gl_runtime.diagnostic.result = result;
    if (message != NULL) {
        (void)snprintf(
            rhi_gl_runtime.diagnostic.message,
            sizeof(rhi_gl_runtime.diagnostic.message),
            "%s",
            message
        );
        rhi_gl_runtime.diagnostic.message_length = (uint32_t)strlen(
            rhi_gl_runtime.diagnostic.message
        );
    }
}

#if !defined(NDEBUG)
static void APIENTRY rhi_gl_debug_callback(
    const GLenum source,
    const GLenum type,
    const GLuint id,
    const GLenum severity,
    const GLsizei length,
    const GLchar *const message,
    const void *const user_data
) {
    (void)source;
    (void)type;
    (void)length;
    (void)user_data;
    if (severity == GL_DEBUG_SEVERITY_HIGH && message != NULL) {
        (void)fprintf(stderr, "rhi-opengl high-severity message (%u): %s\n", id, message);
    }
}

static void rhi_gl_enable_debug_output(void) {
    if (rhi_gl.debug_message_callback == NULL) {
        return;
    }
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    rhi_gl.debug_message_callback(rhi_gl_debug_callback, NULL);
    rhi_gl_runtime.debug_output_enabled = glGetError() == GL_NO_ERROR;
}

static void rhi_gl_disable_debug_output(void) {
    if (!rhi_gl_runtime.debug_output_enabled ||
        rhi_gl.debug_message_callback == NULL) {
        return;
    }
    rhi_gl.debug_message_callback(NULL, NULL);
    glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDisable(GL_DEBUG_OUTPUT);
    rhi_gl_runtime.debug_output_enabled = false;
}
#else
static void rhi_gl_enable_debug_output(void) {}
static void rhi_gl_disable_debug_output(void) {}
#endif

static bool rhi_gl_load_functions(void) {
#define RHI_GL_LOAD(member, type, symbol)                                    \
    do {                                                                       \
        const __GLXextFuncPtr raw_proc =                                      \
            glXGetProcAddressARB((const GLubyte *)(symbol));                  \
        _Static_assert(                                                        \
            sizeof(raw_proc) == sizeof(rhi_gl.member),                        \
            "GLX procedure and target function pointers must have equal size" \
        );                                                                     \
        memcpy(&rhi_gl.member, &raw_proc, sizeof(rhi_gl.member));             \
        if (rhi_gl.member == NULL) {                                           \
            return false;                                                      \
        }                                                                      \
    } while (false)

    RHI_GL_LOAD(gen_buffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
    RHI_GL_LOAD(delete_buffers, PFNGLDELETEBUFFERSPROC, "glDeleteBuffers");
    RHI_GL_LOAD(bind_buffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
    RHI_GL_LOAD(buffer_data, PFNGLBUFFERDATAPROC, "glBufferData");
    RHI_GL_LOAD(buffer_sub_data, PFNGLBUFFERSUBDATAPROC, "glBufferSubData");
    RHI_GL_LOAD(gen_vertex_arrays, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
    RHI_GL_LOAD(delete_vertex_arrays, PFNGLDELETEVERTEXARRAYSPROC, "glDeleteVertexArrays");
    RHI_GL_LOAD(bind_vertex_array, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
    RHI_GL_LOAD(create_shader, PFNGLCREATESHADERPROC, "glCreateShader");
    RHI_GL_LOAD(shader_source, PFNGLSHADERSOURCEPROC, "glShaderSource");
    RHI_GL_LOAD(compile_shader, PFNGLCOMPILESHADERPROC, "glCompileShader");
    RHI_GL_LOAD(get_shader_iv, PFNGLGETSHADERIVPROC, "glGetShaderiv");
    RHI_GL_LOAD(get_shader_info_log, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
    RHI_GL_LOAD(delete_shader, PFNGLDELETESHADERPROC, "glDeleteShader");
    RHI_GL_LOAD(create_program, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
    RHI_GL_LOAD(attach_shader, PFNGLATTACHSHADERPROC, "glAttachShader");
    RHI_GL_LOAD(link_program, PFNGLLINKPROGRAMPROC, "glLinkProgram");
    RHI_GL_LOAD(get_program_iv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
    RHI_GL_LOAD(get_program_info_log, PFNGLGETPROGRAMINFOLOGPROC, "glGetProgramInfoLog");
    RHI_GL_LOAD(delete_program, PFNGLDELETEPROGRAMPROC, "glDeleteProgram");
    RHI_GL_LOAD(use_program, PFNGLUSEPROGRAMPROC, "glUseProgram");
    RHI_GL_LOAD(get_uniform_location, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
    RHI_GL_LOAD(uniform_1i, PFNGLUNIFORM1IPROC, "glUniform1i");
    RHI_GL_LOAD(uniform_2f, PFNGLUNIFORM2FPROC, "glUniform2f");
    RHI_GL_LOAD(enable_vertex_attrib_array, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray");
    RHI_GL_LOAD(vertex_attrib_pointer, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer");
    RHI_GL_LOAD(active_texture, PFNGLACTIVETEXTUREPROC, "glActiveTexture");
    RHI_GL_LOAD(draw_elements_base_vertex, PFNGLDRAWELEMENTSBASEVERTEXPROC, "glDrawElementsBaseVertex");
    RHI_GL_LOAD(fence_sync, PFNGLFENCESYNCPROC, "glFenceSync");
    RHI_GL_LOAD(client_wait_sync, PFNGLCLIENTWAITSYNCPROC, "glClientWaitSync");
    RHI_GL_LOAD(delete_sync, PFNGLDELETESYNCPROC, "glDeleteSync");
    RHI_GL_LOAD(blend_equation, PFNGLBLENDEQUATIONPROC, "glBlendEquation");
    RHI_GL_LOAD(blend_func_separate, PFNGLBLENDFUNCSEPARATEPROC, "glBlendFuncSeparate");

    const __GLXextFuncPtr debug_callback_proc =
        glXGetProcAddressARB((const GLubyte *)"glDebugMessageCallback");
    memcpy(
        &rhi_gl.debug_message_callback,
        &debug_callback_proc,
        sizeof(rhi_gl.debug_message_callback)
    );

#undef RHI_GL_LOAD
    return true;
}

static RhiGlResourceSlot *rhi_gl_find_resource(
    RhiGlResourceSlot *const slots,
    const uint32_t slot_count,
    const uint64_t value,
    const RhiRuntime runtime
) {
    const uint32_t index_plus_one = (uint32_t)value;
    const uint32_t generation = (uint32_t)(value >> 32U);
    if (index_plus_one == 0U || index_plus_one > slot_count ||
        generation == 0U) {
        return NULL;
    }

    RhiGlResourceSlot *const slot = &slots[index_plus_one - 1U];
    return slot->active && slot->generation == generation &&
                   slot->runtime_value == runtime.value
               ? slot
               : NULL;
}

static RhiGlFenceSlot *rhi_gl_find_fence(
    const RhiFence fence,
    const RhiRuntime runtime
) {
    const uint32_t index_plus_one = (uint32_t)fence.value;
    const uint32_t generation = (uint32_t)(fence.value >> 32U);
    if (index_plus_one == 0U || index_plus_one > RHI_GL_MAX_FENCES ||
        generation == 0U) {
        return NULL;
    }

    RhiGlFenceSlot *const slot = &rhi_gl_fence_slots[index_plus_one - 1U];
    return slot->active && slot->generation == generation &&
                   slot->runtime_value == runtime.value
               ? slot
               : NULL;
}

static RhiResult rhi_gl_allocate_resource(
    RhiGlResourceSlot *const slots,
    const uint32_t slot_count,
    const RhiRuntime runtime,
    uint64_t *const out_value,
    RhiGlResourceSlot **const out_slot
) {
    for (uint32_t index = 0U; index < slot_count; ++index) {
        RhiGlResourceSlot *const slot = &slots[index];
        if (slot->active) {
            continue;
        }

        ++slot->generation;
        if (slot->generation == 0U) {
            ++slot->generation;
        }
        slot->active = true;
        slot->runtime_value = runtime.value;
        slot->object = 0U;
        slot->usage = 0U;
        slot->byte_size = 0U;
        slot->width = 0U;
        slot->height = 0U;
        *out_value = rhi_gl_handle_value(index, slot->generation);
        *out_slot = slot;
        return RHI_RESULT_SUCCESS;
    }
    return RHI_RESULT_OUT_OF_MEMORY;
}

static void rhi_gl_release_resource_slot(RhiGlResourceSlot *const slot) {
    slot->active = false;
    slot->runtime_value = 0U;
    slot->object = 0U;
    slot->usage = 0U;
    slot->byte_size = 0U;
    slot->width = 0U;
    slot->height = 0U;
}

static void rhi_gl_release_all_resources(const RhiRuntime runtime) {
    for (uint32_t index = 0U; index < RHI_GL_MAX_FENCES; ++index) {
        RhiGlFenceSlot *const slot = &rhi_gl_fence_slots[index];
        if (slot->active && slot->runtime_value == runtime.value) {
            if (slot->sync != NULL) {
                rhi_gl.delete_sync(slot->sync);
            }
            slot->active = false;
            slot->runtime_value = 0U;
            slot->sync = NULL;
        }
    }
    for (uint32_t index = 0U; index < RHI_GL_MAX_BUFFERS; ++index) {
        RhiGlResourceSlot *const slot = &rhi_gl_buffer_slots[index];
        if (slot->active && slot->runtime_value == runtime.value) {
            rhi_gl.delete_buffers(1, &slot->object);
            rhi_gl_release_resource_slot(slot);
        }
    }
    for (uint32_t index = 0U; index < RHI_GL_MAX_TEXTURES; ++index) {
        RhiGlResourceSlot *const slot = &rhi_gl_texture_slots[index];
        if (slot->active && slot->runtime_value == runtime.value) {
            glDeleteTextures(1, &slot->object);
            rhi_gl_release_resource_slot(slot);
        }
    }
    for (uint32_t index = 0U; index < RHI_GL_MAX_PIPELINES; ++index) {
        RhiGlResourceSlot *const slot = &rhi_gl_pipeline_slots[index];
        if (slot->active && slot->runtime_value == runtime.value) {
            rhi_gl_release_resource_slot(slot);
        }
    }
}

static bool rhi_gl_is_valid_texture_format(const RhiTextureFormat format) {
    return format == RHI_TEXTURE_FORMAT_RGBA8_UNORM ||
           format == RHI_TEXTURE_FORMAT_RGBA8_SRGB;
}

static bool rhi_gl_is_glsizei(const uint32_t value) {
    return value <= (uint32_t)INT32_MAX;
}

static bool rhi_gl_is_glsizeiptr(const uint64_t value) {
    return value <= (uint64_t)INTPTR_MAX;
}

static GLuint rhi_gl_compile_shader(const GLenum type, const char *const source) {
    const GLuint shader = rhi_gl.create_shader(type);
    if (shader == 0U) {
        return 0U;
    }
    rhi_gl.shader_source(shader, 1, &source, NULL);
    rhi_gl.compile_shader(shader);
    GLint compiled = GL_FALSE;
    rhi_gl.get_shader_iv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    rhi_gl.delete_shader(shader);
    return 0U;
}

static bool rhi_gl_create_ui_program(void) {
    static const char vertex_source[] =
        "#version 330 core\n"
        "layout(location = 0) in vec2 a_position;\n"
        "layout(location = 1) in vec2 a_uv;\n"
        "layout(location = 2) in vec4 a_color;\n"
        "uniform vec2 u_display_size;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "void main() {\n"
        "  v_uv = a_uv;\n"
        "  v_color = a_color;\n"
        "  vec2 ndc = vec2(a_position.x / u_display_size.x * 2.0 - 1.0,\n"
        "                  1.0 - a_position.y / u_display_size.y * 2.0);\n"
        "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "}\n";
    static const char fragment_source[] =
        "#version 330 core\n"
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "uniform sampler2D u_texture;\n"
        "out vec4 out_color;\n"
        "void main() { out_color = v_color * texture(u_texture, v_uv); }\n";

    const GLuint vertex_shader = rhi_gl_compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment_shader = rhi_gl_compile_shader(
        GL_FRAGMENT_SHADER,
        fragment_source
    );
    if (vertex_shader == 0U || fragment_shader == 0U) {
        if (vertex_shader != 0U) {
            rhi_gl.delete_shader(vertex_shader);
        }
        if (fragment_shader != 0U) {
            rhi_gl.delete_shader(fragment_shader);
        }
        return false;
    }

    const GLuint program = rhi_gl.create_program();
    if (program == 0U) {
        rhi_gl.delete_shader(vertex_shader);
        rhi_gl.delete_shader(fragment_shader);
        return false;
    }
    rhi_gl.attach_shader(program, vertex_shader);
    rhi_gl.attach_shader(program, fragment_shader);
    rhi_gl.link_program(program);
    rhi_gl.delete_shader(vertex_shader);
    rhi_gl.delete_shader(fragment_shader);

    GLint linked = GL_FALSE;
    rhi_gl.get_program_iv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        rhi_gl.delete_program(program);
        return false;
    }
    const GLint display_size_location = rhi_gl.get_uniform_location(
        program,
        "u_display_size"
    );
    const GLint texture_location = rhi_gl.get_uniform_location(program, "u_texture");
    if (display_size_location < 0 || texture_location < 0) {
        rhi_gl.delete_program(program);
        return false;
    }

    GLuint vertex_array = 0U;
    rhi_gl.gen_vertex_arrays(1, &vertex_array);
    if (vertex_array == 0U || glGetError() != GL_NO_ERROR) {
        if (vertex_array != 0U) {
            rhi_gl.delete_vertex_arrays(1, &vertex_array);
        }
        rhi_gl.delete_program(program);
        return false;
    }
    rhi_gl_runtime.ui_program = program;
    rhi_gl_runtime.ui_vertex_array = vertex_array;
    rhi_gl_runtime.display_size_location = display_size_location;
    rhi_gl_runtime.texture_location = texture_location;
    return true;
}

static RhiResult rhi_gl_validate_draw(
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
        rhi_gl_find_resource(
            rhi_gl_pipeline_slots,
            RHI_GL_MAX_PIPELINES,
            draw->pipeline.value,
            runtime
        ) == NULL ||
        rhi_gl_find_resource(
            rhi_gl_texture_slots,
            RHI_GL_MAX_TEXTURES,
            draw->texture.value,
            runtime
        ) == NULL) {
        return RHI_RESULT_VALIDATION_FAILED;
    }

    RhiGlResourceSlot *const vertex_slot = rhi_gl_find_resource(
        rhi_gl_buffer_slots,
        RHI_GL_MAX_BUFFERS,
        draw->vertex_buffer.value,
        runtime
    );
    RhiGlResourceSlot *const index_slot = rhi_gl_find_resource(
        rhi_gl_buffer_slots,
        RHI_GL_MAX_BUFFERS,
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

static RhiResult RHI_API_CALL rhi_gl_get_plugin_info(
    RhiPluginInfo *const out_info
) {
    if (out_info == NULL ||
        !rhi_gl_has_capacity(out_info->struct_size, sizeof(*out_info))) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint32_t capacity = out_info->struct_size;
    memset(out_info, 0, sizeof(*out_info));
    out_info->struct_size = capacity;
    out_info->abi_version = HARVESTER_RHI_ABI_VERSION;
    out_info->pointer_width = (uint32_t)(sizeof(void *) * 8U);
    out_info->threading_model = RHI_THREADING_EXTERNAL_SYNCHRONIZATION;
    out_info->supported_features = RHI_GL_SUPPORTED_FEATURES;
    out_info->supported_profiles =
        UINT64_C(1) << RHI_CAPABILITY_PROFILE_UI_BASELINE;
    memcpy(out_info->backend_name, "opengl", sizeof("opengl"));
    memcpy(out_info->backend_version, "3.3", sizeof("3.3"));
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_create_runtime(
    const RhiRuntimeCreateInfo *const create_info,
    RhiRuntime *const out_runtime
) {
    if (out_runtime == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_runtime = RHI_RUNTIME_INVALID;
    if (create_info == NULL ||
        !rhi_gl_has_capacity(
            create_info->struct_size,
            rhi_gl_runtime_create_info_base_size()
        ) ||
        create_info->requested_profile != RHI_CAPABILITY_PROFILE_UI_BASELINE ||
        create_info->flags != 0U || create_info->reserved != 0U ||
        rhi_gl_has_unsupported_presentation_binding(create_info) ||
        (create_info->required_features & ~RHI_GL_SUPPORTED_FEATURES) != 0U ||
        (create_info->optional_features & ~RHI_GL_SUPPORTED_FEATURES) != 0U) {
        return RHI_RESULT_UNSUPPORTED;
    }
    if (rhi_gl_runtime.active) {
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    const GLXContext current_context = glXGetCurrentContext();
    if (current_context == NULL) {
        return RHI_RESULT_DEVICE_LOST;
    }
    if (glGetString(GL_VERSION) == NULL || !rhi_gl_load_functions()) {
        return RHI_RESULT_DEVICE_LOST;
    }
    GLint major_version = 0;
    GLint minor_version = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major_version);
    glGetIntegerv(GL_MINOR_VERSION, &minor_version);
    if (major_version < 3 || (major_version == 3 && minor_version < 3)) {
        return RHI_RESULT_UNSUPPORTED;
    }

    ++rhi_gl_runtime.generation;
    if (rhi_gl_runtime.generation == 0U) {
        ++rhi_gl_runtime.generation;
    }
    rhi_gl_runtime.active = true;
    rhi_gl_runtime.context = current_context;
    rhi_gl_runtime.enabled_features =
        create_info->required_features | create_info->optional_features;
    rhi_gl_runtime.ui_program = 0U;
    rhi_gl_runtime.ui_vertex_array = 0U;
    rhi_gl_runtime.display_size_location = -1;
    rhi_gl_runtime.texture_location = -1;
    rhi_gl_runtime.debug_output_enabled = false;
    rhi_gl_set_diagnostic(RHI_RESULT_SUCCESS, "");
    rhi_gl_enable_debug_output();
    if (!rhi_gl_create_ui_program()) {
        rhi_gl_disable_debug_output();
        rhi_gl_runtime.active = false;
        rhi_gl_runtime.context = NULL;
        rhi_gl_set_diagnostic(
            RHI_RESULT_DEVICE_LOST,
            "OpenGL UI shader initialization failed."
        );
        return RHI_RESULT_DEVICE_LOST;
    }
    *out_runtime = rhi_gl_runtime_handle();
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_destroy_runtime(const RhiRuntime runtime) {
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    rhi_gl_release_all_resources(runtime);
    rhi_gl_disable_debug_output();
    if (rhi_gl_runtime.ui_vertex_array != 0U) {
        rhi_gl.delete_vertex_arrays(1, &rhi_gl_runtime.ui_vertex_array);
    }
    if (rhi_gl_runtime.ui_program != 0U) {
        rhi_gl.delete_program(rhi_gl_runtime.ui_program);
    }
    rhi_gl_runtime.active = false;
    rhi_gl_runtime.context = NULL;
    rhi_gl_runtime.enabled_features = 0U;
    rhi_gl_runtime.ui_vertex_array = 0U;
    rhi_gl_runtime.ui_program = 0U;
    rhi_gl_runtime.display_size_location = -1;
    rhi_gl_runtime.texture_location = -1;
    rhi_gl_set_diagnostic(RHI_RESULT_SUCCESS, "");
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_get_runtime_capabilities(
    const RhiRuntime runtime,
    RhiRuntimeCapabilities *const out_capabilities
) {
    if (out_capabilities == NULL ||
        !rhi_gl_has_capacity(out_capabilities->struct_size, sizeof(*out_capabilities)) ||
        !rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    GLint max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    const uint32_t capacity = out_capabilities->struct_size;
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->struct_size = capacity;
    out_capabilities->enabled_profile = RHI_CAPABILITY_PROFILE_UI_BASELINE;
    out_capabilities->enabled_features = rhi_gl_runtime.enabled_features;
    out_capabilities->limits.struct_size = sizeof(out_capabilities->limits);
    out_capabilities->limits.max_texture_dimension_2d =
        max_texture_size > 0 ? (uint32_t)max_texture_size : 0U;
    out_capabilities->limits.max_color_attachments = 1U;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_copy_diagnostic(
    const RhiRuntime runtime,
    RhiDiagnostic *const out_diagnostic
) {
    if (out_diagnostic == NULL ||
        !rhi_gl_has_capacity(out_diagnostic->struct_size, sizeof(*out_diagnostic)) ||
        !rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const uint32_t capacity = out_diagnostic->struct_size;
    memcpy(out_diagnostic, &rhi_gl_runtime.diagnostic, sizeof(*out_diagnostic));
    out_diagnostic->struct_size = capacity;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_get_ui_api(
    const RhiRuntime runtime,
    RhiUiApiV1 *const out_ui_api
) {
    if (out_ui_api == NULL ||
        !rhi_gl_has_capacity(out_ui_api->struct_size, sizeof(*out_ui_api)) ||
        !rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    memcpy(out_ui_api, &RHI_GL_UI_API, sizeof(RHI_GL_UI_API));
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_create_buffer(
    const RhiRuntime runtime,
    const RhiBufferCreateInfo *const create_info,
    RhiBuffer *const out_buffer
) {
    if (out_buffer == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_buffer = RHI_BUFFER_INVALID;
    if (create_info == NULL ||
        !rhi_gl_has_capacity(create_info->struct_size, sizeof(*create_info)) ||
        create_info->byte_size == 0U || !rhi_gl_is_glsizeiptr(create_info->byte_size) ||
        create_info->reserved != 0U || create_info->reserved2 != 0U ||
        create_info->usage == 0U ||
        (create_info->usage &
         ~(RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_INDEX |
           RHI_BUFFER_USAGE_COPY_DST)) != 0U) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    uint64_t value = 0U;
    RhiGlResourceSlot *slot = NULL;
    const RhiResult allocation = rhi_gl_allocate_resource(
        rhi_gl_buffer_slots,
        RHI_GL_MAX_BUFFERS,
        runtime,
        &value,
        &slot
    );
    if (allocation != RHI_RESULT_SUCCESS) {
        return allocation;
    }
    rhi_gl.gen_buffers(1, &slot->object);
    if (slot->object == 0U || glGetError() != GL_NO_ERROR) {
        rhi_gl_release_resource_slot(slot);
        return RHI_RESULT_DEVICE_LOST;
    }
    rhi_gl.bind_buffer(GL_ARRAY_BUFFER, slot->object);
    rhi_gl.buffer_data(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)create_info->byte_size,
        NULL,
        GL_DYNAMIC_DRAW
    );
    if (glGetError() != GL_NO_ERROR) {
        rhi_gl.delete_buffers(1, &slot->object);
        rhi_gl_release_resource_slot(slot);
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    slot->usage = create_info->usage;
    slot->byte_size = create_info->byte_size;
    *out_buffer = (RhiBuffer){value};
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_destroy_buffer(
    const RhiRuntime runtime,
    const RhiBuffer buffer
) {
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlResourceSlot *const slot = rhi_gl_find_resource(
        rhi_gl_buffer_slots,
        RHI_GL_MAX_BUFFERS,
        buffer.value,
        runtime
    );
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    rhi_gl.delete_buffers(1, &slot->object);
    rhi_gl_release_resource_slot(slot);
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_create_texture(
    const RhiRuntime runtime,
    const RhiTextureCreateInfo *const create_info,
    RhiTexture *const out_texture
) {
    if (out_texture == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_texture = RHI_TEXTURE_INVALID;
    if (create_info == NULL ||
        !rhi_gl_has_capacity(create_info->struct_size, sizeof(*create_info)) ||
        create_info->width == 0U || create_info->height == 0U ||
        !rhi_gl_is_glsizei(create_info->width) ||
        !rhi_gl_is_glsizei(create_info->height) || create_info->reserved != 0U ||
        !rhi_gl_is_valid_texture_format(create_info->format) ||
        (create_info->usage & RHI_TEXTURE_USAGE_SAMPLED) == 0U ||
        (create_info->usage &
         ~(RHI_TEXTURE_USAGE_SAMPLED | RHI_TEXTURE_USAGE_COPY_DST)) != 0U) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    uint64_t value = 0U;
    RhiGlResourceSlot *slot = NULL;
    const RhiResult allocation = rhi_gl_allocate_resource(
        rhi_gl_texture_slots,
        RHI_GL_MAX_TEXTURES,
        runtime,
        &value,
        &slot
    );
    if (allocation != RHI_RESULT_SUCCESS) {
        return allocation;
    }
    glGenTextures(1, &slot->object);
    if (slot->object == 0U || glGetError() != GL_NO_ERROR) {
        rhi_gl_release_resource_slot(slot);
        return RHI_RESULT_DEVICE_LOST;
    }
    glBindTexture(GL_TEXTURE_2D, slot->object);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        create_info->format == RHI_TEXTURE_FORMAT_RGBA8_SRGB ?
            GL_SRGB8_ALPHA8 : GL_RGBA8,
        (GLsizei)create_info->width,
        (GLsizei)create_info->height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL
    );
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &slot->object);
        rhi_gl_release_resource_slot(slot);
        return RHI_RESULT_OUT_OF_MEMORY;
    }
    slot->usage = create_info->usage;
    slot->width = create_info->width;
    slot->height = create_info->height;
    *out_texture = (RhiTexture){value};
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_destroy_texture(
    const RhiRuntime runtime,
    const RhiTexture texture
) {
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlResourceSlot *const slot = rhi_gl_find_resource(
        rhi_gl_texture_slots,
        RHI_GL_MAX_TEXTURES,
        texture.value,
        runtime
    );
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    glDeleteTextures(1, &slot->object);
    rhi_gl_release_resource_slot(slot);
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_create_pipeline(
    const RhiRuntime runtime,
    const RhiUiPipelineCreateInfo *const create_info,
    RhiPipeline *const out_pipeline
) {
    if (out_pipeline == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_pipeline = RHI_PIPELINE_INVALID;
    if (create_info == NULL ||
        !rhi_gl_has_capacity(create_info->struct_size, sizeof(*create_info)) ||
        create_info->reserved != 0U ||
        create_info->blend_mode != RHI_UI_BLEND_ALPHA ||
        !rhi_gl_is_valid_texture_format(create_info->color_format)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    uint64_t value = 0U;
    RhiGlResourceSlot *slot = NULL;
    const RhiResult allocation = rhi_gl_allocate_resource(
        rhi_gl_pipeline_slots,
        RHI_GL_MAX_PIPELINES,
        runtime,
        &value,
        &slot
    );
    if (allocation == RHI_RESULT_SUCCESS) {
        *out_pipeline = (RhiPipeline){value};
    }
    return allocation;
}

static RhiResult RHI_API_CALL rhi_gl_destroy_pipeline(
    const RhiRuntime runtime,
    const RhiPipeline pipeline
) {
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlResourceSlot *const slot = rhi_gl_find_resource(
        rhi_gl_pipeline_slots,
        RHI_GL_MAX_PIPELINES,
        pipeline.value,
        runtime
    );
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    rhi_gl_release_resource_slot(slot);
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_write_buffer(
    const RhiRuntime runtime,
    const RhiBufferWriteInfo *const write_info
) {
    if (write_info == NULL ||
        !rhi_gl_has_capacity(write_info->struct_size, sizeof(*write_info)) ||
        write_info->reserved != 0U || write_info->data == NULL ||
        write_info->data_size == 0U || !rhi_gl_is_glsizeiptr(write_info->data_size)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlResourceSlot *const slot = rhi_gl_find_resource(
        rhi_gl_buffer_slots,
        RHI_GL_MAX_BUFFERS,
        write_info->buffer.value,
        runtime
    );
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if ((slot->usage & RHI_BUFFER_USAGE_COPY_DST) == 0U ||
        write_info->offset > slot->byte_size ||
        write_info->data_size > slot->byte_size - write_info->offset ||
        !rhi_gl_is_glsizeiptr(write_info->offset)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    rhi_gl.bind_buffer(GL_ARRAY_BUFFER, slot->object);
    rhi_gl.buffer_sub_data(
        GL_ARRAY_BUFFER,
        (GLintptr)write_info->offset,
        (GLsizeiptr)write_info->data_size,
        write_info->data
    );
    return glGetError() == GL_NO_ERROR ? RHI_RESULT_SUCCESS : RHI_RESULT_DEVICE_LOST;
}

static RhiResult RHI_API_CALL rhi_gl_write_texture(
    const RhiRuntime runtime,
    const RhiTextureWriteInfo *const write_info
) {
    if (write_info == NULL ||
        !rhi_gl_has_capacity(write_info->struct_size, sizeof(*write_info)) ||
        write_info->reserved != 0U || write_info->data == NULL ||
        write_info->width == 0U || write_info->height == 0U ||
        !rhi_gl_is_glsizei(write_info->width) ||
        !rhi_gl_is_glsizei(write_info->height)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const uint64_t minimum_row_size = (uint64_t)write_info->width * 4U;
    if (write_info->bytes_per_row < minimum_row_size ||
        (write_info->bytes_per_row % 4U) != 0U ||
        (write_info->height > 1U &&
         write_info->bytes_per_row >
             (UINT64_MAX - minimum_row_size) /
                 (uint64_t)(write_info->height - 1U))) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    const uint64_t required_data_size =
        (uint64_t)(write_info->height - 1U) * write_info->bytes_per_row +
        minimum_row_size;
    if (write_info->data_size < required_data_size ||
        write_info->bytes_per_row / 4U > (uint32_t)INT32_MAX) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlResourceSlot *const slot = rhi_gl_find_resource(
        rhi_gl_texture_slots,
        RHI_GL_MAX_TEXTURES,
        write_info->texture.value,
        runtime
    );
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if ((slot->usage & RHI_TEXTURE_USAGE_COPY_DST) == 0U ||
        write_info->x > slot->width || write_info->y > slot->height ||
        write_info->width > slot->width - write_info->x ||
        write_info->height > slot->height - write_info->y) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    glBindTexture(GL_TEXTURE_2D, slot->object);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(write_info->bytes_per_row / 4U));
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        (GLint)write_info->x,
        (GLint)write_info->y,
        (GLsizei)write_info->width,
        (GLsizei)write_info->height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        write_info->data
    );
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return glGetError() == GL_NO_ERROR ? RHI_RESULT_SUCCESS : RHI_RESULT_DEVICE_LOST;
}

static RhiResult RHI_API_CALL rhi_gl_submit(
    const RhiRuntime runtime,
    const RhiUiSubmitInfo *const submit_info,
    RhiFence *const out_fence
) {
    if (out_fence == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    *out_fence = RHI_FENCE_INVALID;
    if (submit_info == NULL ||
        !rhi_gl_has_capacity(submit_info->struct_size, sizeof(*submit_info)) ||
        (submit_info->draw_count != 0U && submit_info->draws == NULL)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    if (submit_info->reserved != 0U || submit_info->reserved2 != 0U ||
        submit_info->draw_count > RHI_GL_MAX_DRAWS_PER_SUBMIT ||
        submit_info->target_width == 0U || submit_info->target_height == 0U ||
        !rhi_gl_is_glsizei(submit_info->target_width) ||
        !rhi_gl_is_glsizei(submit_info->target_height) ||
        (submit_info->index_format != RHI_INDEX_FORMAT_UINT16 &&
         submit_info->index_format != RHI_INDEX_FORMAT_UINT32)) {
        return RHI_RESULT_VALIDATION_FAILED;
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0U; index < submit_info->draw_count; ++index) {
        const RhiResult result = rhi_gl_validate_draw(
            runtime,
            submit_info,
            &submit_info->draws[index]
        );
        if (result != RHI_RESULT_SUCCESS) {
            return result;
        }
    }

    RhiGlFenceSlot *fence_slot = NULL;
    uint32_t fence_index = 0U;
    for (uint32_t index = 0U; index < RHI_GL_MAX_FENCES; ++index) {
        if (!rhi_gl_fence_slots[index].active) {
            fence_slot = &rhi_gl_fence_slots[index];
            fence_index = index;
            ++fence_slot->generation;
            if (fence_slot->generation == 0U) {
                ++fence_slot->generation;
            }
            break;
        }
    }
    if (fence_slot == NULL) {
        return RHI_RESULT_OUT_OF_MEMORY;
    }

    rhi_gl.use_program(rhi_gl_runtime.ui_program);
    rhi_gl.uniform_2f(
        rhi_gl_runtime.display_size_location,
        (GLfloat)submit_info->target_width,
        (GLfloat)submit_info->target_height
    );
    rhi_gl.uniform_1i(rhi_gl_runtime.texture_location, 0);
    rhi_gl.bind_vertex_array(rhi_gl_runtime.ui_vertex_array);
    glViewport(
        0,
        0,
        (GLsizei)submit_info->target_width,
        (GLsizei)submit_info->target_height
    );
    glEnable(GL_BLEND);
    rhi_gl.blend_equation(GL_FUNC_ADD);
    rhi_gl.blend_func_separate(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA,
        GL_ONE,
        GL_ONE_MINUS_SRC_ALPHA
    );
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    rhi_gl.active_texture(GL_TEXTURE0);

    for (uint32_t index = 0U; index < submit_info->draw_count; ++index) {
        const RhiUiDraw *const draw = &submit_info->draws[index];
        RhiGlResourceSlot *const vertex_slot = rhi_gl_find_resource(
            rhi_gl_buffer_slots,
            RHI_GL_MAX_BUFFERS,
            draw->vertex_buffer.value,
            runtime
        );
        RhiGlResourceSlot *const index_slot = rhi_gl_find_resource(
            rhi_gl_buffer_slots,
            RHI_GL_MAX_BUFFERS,
            draw->index_buffer.value,
            runtime
        );
        RhiGlResourceSlot *const texture_slot = rhi_gl_find_resource(
            rhi_gl_texture_slots,
            RHI_GL_MAX_TEXTURES,
            draw->texture.value,
            runtime
        );
        rhi_gl.bind_buffer(GL_ARRAY_BUFFER, vertex_slot->object);
        rhi_gl.bind_buffer(GL_ELEMENT_ARRAY_BUFFER, index_slot->object);
        rhi_gl.enable_vertex_attrib_array(0U);
        rhi_gl.enable_vertex_attrib_array(1U);
        rhi_gl.enable_vertex_attrib_array(2U);
        rhi_gl.vertex_attrib_pointer(
            0U,
            2,
            GL_FLOAT,
            GL_FALSE,
            (GLsizei)sizeof(RhiUiVertex),
            (const void *)offsetof(RhiUiVertex, position)
        );
        rhi_gl.vertex_attrib_pointer(
            1U,
            2,
            GL_FLOAT,
            GL_FALSE,
            (GLsizei)sizeof(RhiUiVertex),
            (const void *)offsetof(RhiUiVertex, uv)
        );
        rhi_gl.vertex_attrib_pointer(
            2U,
            4,
            GL_UNSIGNED_BYTE,
            GL_TRUE,
            (GLsizei)sizeof(RhiUiVertex),
            (const void *)offsetof(RhiUiVertex, color_rgba8)
        );
        glBindTexture(GL_TEXTURE_2D, texture_slot->object);
        glScissor(
            draw->scissor.x,
            (GLint)submit_info->target_height -
                draw->scissor.y - (GLint)draw->scissor.height,
            (GLsizei)draw->scissor.width,
            (GLsizei)draw->scissor.height
        );
        const uint64_t index_offset =
            (uint64_t)draw->first_index *
            (submit_info->index_format == RHI_INDEX_FORMAT_UINT16 ? 2U : 4U);
        rhi_gl.draw_elements_base_vertex(
            GL_TRIANGLES,
            (GLsizei)draw->index_count,
            submit_info->index_format == RHI_INDEX_FORMAT_UINT16 ?
                GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            (const void *)(uintptr_t)index_offset,
            draw->vertex_offset
        );
    }
    if (glGetError() != GL_NO_ERROR) {
        rhi_gl_set_diagnostic(RHI_RESULT_DEVICE_LOST, "OpenGL UI submission failed.");
        return RHI_RESULT_DEVICE_LOST;
    }

    fence_slot->sync = rhi_gl.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
    if (fence_slot->sync == NULL || glGetError() != GL_NO_ERROR) {
        fence_slot->sync = NULL;
        return RHI_RESULT_DEVICE_LOST;
    }
    fence_slot->active = true;
    fence_slot->runtime_value = runtime.value;
    *out_fence = (RhiFence){
        rhi_gl_handle_value(fence_index, fence_slot->generation),
    };
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_wait_fence(
    const RhiRuntime runtime,
    const RhiFence fence,
    const uint64_t timeout_nanoseconds
) {
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlFenceSlot *const slot = rhi_gl_find_fence(fence, runtime);
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    const GLenum wait_result = rhi_gl.client_wait_sync(
        slot->sync,
        GL_SYNC_FLUSH_COMMANDS_BIT,
        (GLuint64)timeout_nanoseconds
    );
    if (wait_result == GL_ALREADY_SIGNALED ||
        wait_result == GL_CONDITION_SATISFIED) {
        return RHI_RESULT_SUCCESS;
    }
    if (wait_result == GL_TIMEOUT_EXPIRED) {
        return RHI_RESULT_TIMEOUT;
    }
    rhi_gl_set_diagnostic(RHI_RESULT_DEVICE_LOST, "OpenGL fence wait failed.");
    return RHI_RESULT_DEVICE_LOST;
}

static RhiResult RHI_API_CALL rhi_gl_destroy_fence(
    const RhiRuntime runtime,
    const RhiFence fence
) {
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    RhiGlFenceSlot *const slot = rhi_gl_find_fence(fence, runtime);
    if (slot == NULL) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    rhi_gl.delete_sync(slot->sync);
    slot->active = false;
    slot->runtime_value = 0U;
    slot->sync = NULL;
    return RHI_RESULT_SUCCESS;
}

static RhiResult RHI_API_CALL rhi_gl_clear_target(
    const RhiRuntime runtime,
    const RhiUiClearInfo *const clear_info
) {
    if (clear_info == NULL ||
        !rhi_gl_has_capacity(clear_info->struct_size, sizeof(*clear_info)) ||
        clear_info->target_width == 0U || clear_info->target_height == 0U ||
        !rhi_gl_is_glsizei(clear_info->target_width) ||
        !rhi_gl_is_glsizei(clear_info->target_height) || clear_info->reserved != 0U) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0U; index < 4U; ++index) {
        if (!isfinite(clear_info->color_rgba[index])) {
            return RHI_RESULT_VALIDATION_FAILED;
        }
    }
    if (!rhi_gl_is_runtime(runtime)) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }
    glViewport(
        0,
        0,
        (GLsizei)clear_info->target_width,
        (GLsizei)clear_info->target_height
    );
    glClearColor(
        clear_info->color_rgba[0],
        clear_info->color_rgba[1],
        clear_info->color_rgba[2],
        clear_info->color_rgba[3]
    );
    glClear(GL_COLOR_BUFFER_BIT);
    return glGetError() == GL_NO_ERROR ? RHI_RESULT_SUCCESS : RHI_RESULT_DEVICE_LOST;
}

RHI_PLUGIN_EXPORT const RhiPluginApiV1 *RHI_API_CALL
harvester_rhi_get_plugin_api(const uint32_t requested_abi_version) {
    if (requested_abi_version != HARVESTER_RHI_ABI_VERSION) {
        return NULL;
    }
    return &RHI_GL_PLUGIN_API;
}
