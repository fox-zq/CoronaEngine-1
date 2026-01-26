include(ExternalProject)

ExternalProject_Add(
        cef_download
        URL ${CEF_DOWNLOAD_URL}
        DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}/third_party/cef/download
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/cef/src
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        UPDATE_COMMAND ""
)


set(CORONA_EMBEDDED_PY_DIR "${PROJECT_SOURCE_DIR}/third_party/Python-3.13.7" CACHE PATH "Embedded (bundled) Python directory path")

# ------------------------------------------------------------------------------
# Python Discovery
# ------------------------------------------------------------------------------
set(Python_ROOT_DIR "${CORONA_EMBEDDED_PY_DIR}" CACHE FILEPATH "Embedded Python root directory" FORCE)
message(STATUS "[Python] Using embedded Python3: ${Python_ROOT_DIR}")



# Prefer FindPython3 to locate the embedded Python distribution
find_package(Python COMPONENTS Interpreter Development REQUIRED)

if(Python3_VERSION)
    message(STATUS "  Python version       : ${Python_VERSION}")
endif()
#find_package(pybind11 REQUIRED)

include_directories(${Python_INCLUDE_DIRS})
#include_directories(${pybind11_INCLUDE_DIRS})

# ImGui with SDL3 + OpenGL3 backend (支持多视口/Viewports)
# Create a library target for ImGui backend files
add_library(imgui_backends STATIC
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui_backends PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui_backends PUBLIC imgui SDL3::SDL3-static opengl32)

# Force static runtime for imgui_backends
if(MSVC)
    set_property(TARGET imgui_backends PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(LEARNVULKAN_ENABLE_CEF "Enable CEF integration for LearnVulkanExample" ON)

set(CEF_AVAILABLE OFF)
if(LEARNVULKAN_ENABLE_CEF)
    # Layout:
    # third_party/cef/download/<cef_binary_...>.tar.bz2
    # third_party/cef/src/  (will contain the extracted binary distribution root)
    set(CEF_TP_DIR "${PROJECT_SOURCE_DIR}/third_party/cef")
    set(CEF_ROOT "${CEF_TP_DIR}/src")
    set(CEF_ARCHIVE_PATH "${CEF_TP_DIR}/download/${CEF_ARCHIVE}")

    # If CEF_ROOT is empty, we likely haven't extracted yet.
    # Extract the archive into third_party/cef/ and then point CEF_ROOT to the extracted cef_binary_* folder.
    if(NOT EXISTS "${CEF_ROOT}/include/cef_base.h")
        file(MAKE_DIRECTORY "${CEF_TP_DIR}")
    endif()

    # Download archive if missing
    if(NOT EXISTS "${CEF_ARCHIVE_PATH}")
        file(MAKE_DIRECTORY "${CEF_TP_DIR}/download")
        message(STATUS "CEF: downloading ${CEF_DOWNLOAD_URL} -> ${CEF_ARCHIVE_PATH}")
        file(DOWNLOAD
                "${CEF_DOWNLOAD_URL}"
                "${CEF_ARCHIVE_PATH}"
                SHOW_PROGRESS
                STATUS _cef_dl_status
                LOG _cef_dl_log
        )
        list(GET _cef_dl_status 0 _cef_dl_code)
        list(GET _cef_dl_status 1 _cef_dl_msg)
        if(NOT _cef_dl_code EQUAL 0)
            message(FATAL_ERROR "CEF download failed (${_cef_dl_code}): ${_cef_dl_msg}\n${_cef_dl_log}")
        endif()
    endif()

    # Extract if headers are missing
    if(NOT EXISTS "${CEF_ROOT}/include/cef_base.h")
        message(STATUS "CEF: extracting ${CEF_ARCHIVE_PATH} -> ${CEF_TP_DIR}")
        file(ARCHIVE_EXTRACT INPUT "${CEF_ARCHIVE_PATH}" DESTINATION "${CEF_TP_DIR}")

        # Auto-detect extracted root folder (cef_binary_*) and make ${CEF_ROOT} point to it.
        file(GLOB _cef_roots LIST_DIRECTORIES true "${CEF_TP_DIR}/cef_binary_*" )
        list(LENGTH _cef_roots _cef_root_count)
        if(_cef_root_count GREATER 0)
            # Prefer the first match; typically there is exactly one.
            list(GET _cef_roots 0 _cef_detected_root)
            set(CEF_ROOT "${_cef_detected_root}")
        endif()
    endif()

    # Finalize include/resource dirs with the resolved CEF_ROOT
    set(CEF_INCLUDE_DIR "${CEF_ROOT}/include")
    set(CEF_RES_DIR "${CEF_ROOT}/Resources")

    if(EXISTS "${CEF_INCLUDE_DIR}/cef_base.h")
        set(CEF_AVAILABLE ON)
        message(STATUS "CEF: ready at ${CEF_ROOT}")
    else()
        message(FATAL_ERROR "CEF headers not found: expected ${CEF_INCLUDE_DIR}/cef_base.h")
    endif()
endif()

# Sanity check: this header must exist.
if(NOT EXISTS "${CEF_INCLUDE_DIR}/cef_base.h")
    message(FATAL_ERROR "CEF headers not found: expected ${CEF_INCLUDE_DIR}/cef_base.h")
endif()

# Add CEF wrapper target (libcef_dll_wrapper)
# We need to build this from source to match our compiler settings (Release/Debug, MT/MD, etc.)
if(EXISTS "${CEF_ROOT}/CMakeLists.txt")
    # Use a binary dir to keep it clean
    add_subdirectory("${CEF_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/cef_wrapper" EXCLUDE_FROM_ALL)
endif()

# Pick Debug/Release folder depending on generator/config.
if(CMAKE_CONFIGURATION_TYPES)
    # Multi-config generator (e.g. Visual Studio): use generator expression at build time.
    set(CEF_CFG_DIR "$<CONFIG>")
else()
    # Single-config generator (e.g. Ninja): use CMAKE_BUILD_TYPE at configure time.
    set(CEF_CFG_DIR "${CMAKE_BUILD_TYPE}")
    if(CEF_CFG_DIR STREQUAL "")
        set(CEF_CFG_DIR "Release")
    endif()
endif()

set(CEF_BIN_DIR "${CEF_ROOT}/${CEF_CFG_DIR}")

# Prefer explicit full paths (more reliable than link_directories + bare names)
# NOTE: for multi-config this contains a generator expression and can NOT be used with EXISTS() at configure time.
set(CEF_LIBCEF_DEBUG "${CEF_ROOT}/Debug/libcef.lib")
set(CEF_LIBCEF_RELEASE "${CEF_ROOT}/Release/libcef.lib")
set(CEF_SANDBOX_LIB "${CEF_BIN_DIR}/cef_sandbox.lib")




# ImGui System 配置
file(GLOB IMGUI_SYSTEM_SOURCES
    imgui_system.cpp
    res/BrowserWindow.cpp
    res/WindowsManager.cpp
)


corona_add_system(imgui
    SOURCES ${IMGUI_SYSTEM_SOURCES}
    DEPENDENCIES 
        libcef_dll_wrapper
        debug "${CEF_LIBCEF_DEBUG}"
        optimized "${CEF_LIBCEF_RELEASE}"
        SDL3::SDL3-static
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
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/build_deps/imgui-src
        ${Python_INCLUDE_DIRS}
        ${CEF_INCLUDE_DIR}
        ${CEF_ROOT}
)