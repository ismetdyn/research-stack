cmake_minimum_required(VERSION 3.28)

project(HarvesterDesktop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)
include(cmake/HarvesterDependencies.cmake)
harvester_configure_desktop_dependencies()

function(harvester_enable_strict_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
        )
    endif()
endfunction()

add_library(rhi_api STATIC src/rhi_api/rhi_api.c)
add_library(harvester::rhi_api ALIAS rhi_api)
target_compile_features(rhi_api PUBLIC c_std_17)
target_include_directories(
    rhi_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
harvester_enable_strict_warnings(rhi_api)

add_library(rhi_cpp STATIC src/rhi_cpp/rhi_cpp.cpp)
add_library(harvester::rhi_cpp ALIAS rhi_cpp)
target_compile_features(rhi_cpp PUBLIC cxx_std_20)
target_link_libraries(rhi_cpp PUBLIC harvester::rhi_api)
harvester_enable_strict_warnings(rhi_cpp)

add_library(rhi_loader STATIC src/rhi_loader/rhi_loader.cpp)
add_library(harvester::rhi_loader ALIAS rhi_loader)
target_compile_features(rhi_loader PUBLIC cxx_std_20)
target_link_libraries(rhi_loader PUBLIC harvester::rhi_api)
target_link_libraries(rhi_loader PRIVATE ${CMAKE_DL_LIBS})
harvester_enable_strict_warnings(rhi_loader)

add_library(platform_sdl3 STATIC src/platform_sdl3/sdl3_imgui_platform.cpp)
add_library(harvester::platform_sdl3 ALIAS platform_sdl3)
target_compile_features(platform_sdl3 PUBLIC cxx_std_20)
target_include_directories(
    platform_sdl3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    platform_sdl3
    PUBLIC
        harvester::imgui
        harvester::imgui_sdl3_backend
        harvester::sdl3
)
harvester_enable_strict_warnings(platform_sdl3)

add_library(
    sdl3_vulkan_presentation
    STATIC
        src/platform_sdl3/sdl3_vulkan_presentation.cpp
)
add_library(
    harvester::sdl3_vulkan_presentation
    ALIAS sdl3_vulkan_presentation
)
target_compile_features(sdl3_vulkan_presentation PUBLIC cxx_std_20)
target_include_directories(
    sdl3_vulkan_presentation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    sdl3_vulkan_presentation
    PUBLIC
        harvester::rhi_api
    PRIVATE
        harvester::sdl3
        harvester::vulkan_headers
)
harvester_enable_strict_warnings(sdl3_vulkan_presentation)

add_library(rhi_null MODULE src/rhi_null/rhi_null.c)
add_library(harvester::rhi_null ALIAS rhi_null)
target_compile_features(rhi_null PRIVATE c_std_17)
target_link_libraries(rhi_null PRIVATE harvester::rhi_api)
target_compile_definitions(rhi_null PRIVATE HARVESTER_RHI_PLUGIN_BUILD=1)
harvester_enable_strict_warnings(rhi_null)

add_library(rhi_opengl MODULE src/rhi_opengl/rhi_opengl.c)
add_library(harvester::rhi_opengl ALIAS rhi_opengl)
target_compile_features(rhi_opengl PRIVATE c_std_17)
target_link_libraries(rhi_opengl PRIVATE harvester::rhi_api OpenGL::GL)
target_compile_definitions(rhi_opengl PRIVATE HARVESTER_RHI_PLUGIN_BUILD=1)
harvester_enable_strict_warnings(rhi_opengl)

add_library(rhi_vulkan MODULE src/rhi_vulkan/rhi_vulkan.cpp)
add_library(harvester::rhi_vulkan ALIAS rhi_vulkan)
target_compile_features(rhi_vulkan PRIVATE cxx_std_20)
target_link_libraries(
    rhi_vulkan
    PRIVATE
        harvester::rhi_api
        harvester::volk
        harvester::vulkan_headers
)
target_compile_definitions(rhi_vulkan PRIVATE HARVESTER_RHI_PLUGIN_BUILD=1)
harvester_enable_strict_warnings(rhi_vulkan)

add_library(ui_renderer_rhi STATIC src/ui_renderer_rhi/imgui_rhi_renderer.cpp)
add_library(harvester::ui_renderer_rhi ALIAS ui_renderer_rhi)
target_compile_features(ui_renderer_rhi PUBLIC cxx_std_20)
target_include_directories(
    ui_renderer_rhi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    ui_renderer_rhi
    PUBLIC
        harvester::rhi_api
    PRIVATE
        harvester::imgui
)
harvester_enable_strict_warnings(ui_renderer_rhi)

add_library(desktop_dashboard STATIC src/desktop_app/research_dashboard.cpp)
add_library(harvester::desktop_dashboard ALIAS desktop_dashboard)
target_compile_features(desktop_dashboard PUBLIC cxx_std_20)
target_include_directories(
    desktop_dashboard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(desktop_dashboard PRIVATE harvester::imgui)
harvester_enable_strict_warnings(desktop_dashboard)

add_library(
    harvester_fake_service
    STATIC
    src/harvester_bridge/fake_research_service.cpp
)
add_library(harvester::fake_service ALIAS harvester_fake_service)
target_compile_features(harvester_fake_service PUBLIC cxx_std_20)
target_include_directories(
    harvester_fake_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
harvester_enable_strict_warnings(harvester_fake_service)

add_library(
    harvester_worker_protocol
    STATIC
    src/harvester_bridge/worker_protocol.cpp
)
add_library(harvester::worker_protocol ALIAS harvester_worker_protocol)
target_compile_features(harvester_worker_protocol PUBLIC cxx_std_20)
target_include_directories(
    harvester_worker_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
harvester_enable_strict_warnings(harvester_worker_protocol)

add_library(
    harvester_python_service
    STATIC
    src/harvester_bridge/python_research_service.cpp
)
add_library(harvester::python_service ALIAS harvester_python_service)
target_compile_features(harvester_python_service PUBLIC cxx_std_20)
target_include_directories(
    harvester_python_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    harvester_python_service
    PUBLIC
        harvester::worker_protocol
)
harvester_enable_strict_warnings(harvester_python_service)

add_executable(harvester_desktop src/desktop_app/main.cpp)
target_compile_features(harvester_desktop PRIVATE cxx_std_20)
target_link_libraries(
    harvester_desktop
    PRIVATE
        harvester::desktop_dashboard
        harvester::python_service
        harvester::platform_sdl3
        harvester::sdl3_vulkan_presentation
        harvester::rhi_loader
        harvester::ui_renderer_rhi
)
target_compile_definitions(
    harvester_desktop
    PRIVATE
        HARVESTER_OPENGL_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_opengl>"
        HARVESTER_VULKAN_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_vulkan>"
        HARVESTER_PYTHON_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_dependencies(harvester_desktop rhi_opengl rhi_vulkan)
harvester_enable_strict_warnings(harvester_desktop)

if(BUILD_TESTING)
    add_library(rhi_malformed MODULE tests/rhi_malformed_plugin.c)
    set_target_properties(
        rhi_malformed
        PROPERTIES
            OUTPUT_NAME rhi_null
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/malformed"
    )
    target_compile_features(rhi_malformed PRIVATE c_std_17)
    target_link_libraries(rhi_malformed PRIVATE harvester::rhi_api)
    target_compile_definitions(rhi_malformed PRIVATE HARVESTER_RHI_PLUGIN_BUILD=1)
    harvester_enable_strict_warnings(rhi_malformed)

    # Her bozukluk türü için ayrı modül; hepsi kendi dizininde librhi_null.so.
    set(malformed_variant_abi RHI_MALFORMED_ABI_DELTA=1U)
    set(malformed_variant_truncated RHI_MALFORMED_TRUNCATE=1)
    set(malformed_variant_null_entry RHI_MALFORMED_NULL_CREATE=1)
    set(malformed_variant_no_symbol RHI_MALFORMED_NO_ENTRYPOINT=1)
    foreach(variant IN ITEMS abi truncated null_entry no_symbol)
        add_library(rhi_malformed_${variant} MODULE tests/rhi_malformed_plugin.c)
        set_target_properties(
            rhi_malformed_${variant}
            PROPERTIES
                OUTPUT_NAME rhi_null
                LIBRARY_OUTPUT_DIRECTORY
                    "${CMAKE_CURRENT_BINARY_DIR}/malformed_${variant}"
        )
        target_compile_features(rhi_malformed_${variant} PRIVATE c_std_17)
        target_link_libraries(rhi_malformed_${variant} PRIVATE harvester::rhi_api)
        target_compile_definitions(
            rhi_malformed_${variant}
            PRIVATE HARVESTER_RHI_PLUGIN_BUILD=1 ${malformed_variant_${variant}}
        )
        harvester_enable_strict_warnings(rhi_malformed_${variant})
    endforeach()
endif()

add_executable(rhi_smoke_host src/smoke_host/main.cpp)
target_compile_features(rhi_smoke_host PRIVATE cxx_std_20)
target_link_libraries(
    rhi_smoke_host
    PRIVATE
        harvester::rhi_api
        harvester::rhi_cpp
        harvester::rhi_loader
)
target_compile_definitions(
    rhi_smoke_host
    PRIVATE
        RHI_SMOKE_NULL_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_null>"
)
add_dependencies(rhi_smoke_host rhi_null)
harvester_enable_strict_warnings(rhi_smoke_host)

if(BUILD_TESTING)
    add_executable(rhi_api_c_abi_test tests/rhi_api_c_abi_test.c)
    target_link_libraries(rhi_api_c_abi_test PRIVATE harvester::rhi_api)
    harvester_enable_strict_warnings(rhi_api_c_abi_test)
    add_test(NAME rhi_api_c_abi_test COMMAND rhi_api_c_abi_test)

    add_executable(rhi_plugin_validation_test tests/rhi_plugin_validation_test.c)
    target_link_libraries(rhi_plugin_validation_test PRIVATE harvester::rhi_api)
    harvester_enable_strict_warnings(rhi_plugin_validation_test)
    add_test(NAME rhi_plugin_validation_test COMMAND rhi_plugin_validation_test)

    add_executable(rhi_cpp_wrapper_test tests/rhi_cpp_wrapper_test.cpp)
    target_link_libraries(rhi_cpp_wrapper_test PRIVATE harvester::rhi_cpp)
    harvester_enable_strict_warnings(rhi_cpp_wrapper_test)
    add_test(NAME rhi_cpp_wrapper_test COMMAND rhi_cpp_wrapper_test)

    add_executable(rhi_loader_test tests/rhi_loader_test.cpp)
    target_link_libraries(rhi_loader_test PRIVATE harvester::rhi_loader)
    target_compile_definitions(
        rhi_loader_test
        PRIVATE
            RHI_TEST_NULL_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_null>"
            RHI_TEST_MALFORMED_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_malformed>"
            RHI_TEST_MALFORMED_ROOT="${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_dependencies(
        rhi_loader_test
        rhi_null rhi_malformed rhi_malformed_abi rhi_malformed_truncated
        rhi_malformed_null_entry rhi_malformed_no_symbol
    )
    harvester_enable_strict_warnings(rhi_loader_test)
    add_test(NAME rhi_loader_test COMMAND rhi_loader_test)

    add_executable(rhi_ui_null_test tests/rhi_ui_null_test.cpp)
    target_link_libraries(rhi_ui_null_test PRIVATE harvester::rhi_loader)
    target_compile_definitions(
        rhi_ui_null_test
        PRIVATE
            RHI_TEST_NULL_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_null>"
    )
    add_dependencies(rhi_ui_null_test rhi_null)
    harvester_enable_strict_warnings(rhi_ui_null_test)
    add_test(NAME rhi_ui_null_test COMMAND rhi_ui_null_test)

    add_executable(imgui_rhi_renderer_null_test tests/imgui_rhi_renderer_null_test.cpp)
    target_link_libraries(
        imgui_rhi_renderer_null_test
        PRIVATE
            harvester::rhi_loader
            harvester::ui_renderer_rhi
            harvester::imgui
    )
    target_compile_definitions(
        imgui_rhi_renderer_null_test
        PRIVATE
            RHI_TEST_NULL_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_null>"
    )
    add_dependencies(imgui_rhi_renderer_null_test rhi_null)
    harvester_enable_strict_warnings(imgui_rhi_renderer_null_test)
    add_test(
        NAME imgui_rhi_renderer_null_test
        COMMAND imgui_rhi_renderer_null_test
    )

    add_executable(
        sdl3_vulkan_presentation_test
        tests/sdl3_vulkan_presentation_test.cpp
    )
    target_link_libraries(
        sdl3_vulkan_presentation_test
        PRIVATE
            harvester::rhi_api
            harvester::sdl3_vulkan_presentation
    )
    harvester_enable_strict_warnings(sdl3_vulkan_presentation_test)
    add_test(
        NAME sdl3_vulkan_presentation_test
        COMMAND sdl3_vulkan_presentation_test
    )

    add_executable(
        sdl3_imgui_platform_test
        tests/sdl3_imgui_platform_test.cpp
    )
    target_link_libraries(
        sdl3_imgui_platform_test
        PRIVATE
            harvester::platform_sdl3
    )
    harvester_enable_strict_warnings(sdl3_imgui_platform_test)
    add_test(NAME sdl3_imgui_platform_test COMMAND sdl3_imgui_platform_test)

    add_executable(
        sdl3_vulkan_wsi_smoke
        tests/sdl3_vulkan_wsi_smoke.cpp
    )
    target_link_libraries(
        sdl3_vulkan_wsi_smoke
        PRIVATE
            harvester::platform_sdl3
            harvester::sdl3_vulkan_presentation
            harvester::vulkan_headers
    )
    harvester_enable_strict_warnings(sdl3_vulkan_wsi_smoke)

    add_executable(
        rhi_vulkan_runtime_smoke
        tests/rhi_vulkan_runtime_smoke.cpp
    )
    target_link_libraries(
        rhi_vulkan_runtime_smoke
        PRIVATE
            harvester::platform_sdl3
            harvester::sdl3_vulkan_presentation
            harvester::rhi_loader
    )
    target_compile_definitions(
        rhi_vulkan_runtime_smoke
        PRIVATE
            RHI_TEST_VULKAN_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_vulkan>"
    )
    add_dependencies(rhi_vulkan_runtime_smoke rhi_vulkan)
    harvester_enable_strict_warnings(rhi_vulkan_runtime_smoke)

    add_executable(rhi_vulkan_ui_smoke tests/rhi_vulkan_ui_smoke.cpp)
    target_link_libraries(
        rhi_vulkan_ui_smoke
        PRIVATE
            harvester::platform_sdl3
            harvester::sdl3_vulkan_presentation
            harvester::rhi_loader
    )
    target_compile_definitions(
        rhi_vulkan_ui_smoke
        PRIVATE
            RHI_TEST_VULKAN_PLUGIN_DIR="$<TARGET_FILE_DIR:rhi_vulkan>"
    )
    add_dependencies(rhi_vulkan_ui_smoke rhi_vulkan)
    harvester_enable_strict_warnings(rhi_vulkan_ui_smoke)

    add_executable(fake_research_service_test tests/fake_research_service_test.cpp)
    target_link_libraries(
        fake_research_service_test
        PRIVATE
            harvester::fake_service
    )
    harvester_enable_strict_warnings(fake_research_service_test)
    add_test(NAME fake_research_service_test COMMAND fake_research_service_test)

    add_executable(worker_protocol_test tests/worker_protocol_test.cpp)
    target_link_libraries(
        worker_protocol_test
        PRIVATE
            harvester::worker_protocol
    )
    harvester_enable_strict_warnings(worker_protocol_test)
    add_test(NAME worker_protocol_test COMMAND worker_protocol_test)

    add_executable(
        python_research_service_test
        tests/python_research_service_test.cpp
    )
    target_link_libraries(
        python_research_service_test
        PRIVATE
            harvester::python_service
    )
    target_compile_definitions(
        python_research_service_test
        PRIVATE
            PYTHON_WORKER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/python_worker_fixture.py"
            PYTHON_WORKER_FIXTURE_WORKDIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    harvester_enable_strict_warnings(python_research_service_test)
    add_test(NAME python_research_service_test COMMAND python_research_service_test)

    add_test(NAME rhi_smoke_host COMMAND rhi_smoke_host)
endif()
