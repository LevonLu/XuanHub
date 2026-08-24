# tests/CMakeLists.txt — 核心配置与密钥持久化的轻量回归测试
add_executable(xuanhub_config_tests
    ConfigServiceTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ConfigService.h
    ${CMAKE_SOURCE_DIR}/app/src/ConfigService.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SecretStore.h
    ${CMAKE_SOURCE_DIR}/app/src/SecretStore.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ConfigSecretProcessor.h
    ${CMAKE_SOURCE_DIR}/app/src/ConfigSecretProcessor.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SettingsPluginModel.h
    ${CMAKE_SOURCE_DIR}/app/src/SettingsPluginModel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SettingsPluginSortModel.h
    ${CMAKE_SOURCE_DIR}/app/src/SettingsPluginSortModel.cpp
)

set_target_properties(xuanhub_config_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_config_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
    ${CMAKE_SOURCE_DIR}/lib/barplugin/include
)
target_link_libraries(xuanhub_config_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    crypt32
)

# CTest 从 build/tests/Release 直接启动，不经过主程序的 windeployqt；把最小 Qt
# 运行时放到测试 exe 同目录，避免开发机 PATH 偶然包含 Qt 才能跑的假绿测试。
add_custom_command(TARGET xuanhub_config_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_config_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_config_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:xuanhub_config_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_config_tests>"
    VERBATIM
)

add_test(NAME xuanhub_config_tests COMMAND xuanhub_config_tests)

