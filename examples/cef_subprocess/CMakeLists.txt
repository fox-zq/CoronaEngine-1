# CEF Subprocess - 轻量级 CEF 子进程可执行文件
add_executable(cef_subprocess WIN32 main.cpp)

target_link_libraries(cef_subprocess PRIVATE
    libcef_dll_wrapper
    $<$<CONFIG:Debug>:${CEF_LIBCEF_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${CEF_LIBCEF_RELEASE}>
)

target_include_directories(cef_subprocess PRIVATE
    ${CEF_INCLUDE_DIR}
    ${CEF_ROOT}
)

target_compile_features(cef_subprocess PUBLIC cxx_std_20)

# 输出到与主程序相同的目录
set_target_properties(cef_subprocess PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/engine"
)

