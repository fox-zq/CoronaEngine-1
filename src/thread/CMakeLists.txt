include_guard(GLOBAL)

set(_CORONA_THREAD_DIR ${CMAKE_CURRENT_SOURCE_DIR})

corona_collect_module(Thread ${_CORONA_THREAD_DIR})

add_library(CoronaThread INTERFACE)
add_library(corona::thread ALIAS CoronaThread)

if(CORONA_THREAD_PRIVATE_SOURCES)
    target_sources(CoronaThread INTERFACE ${CORONA_THREAD_PRIVATE_SOURCES})
endif()

if(CORONA_THREAD_PUBLIC_HEADERS)
    target_sources(CoronaThread INTERFACE ${CORONA_THREAD_PUBLIC_HEADERS})
endif()

target_sources(CoronaThread
    INTERFACE
    ${CORONA_ENGINE_PUBLIC_INCLUDE_ROOT}/corona/threading/SafeCommandQueue.h
    ${CORONA_ENGINE_PUBLIC_INCLUDE_ROOT}/corona/threading/SafeDataCache.h
)

target_compile_features(CoronaThread INTERFACE cxx_std_20)

corona_target_use_public_includes(CoronaThread SCOPE INTERFACE)

target_link_libraries(CoronaThread
    INTERFACE
    Corona::Logger
    cabbage::concurrent
)