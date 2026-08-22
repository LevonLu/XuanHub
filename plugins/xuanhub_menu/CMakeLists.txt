# 玄枢菜单插件（MODULE → xuanhub_menu.dll）
add_library(xuanhub_menu MODULE
    XuanHubMenuPlugin.h
    XuanHubMenuPlugin.cpp
    XuanHubMenuViewModel.h
    XuanHubMenuViewModel.cpp
)

set_target_properties(xuanhub_menu PROPERTIES AUTOMOC ON)

target_link_libraries(xuanhub_menu PRIVATE
    barplugin
    Qt6::Core
)

target_include_directories(xuanhub_menu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(xuanhub_menu PROPERTIES
    PREFIX ""
    OUTPUT_NAME xuanhub_menu
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/xuanhub_menu"
)

# VS 生成器下 MODULE 的 DLL 不遵循 RUNTIME_OUTPUT_DIRECTORY，POST_BUILD 兜底复制。
add_custom_command(TARGET xuanhub_menu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:xuanhub_menu>"
        "${XUANHUB_BIN_DIR}/plugins/xuanhub_menu/xuanhub_menu.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/XuanHubMenu.qml"
        "${XUANHUB_BIN_DIR}/plugins/xuanhub_menu/XuanHubMenu.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/XuanHubGlyph.qml"
        "${XUANHUB_BIN_DIR}/plugins/xuanhub_menu/XuanHubGlyph.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/xuanhub_menu/metadata.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
        "${XUANHUB_BIN_DIR}/plugins/xuanhub_menu/i18n"
)
