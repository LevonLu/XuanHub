# PF-11 原生隔离 Worker 技术原型；只参与开发/CI，不随当前便携包发布。
add_executable(XuanHubPluginHost
    NativeCounterWorker.cpp
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.h
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.cpp
)
target_include_directories(XuanHubPluginHost PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/pybridge)
target_link_libraries(XuanHubPluginHost PRIVATE Qt6::Core)
set_target_properties(XuanHubPluginHost PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}")
add_custom_command(TARGET XuanHubPluginHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:XuanHubPluginHost>"
    VERBATIM)

add_executable(xuanhub_native_worker_isolation_tests
    NativeWorkerIsolationTest.cpp
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.h
    ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyBridgeProtocol.cpp
)
set_target_properties(xuanhub_native_worker_isolation_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_native_worker_isolation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/pybridge)
target_compile_definitions(xuanhub_native_worker_isolation_tests PRIVATE
    XUANHUB_NATIVE_WORKER="$<TARGET_FILE:XuanHubPluginHost>")
target_link_libraries(xuanhub_native_worker_isolation_tests PRIVATE
    Qt6::Core Qt6::Test psapi)
add_dependencies(xuanhub_native_worker_isolation_tests XuanHubPluginHost)
add_custom_command(TARGET xuanhub_native_worker_isolation_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>"
        "$<TARGET_FILE_DIR:xuanhub_native_worker_isolation_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:xuanhub_native_worker_isolation_tests>"
    VERBATIM)
add_test(NAME xuanhub_native_worker_isolation_tests
    COMMAND xuanhub_native_worker_isolation_tests)
