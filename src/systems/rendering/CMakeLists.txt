include_guard(GLOBAL)

set(_CORONA_SYSTEM_RENDERING_DIR ${CMAKE_CURRENT_SOURCE_DIR})

corona_collect_module(SystemRendering ${_CORONA_SYSTEM_RENDERING_DIR})

add_library(CoronaSystemRendering STATIC)
add_library(corona::system::rendering ALIAS CoronaSystemRendering)

if(CORONA_SYSTEMRENDERING_PRIVATE_SOURCES)
    target_sources(CoronaSystemRendering PRIVATE ${CORONA_SYSTEMRENDERING_PRIVATE_SOURCES})
endif()

target_sources(CoronaSystemRendering
    PUBLIC
    ${CORONA_ENGINE_PUBLIC_INCLUDE_ROOT}/corona/systems/RenderingSystem.h
)

target_compile_features(CoronaSystemRendering PUBLIC cxx_std_20)

corona_target_use_public_includes(CoronaSystemRendering)

target_link_libraries(CoronaSystemRendering
    PUBLIC
    corona::system::interface
    corona::core

    CoronaResource::Resource
    CabbageHardware
)