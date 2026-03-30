# ==============================================================================
# corona_options.cmake
#
# Purpose:
# Centralize project-level options and high-level feature toggles.
#
# Notes:
# - Consolidates boolean/path/version switches that may be provided via `-D`.
# - Provides a single place to document and extend build configuration knobs.
# - Future feature switches (tests, installers, additional third parties) should
# also be added here.
# ==============================================================================

include_guard(GLOBAL)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

option(CORONA_AUTO_INSTALL_PY_DEPS "Auto-install missing Python packages during configure" ON)
option(BUILD_SHARED_LIBS "Build as shared libraries (default OFF for static)" OFF)
option(BUILD_CORONA_EDITOR "Build Corona editor" ON)
option(BUILD_CORONA_RUNTIME "Build Corona runtime" ON)
option(BUILD_CORONA_TESTING "Build Corona test suite" ${PROJECT_IS_TOP_LEVEL})
option(BUILD_CORONA_EXAMPLES "Build example programs" ${PROJECT_IS_TOP_LEVEL})
option(CORONA_BUILD_HARDWARE "Build Corona Hardware features" ON)
option(CORONA_BUILD_VISION "Build Corona Vision features" OFF)
message(STATUS "[Options] CORONA_AUTO_INSTALL_PY_DEPS             = ${CORONA_AUTO_INSTALL_PY_DEPS}")
message(STATUS "[Options] BUILD_SHARED_LIBS                       = ${BUILD_SHARED_LIBS}")
message(STATUS "[Options] BUILD_CORONA_EDITOR                     = ${BUILD_CORONA_EDITOR}")
message(STATUS "[Options] BUILD_CORONA_RUNTIME                    = ${BUILD_CORONA_RUNTIME}")
message(STATUS "[Options] BUILD_CORONA_TESTING                    = ${BUILD_CORONA_TESTING}")
message(STATUS "[Options] BUILD_CORONA_EXAMPLES                   = ${BUILD_CORONA_EXAMPLES}")
message(STATUS "[Options] CORONA_BUILD_HARDWARE                   = ${CORONA_BUILD_HARDWARE}")
message(STATUS "[Options] CORONA_BUILD_VISION                     = ${CORONA_BUILD_VISION}")
