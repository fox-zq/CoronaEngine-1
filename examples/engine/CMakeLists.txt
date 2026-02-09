# ==============================================================================
# CoronaEngine - Main Engine Example
#
# 这是 CoronaEngine 的主要可执行程序，演示完整的引擎初始化和运行流程
# ==============================================================================

corona_add_example(
    NAME corona_engine
    SOURCES main.cpp
    COPY_ASSETS
)

# 链接 CoronaEngine 核心库（已包含所有系统）
target_link_libraries(corona_engine PRIVATE
    corona::engine
)

# 确保 cef_subprocess 在 corona_engine 之前构建
if(TARGET cef_subprocess)
    add_dependencies(corona_engine cef_subprocess)
endif()

