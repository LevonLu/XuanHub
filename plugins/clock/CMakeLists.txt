# Clock 插件（MODULE → clock.dll）
# 用标准 add_library MODULE；Q_PLUGIN_METADATA/IID 由 AUTOMOC 处理。
add_library(clock MODULE
    ClockPlugin.h
    ClockPlugin.cpp
    ClockViewModel.h
    ClockViewModel.cpp
)

set_target_properties(clock PROPERTIES AUTOMOC ON)

target_link_libraries(clock PRIVATE
    barplugin
    Qt6::Core
)

target_include_directories(clock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 输出 clock.dll（无 lib 前缀），与 Clock.qml/metadata.json 同目录
set_target_properties(clock PROPERTIES
    PREFIX ""
    OUTPUT_NAME clock
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/clock"
)

# 复制 clock.dll（MODULE 在 VS 生成器下 .dll 不走 RUNTIME_OUTPUT_DIRECTORY，用 POST_BUILD 兜底）
# 及 QML、元数据到运行时插件目录
add_custom_command(TARGET clock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:clock>"
        "${XUANHUB_BIN_DIR}/plugins/clock/clock.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Clock.qml"
        "${XUANHUB_BIN_DIR}/plugins/clock/Clock.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/clock/metadata.json"
)
