#include <harvester/rhi/rhi_cpp.hpp>

namespace harvester::rhi {

RhiResult PluginApiView::from(
    const RhiPluginApiV1 *api,
    PluginApiView *out_view
) noexcept {
    if (out_view == nullptr) {
        return RHI_RESULT_INVALID_ARGUMENT;
    }

    const RhiResult validation = harvester_rhi_validate_plugin_api_v1(api);
    if (validation != RHI_RESULT_SUCCESS) {
        *out_view = PluginApiView{};
        return validation;
    }

    out_view->api_ = api;
    return RHI_RESULT_SUCCESS;
}

}  // namespace harvester::rhi
