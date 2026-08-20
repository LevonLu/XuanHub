# tests/CMakeLists.txt — 核心配置与密钥持久化的轻量回归测试
add_executable(xuanhub_config_tests
    ConfigServiceTest.cpp
    ${CMAKE_SOURCE_DIR}/app/src/ConfigService.h
    ${CMAKE_SOURCE_DIR}/app/src/ConfigService.cpp
    ${CMAKE_SOURCE_DIR}/app/src/SecretStore.h
    ${CMAKE_SOURCE_DIR}/app/src/SecretStore.cpp
)

set_target_properties(xuanhub_config_tests PROPERTIES AUTOMOC ON)
target_include_directories(xuanhub_config_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)
target_link_libraries(xuanhub_config_tests PRIVATE
    Qt6::Core
    Qt6::Test
    crypt32
)

# CTest 从 build/tests/Release 直接启动，不经过主程序的 windeployqt；把最小 Qt
# 运行时放到测试 exe 同目录，避免开发机 PATH 偶然包含 Qt 才能跑的假绿测试。
add_custom_command(TARGET xuanhub_config_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_config_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_config_tests>"
    VERBATIM
)

add_test(NAME xuanhub_config_tests COMMAND xuanhub_config_tests)

# 协议回归只需可执行解释器，不需要开发头/库；找不到时不让核心 CTest 失效。
find_package(Python 3.12 QUIET COMPONENTS Interpreter)
if(Python_Interpreter_FOUND)
    add_test(NAME xuanhub_pyworker_tests
        COMMAND "${Python_EXECUTABLE}" -B -m unittest plugins_py/tests/test_bridge_worker.py -v)
    set_tests_properties(xuanhub_pyworker_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(xuanhub_pyworker_viewmodel_tests
        PyWorkerViewModelTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyWorkerViewModel.h
        ${CMAKE_SOURCE_DIR}/plugins/pybridge/PyWorkerViewModel.cpp
    )
    set_target_properties(xuanhub_pyworker_viewmodel_tests PROPERTIES AUTOMOC ON)
    target_include_directories(xuanhub_pyworker_viewmodel_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/pybridge
    )
    target_link_libraries(xuanhub_pyworker_viewmodel_tests PRIVATE
        barplugin
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    add_custom_command(TARGET xuanhub_pyworker_viewmodel_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:barplugin>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:xuanhub_pyworker_viewmodel_tests>"
        VERBATIM
    )
    add_test(NAME xuanhub_pyworker_viewmodel_tests COMMAND xuanhub_pyworker_viewmodel_tests)
    set_tests_properties(xuanhub_pyworker_viewmodel_tests PROPERTIES
        ENVIRONMENT "XUANHUB_TEST_PYTHON=${Python_EXECUTABLE};XUANHUB_TEST_WORKER=${CMAKE_SOURCE_DIR}/plugins_py/bridge_worker.py")
endif()
