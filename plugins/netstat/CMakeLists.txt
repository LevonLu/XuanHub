# netstat 插件（MODULE → netstat.dll）
add_library(netstat MODULE
    NetStatPlugin.h   NetStatPlugin.cpp
    NetStatWorker.h   NetStatWorker.cpp
    NetStatViewModel.h NetStatViewModel.cpp
)
set_target_properties(netstat PROPERTIES AUTOMOC ON)
target_link_libraries(netstat PRIVATE barplugin Qt6::Core iphlpapi ws2_32)
target_include_directories(netstat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(netstat PROPERTIES
    PREFIX ""
    OUTPUT_NAME netstat
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/netstat"
)

add_custom_command(TARGET netstat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:netstat>"
        "${XUANHUB_BIN_DIR}/plugins/netstat/netstat.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/NetStat.qml"
        "${XUANHUB_BIN_DIR}/plugins/netstat/NetStat.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/netstat/metadata.json"
)