add_executable(xuanhub_locale_tests
    LocaleTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/JsonTranslator.h
    ${CMAKE_SOURCE_DIR}/app/src/JsonTranslator.cpp
    ${CMAKE_SOURCE_DIR}/app/src/LocaleManager.h
    ${CMAKE_SOURCE_DIR}/app/src/LocaleManager.cpp
)
set_target_properties(xuanhub_locale_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_locale_tests PRIVATE ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(xuanhub_locale_tests PRIVATE barplugin Qt6::Core Qt6::Qml Qt6::Test)
add_custom_command(TARGET xuanhub_locale_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_locale_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_locale_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Qml>" "$<TARGET_FILE_DIR:xuanhub_locale_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_locale_tests>"
    VERBATIM
)
add_test(NAME xuanhub_locale_tests COMMAND xuanhub_locale_tests)

add_executable(xuanhub_theme_tests
    ThemeTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ThemeRegistry.h
    ${CMAKE_SOURCE_DIR}/app/src/ThemeRegistry.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ThemeManager.h
    ${CMAKE_SOURCE_DIR}/app/src/ThemeManager.cpp
)
set_target_properties(xuanhub_theme_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_theme_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
    ${CMAKE_SOURCE_DIR}/lib/barplugin/include
)
target_link_libraries(xuanhub_theme_tests PRIVATE
    barplugin Qt6::Core Qt6::Gui Qt6::Test
)
target_compile_definitions(xuanhub_theme_tests PRIVATE
    XUANHUB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_custom_command(TARGET xuanhub_theme_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_theme_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_theme_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:xuanhub_theme_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_theme_tests>"
    VERBATIM
)
add_test(NAME xuanhub_theme_tests COMMAND xuanhub_theme_tests)

add_executable(xuanhub_qml_smoke
    QmlSmokeMain.cpp
)
set_target_properties(xuanhub_qml_smoke PROPERTIES AUTOMOC ON)
set_target_properties(xuanhub_qml_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}")
target_link_libraries(xuanhub_qml_smoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
)
add_dependencies(xuanhub_qml_smoke XuanHub)
if(TARGET Qt6::QOffscreenIntegrationPlugin)
    add_custom_command(TARGET xuanhub_qml_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
            "${XUANHUB_BIN_DIR}/platforms/$<TARGET_FILE_NAME:Qt6::QOffscreenIntegrationPlugin>"
        VERBATIM)
endif()
add_test(NAME xuanhub_qml_smoke_hello
    COMMAND xuanhub_qml_smoke
        "${CMAKE_SOURCE_DIR}/plugins_py/hello/Hello.qml"
        "${CMAKE_SOURCE_DIR}/app/qml/PluginPopup.qml")
set_tests_properties(xuanhub_qml_smoke_hello PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${XUANHUB_BIN_DIR}/platforms;QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts"
    TIMEOUT 15)

add_executable(xuanhub_plugin_slot_health_tests
    PluginSlotHealthTest.cpp
)
set_target_properties(xuanhub_plugin_slot_health_tests PROPERTIES AUTOMOC ON)
set_target_properties(xuanhub_plugin_slot_health_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}")
target_compile_definitions(xuanhub_plugin_slot_health_tests PRIVATE
    XUANHUB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(xuanhub_plugin_slot_health_tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test
)
add_dependencies(xuanhub_plugin_slot_health_tests XuanHub)
add_test(NAME xuanhub_plugin_slot_health_tests
    COMMAND xuanhub_plugin_slot_health_tests)
set_tests_properties(xuanhub_plugin_slot_health_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${XUANHUB_BIN_DIR}/platforms;QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts"
    TIMEOUT 15)

add_executable(xuanhub_popup_service_tests
    PopupServiceTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PopupService.h
    ${CMAKE_SOURCE_DIR}/app/src/PopupService.cpp
)
set_target_properties(xuanhub_popup_service_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_popup_service_tests PRIVATE ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(xuanhub_popup_service_tests PRIVATE Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_popup_service_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_popup_service_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_popup_service_tests>"
    VERBATIM
)
add_test(NAME xuanhub_popup_service_tests COMMAND xuanhub_popup_service_tests)

add_executable(xuanhub_responsive_layout_tests
    ResponsiveLayoutEngineTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ResponsiveLayoutEngine.h
    ${CMAKE_SOURCE_DIR}/app/src/ResponsiveLayoutEngine.cpp
)
set_target_properties(xuanhub_responsive_layout_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_responsive_layout_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_responsive_layout_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::Test
)
add_custom_command(TARGET xuanhub_responsive_layout_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_responsive_layout_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_responsive_layout_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_responsive_layout_tests>"
    VERBATIM
)
add_test(NAME xuanhub_responsive_layout_tests COMMAND xuanhub_responsive_layout_tests)

add_executable(xuanhub_pybridge_protocol_tests
    PyBridgeProtocolTest.cpp
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.h
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.cpp
)
set_target_properties(xuanhub_pybridge_protocol_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_pybridge_protocol_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/pybridge
)
target_link_libraries(xuanhub_pybridge_protocol_tests PRIVATE Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_pybridge_protocol_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_pybridge_protocol_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_pybridge_protocol_tests>"
    VERBATIM
)
add_test(NAME xuanhub_pybridge_protocol_tests COMMAND xuanhub_pybridge_protocol_tests)

add_executable(xuanhub_python_dependency_spec_tests
    PythonDependencySpecTest.cpp
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PythonDependencySpec.h
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PythonDependencySpec.cpp
)
set_target_properties(xuanhub_python_dependency_spec_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_python_dependency_spec_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/pybridge
)
target_link_libraries(xuanhub_python_dependency_spec_tests PRIVATE Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_python_dependency_spec_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_python_dependency_spec_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_python_dependency_spec_tests>"
    VERBATIM
)
add_test(NAME xuanhub_python_dependency_spec_tests
    COMMAND xuanhub_python_dependency_spec_tests)

add_executable(xuanhub_plugin_model_tests
    PluginListModelTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginListModel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginListModel.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginFilterModel.h
    ${CMAKE_SOURCE_DIR}/app/src/ResponsiveLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ResponsiveLayoutEngine.h
)
set_target_properties(xuanhub_plugin_model_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_model_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_model_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::Test
)
add_custom_command(TARGET xuanhub_plugin_model_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_model_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_model_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_model_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_model_tests COMMAND xuanhub_plugin_model_tests)

add_executable(xuanhub_plugin_diagnostics_tests
    PluginDiagnosticsTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginDiagnostics.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginDiagnostics.cpp
)
set_target_properties(xuanhub_plugin_diagnostics_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_diagnostics_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
    ${CMAKE_SOURCE_DIR}/lib/barplugin/include
)
target_link_libraries(xuanhub_plugin_diagnostics_tests PRIVATE
    barplugin Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_plugin_diagnostics_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_diagnostics_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_diagnostics_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_diagnostics_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_diagnostics_tests
    COMMAND xuanhub_plugin_diagnostics_tests)

add_executable(xuanhub_plugin_changeset_tests
    PluginChangeSetTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginChangeSet.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginChangeSet.cpp
)
set_target_properties(xuanhub_plugin_changeset_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_changeset_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_changeset_tests PRIVATE Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_plugin_changeset_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_changeset_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_changeset_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_changeset_tests COMMAND xuanhub_plugin_changeset_tests)

add_executable(xuanhub_semantic_version_tests
    SemanticVersionTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
)
set_target_properties(xuanhub_semantic_version_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_semantic_version_tests PRIVATE ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(xuanhub_semantic_version_tests PRIVATE Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_semantic_version_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_semantic_version_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_semantic_version_tests>"
    VERBATIM
)
add_test(NAME xuanhub_semantic_version_tests COMMAND xuanhub_semantic_version_tests)

add_executable(xuanhub_plugin_manifest_tests
    PluginManifestTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PythonPluginManifest.h
    ${CMAKE_SOURCE_DIR}/app/src/PythonPluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginContractValidator.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginContractValidator.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
)
set_target_properties(xuanhub_plugin_manifest_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_manifest_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_manifest_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::Test
)
add_custom_command(TARGET xuanhub_plugin_manifest_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_manifest_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_manifest_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_manifest_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_manifest_tests COMMAND xuanhub_plugin_manifest_tests)

# 协议和插件发布工具只需可执行解释器，不需要开发头/库；找不到时不让核心 CTest 失效。
find_package(Python 3.12 QUIET COMPONENTS Interpreter)

add_executable(xuanhub_plugin_package_tests
    PluginPackageValidatorTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginPackageValidator.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginPackageValidator.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginPackageStore.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginPackageStore.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PythonPluginManifest.h
    ${CMAKE_SOURCE_DIR}/app/src/PythonPluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginContractValidator.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginContractValidator.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
)
set_target_properties(xuanhub_plugin_package_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_package_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_package_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::CorePrivate
    Qt6::Test
    bcrypt
)
if(Python_Interpreter_FOUND)
    target_compile_definitions(xuanhub_plugin_package_tests PRIVATE
        XUANHUB_TEST_PYTHON="${Python_EXECUTABLE}"
        XUANHUB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
add_custom_command(TARGET xuanhub_plugin_package_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_package_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_package_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_package_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_package_tests COMMAND xuanhub_plugin_package_tests)

add_executable(xuanhub_plugin_update_index_tests
    PluginUpdateIndexTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginUpdateIndex.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginUpdateIndex.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
)
set_target_properties(xuanhub_plugin_update_index_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_update_index_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(xuanhub_plugin_update_index_tests PRIVATE
    Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_plugin_update_index_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_update_index_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_update_index_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_update_index_tests
    COMMAND xuanhub_plugin_update_index_tests)

add_executable(xuanhub_plugin_update_service_tests
    PluginUpdateServiceTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginUpdateService.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginUpdateService.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginUpdateIndex.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginUpdateIndex.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginPackageValidator.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginPackageValidator.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PythonPluginManifest.h
    ${CMAKE_SOURCE_DIR}/app/src/PythonPluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginContractValidator.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginContractValidator.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
)
set_target_properties(xuanhub_plugin_update_service_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_update_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(xuanhub_plugin_update_service_tests PRIVATE
    barplugin Qt6::Core Qt6::CorePrivate Qt6::Network Qt6::Test bcrypt)
add_custom_command(TARGET xuanhub_plugin_update_service_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_update_service_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_update_service_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:xuanhub_plugin_update_service_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_update_service_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_update_service_tests
    COMMAND xuanhub_plugin_update_service_tests)

add_executable(xuanhub_plugin_runtime_tests
    PluginRuntimeTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginRuntime.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginRuntime.cpp
)
set_target_properties(xuanhub_plugin_runtime_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_runtime_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_runtime_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::Test
)
add_custom_command(TARGET xuanhub_plugin_runtime_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_runtime_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_runtime_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_runtime_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_runtime_tests COMMAND xuanhub_plugin_runtime_tests)

add_executable(xuanhub_plugin_dependency_tests
    PluginDependencyGraphTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginDependencyGraph.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginDependencyGraph.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
    ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
)
set_target_properties(xuanhub_plugin_dependency_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_dependency_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_dependency_tests PRIVATE
    barplugin
    Qt6::Core
    Qt6::Test
)
add_custom_command(TARGET xuanhub_plugin_dependency_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_dependency_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_dependency_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_dependency_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_dependency_tests COMMAND xuanhub_plugin_dependency_tests)

add_executable(xuanhub_eventbus_tests EventBusTest.cpp)
set_target_properties(xuanhub_eventbus_tests PROPERTIES AUTOMOC ON)
target_link_libraries(xuanhub_eventbus_tests PRIVATE barplugin Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_eventbus_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_eventbus_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_eventbus_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_eventbus_tests>"
    VERBATIM
)
add_test(NAME xuanhub_eventbus_tests COMMAND xuanhub_eventbus_tests)

add_executable(xuanhub_host_services_tests HostServicesTest.cpp)
set_target_properties(xuanhub_host_services_tests PROPERTIES AUTOMOC ON)
target_link_libraries(xuanhub_host_services_tests PRIVATE barplugin Qt6::Core Qt6::Test)
add_custom_command(TARGET xuanhub_host_services_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_host_services_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_host_services_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_host_services_tests>"
    VERBATIM
)
add_test(NAME xuanhub_host_services_tests COMMAND xuanhub_host_services_tests)

add_executable(xuanhub_plugin_host_services_tests
    PluginHostServicesTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginHostServices.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginHostServices.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginContextObject.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginContextObject.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SecretStore.h
    ${CMAKE_SOURCE_DIR}/app/src/SecretStore.cpp
)
set_target_properties(xuanhub_plugin_host_services_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_plugin_host_services_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_plugin_host_services_tests PRIVATE
    barplugin Qt6::Core Qt6::Gui Qt6::Test crypt32
)
add_custom_command(TARGET xuanhub_plugin_host_services_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_host_services_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_host_services_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:xuanhub_plugin_host_services_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_host_services_tests>"
    VERBATIM
)
add_test(NAME xuanhub_plugin_host_services_tests COMMAND xuanhub_plugin_host_services_tests)

if(Python_Interpreter_FOUND)
    add_test(NAME xuanhub_plugin_cli_tests
        COMMAND "${Python_EXECUTABLE}" -B -m unittest discover
            -s tests -p test_xuanhub_plugin_cli.py -v)
    set_tests_properties(xuanhub_plugin_cli_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "PYTHONUTF8=1;XUANHUB_PLUGIN_CONTRACT_PROBE=$<TARGET_FILE:xuanhub_plugin_contract_probe>")

    # 每个内置插件都通过同一公开 CLI；单独注册测试便于 CI 直接定位到具体插件。
    set(XUANHUB_BUILTIN_PLUGIN_DIRS
        plugins/clock
        plugins/pybridge
        plugins/sysinfo
        plugins/xuanhub_menu
        plugins_py/countdown
        plugins_py/heartrate
        plugins_py/hello
        plugins_py/intime
        plugins_py/todo
        plugins_py/usage_hub
    )
    foreach(plugin_dir IN LISTS XUANHUB_BUILTIN_PLUGIN_DIRS)
        string(REPLACE "/" "_" plugin_test_name "${plugin_dir}")
        add_test(NAME "xuanhub_validate_${plugin_test_name}"
            COMMAND "${Python_EXECUTABLE}" -B
                "${CMAKE_SOURCE_DIR}/scripts/xuanhub_plugin.py"
                validate "${CMAKE_SOURCE_DIR}/${plugin_dir}" --json)
    endforeach()

    add_test(NAME xuanhub_pyworker_tests
        COMMAND "${Python_EXECUTABLE}" -B -m unittest discover -s plugins_py/tests -v)
    set_tests_properties(xuanhub_pyworker_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(xuanhub_python_dependency_doctor_tests
        PythonDependencyDoctorTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PythonDependencySpec.h
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PythonDependencySpec.cpp
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PythonDependencyDoctor.h
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PythonDependencyDoctor.cpp
    )
    set_target_properties(xuanhub_python_dependency_doctor_tests PROPERTIES AUTOMOC ON)
    target_include_directories(xuanhub_python_dependency_doctor_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/pybridge
    )
    target_link_libraries(xuanhub_python_dependency_doctor_tests PRIVATE
        Qt6::Core Qt6::Test)
    add_custom_command(TARGET xuanhub_python_dependency_doctor_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_python_dependency_doctor_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_python_dependency_doctor_tests>"
        VERBATIM
    )
    add_test(NAME xuanhub_python_dependency_doctor_tests
        COMMAND xuanhub_python_dependency_doctor_tests)
    set_tests_properties(xuanhub_python_dependency_doctor_tests PROPERTIES
        ENVIRONMENT "XUANHUB_TEST_PYTHON=${Python_EXECUTABLE};XUANHUB_TEST_PYTHON_DOCTOR=${CMAKE_SOURCE_DIR}/plugins_py/dependency_doctor.py")

    add_executable(xuanhub_pyworker_viewmodel_tests
        PyWorkerViewModelTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyWorkerViewModel.h
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyWorkerViewModel.cpp
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.h
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginHostServices.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginHostServices.cpp
        ${CMAKE_SOURCE_DIR}/app/src/SecretStore.h
        ${CMAKE_SOURCE_DIR}/app/src/SecretStore.cpp
    )
    set_target_properties(xuanhub_pyworker_viewmodel_tests PROPERTIES AUTOMOC ON)
    target_include_directories(xuanhub_pyworker_viewmodel_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/pybridge
        ${CMAKE_SOURCE_DIR}/app/src
    )
    target_link_libraries(xuanhub_pyworker_viewmodel_tests PRIVATE
        barplugin
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Test
        crypt32
    )
    add_custom_command(TARGET xuanhub_pyworker_viewmodel_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        VERBATIM
    )
    add_test(NAME xuanhub_pyworker_viewmodel_tests COMMAND xuanhub_pyworker_viewmodel_tests)
    set_tests_properties(xuanhub_pyworker_viewmodel_tests PROPERTIES
        ENVIRONMENT "XUANHUB_TEST_PYTHON=${Python_EXECUTABLE};XUANHUB_TEST_WORKER=${CMAKE_SOURCE_DIR}/plugins_py/bridge_worker.py;XUANHUB_TEST_PYTHON_PLUGINS=${CMAKE_SOURCE_DIR}/plugins_py")

    add_executable(xuanhub_plugin_manager_tests
        PluginManagerTest.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginManager.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginManager.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginManifest.cpp
        ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.h
        ${CMAKE_SOURCE_DIR}/app/src/SemanticVersion.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginRuntime.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginRuntime.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginHostServices.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginHostServices.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginContextObject.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginContextObject.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginActionRegistry.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginActionRegistry.cpp
        ${CMAKE_SOURCE_DIR}/app/src/SecretStore.h
        ${CMAKE_SOURCE_DIR}/app/src/SecretStore.cpp
        ${CMAKE_SOURCE_DIR}/app/src/PluginDependencyGraph.h
        ${CMAKE_SOURCE_DIR}/app/src/PluginDependencyGraph.cpp
    )
    set_target_properties(xuanhub_plugin_manager_tests PROPERTIES AUTOMOC ON)
    target_include_directories(xuanhub_plugin_manager_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
    )
    target_link_libraries(xuanhub_plugin_manager_tests PRIVATE
        barplugin
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        crypt32
    )
    target_compile_definitions(xuanhub_plugin_manager_tests PRIVATE
        XUANHUB_VERSION="${PROJECT_VERSION}"
    )
    add_dependencies(xuanhub_plugin_manager_tests clock sysinfo xuanhub_menu pybridge)
    add_custom_command(TARGET xuanhub_plugin_manager_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_plugin_manager_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_plugin_manager_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:xuanhub_plugin_manager_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_plugin_manager_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/plugins_py"
            "$<TARGET_FILE_DIR:xuanhub_plugin_manager_tests>/plugins_py"
        VERBATIM
    )
    add_test(NAME xuanhub_plugin_manager_tests COMMAND xuanhub_plugin_manager_tests)
    set_tests_properties(xuanhub_plugin_manager_tests PROPERTIES
        ENVIRONMENT "XUANHUB_TEST_PLUGINS=${CMAKE_BINARY_DIR}/dist/$<CONFIG>/plugins")
endif()
