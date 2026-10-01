#include <stddef.h>

#include <harvester/rhi/rhi_api.h>

_Static_assert(sizeof(RhiRuntime) == sizeof(uint64_t), "runtime handle ABI");
_Static_assert(sizeof(RhiBuffer) == sizeof(uint64_t), "buffer handle ABI");
_Static_assert(
    offsetof(RhiPluginApiV1, struct_size) == 0U,
    "plugin table starts with struct_size"
);
_Static_assert(
    offsetof(RhiPluginApiV1, abi_version) == sizeof(uint32_t),
    "plugin table ABI version follows struct_size"
);

int main(void) {
    const RhiRuntime runtime = RHI_RUNTIME_INVALID;
    const RhiBuffer buffer = RHI_BUFFER_INVALID;

    return harvester_rhi_abi_version() == HARVESTER_RHI_ABI_VERSION &&
                   runtime.value == UINT64_C(0) && buffer.value == UINT64_C(0)
               ? 0
               : 1;
}
