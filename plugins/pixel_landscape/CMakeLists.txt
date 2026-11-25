# 朝暮插件
add_library(pixel_landscape MODULE
    PixelLandscapePlugin.h
    PixelLandscapePlugin.cpp
    PixelLandscapeViewModel.h
    PixelLandscapeViewModel.cpp
    SolarCalculator.h SolarCalculator.cpp
    LandscapeLocation.h LandscapeLocation.cpp
)
set_target_properties(pixel_landscape PROPERTIES AUTOMOC ON PREFIX "" OUTPUT_NAME pixel_landscape)
target_link_libraries(pixel_landscape PRIVATE barplugin Qt6::Core windowsapp)
target_include_directories(pixel_landscape PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(pixel_landscape PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/pixel_landscape")
add_custom_command(TARGET pixel_landscape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:pixel_landscape>" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/pixel_landscape.dll"
)
# QML/绘图数据更新不一定重新链接 DLL，独立资源目标保证每次构建同步。
add_custom_target(pixel_landscape_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/PixelLandscapeSettings.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/CityPresets.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/CITY-DATA-LICENSE.md"
        "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/PixelLandscape.qml" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/PixelLandscape.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LandscapeScene.qml" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/LandscapeScene.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LandscapeFrame.qml" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/LandscapeFrame.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LandscapeController.qml" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/LandscapeController.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/SceneArt.js" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/SceneArt.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LighthouseArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/StreetArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/CampsiteArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/RailwayArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/IslandArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/WindowCityArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/BelowSurfaceArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/WestwardArt.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/WestwardPilgrims.js"
        "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/metadata.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/i18n" "${XUANHUB_BIN_DIR}/plugins/pixel_landscape/i18n")
add_dependencies(pixel_landscape_assets pixel_landscape)
