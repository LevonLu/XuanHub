cmake_minimum_required(VERSION 3.21)

project(XuanHub
    VERSION 0.2.1
    DESCRIPTION "玄枢 / XuanHub — Qt6/QML Windows 插件化状态栏框架"
    LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)

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
add_subdirectory(plugins/xuanhub_menu)
add_subdirectory(plugins/clock)
add_subdirectory(plugins/sysinfo)

# pybridge 不再内嵌 CPython：只需 Qt 网络组件启动独立 python.exe worker，
# 因而构建机无需 Python 开发包。运行时找不到 Python 3.12+
# 时插件自行降级，不影响宿主与其他插件。
add_subdirectory(plugins/pybridge)

# 第三方原生插件 SDK：安装 barplugin 导入目标、公共头和版本化 CMake 包。
set(XUANHUB_SDK_QT_VERSION "6.9.3")
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/sdk/cmake/XuanHubPluginConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/XuanHubPluginConfig.cmake"
    INSTALL_DESTINATION "lib/cmake/XuanHubPlugin"
)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/XuanHubPluginConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(EXPORT XuanHubPluginTargets
    FILE XuanHubPluginTargets.cmake
    NAMESPACE XuanHub::
    DESTINATION lib/cmake/XuanHubPlugin
)
install(FILES
    "${CMAKE_BINARY_DIR}/XuanHubPluginConfig.cmake"
    "${CMAKE_BINARY_DIR}/XuanHubPluginConfigVersion.cmake"
    DESTINATION lib/cmake/XuanHubPlugin
)
install(FILES LICENSE DESTINATION share/XuanHubPlugin)

if(BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(experiments/native_worker)
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
