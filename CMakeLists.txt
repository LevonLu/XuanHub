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
add_subdirectory(plugins/launcher)
add_subdirectory(plugins/sysinfo)

# pybridge（Python 桥接）为可选组件：依赖构建机的 Python ≥3.12 开发包
#（稳定 ABI 头文件 + python3.lib）与 third_party/nanobind 子模块，
# 缺任一则跳过该插件而非让整个配置失败——其余功能不受影响，
# 运行时 Python 子插件整体不可用而已
find_package(Python 3.12 QUIET COMPONENTS Interpreter Development Development.SABIModule)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nanobind/cmake/nanobind-config.cmake")
    set(_nanobind_ok TRUE)
else()
    set(_nanobind_ok FALSE)
endif()
if(Python_FOUND AND _nanobind_ok)
    add_subdirectory(plugins/pybridge)
elseif(NOT Python_FOUND)
    message(WARNING "未找到 Python >=3.12 开发包，跳过 pybridge 插件"
        "（Python 子插件将不可用；安装 python.org 发行版后重新 configure 即可启用）")
else()
    message(WARNING "third_party/nanobind 子模块未初始化，跳过 pybridge 插件"
        "（执行 git submodule update --init --recursive 后重新 configure 即可启用）")
endif()

# ── 便携打包目标 ─────────────────────────────────────────────
# 用法：cmake --build --preset default --target package_portable
# 在 Release 构建完成后执行，收集 MSVC 运行时 / Python 标准库 / 清理 /
# 创建 portable 标记，打包为 XuanHub-vX.Y.Z-portable-x64.zip。
find_program(PWSH_EXE NAMES pwsh powershell)
if(PWSH_EXE)
    add_custom_target(package_portable
        COMMAND "${PWSH_EXE}" -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_SOURCE_DIR}/scripts/package_portable.ps1"
            -Config "$<CONFIG>"
        COMMENT "打包 XuanHub 便携版本..."
        VERBATIM
    )
else()
    message(STATUS "PowerShell 未找到，package_portable 目标不可用")
endif()
