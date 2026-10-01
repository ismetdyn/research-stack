#include <harvester/rhi/rhi_loader.hpp>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace harvester::rhi {

namespace {

struct BackendDescriptor {
    const char *filename;
    const char *name;
};

const BackendDescriptor *backend_descriptor(
    const RendererBackend backend
) noexcept {
#if defined(__linux__)
    static const BackendDescriptor null_backend = {"librhi_null.so", "null"};
    static const BackendDescriptor opengl_backend = {
        "librhi_opengl.so",
        "opengl",
    };
    static const BackendDescriptor vulkan_backend = {
        "librhi_vulkan.so",
        "vulkan",
    };

    switch (backend) {
        case RendererBackend::Null:
            return &null_backend;
        case RendererBackend::OpenGL:
            return &opengl_backend;
        case RendererBackend::Vulkan:
            return &vulkan_backend;
    }
#else
    (void)backend;
#endif

    return nullptr;
}

bool path_is_direct_child(
    const std::filesystem::path &directory,
    const std::filesystem::path &file
) noexcept {
    return file.parent_path() == directory;
}

bool fixed_string_equals(
    const char *const storage,
    const size_t storage_capacity,
    const char *const expected
) noexcept {
    const size_t expected_size = std::strlen(expected);
    return expected_size < storage_capacity &&
           std::memcmp(storage, expected, expected_size) == 0 &&
           storage[expected_size] == '\0';
}

// Hata kodunu döndürür; tanı isteniyorsa tek satırlık mesajı yazar. Mesaj
// yalnızca eklenti yolu ve beklenen/gerçek değer taşır, sır içermez.
RhiResult fail(
    std::string *const diagnostic,
    const RhiResult result,
    std::string message
) {
    if (diagnostic != nullptr) {
        *diagnostic = std::move(message);
    }
    return result;
}

// Doğrulama kodunu, tablodaki gerçek değerlerle açıklanabilir mesaja çevirir.
std::string describe_api_failure(
    const RhiPluginApiV1 *const api,
    const std::string &path
) {
    const std::string prefix = "plugin " + path + ": ";
    if (api == nullptr) {
        return prefix + "entry point returned NULL for requested ABI version " +
               std::to_string(HARVESTER_RHI_ABI_VERSION);
    }
    if (api->abi_version != HARVESTER_RHI_ABI_VERSION) {
        return prefix + "ABI version mismatch, expected " +
               std::to_string(HARVESTER_RHI_ABI_VERSION) + " but plugin reports " +
               std::to_string(api->abi_version);
    }
    const std::size_t required = offsetof(RhiPluginApiV1, copy_diagnostic) +
                                 sizeof(api->copy_diagnostic);
    if (api->struct_size < required) {
        return prefix + "plugin table struct_size " +
               std::to_string(api->struct_size) + " is smaller than required " +
               std::to_string(required);
    }
    const struct {
        const char *name;
        const void *entry;
    } entries[] = {
        {"get_plugin_info", reinterpret_cast<const void *>(api->get_plugin_info)},
        {"create_runtime", reinterpret_cast<const void *>(api->create_runtime)},
        {"destroy_runtime", reinterpret_cast<const void *>(api->destroy_runtime)},
        {"get_runtime_capabilities",
         reinterpret_cast<const void *>(api->get_runtime_capabilities)},
        {"copy_diagnostic", reinterpret_cast<const void *>(api->copy_diagnostic)},
    };
    for (const auto &entry : entries) {
        if (entry.entry == nullptr) {
            return prefix + "required entry point " + entry.name + " is NULL";
        }
    }
    return prefix + "plugin table validation failed";
}

}  // namespace

LoadedPlugin::~LoadedPlugin() {
    reset();
}

LoadedPlugin::LoadedPlugin(LoadedPlugin &&other) noexcept
    : module_handle_(other.module_handle_), api_(other.api_) {
    other.module_handle_ = nullptr;
    other.api_ = nullptr;
}

LoadedPlugin &LoadedPlugin::operator=(LoadedPlugin &&other) noexcept {
    if (this != &other) {
        reset();
        module_handle_ = other.module_handle_;
        api_ = other.api_;
        other.module_handle_ = nullptr;
        other.api_ = nullptr;
    }

    return *this;
}

