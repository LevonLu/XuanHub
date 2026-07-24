# sysinfo 插件（MODULE → sysinfo.dll）
# 采集 CPU（PDH）/ 内存（GlobalMemoryStatusEx）/ GPU（PDH 引擎利用率，回落 DXGI 显存）/
# 网络上下行速率（GetIfTable2 差值）占用率，统一为可定制布局的多指标显示。
# 链接库：pdh（CPU/GPU 计数器）、d3d11/dxgi（GPU 显存回落）、iphlpapi/ws2_32（网络字节采集）
# —— 全为 Windows 系统 API，零外部依赖。
add_library(sysinfo MODULE
    SysInfoPlugin.h   SysInfoPlugin.cpp
    SysInfoWorker.h   SysInfoWorker.cpp
    SysInfoViewModel.h SysInfoViewModel.cpp
)
set_target_properties(sysinfo PROPERTIES AUTOMOC ON)
target_link_libraries(sysinfo PRIVATE barplugin Qt6::Core Qt6::Gui pdh d3d11 dxgi iphlpapi ws2_32)
target_include_directories(sysinfo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(sysinfo PROPERTIES
    PREFIX ""
    OUTPUT_NAME sysinfo
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/sysinfo"
)

# VS 生成器下 MODULE 的 .dll 不走 RUNTIME_OUTPUT_DIRECTORY，POST_BUILD copy_if_different 是兜底（必保留）
add_custom_command(TARGET sysinfo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:sysinfo>"
        "${XUANHUB_BIN_DIR}/plugins/sysinfo/sysinfo.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo.qml"
        "${XUANHUB_BIN_DIR}/plugins/sysinfo/SysInfo.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/SysInfoSettings.qml"
        "${XUANHUB_BIN_DIR}/plugins/sysinfo/SysInfoSettings.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/sysinfo/metadata.json"
)
