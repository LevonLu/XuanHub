# PyBridge 插件 —— 每个 Python 子插件在独立 python.exe worker 中运行。
# 宿主 DLL 不链接 CPython：Python 崩溃、GIL 卡死或扩展模块异常不会进入 XuanHub 进程。
add_library(pybridge MODULE
    PyBridgePlugin.h
    PyBridgePlugin.cpp
    PyBridgeProtocol.h
    PyBridgeProtocol.cpp
    PythonDependencySpec.h
    PythonDependencySpec.cpp
    PythonDependencyDoctor.h
    PythonDependencyDoctor.cpp
    PyWorkerViewModel.h
    PyWorkerViewModel.cpp
)

set_target_properties(pybridge PROPERTIES AUTOMOC ON)

target_link_libraries(pybridge PRIVATE
    barplugin
    Qt6::Core
    Qt6::Network
)

target_include_directories(pybridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 开发机 Python 路径的可选兜底；分发构建留空，运行时继续按 Bundle/注册表/py/PATH 探测。
set(XUANHUB_PYTHON_HOME_FALLBACK "" CACHE PATH
    "Python worker 运行时目录兜底（仅本机开发用，分发构建留空）")
target_compile_definitions(pybridge PRIVATE XUANHUB_PYTHON_HOME="${XUANHUB_PYTHON_HOME_FALLBACK}")

set_target_properties(pybridge PROPERTIES
    PREFIX ""
    OUTPUT_NAME pybridge
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/pybridge"
)

add_custom_command(TARGET pybridge POST_BUILD
    # 从内嵌解释器版本升级时清理旧运行时残留，确保分发目录不会继续携带 pybackend.dll。
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "${XUANHUB_BIN_DIR}/pybackend.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:pybridge>"
        "${XUANHUB_BIN_DIR}/plugins/pybridge/pybridge.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/pybridge/metadata.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
        "${XUANHUB_BIN_DIR}/plugins/pybridge/i18n"
    # 定向清除退役插件代码，保留运行目录中的用户账户配置和第三方插件。
    COMMAND ${CMAKE_COMMAND}
        "-DXUANHUB_RUNTIME_DIR=${XUANHUB_BIN_DIR}"
        -P "${CMAKE_SOURCE_DIR}/scripts/retire_usage_hub.cmake"
    # bridge_worker.py 与子插件一同部署；新 worker 进程只读取这棵目录。
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/plugins_py"
        "${XUANHUB_BIN_DIR}/plugins_py"
)
