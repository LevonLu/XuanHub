cmake_minimum_required(VERSION 3.21)

project(XuanHub
    VERSION 0.1.0
    DESCRIPTION "玄枢 / XuanHub — Qt6/QML Windows 插件化状态栏框架"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # 与 Qt 官方预编译库保持一致的 CRT（Release=/MD, Debug=/MDd），避免插件加载时 CRT 冲突
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # /utf-8 保证源码/字符串中的中文（项目名"玄枢"等）正确处理
    add_compile_options(/Zc:__cplusplus /permissive- /utf-8 /W3)
endif()

find_package(Qt6 6.9 REQUIRED COMPONENTS Quick Widgets Network QuickControls2 Test)
qt_standard_project_setup()

# 统一运行时输出根：build/default/dist/<CONFIG>/（exe 与 plugins 同根）
set(XUANHUB_BIN_DIR "${CMAKE_BINARY_DIR}/dist/$<CONFIG>" CACHE INTERNAL "runtime output root")

add_subdirectory(lib/barplugin)
add_subdirectory(app)
add_subdirectory(plugins/clock)
add_subdirectory(plugins/netstat)
add_subdirectory(plugins/launcher)
add_subdirectory(plugins/pybridge)
