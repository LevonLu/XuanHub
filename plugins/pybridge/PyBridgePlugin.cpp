// PyBridgePlugin.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "PyBridgePlugin.h"
#include "PyProxyViewModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <pybind11/embed.h>
#include "barplugin/EventBus.h"

namespace py = pybind11;

namespace {
// 解释器是进程级单例。CPython 重复 init/finalize 不稳定，
// 因此热重载（shutdownAll → loadFrom）周期内保持存活、不 finalize；
// 进程退出由 OS 整体回收 —— 主动 finalize 在 pybind11 静态对象
// 析构顺序下反而易崩（plan.md §12 M6 决策）。
bool g_interpreterReady = false;

// Python 环境检测结果（进程内只检测一次）：无 Python 3.13 标准库时
// 不初始化解释器，Python 子插件整体停用，宿主与其余插件不受影响。
bool g_pythonChecked   = false;
bool g_pythonAvailable = false;
bool g_pyNotifySent    = false;    // 「未检测到 Python」提示只发一次

// 以 Lib/os.py 为地标校验候选目录是完整的 Python 标准库
bool hasStdlib(const QString& home) {
    return !home.isEmpty() && QDir(home).exists(QStringLiteral("Lib/os.py"));
}

// 用户自装的 python.org 3.13（注册表 InstallPath 默认值，HKCU 优先）
QString registryPythonHome() {
    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (const HKEY root : roots) {
        wchar_t buf[MAX_PATH] = {};
        DWORD size = sizeof(buf);
        if (RegGetValueW(root, L"Software\\Python\\PythonCore\\3.13\\InstallPath",
                         nullptr, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS
            && buf[0] != L'\0') {
            return QString::fromWCharArray(buf);
        }
    }
    return {};
}

// 读取子插件目录的 plugin.json；文件缺失/解析失败返回空对象
QJsonObject readPluginJson(const QDir& d) {
    QFile f(d.absoluteFilePath(QStringLiteral("plugin.json")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
} // namespace

xuanhub::PluginMetadata PyBridgePlugin::metadata() const {
    return {QStringLiteral("pybridge"), QStringLiteral("PyBridge"),
            QStringLiteral("0.1.0"), QStringLiteral("XuanHub")};
}

bool PyBridgePlugin::init(xuanhub::BarPluginContext* ctx) {
    m_ctx = *ctx;  // 拷贝保存（EventBus 等后续子插件能力用）

    // Python 环境检测（进程内一次）：找不到 Python 3.13 标准库就不初始化
    // 解释器（缺标准库初始化会触发 fatal error 拖垮宿主），Python 子插件
    // 整体停用、其余功能不受影响。定位优先级：
    //   内置 Lib/（Bundle 打包，压掉用户 PYTHONHOME）→ 用户 PYTHONHOME
    //   → 构建机烧入路径兜底（开发场景）→ 注册表 python.org 3.13（用户自装）
    if (!g_pythonChecked) {
        g_pythonChecked = true;
        // 调试钩子：强制走「无 Python 环境」路径（验收降级/提示行为用）
        if (qEnvironmentVariableIsSet("XUANHUB_DISABLE_PYTHON")) {
            qWarning() << "[PyBridge] XUANHUB_DISABLE_PYTHON set, python force-disabled";
        } else {
            const QString exeDir = QCoreApplication::applicationDirPath();
            if (hasStdlib(exeDir)) {
                qputenv("PYTHONHOME", exeDir.toUtf8());
                g_pythonAvailable = true;
                qDebug() << "[PyBridge] PYTHONHOME -> bundled" << exeDir;
            } else if (hasStdlib(qEnvironmentVariable("PYTHONHOME"))) {
                g_pythonAvailable = true;  // 用户显式指定，原样使用
                qDebug() << "[PyBridge] PYTHONHOME env ->" << qEnvironmentVariable("PYTHONHOME");
            } else if (hasStdlib(QStringLiteral(XUANHUB_PYTHON_HOME))) {
                qputenv("PYTHONHOME", XUANHUB_PYTHON_HOME);
                g_pythonAvailable = true;
                qDebug() << "[PyBridge] PYTHONHOME fallback ->" << XUANHUB_PYTHON_HOME;
            } else if (const QString reg = registryPythonHome(); hasStdlib(reg)) {
                // 显式 qputenv，不依赖 CPython getpath 的隐式注册表查找
                qputenv("PYTHONHOME", reg.toUtf8());
                g_pythonAvailable = true;
                qDebug() << "[PyBridge] PYTHONHOME registry ->" << reg;
            }
        }
        if (!g_pythonAvailable)
            qWarning() << "[PyBridge] no Python 3.13 stdlib found;"
                          " python sub-plugins disabled (install Python 3.13 to enable)";
    }

    if (g_pythonAvailable && !g_interpreterReady) {
        py::initialize_interpreter();
        // 关键：初始化后主线程默认持有 GIL，而它在 Qt 事件循环里从不进入
        // 解释器求值循环，永不主动让出 —— python 后台线程会饿死
        //（实测 worker 3 秒只跑到 1 次）。此处永久 release（有意泄漏，
        // 进程生命周期内不恢复）；此后一切 python 调用一律 gil_scoped_acquire。
        static auto* g_gilRelease = new py::gil_scoped_release();
        Q_UNUSED(g_gilRelease);
        g_interpreterReady = true;
        qInfo().noquote() << "[PyBridge] python" << Py_GetVersion() << "initialized";
    }

    // 无 Python 环境：经 EventBus 请求宿主弹一次提示（托盘订阅 app.notify；
    // 插件加载早于托盘事件循环，接收端需自行顺延到事件循环里再弹）
    if (!g_pythonAvailable && !g_pyNotifySent && m_ctx.eventBus) {
        g_pyNotifySent = true;
        m_ctx.eventBus->publish(QStringLiteral("app.notify"), {
            {QStringLiteral("title"), QStringLiteral("XuanHub")},
            {QStringLiteral("message"), QStringLiteral(
                "未检测到 Python 3.13 环境，Python 插件已停用。"
                "安装 Python 3.13（python.org）后重启即可启用。")},
        });
    }
    return true;
}

QObject* PyBridgePlugin::viewModel() { return nullptr; }

QUrl PyBridgePlugin::qmlUrl() const { return QUrl(); }

void PyBridgePlugin::shutdown() {
    // 解释器不随插件 shutdown finalize（见文件头注释）
    qDebug() << "[PyBridge] shutdown:" << m_vms.size() << "sub-plugin VM(s) (interpreter kept alive)";
    qDeleteAll(m_vms);
    m_vms.clear();
    m_dirs.clear();
}

QStringList PyBridgePlugin::subPluginIds() {
    QStringList ids;
    m_dirs.clear();
    if (!g_pythonAvailable) return ids;  // 无 Python 环境：不枚举子插件（整体停用）
    // 与 C++ 插件的 plugins/ 平级：<exeDir>/plugins_py/
    const QDir base(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins_py"));
    if (!base.exists()) {
        qDebug() << "[PyBridge] plugins_py not found:" << base.path();
        return ids;
    }
    for (const QString& sub : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir d(base.filePath(sub));
        // 发现阶段只读 plugin.json，不 import python 代码（权威元数据在 json 里）
        const QJsonObject obj = readPluginJson(d);
        const QString id = obj.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            qWarning() << "[PyBridge] skip" << d.path() << ": plugin.json 缺失或无 id";
            continue;
        }
        if (!d.exists(QStringLiteral("main.py"))) {
            qWarning() << "[PyBridge] skip" << id << ": main.py 缺失";
            continue;
        }
        // 扫阶段即查重并给出双方路径（PluginManager 的去重是兜底，
        // 但那时 m_dirs 已被覆盖，日志看不出冲突发生在哪两个目录之间）
        if (m_dirs.contains(id)) {
            qWarning() << "[PyBridge] duplicate sub-plugin id" << id << ":"
                       << d.path() << "vs" << m_dirs.value(id) << "-> keep first, skip latter";
            continue;
        }
        ids << id;
        m_dirs.insert(id, d.absolutePath());
        qDebug() << "[PyBridge] found sub-plugin:" << id << "@" << d.path();
    }
    return ids;
}

xuanhub::SubPluginInfo PyBridgePlugin::createSubPlugin(const QString& id, const QVariantMap& cfg) {
    xuanhub::SubPluginInfo info;
    const QString dirPath = m_dirs.value(id);
    if (dirPath.isEmpty()) {
        qWarning() << "[PyBridge] createSubPlugin: unknown id" << id << "(not from subPluginIds)";
        return info;
    }
    const QDir d(dirPath);
    const QJsonObject obj = readPluginJson(d);
    const auto qmls = d.entryList({QStringLiteral("*.qml")}, QDir::Files);
    if (qmls.isEmpty()) {
        qWarning() << "[PyBridge] skip" << id << ": 目录内无 .qml";
        return info;
    }
    auto* vm = new PyProxyViewModel(id, dirPath, cfg, this);
    if (!vm->load()) {
        delete vm;  // 脚本语法错误/缺 update()：记日志后跳过，不影响宿主与其他插件
        return info;
    }
    m_vms.append(vm);
    info.meta = {id,
                 obj.value(QStringLiteral("name")).toString(id),
                 obj.value(QStringLiteral("version")).toString(),
                 obj.value(QStringLiteral("author")).toString()};
    info.viewModel = vm;
    info.qmlFilePath = qmls.first();
    info.dirPath = dirPath;
    return info;
}
