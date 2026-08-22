# 玄枢主可执行 XuanHub.exe
qt_add_executable(XuanHub WIN32
    src/main.cpp

    # Windows Shell 从 PE 资源读取的多尺寸应用图标
    resources/xuanhub.rc

    src/BarWindow.h
    src/BarWindow.cpp

    src/AppBarWin32.h
    src/AppBarWin32.cpp

    src/WorkAreaGuard.h
    src/WorkAreaGuard.cpp

    src/TaskbarEmbed.h
    src/TaskbarEmbed.cpp

    src/ConfigService.h
    src/ConfigService.cpp

    src/JsonTranslator.h
    src/JsonTranslator.cpp

    src/LocaleManager.h
    src/LocaleManager.cpp

    src/SecretStore.h
    src/SecretStore.cpp

    src/ConfigHotReloader.h
    src/ConfigHotReloader.cpp

    src/PluginChangeSet.h
    src/PluginChangeSet.cpp

    src/SemanticVersion.h
    src/SemanticVersion.cpp
    src/PluginManifest.h
    src/PluginManifest.cpp

    src/AutoStart.h

    src/SettingsViewModel.h
    src/SettingsViewModel.cpp

    src/SettingsPluginModel.h
    src/SettingsPluginModel.cpp

    src/SettingsPluginSortModel.h
    src/SettingsPluginSortModel.cpp

    src/SettingsPluginProxy.h
    src/SettingsPluginProxy.cpp

    src/SettingsController.h
    src/SettingsController.cpp

    src/PluginManager.h
    src/PluginManager.cpp

    src/PluginFilterModel.h
    src/PluginFilterModel.cpp

    src/ThemeManager.h
    src/ThemeManager.cpp

    src/ThemeRegistry.h
    src/ThemeRegistry.cpp

    src/TrayIcon.h
    src/TrayIcon.cpp

    src/PluginListModel.h
    src/PluginListModel.cpp
)

qt_policy(SET QTP0001 NEW)  # QML 模块资源前缀 :/qt/qml/
qt_policy(SET QTP0004 NEW)  # 为含 .qml 的子目录自动生成 qmldir

# UiMetrics 是设计令牌 singleton：需标记 QT_QML_SINGLETON_TYPE，
# 让自动生成的 qmldir 以 singleton 注册（各 Ui*.qml 经 import XuanHub 引用）
set_source_files_properties(qml/UiMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(XuanHub
    URI XuanHub
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/Bar.qml
        qml/PluginSlot.qml
        qml/SettingsWindow.qml
        # UI 设计令牌与自绘控件库（Basic 样式 + palette 驱动，设置窗口用）
        qml/UiMetrics.qml
        qml/UiButton.qml
        qml/UiSwitch.qml
        qml/UiComboBox.qml
        qml/UiSpinBox.qml
        qml/UiTextField.qml
        qml/UiCard.qml
    RESOURCES
        resources/default_config.json
        i18n/zh-CN.json
        i18n/en-US.json
        # 窗口使用完整品牌图标；托盘另用针对 16–24 px 简化的紧凑 X 图标
        resources/xuanhub-icon-256.png
        resources/xuanhub-tray-icon-64.png
)

target_link_libraries(XuanHub PRIVATE
    Qt6::Quick
    Qt6::QuickControls2   # 设置窗口（M8）的表单控件
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test             # --uitest 调试钩子用 QTest 注入鼠标/键盘（devUiEvent）
    barplugin            # 插件接口库
    shell32              # SHAppBarMessage
    user32               # SetWindowPos / 工作区
    gdi32
    crypt32              # Windows DPAPI（插件 API Key 密钥库）
)

target_compile_definitions(XuanHub PRIVATE
    XUANHUB_VERSION="${PROJECT_VERSION}"
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

set_target_properties(XuanHub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}"
)

# 构建后自动重部署：补齐 Qt 运行时 DLL 与 QML 插件（--qmldir 扫描 QML import，
# 缺它会漏 qml/QtQuick/*/插件 DLL —— M8 设置窗口曾因此打开空白）。
# 每次 XuanHub 重新链接后执行；手动部署说明见 AGENTS.md §8。
# 优先用 Qt6 自带的 windeployqt 导入目标（随 Qt 定位，天然无机器路径）；
# 目标不存在时退回 find_program（CMAKE_PREFIX_PATH 可能是列表，逐个拼 bin）
if(TARGET Qt6::windeployqt)
    set(WINDEPLOYQT_EXE "$<TARGET_FILE:Qt6::windeployqt>")
else()
    set(_windeployqt_hints "")
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND _windeployqt_hints "${_prefix}/bin")
    endforeach()
    find_program(WINDEPLOYQT_EXE NAMES windeployqt HINTS ${_windeployqt_hints} NO_CACHE)
endif()
if(WINDEPLOYQT_EXE)
    add_custom_command(TARGET XuanHub POST_BUILD
        COMMAND "${WINDEPLOYQT_EXE}" "$<IF:$<CONFIG:Debug>,--debug,--release>"
                --qmldir "${CMAKE_SOURCE_DIR}/app/qml"
                --qmldir "${CMAKE_SOURCE_DIR}/plugins"
                --qmldir "${CMAKE_SOURCE_DIR}/plugins_py"
                "$<TARGET_FILE:XuanHub>"
        VERBATIM
    )
else()
    message(WARNING "windeployqt 未找到，dist 不会自动部署 Qt 依赖（请手动执行，见 AGENTS.md §8）")
endif()

# M9：部署 themes/ 主题包和许可证到运行时目录。
# 每次 XuanHub 链接后整树同步主题，并确保二进制分发满足 BSD-3-Clause 署名要求。
add_custom_command(TARGET XuanHub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/themes"
        "${XUANHUB_BIN_DIR}/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${XUANHUB_BIN_DIR}/LICENSE"
    VERBATIM
)
