# lib/barplugin — 插件接口与横切服务（SHARED 库）
# 宿主与所有插件 DLL 共享同一份 EventBus（单一 staticMetaObject / 单一代码副本），
# 避免 STATIC 链接下跨 DLL 的对象语义与 ABI 隐患（Python Bridge 阶段的前置条件）。
add_library(barplugin SHARED
    src/EventBus.cpp
    src/PluginEventScope.cpp
    src/PluginPathValidator.cpp
    src/ProcessReaper.cpp
    include/barplugin/EventBus.h
    include/barplugin/HostServices.h
    include/barplugin/IBarPluginConfigurable.h
    include/barplugin/IBarPlugin2.h
    include/barplugin/IBarPluginHostConfigurable.h
    include/barplugin/IBarPluginHostConfigMigration.h
    include/barplugin/IBarPluginHostDiagnostics.h
    include/barplugin/IBarPluginHostDependencies.h
    include/barplugin/IBarPluginHostEventBus.h
    include/barplugin/IBarPluginHostLifecycle.h
    include/barplugin/IBarPluginHostServices.h
    include/barplugin/IPluginConfigMigration.h
    include/barplugin/PluginHealth.h
    include/barplugin/PluginDescriptor.h
    include/barplugin/PluginEventScope.h
    include/barplugin/PluginPathValidator.h
    include/barplugin/ProcessReaper.h
)

set_target_properties(barplugin PROPERTIES
    AUTOMOC ON
    # barplugin.dll 与 XuanHub.exe 同目录；插件 DLL 经 Windows 标准搜索顺序
    #（应用程序目录优先）解析到它
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 仅编译本体时导出符号（见 barplugin_export.h）
target_compile_definitions(barplugin PRIVATE BARPLUGIN_LIBRARY)

target_include_directories(barplugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(barplugin PUBLIC Qt6::Core)

install(TARGETS barplugin EXPORT XuanHubPluginTargets
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/barplugin DESTINATION include)
