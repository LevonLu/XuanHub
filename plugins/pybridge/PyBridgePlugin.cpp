// PyBridgePlugin.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "PyBridgePlugin.h"
#include "PyBackendApi.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include "barplugin/EventBus.h"

namespace {
// 解释器是进程级单例。CPython 重复 init/finalize 不稳定，
// 因此热重载（shutdownAll → loadFrom）周期内保持存活、不 finalize；
// 进程退出由 OS 整体回收 —— 主动 finalize 在绑定库静态对象
// 析构顺序下反而易崩（M6 决策记录）。
// 注：本 DLL（pybridge）不触碰任何 CPython/nanobind 符号——nanobind
// 以数据符号导入 python3.dll，无法 /DELAYLOAD（LNK1194），故解释器
// 初始化（裸 Py_Initialize，nanobind 官方不支持嵌入）与代理 VM 实现
// 整体收进 exe 旁的 pybackend.dll，经 PyBackendApi.h 的窄函数接口调用。
// Python 环境检测结果（进程内只检测一次）：无 Python 3.12+ 标准库时
// 不初始化解释器，Python 子插件整体停用，宿主与其余插件不受影响。
// 注意不能再用文件级静态量存：pybridge.dll 每次热重载都被 unload，静态量
// 随之复位（曾导致每次保存配置都重新检测 + 重复弹"未检测到 Python"气泡）。
// qApp 归 exe 所有、生命周期 = 进程，检测结果存它的动态属性。
bool g_interpreterReady = false;

QCoreApplication* hostApp() { return QCoreApplication::instance(); }

bool pyChecked() { return hostApp() && hostApp()->property("xuanhub.pyChecked").toBool(); }
void setPyChecked() { if (hostApp()) hostApp()->setProperty("xuanhub.pyChecked", true); }

bool pyAvailable() { return hostApp() && hostApp()->property("xuanhub.pyAvailable").toBool(); }
void setPyAvailable(bool v) { if (hostApp()) hostApp()->setProperty("xuanhub.pyAvailable", v); }

QString pyHome() {
    return hostApp() ? hostApp()->property("xuanhub.pyHome").toString() : QString();
}
void setPyHome(const QString& h) { if (hostApp()) hostApp()->setProperty("xuanhub.pyHome", h); }

// 「未检测到 Python」提示每次进程只发一次
bool pyNotified() { return hostApp() && hostApp()->property("xuanhub.pyNotified").toBool(); }
void setPyNotified() { if (hostApp()) hostApp()->setProperty("xuanhub.pyNotified", true); }

// qputenv("PYTHONHOME") 前记录原值：存在性单独用 recorded 标记表达
//（空串也是合法原值，且属性值为无效 QVariant 等价于未设置，不能靠
// toString().isEmpty() 区分「原本未设置」）。存 qApp 动态属性原因见文件头
void recordPythonHomeEnv() {
    QCoreApplication* app = hostApp();
    if (!app || app->property("xuanhub.pyHomeEnvRecorded").toBool()) return;
    app->setProperty("xuanhub.pyHomeEnvRecorded", true);
    if (qEnvironmentVariableIsSet("PYTHONHOME"))
        app->setProperty("xuanhub.pyHomeEnvOrig", qEnvironmentVariable("PYTHONHOME"));
}

// 恢复 init 期临时设置的 PYTHONHOME：CPython 只在初始化路径解析阶段读它，
// 之后模块搜索路径已固化进 sys.path，运行期不再读取；不恢复的话该变量会
// 被 launcher 等插件启动的子进程继承（Bundle 打包时子进程拿到内置裁剪版
// 路径必报错）。一次性：恢复后清标记，避免热重载重复覆盖用户后来改的值
void restorePythonHomeEnv() {
    QCoreApplication* app = hostApp();
    if (!app || !app->property("xuanhub.pyHomeEnvRecorded").toBool()) return;
    app->setProperty("xuanhub.pyHomeEnvRecorded", false);
    const QVariant orig = app->property("xuanhub.pyHomeEnvOrig");
    if (orig.isValid())
        qputenv("PYTHONHOME", orig.toString().toUtf8());
    else
        qunsetenv("PYTHONHOME");   // 原本未设置：撤掉，恢复未设置状态
}

// 从 home 下的 python3<minor>.dll 文件名判定次版本号（不启动进程）；
// python3.dll 是稳定 ABI 转发器、无版本号，跳过。找不到返回 -1。
int pythonMinorVersion(const QString& home) {
    if (home.isEmpty()) return -1;
    const QDir d(home);
    const auto dlls = d.entryList({QStringLiteral("python3*.dll")}, QDir::Files);
    for (const QString& f : dlls) {
        const QRegularExpression re(QStringLiteral("^python3(\\d+)\\.dll$"),
                                    QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(f);
        if (m.hasMatch()) return m.captured(1).toInt();
    }
    return -1;
}

// 校验候选目录是可用的 Python ≥3.12 安装：版本 DLL + Lib/os.py 地标
bool hasStdlib(const QString& home) {
    return pythonMinorVersion(home) >= 12
        && QDir(home).exists(QStringLiteral("Lib/os.py"));
}

// 注册表枚举（python.org 安装器写入）：Software\Python\PythonCore\<ver>\InstallPath，
// HKCU→HKLM 全版本枚举，取 ≥3.12 中最高者
QString registryPythonHome() {
    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    QString best;
    int bestMinor = 11;  // 下限 3.12
    for (const HKEY root : roots) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(root, L"Software\\Python\\PythonCore", 0, KEY_READ, &hk)
            != ERROR_SUCCESS)
            continue;
        wchar_t name[64];
        for (DWORD i = 0;; ++i) {
            DWORD len = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(hk, i, name, &len, nullptr, nullptr, nullptr, nullptr)
                != ERROR_SUCCESS)
                break;
            // 版本键形如 "3.13"（32 位安装带 "-32" 后缀，剥掉再比）
            const QString v = QString::fromWCharArray(name);
            const auto parts = v.split(QLatin1Char('.'));
            if (parts.size() < 2 || parts[0] != QLatin1String("3")) continue;
            bool ok = false;
            const int minor = parts[1].split(QLatin1Char('-')).first().toInt(&ok);
            if (!ok || minor <= bestMinor) continue;
            wchar_t buf[MAX_PATH] = {};
            DWORD size = sizeof(buf);
            const QString sub = QStringLiteral("Software\\Python\\PythonCore\\") + v
                              + QStringLiteral("\\InstallPath");
            if (RegGetValueW(root, reinterpret_cast<LPCWSTR>(sub.utf16()),
                             nullptr, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS
                && buf[0] != L'\0') {
                best = QString::fromWCharArray(buf);
                bestMinor = minor;
            }
        }
        RegCloseKey(hk);
    }
    return best;
}

