# PyBridge 插件（MODULE → pybridge.dll）—— Python 桥接宿主（M6）
# 内嵌 CPython 解释器，承载 plugins_py/ 下的 Python 子插件。

# 本机 python.org 安装，FindPython 经注册表发现
find_package(Python 3.13 REQUIRED COMPONENTS Interpreter Development)

# python<XY>.dll 拷到 exe 旁：pybridge.dll 位于 plugins/pybridge/ 子目录，
# 其依赖解析时 exe 目录总在 DLL 搜索路径内，插件子目录不在
get_filename_component(_python_home "${Python_EXECUTABLE}" DIRECTORY)
set(_python_dll "${_python_home}/python${Python_VERSION_MAJOR}${Python_VERSION_MINOR}.dll")

# pybind11：优先 third_party/pybind11 本地副本（网络受限时手动下载放入）；
# 否则 FetchContent 拉取。也可用 -DFETCHCONTENT_SOURCE_DIR_PYBIND11=<路径> 显式指定。
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/pybind11/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_PYBIND11 "${CMAKE_SOURCE_DIR}/third_party/pybind11")
endif()
include(FetchContent)
FetchContent_Declare(pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v3.0.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pybind11)

add_library(pybridge MODULE
    PyBridgePlugin.h
    PyBridgePlugin.cpp
    PyProxyViewModel.h
    PyProxyViewModel.cpp
)

set_target_properties(pybridge PROPERTIES AUTOMOC ON)

target_link_libraries(pybridge PRIVATE
    barplugin
    Qt6::Core
    pybind11::embed
)

target_include_directories(pybridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Python 头文件中 PyType_Spec.slots 字段与 Qt 的 slots 宏冲突；
# pybridge 内一律用 Q_SIGNALS/Q_EMIT 显式写法，不用 signals/slots/emit 关键字
target_compile_definitions(pybridge PRIVATE QT_NO_KEYWORDS)

# 构建机的 Python 安装目录，烧进 DLL 作为嵌入式运行时找标准库的兜底
#（embeddable 分发时 Lib 在 exe 旁，地标搜索命中，该兜底自动失效）
target_compile_definitions(pybridge PRIVATE XUANHUB_PYTHON_HOME="${_python_home}")

# 输出 pybridge.dll（无 lib 前缀）到运行时插件目录
set_target_properties(pybridge PROPERTIES
    PREFIX ""
    OUTPUT_NAME pybridge
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/pybridge"
)

add_custom_command(TARGET pybridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:pybridge>"
        "${XUANHUB_BIN_DIR}/plugins/pybridge/pybridge.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
        "${XUANHUB_BIN_DIR}/plugins/pybridge/metadata.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_python_dll}"
        "${XUANHUB_BIN_DIR}/"
    # Python 子插件目录整树部署（与 plugins/ 平级；改 .py/.qml 后重编即同步）
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/plugins_py"
        "${XUANHUB_BIN_DIR}/plugins_py"
)
