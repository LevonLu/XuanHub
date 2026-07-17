# 玄枢主可执行 XuanHub.exe
qt_add_executable(XuanHub WIN32
    src/main.cpp

    src/BarWindow.h
    src/BarWindow.cpp

    src/AppBarWin32.h
    src/AppBarWin32.cpp

    src/WorkAreaGuard.h
    src/WorkAreaGuard.cpp

    src/ConfigService.h
    src/ConfigService.cpp

    src/ConfigHotReloader.h
    src/ConfigHotReloader.cpp

    src/AutoStart.h

    src/SettingsViewModel.h
    src/SettingsViewModel.cpp

    src/SettingsPluginModel.h
    src/SettingsPluginModel.cpp

    src/SettingsController.h
    src/SettingsController.cpp

    src/PluginManager.h
    src/PluginManager.cpp

    src/PluginFilterModel.h
    src/PluginFilterModel.cpp

    src/ThemeManager.h
    src/ThemeManager.cpp

    src/TrayIcon.h
    src/TrayIcon.cpp

    src/PluginListModel.h
    src/PluginListModel.cpp
)

qt_policy(SET QTP0001 NEW)  # QML 模块资源前缀 :/qt/qml/
qt_policy(SET QTP0004 NEW)  # 为含 .qml 的子目录自动生成 qmldir

qt_add_qml_module(XuanHub
    URI XuanHub
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/Bar.qml
        qml/PluginSlot.qml
        qml/SettingsWindow.qml
    RESOURCES
        resources/default_config.json
)

target_link_libraries(XuanHub PRIVATE
    Qt6::Quick
    Qt6::QuickControls2   # 设置窗口（M8）的表单控件
    Qt6::Gui
    Qt6::Widgets
    barplugin            # 插件接口库
    shell32              # SHAppBarMessage
    user32               # SetWindowPos / 工作区
    gdi32
)

target_compile_definitions(XuanHub PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

set_target_properties(XuanHub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}"
)
