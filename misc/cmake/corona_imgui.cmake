include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG master
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG master
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(volk)

FetchContent_Declare(
        Vulkan-Headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG main
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Vulkan-Headers)

FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG master
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

FetchContent_Declare(
        SDL
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.0
        GIT_SHALLOW ON
)
FetchContent_MakeAvailable(SDL)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.5-docking
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
        nanobind
        GIT_REPOSITORY https://github.com/wjakob/nanobind.git
        GIT_TAG v2.10.2
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(nanobind)

# Manually define imgui target since it has no CMakeLists.txt
if(NOT TARGET imgui)
    add_library(imgui STATIC
            "${imgui_SOURCE_DIR}/imgui.cpp"
            "${imgui_SOURCE_DIR}/imgui_demo.cpp"
            "${imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    )
    target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")

    # Force static runtime (MT/MTd) to match the rest of the project
    if(MSVC)
        set_property(TARGET imgui PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# Vulkan loader (Windows: vulkan-1.lib) comes from Vulkan SDK
find_package(Vulkan REQUIRED)

# 确保所有第三方库也使用静态运行时 (MT/MTd)
if(MSVC)
    foreach(tgt glfw volk SDL3-static SDL3_test imgui)
        if(TARGET ${tgt})
            set_property(TARGET ${tgt} PROPERTY
                    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endforeach()
endif()

set(CEF_AutoBuildDemo "143.0.14+gdd46a37+chromium-143.0.7499.193")

if(WIN32)
    set(CEF_ARCHIVE "cef_binary_${CEF_AutoBuildDemo}_windows64.tar.bz2")
    set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_ARCHIVE}")
    set(CEF_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef/src/Release")
elseif(UNIX AND NOT APPLE)
    set(CEF_ARCHIVE "cef_binary_${CEF_AutoBuildDemo}_linux64.tar.bz2")
    set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_ARCHIVE}")
    set(CEF_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef/src/Release")
elseif(APPLE)
    set(CEF_ARCHIVE "cef_binary_${CEF_AutoBuildDemo}_macosx64.tar.bz2")
    set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_ARCHIVE}")
    set(CEF_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef/src/Release")
endif()

