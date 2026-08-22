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
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    crypt32
)

# CTest 从 build/tests/Release 直接启动，不经过主程序的 windeployqt；把最小 Qt
# 运行时放到测试 exe 同目录，避免开发机 PATH 偶然包含 Qt 才能跑的假绿测试。
add_custom_command(TARGET xuanhub_config_tests POST_BUILD
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

add_executable(xuanhub_plugin_model_tests
    PluginListModelTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginListModel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginListModel.h
    ${CMAKE_SOURCE_DIR}/app/src/PluginFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/PluginFilterModel.h
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

# 协议回归只需可执行解释器，不需要开发头/库；找不到时不让核心 CTest 失效。
find_package(Python 3.12 QUIET COMPONENTS Interpreter)
if(Python_Interpreter_FOUND)
    add_test(NAME xuanhub_pyworker_tests
        COMMAND "${Python_EXECUTABLE}" -B -m unittest discover -s plugins_py/tests -v)
    set_tests_properties(xuanhub_pyworker_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(xuanhub_pyworker_viewmodel_tests
        PyWorkerViewModelTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyWorkerViewModel.h
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyWorkerViewModel.cpp
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
        ENVIRONMENT "XUANHUB_TEST_PYTHON=${Python_EXECUTABLE};XUANHUB_TEST_WORKER=${CMAKE_SOURCE_DIR}/plugins_py/bridge_worker.py")

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
    add_dependencies(xuanhub_plugin_manager_tests clock xuanhub_menu pybridge)
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
