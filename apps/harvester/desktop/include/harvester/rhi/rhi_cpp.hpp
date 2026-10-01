#ifndef HARVESTER_RHI_CPP_HPP
#define HARVESTER_RHI_CPP_HPP

#include <cstdint>

#include <harvester/rhi/rhi_api.h>

namespace harvester::rhi {

class PluginApiView final {
public:
    constexpr PluginApiView() noexcept = default;

    static RhiResult from(
        const RhiPluginApiV1 *api,
        PluginApiView *out_view
    ) noexcept;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return api_ != nullptr;
    }

    [[nodiscard]] constexpr std::uint32_t abi_version() const noexcept {
        return api_ == nullptr ? 0U : api_->abi_version;
    }

    [[nodiscard]] constexpr const RhiPluginApiV1 *get() const noexcept {
        return api_;
    }

private:
    const RhiPluginApiV1 *api_ = nullptr;
};

}  // namespace harvester::rhi

#endif
