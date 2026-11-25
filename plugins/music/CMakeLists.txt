# 音乐插件与本地歌词解析
find_package(Qt6ZlibPrivate REQUIRED)

add_library(music_lyrics STATIC
    MusicLyrics.cpp
    third_party/qqmusicdes/des.cpp
)
target_link_libraries(music_lyrics PUBLIC Qt6::Core PRIVATE Qt6::ZlibPrivate)
target_include_directories(music_lyrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(music MODULE
    MusicPlugin.h
    MusicPlugin.cpp
    MusicViewModel.h
    MusicViewModel.cpp
    MusicWorker.h
    MusicWorker.cpp
)
set_target_properties(music PROPERTIES AUTOMOC ON PREFIX "" OUTPUT_NAME music)
target_link_libraries(music PRIVATE barplugin music_lyrics windowsapp)
target_compile_definitions(music PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set(music_output "${XUANHUB_BIN_DIR}/plugins/music")
set_target_properties(music PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${music_output}")
add_custom_target(music_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${music_output}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:music>"
        "${CMAKE_CURRENT_SOURCE_DIR}/Music.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-LICENSES.txt"
        "${music_output}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/i18n" "${music_output}/i18n"
    VERBATIM
)
add_dependencies(music_assets music)
