#include <cassert>

#include <harvester/platform/sdl3_vulkan_presentation.hpp>

int main() {
    harvester::platform::Sdl3VulkanPresentation presentation;

    assert(presentation.binding() == nullptr);
    assert(!presentation.initialize(nullptr));
    assert(presentation.binding() == nullptr);
    assert(!presentation.last_error().empty());

    presentation.shutdown();
    return 0;
}