// py launcher（python.org 安装器附带）：`py -3` 选最高的 3.x，打印次版本与前缀
QString pyLauncherHome() {
    QProcess p;
    p.start(QStringLiteral("py"), {QStringLiteral("-3"), QStringLiteral("-c"),
        QStringLiteral("import sys; print(sys.version_info[1]); print(sys.prefix)")});
    if (!p.waitForFinished(3000) || p.exitCode() != 0) return {};
    const auto lines = QString::fromUtf8(p.readAllStandardOutput())
                           .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return {};
    bool ok = false;
    const int minor = lines[0].trimmed().toInt(&ok);
    if (!ok || minor < 12) return {};
    return lines[1].trimmed();
}

// PATH 上的 python / python3（conda、uv、手动加 PATH 的 python.org 安装等）。
// WindowsApps 的 Store 占位 stub 没有 Lib/os.py，会被 hasStdlib 自然滤掉
QString pathPythonHome() {
    for (const char* exe : {"python", "python3"}) {
        const QString p = QStandardPaths::findExecutable(QLatin1String(exe));
        if (!p.isEmpty()) return QFileInfo(p).absolutePath();
    }
    return {};
}

// 显式全路径预装解释器 DLL：用户安装目录通常不在 PATH，
// 直接 LoadLibrary("python3.dll") 会失败。先装版本化 python3XX.dll，
// 再装 python3.dll——其导出转发到前者，按名命中已加载模块，不依赖搜索路径。
// 用 LoadLibraryExW + LOAD_WITH_ALTERED_SEARCH_PATH（而非 LoadLibraryW）：
// 标准搜索顺序解析 DLL 依赖时不含被加载 DLL 所在目录，python3XX.dll 依赖的
// vcruntime140.dll 等就在 Python 安装目录里，用户机器该目录通常不在 PATH。
// 返回两个 DLL 是否都加载成功（架构不匹配等会失败）。
bool preloadPythonDlls(const QString& home) {
    const QDir d(home);
    bool versioned = false;
    const auto dlls = d.entryList({QStringLiteral("python3*.dll")}, QDir::Files);
    for (const QString& f : dlls) {
        if (f.compare(QStringLiteral("python3.dll"), Qt::CaseInsensitive) == 0) continue;
        const QString full = d.absoluteFilePath(f);
        if (LoadLibraryExW(reinterpret_cast<LPCWSTR>(full.utf16()), nullptr,
                           LOAD_WITH_ALTERED_SEARCH_PATH))
            versioned = true;
    }
    const QString sabi = d.absoluteFilePath(QStringLiteral("python3.dll"));
    const bool sabiOk = LoadLibraryExW(reinterpret_cast<LPCWSTR>(sabi.utf16()), nullptr,
                                       LOAD_WITH_ALTERED_SEARCH_PATH) != nullptr;
    return versioned && sabiOk;
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

    // Python 环境检测（进程内一次）：找不到 Python 3.12+ 标准库就不初始化
    // 解释器（缺标准库初始化会触发 fatal error 拖垮宿主），Python 子插件
    // 整体停用、其余功能不受影响。定位优先级：
    //   内置 Lib/（Bundle 打包，压掉用户 PYTHONHOME）→ 用户 PYTHONHOME
    //   → 构建机烧入路径兜底（开发场景）→ 注册表枚举（≥3.12 取最高）
    //   → py launcher → PATH
    if (!pyChecked()) {
        setPyChecked();
        // 调试钩子：强制走「无 Python 环境」路径（验收降级/提示行为用）
        if (qEnvironmentVariableIsSet("XUANHUB_DISABLE_PYTHON")) {
            qWarning() << "[PyBridge] XUANHUB_DISABLE_PYTHON set, python force-disabled";
        } else {
            const QString exeDir = QCoreApplication::applicationDirPath();
            struct Cand { QString home; const char* tag; };
            const Cand cands[] = {
                { exeDir, "bundled" },
                { qEnvironmentVariable("PYTHONHOME"), "env" },
                { QStringLiteral(XUANHUB_PYTHON_HOME), "fallback" },
                { registryPythonHome(), "registry" },
                { pyLauncherHome(), "py-launcher" },
                { pathPythonHome(), "PATH" },
            };
            for (const auto& c : cands) {
                if (!hasStdlib(c.home)) continue;
                // 显式 qputenv，不依赖 CPython getpath 的隐式查找。
                // 先记原值：解释器初始化完成后即恢复（见 restorePythonHomeEnv），
                // 避免进程全局环境污染插件启动的子进程
                recordPythonHomeEnv();
                qputenv("PYTHONHOME", c.home.toUtf8());
                setPyHome(c.home);
                setPyAvailable(true);
                qDebug() << "[PyBridge] PYTHONHOME" << c.tag << "->" << c.home;
                break;
            }
        }
        if (!pyAvailable())
            qWarning() << "[PyBridge] no Python 3.12+ stdlib found;"
                          " python sub-plugins disabled (install Python 3.12+ to enable)";
    }

    if (pyAvailable() && !g_interpreterReady) {
        // pybackend.dll 静态导入 python3.dll（用户安装目录通常不在 PATH）：
        // 必须先按发现的目录全路径预装解释器 DLL，再显式加载 pybackend.dll
        //（pybridge 侧 /DELAYLOAD:pybackend.dll，此处 LoadLibrary 命中后
        // 后续对导出函数的延迟加载调用即安全），任一步失败按无环境降级
        if (!preloadPythonDlls(pyHome())) {
            qWarning() << "[PyBridge] failed to load python3.dll from" << pyHome()
                       << "(arch mismatch?) - python sub-plugins disabled";
            setPyAvailable(false);
        } else if (!LoadLibraryW(reinterpret_cast<LPCWSTR>(
                       (QCoreApplication::applicationDirPath()
                        + QStringLiteral("/pybackend.dll")).utf16()))) {
            qWarning() << "[PyBridge] failed to load pybackend.dll"
                          " - python sub-plugins disabled";
            setPyAvailable(false);
        } else if (!xb_py_initialize()) {
            setPyAvailable(false);
        } else {
            g_interpreterReady = true;
        }
        // 无论成败都恢复 PYTHONHOME：成功时路径已固化进 sys.path，CPython
        // 运行期不再读它；失败时更不应把临时值留给整个进程及其子进程
        restorePythonHomeEnv();
    }

    // 无 Python 环境：经 EventBus 请求宿主弹一次提示（托盘订阅 app.notify；
    // 插件加载早于托盘事件循环，接收端需自行顺延到事件循环里再弹）。
    // pyNotified 存 qApp（见文件头），跨热重载保持「每次进程只发一次」
    if (!pyAvailable() && !pyNotified() && m_ctx.eventBus) {
        setPyNotified();
        m_ctx.eventBus->publish(QStringLiteral("app.notify"), {
            {QStringLiteral("title"), QStringLiteral("XuanHub")},
            {QStringLiteral("message"), QStringLiteral(
                "未检测到 Python 3.12+ 环境，Python 插件已停用。"
                "安装 Python（python.org）后重启即可启用。")},
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
    if (!pyAvailable()) return ids;  // 无 Python 环境：不枚举子插件（整体停用）
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
    auto qmls = d.entryList({QStringLiteral("*.qml")}, QDir::Files);
    // settingsQml 是设置窗口的自绘页、不是 bar 视图：entryList 按字母序，
    // 不排除时命名排序在前的 Settings 页会被 first() 误选为视图
    const QString settingsQml = obj.value(QStringLiteral("settingsQml")).toString();
    if (!settingsQml.isEmpty())
        qmls.removeAll(QFileInfo(settingsQml).fileName());
    if (qmls.isEmpty()) {
        qWarning() << "[PyBridge] skip" << id << ": 目录内无 .qml";
        return info;
    }
    QObject* vm = xb_py_vm_load(id, dirPath, cfg, this);
    if (!vm) return info;  // 脚本语法错误/缺 update()：工厂内已记日志，跳过不影响其他插件
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
