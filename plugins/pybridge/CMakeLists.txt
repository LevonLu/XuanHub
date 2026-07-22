# PyBridge 插件 —— Python 桥接宿主（M6），内嵌 CPython，承载 plugins_py/ 子插件。
#
# 稳定 ABI（M12 → nanobind 迁移）：Py_LIMITED_API=0x030C0000 + 链接 python3.lib，
# 一份产物通吃 Python 3.12+（3.12/3.13/3.14…），不再与构建机 Python 次版本绑定。
# 构建期 find_package 的 3.12 只是对构建机开发包的下限要求；运行时下限 3.12
# 是 nanobind 稳定 ABI 的硬性要求（依赖 3.12 的 PyType_FromMetaclass() 与
# vectorcall 的 limited API 绑定）。
#
# 双 DLL 结构（LNK1194 规避，详见 PyBackendApi.h 头注释）：
#   pybackend.dll（exe 旁）：唯一触碰 CPython/nanobind 的模块，静态导入
#       python3.dll；放 exe 根目录，天然避开 PluginManager 的插件目录扫描。
#   pybridge.dll（plugins/pybridge/）：Qt 插件外壳 + Python 环境探测/DLL 预装，
#       /DELAYLOAD:pybackend.dll，只经 PyBackendApi.h 的窄函数接口调后端。

# 本机 python.org 安装，FindPython 经注册表发现；
# Development 提供 Python::Module（nanobind-config 前置检查要求），
# Development.SABIModule 提供 Python::SABIModule（python3.lib + 头文件目录）
find_package(Python 3.12 REQUIRED COMPONENTS Interpreter Development Development.SABIModule)

get_filename_component(_python_home "${Python_EXECUTABLE}" DIRECTORY)

# nanobind：third_party/nanobind 本地副本（v2.14.0，含 ext/robin_map）
find_package(nanobind REQUIRED
    PATHS "${CMAKE_SOURCE_DIR}/third_party/nanobind/cmake" NO_DEFAULT_PATH)

# 复刻 nanobind_add_module(... STABLE_ABI) 的内部动作（nanobind-config.cmake），
# 但保留自己的 DLL 目标与文件名
nanobind_build_library(nanobind-static-abi3)   # 名字含 -abi3 → PUBLIC 链接 Python::SABIModule
target_compile_definitions(nanobind-static-abi3 PUBLIC Py_LIMITED_API=0x030C0000)

# ---- 后端：pybackend.dll（exe 旁，唯一链接 python3.lib 的模块）----
add_library(pybackend SHARED
    PyBackendApi.h
    PyBackend.cpp
    PyProxyViewModel.h
    PyProxyViewModel.cpp
)

set_target_properties(pybackend PROPERTIES AUTOMOC ON)

# nanobind-static-abi3 PUBLIC 传递 Py_LIMITED_API=0x030C0000 与
# Python::SABIModule（python3.lib 稳定 ABI 导入库，版本无关）
target_link_libraries(pybackend PRIVATE
    Qt6::Core
    nanobind-static-abi3
)

nanobind_compile_options(pybackend)

# XUANHUB_PYBACKEND_BUILD：PyBackendApi.h 导出宏切换；
# QT_NO_KEYWORDS：Python 头 PyType_Spec.slots 与 Qt slots 宏冲突，
# 本目录一律用 Q_SIGNALS/Q_EMIT 显式写法
target_compile_definitions(pybackend PRIVATE XUANHUB_PYBACKEND_BUILD QT_NO_KEYWORDS)

target_include_directories(pybackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 输出 pybackend.dll 到 exe 旁（不放 plugins/pybridge/：PluginManager 会扫描
# 该目录所有 *.dll，后端 DLL 不是 Qt 插件，会被 IID 校验反复告警）
set_target_properties(pybackend PROPERTIES
    PREFIX ""
    OUTPUT_NAME pybackend
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}"
)

add_custom_command(TARGET pybackend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:pybackend>"
        "${XUANHUB_BIN_DIR}/pybackend.dll"
)

# ---- 外壳：pybridge.dll（Qt 插件，不触碰任何 CPython/nanobind 符号）----
add_library(pybridge MODULE
    PyBridgePlugin.h
    PyBridgePlugin.cpp
    PyBackendApi.h
)

set_target_properties(pybridge PROPERTIES AUTOMOC ON)

target_link_libraries(pybridge PRIVATE
    barplugin
    Qt6::Core
    pybackend
    delayimp             # /DELAYLOAD 支持库
)

# pybackend.dll 延迟加载：它静态导入 python3.dll（用户安装目录不在 PATH），
# 必须延迟到 init 完成解释器 DLL 预装 + 显式 LoadLibrary 自检之后
#（见 PyBridgePlugin.cpp）；仅函数导入，/DELAYLOAD 合法
target_link_options(pybridge PRIVATE /DELAYLOAD:pybackend.dll)

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
    # Python 子插件目录整树部署（与 plugins/ 平级；改 .py/.qml 后重编即同步）
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/plugins_py"
        "${XUANHUB_BIN_DIR}/plugins_py"
)
