# ==============================================================================
# corona_helicon_shader_compile_override.cmake
#
# Purpose:
#   Override Helicon shader compilation helper from CabbageHardware so shader
#   headers are named as "<full-shader-filename>.hpp".
#
# Why:
#   Upstream helper currently forwards "-output-file-extension" using a full
#   multi-part extension (e.g. ".comp.glsl.hpp"), while ShaderCompileScripts
#   itself appends that value to "path.stem()". For "foo.comp.glsl", this
#   becomes "foo.comp.comp.glsl.hpp" and no longer matches #include GLSL(...).
# ==============================================================================

include_guard(GLOBAL)

if(NOT COMMAND _helicon_parse_shader_includes)
    message(STATUS "[Helicon Override] Upstream parser is unavailable; skipping override.")
    return()
endif()

function(_corona_helicon_get_last_extension INPUT_FILE OUT_LAST_EXT)
    get_filename_component(_name "${INPUT_FILE}" NAME)
    string(REGEX MATCH "\\.[^.]+$" _last_ext "${_name}")

    if(NOT _last_ext)
        set(_last_ext ".h")
    endif()

    set(${OUT_LAST_EXT} "${_last_ext}" PARENT_SCOPE)
endfunction()

function(helicon_compile_shaders TARGET_NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "" ${ARGN})

    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/Src/Helicon/Compiler/HardcodeShaders")
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    get_target_property(TARGET_SOURCES ${TARGET_NAME} SOURCES)
    get_target_property(TARGET_SOURCE_DIR ${TARGET_NAME} SOURCE_DIR)

    if(NOT TARGET_SOURCES)
        message(STATUS "[Helicon] ${TARGET_NAME}: No sources found, skipping shader compilation")
        return()
    endif()

    _helicon_parse_shader_includes(
        "${TARGET_SOURCES}" "${TARGET_SOURCE_DIR}"
        SHADER_LANGS SHADER_PATHS SHADER_REL_PATHS
    )

    list(LENGTH SHADER_PATHS SHADER_COUNT)
    if(SHADER_COUNT EQUAL 0)
        message(STATUS "[Helicon] ${TARGET_NAME}: No shader includes found")
        return()
    endif()

    message(STATUS "[Helicon] ${TARGET_NAME}: Found ${SHADER_COUNT} shader(s) to compile")

    set(ALL_GENERATED_HEADERS "")

    math(EXPR LAST_IDX "${SHADER_COUNT} - 1")
    foreach(IDX RANGE ${LAST_IDX})
        list(GET SHADER_LANGS ${IDX} SHADER_LANG)
        list(GET SHADER_PATHS ${IDX} SHADER_PATH)
        list(GET SHADER_REL_PATHS ${IDX} SHADER_REL_PATH)

        if(NOT EXISTS "${SHADER_PATH}")
            message(WARNING "[Helicon] Shader file not found: ${SHADER_PATH}")
            continue()
        endif()

        get_filename_component(SHADER_REL_DIR "${SHADER_REL_PATH}" DIRECTORY)
        get_filename_component(SHADER_NAME "${SHADER_PATH}" NAME)
        get_filename_component(SHADER_EXT "${SHADER_PATH}" EXT)

        if(SHADER_REL_DIR)
            set(OUTPUT_SUBDIR "${ARG_OUTPUT_DIR}/${SHADER_REL_DIR}")
        else()
            set(OUTPUT_SUBDIR "${ARG_OUTPUT_DIR}")
        endif()

        file(MAKE_DIRECTORY "${OUTPUT_SUBDIR}")

        set(OUTPUT_HEADER "${OUTPUT_SUBDIR}/${SHADER_NAME}.hpp")

        string(TOLOWER "${SHADER_LANG}" LANG_LOWER)

        get_filename_component(SHADER_NAME_WE "${SHADER_PATH}" NAME_WE)
        string(TOLOWER "${SHADER_NAME_WE}" SHADER_NAME_LOWER)
        string(TOLOWER "${SHADER_EXT}" SHADER_EXT_LOWER)

        set(SHADER_STAGE_ARG "")
        if(SHADER_EXT_LOWER MATCHES "\\.(vert|vs)")
            set(SHADER_STAGE_ARG "-t" "vert")
        elseif(SHADER_EXT_LOWER MATCHES "\\.(frag|fs)")
            set(SHADER_STAGE_ARG "-t" "frag")
        elseif(SHADER_EXT_LOWER MATCHES "\\.(comp|cs)")
            set(SHADER_STAGE_ARG "-t" "comp")
        elseif(SHADER_NAME_LOWER MATCHES "frag")
            set(SHADER_STAGE_ARG "-t" "frag")
        elseif(SHADER_NAME_LOWER MATCHES "comp|compute")
            set(SHADER_STAGE_ARG "-t" "comp")
        elseif(SHADER_NAME_LOWER MATCHES "vert")
            set(SHADER_STAGE_ARG "-t" "vert")
        endif()

        _corona_helicon_get_last_extension("${SHADER_NAME}" SHADER_LAST_EXT)
        set(SCRIPT_OUTPUT_FILE_EXTENSION "${SHADER_LAST_EXT}.hpp")

        message(STATUS "[Helicon]   - ${SHADER_REL_PATH} (${SHADER_LANG}) -> ${SHADER_REL_PATH}.hpp")

        add_custom_command(
            OUTPUT "${OUTPUT_HEADER}"
            COMMAND $<TARGET_FILE:ShaderCompileScripts>
                -l ${LANG_LOWER}
                -s "${SHADER_PATH}"
                -o "${OUTPUT_SUBDIR}"
                -output-file-extension "${SCRIPT_OUTPUT_FILE_EXTENSION}"
                ${SHADER_STAGE_ARG}
            DEPENDS "${SHADER_PATH}" ShaderCompileScripts
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "[Helicon] Compiling shader: ${SHADER_REL_PATH}"
            VERBATIM
        )

        list(APPEND ALL_GENERATED_HEADERS "${OUTPUT_HEADER}")
    endforeach()

    if(ALL_GENERATED_HEADERS)
        set(SHADER_TARGET "${TARGET_NAME}_shaders")
        add_custom_target(${SHADER_TARGET}
            DEPENDS ${ALL_GENERATED_HEADERS}
            COMMENT "[Helicon] All shaders for ${TARGET_NAME} compiled"
        )

        add_dependencies(${TARGET_NAME} ${SHADER_TARGET})
        target_include_directories(${TARGET_NAME} PUBLIC "${ARG_OUTPUT_DIR}")

        message(STATUS "[Helicon] ${TARGET_NAME}: Output directory: ${ARG_OUTPUT_DIR}")
    endif()
endfunction()
