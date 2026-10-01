#include <cassert>

#include <harvester/platform/sdl3_imgui_platform.hpp>

int main() {
    harvester::platform::Sdl3ImGuiPlatform platform;
    const auto invalid_backend =
        static_cast<harvester::platform::Sdl3PresentationBackend>(99U);

    assert(!platform.initialize("test", 1U, 1U, invalid_backend));
    assert(platform.native_window_for_wsi() == nullptr);
    assert(!platform.last_error().empty());
    return 0;
}
