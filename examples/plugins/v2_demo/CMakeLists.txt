# 第三方 SDK V2 示例 —— 只依赖已安装的 XuanHubPlugin 包和 Qt Core。
cmake_minimum_required(VERSION 3.21)
project(XuanHubV2Demo VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "XuanHub 原生插件只支持 Windows + MSVC 2022 x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_compile_options(/Zc:__cplusplus /permissive- /utf-8 /W3)

find_package(XuanHubPlugin CONFIG REQUIRED)

add_library(v2_demo MODULE DemoPlugin.h DemoPlugin.cpp)
set_target_properties(v2_demo PROPERTIES
    AUTOMOC ON
    PREFIX ""
    OUTPUT_NAME v2_demo
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/v2_demo"
)
target_link_libraries(v2_demo PRIVATE XuanHub::barplugin Qt6::Core)

add_custom_command(TARGET v2_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:v2_demo>" "${CMAKE_BINARY_DIR}/dist/v2_demo/v2_demo.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Demo.qml"
        "${CMAKE_BINARY_DIR}/dist/v2_demo/Demo.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${CMAKE_BINARY_DIR}/dist/v2_demo/metadata.json"
    VERBATIM
)
