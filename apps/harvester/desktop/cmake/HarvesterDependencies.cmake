include_guard(GLOBAL)

include(FetchContent)

# Every bundled revision is a full immutable Git object ID from an upstream
# release tag. FetchContent stays disconnected after its first successful
# population, so reconfiguration never advances a dependency implicitly.
set(
    HARVESTER_DEPENDENCY_PROVIDER
    "BUNDLED"
    CACHE STRING
    "Dependency provider: BUNDLED"
)
set_property(
    CACHE HARVESTER_DEPENDENCY_PROVIDER
    PROPERTY STRINGS BUNDLED
)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

function(harvester_configure_desktop_dependencies)
    if(TARGET harvester::sdl3 AND TARGET harvester::imgui)
        return()
    endif()

    if(NOT HARVESTER_DEPENDENCY_PROVIDER STREQUAL "BUNDLED")
        message(
            FATAL_ERROR
            "Unsupported HARVESTER_DEPENDENCY_PROVIDER='${HARVESTER_DEPENDENCY_PROVIDER}'. "
            "Only BUNDLED is supported while the ImGui renderer is vendored."
        )
    endif()

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL_DOCS ON CACHE BOOL "" FORCE)
    # The desktop client needs only an X11 OpenGL window and input. Disabling
    # unrelated platform drivers avoids turning optional distro development
    # packages (for example XScreenSaver) into build requirements.
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
    set(SDL_KMSDRM OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
    set(SDL_X11 ON CACHE BOOL "" FORCE)
    set(SDL_X11_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_X11_XCURSOR OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XDBE OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XFIXES OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XINPUT OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XRANDR OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XSHAPE OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XSYNC OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        harvester_sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 8e37db5e797b6167f3a00d697d816a684bd259c7
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(harvester_sdl3)
    add_library(harvester_sdl3_target INTERFACE)
    target_link_libraries(harvester_sdl3_target INTERFACE SDL3::SDL3)
    add_library(harvester::sdl3 ALIAS harvester_sdl3_target)

    FetchContent_Declare(
        harvester_vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG 0d3f509e57041fbd073a1ee84cd9ccd36d148446
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(harvester_vulkan_headers)
    add_library(harvester_vulkan_headers_target INTERFACE)
    target_include_directories(
        harvester_vulkan_headers_target
        SYSTEM INTERFACE
            ${harvester_vulkan_headers_SOURCE_DIR}/include
    )
    add_library(
        harvester::vulkan_headers
        ALIAS harvester_vulkan_headers_target
    )

    FetchContent_Declare(
        harvester_volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG d979819fd03de3a7606d6e23d6ff4968942599da
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(harvester_volk)
    add_library(harvester_volk_target INTERFACE)
    target_link_libraries(harvester_volk_target INTERFACE volk::volk)
    add_library(harvester::volk ALIAS harvester_volk_target)

    FetchContent_Declare(
        harvester_imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 01380c579715e62fb9a8d6ec0502c4ea83bfde6e
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(harvester_imgui)
    add_library(
        harvester_imgui
        STATIC
            ${harvester_imgui_SOURCE_DIR}/imgui.cpp
            ${harvester_imgui_SOURCE_DIR}/imgui_draw.cpp
            ${harvester_imgui_SOURCE_DIR}/imgui_tables.cpp
            ${harvester_imgui_SOURCE_DIR}/imgui_widgets.cpp
    )
    target_include_directories(
        harvester_imgui
        SYSTEM PUBLIC
            ${harvester_imgui_SOURCE_DIR}
    )
    add_library(harvester::imgui ALIAS harvester_imgui)

    add_library(
        harvester_imgui_sdl3_backend
        STATIC
            ${harvester_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    )
    target_include_directories(
        harvester_imgui_sdl3_backend
        SYSTEM PUBLIC
            ${harvester_imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(
        harvester_imgui_sdl3_backend
        PUBLIC
            harvester::imgui
            harvester::sdl3
    )
    add_library(
        harvester::imgui_sdl3_backend
        ALIAS harvester_imgui_sdl3_backend
    )

    find_package(OpenGL REQUIRED)
endfunction()
