# Launcher 插件（MODULE → launcher.dll）
add_library(launcher MODULE
    LauncherPlugin.h   LauncherPlugin.cpp
    LauncherViewModel.h LauncherViewModel.cpp
)
set_target_properties(launcher PROPERTIES AUTOMOC ON)
target_link_libraries(launcher PRIVATE barplugin Qt6::Core)
target_include_directories(launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(launcher PROPERTIES
    PREFIX ""
    OUTPUT_NAME launcher
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/launcher"
)

add_custom_command(TARGET launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:launcher>"
        "${XUANHUB_BIN_DIR}/plugins/launcher/launcher.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Launcher.qml"
        "${XUANHUB_BIN_DIR}/plugins/launcher/Launcher.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/launcher/metadata.json"
)
