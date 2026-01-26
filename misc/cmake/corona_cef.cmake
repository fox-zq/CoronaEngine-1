# ==============================================================================
# corona_cef.cmake
#
# Purpose:
#   Download and configure Chromium Embedded Framework (CEF) library.
# ==============================================================================

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

include(ExternalProject)

ExternalProject_Add(
        cef_download
        URL ${CEF_DOWNLOAD_URL}
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/third_party/cef/download
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/cef/src
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        UPDATE_COMMAND ""
)

option(CORONA_ENABLE_CEF "Enable CEF integration for CoronaEngine" ON)

set(CEF_AVAILABLE OFF)
if(CORONA_ENABLE_CEF)
    if(DEFINED PROJECT_SOURCE_DIR)
        set(CEF_TP_DIR "${PROJECT_SOURCE_DIR}/third_party/cef")
    else()
        set(CEF_TP_DIR "${CMAKE_SOURCE_DIR}/third_party/cef")
    endif()

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

        # Auto-detect extracted root folder (cef_binary_*) and rename it to 'src'
        file(GLOB _cef_roots LIST_DIRECTORIES true "${CEF_TP_DIR}/cef_binary_*" )
        list(LENGTH _cef_roots _cef_root_count)
        if(_cef_root_count GREATER 0)
            # Prefer the first match; typically there is exactly one.
            list(GET _cef_roots 0 _cef_detected_root)

            # Rename extraction result to 'src' to match project convention
            # Ensure target 'src' directory is clean before renaming
            if(EXISTS "${CEF_TP_DIR}/src")
                file(REMOVE_RECURSE "${CEF_TP_DIR}/src")
            endif()

            message(STATUS "CEF: renaming ${_cef_detected_root} -> ${CEF_TP_DIR}/src")
            file(RENAME "${_cef_detected_root}" "${CEF_TP_DIR}/src")

            set(CEF_ROOT "${CEF_TP_DIR}/src")
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
    add_subdirectory("${CEF_ROOT}" "${CMAKE_BINARY_DIR}/cef_wrapper" EXCLUDE_FROM_ALL)
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