RhiResult LoadedPlugin::load(
    const RendererBackend backend,
    const std::filesystem::path &trusted_directory,
    LoadedPlugin *const out_plugin,
    std::string *const out_diagnostic
) {
    if (out_diagnostic != nullptr) {
        out_diagnostic->clear();
    }
    if (out_plugin == nullptr) {
        return fail(out_diagnostic, RHI_RESULT_INVALID_ARGUMENT, "out_plugin is NULL");
    }

    out_plugin->reset();

    const BackendDescriptor *const descriptor = backend_descriptor(backend);
    if (descriptor == nullptr) {
        return fail(
            out_diagnostic,
            RHI_RESULT_INVALID_ARGUMENT,
            "unknown renderer backend value " +
                std::to_string(static_cast<std::uint32_t>(backend)) +
                ", expected null, opengl or vulkan"
        );
    }

#if !defined(__linux__)
    (void)trusted_directory;
    return fail(out_diagnostic, RHI_RESULT_UNSUPPORTED, "plugin loading is only supported on Linux");
#else
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::canonical(trusted_directory, error);
    if (error || !std::filesystem::is_directory(directory, error) || error) {
        return fail(
            out_diagnostic,
            RHI_RESULT_INVALID_ARGUMENT,
            "plugin directory " + trusted_directory.string() +
                " is not an accessible directory"
        );
    }

    const std::filesystem::path plugin_path =
        std::filesystem::canonical(directory / descriptor->filename, error);
    if (error || !path_is_direct_child(directory, plugin_path) ||
        !std::filesystem::is_regular_file(plugin_path, error) || error) {
        return fail(
            out_diagnostic,
            RHI_RESULT_INVALID_ARGUMENT,
            "allowlisted plugin " + std::string{descriptor->filename} +
                " is missing or not a direct regular file in " +
                directory.string()
        );
    }
    const std::string path_text = plugin_path.string();

    void *const module_handle =
        dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module_handle == nullptr) {
        const char *const reason = dlerror();
        return fail(
            out_diagnostic,
            RHI_RESULT_INTERNAL_ERROR,
            "plugin " + path_text + ": dlopen failed: " +
                (reason != nullptr ? reason : "unknown reason")
        );
    }
    // Mesaj kurulurken bad_alloc fırlarsa da modül kapansın; başarıda release edilir.
    struct ModuleCloser {
        void operator()(void *handle) const noexcept { (void)dlclose(handle); }
    };
    std::unique_ptr<void, ModuleCloser> module_guard{module_handle};

    void *const raw_entrypoint =
        dlsym(module_handle, HARVESTER_RHI_PLUGIN_ENTRYPOINT_NAME);
    if (raw_entrypoint == nullptr) {
        return fail(
            out_diagnostic,
            RHI_RESULT_ABI_MISMATCH,
            "plugin " + path_text + ": missing entry point symbol " +
                HARVESTER_RHI_PLUGIN_ENTRYPOINT_NAME
        );
    }

    static_assert(
        sizeof(raw_entrypoint) == sizeof(RhiGetPluginApiFn),
        "Linux requires data and function pointers of equal size for dlsym"
    );
    RhiGetPluginApiFn entrypoint = nullptr;
    std::memcpy(&entrypoint, &raw_entrypoint, sizeof(entrypoint));

    const RhiPluginApiV1 *const api =
        entrypoint(HARVESTER_RHI_ABI_VERSION);
    const RhiResult validation = harvester_rhi_validate_plugin_api_v1(api);
    if (validation != RHI_RESULT_SUCCESS) {
        const std::string message = describe_api_failure(api, path_text);
        return fail(out_diagnostic, validation, message);
    }

    RhiPluginInfo info{};
    info.struct_size = sizeof(info);
    std::string info_problem;
    if (api->get_plugin_info(&info) != RHI_RESULT_SUCCESS) {
        info_problem = "get_plugin_info did not return success";
    } else if (info.abi_version != HARVESTER_RHI_ABI_VERSION) {
        info_problem = "plugin info ABI version expected " +
                       std::to_string(HARVESTER_RHI_ABI_VERSION) + " but is " +
                       std::to_string(info.abi_version);
    } else if (info.pointer_width != sizeof(void *) * 8U) {
        info_problem = "pointer width expected " +
                       std::to_string(sizeof(void *) * 8U) + " but is " +
                       std::to_string(info.pointer_width);
    } else if (info.threading_model != RHI_THREADING_EXTERNAL_SYNCHRONIZATION) {
        info_problem = "threading model expected " +
                       std::to_string(RHI_THREADING_EXTERNAL_SYNCHRONIZATION) +
                       " but is " + std::to_string(info.threading_model);
    } else if (!fixed_string_equals(
                   info.backend_name,
                   sizeof(info.backend_name),
                   descriptor->name
               )) {
        // Eklentiden gelen ad güvenilmez: sınırlı uzunlukta okunur.
        const std::size_t name_length =
            strnlen(info.backend_name, sizeof(info.backend_name));
        std::string reported{info.backend_name, name_length};
        // Kontrol karakterleri tek satırlık tanı mesajını bozmasın.
        for (char &character : reported) {
            if (character < 0x20 || character > 0x7E) {
                character = '?';
            }
        }
        info_problem = "backend name expected \"" + std::string{descriptor->name} +
                       "\" but plugin reports \"" + reported + "\"";
    }
    if (!info_problem.empty()) {
        return fail(
            out_diagnostic,
            RHI_RESULT_VALIDATION_FAILED,
            "plugin " + path_text + ": " + info_problem
        );
    }

    out_plugin->module_handle_ = module_guard.release();
    out_plugin->api_ = api;
    return RHI_RESULT_SUCCESS;
#endif
}

void LoadedPlugin::reset() noexcept {
#if defined(__linux__)
    if (module_handle_ != nullptr) {
        (void)dlclose(module_handle_);
    }
#endif
    module_handle_ = nullptr;
    api_ = nullptr;
}

bool LoadedPlugin::valid() const noexcept {
    return module_handle_ != nullptr && api_ != nullptr;
}

const RhiPluginApiV1 *LoadedPlugin::api() const noexcept {
    return api_;
}

}  // namespace harvester::rhi
