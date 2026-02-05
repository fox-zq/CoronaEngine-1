# Create a library target for ImGui backend files
add_library(imgui_backends STATIC
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(imgui_backends PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)
target_compile_definitions(imgui_backends PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui_backends PUBLIC imgui SDL3::SDL3-static volk)


# ImGui System 配置
file(GLOB IMGUI_SYSTEM_SOURCES
    imgui_system.cpp
    vulkan_backend.cpp
    browser/browser_manager.cpp
    browser/cef_client.cpp
    browser/cef_handler.cpp
    browser/cef_py_bind.cpp
    browser/sdl_key_utils.cpp
)


corona_add_system(imgui
    SOURCES ${IMGUI_SYSTEM_SOURCES}
    DEPENDENCIES 
        libcef_dll_wrapper
        debug "${CEF_LIBCEF_DEBUG}"
        optimized "${CEF_LIBCEF_RELEASE}"
        SDL3::SDL3-static
        volk
        imgui
        imgui_backends
        ws2_32
        shlwapi
        dbghelp
        psapi
        version
        winmm
        Python::Python
        nanobind-static
        ktm
        corona::resource::manager
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/build_deps/imgui-src
        ${volk_SOURCE_DIR}
        ${CEF_INCLUDE_DIR}
        ${CEF_ROOT}
)

target_compile_definitions(corona_imgui_system PUBLIC VK_NO_PROTOTYPES)
