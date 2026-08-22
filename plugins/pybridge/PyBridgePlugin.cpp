// PyBridgePlugin.cpp — Python 子插件发现、运行时探测与隔离 worker 工厂
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "PyBridgePlugin.h"
#include "PyWorkerViewModel.h"

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
// 运行时探测结果放 qApp 动态属性：pybridge.dll 热重载会被 unload，文件静态变量会复位。
QCoreApplication* hostApp() { return QCoreApplication::instance(); }

bool pyChecked() { return hostApp() && hostApp()->property("xuanhub.pyChecked").toBool(); }
void setPyChecked() { if (hostApp()) hostApp()->setProperty("xuanhub.pyChecked", true); }

bool pyAvailable() { return hostApp() && hostApp()->property("xuanhub.pyAvailable").toBool(); }
void setPyAvailable(bool value) { if (hostApp()) hostApp()->setProperty("xuanhub.pyAvailable", value); }

QString pyExecutable() {
    return hostApp() ? hostApp()->property("xuanhub.pyExecutable").toString() : QString();
}
void setPyExecutable(const QString& executable) {
    if (hostApp()) hostApp()->setProperty("xuanhub.pyExecutable", executable);
}

bool pyNotified() { return hostApp() && hostApp()->property("xuanhub.pyNotified").toBool(); }
void setPyNotified() { if (hostApp()) hostApp()->setProperty("xuanhub.pyNotified", true); }

int pythonMinorVersion(const QString& home) {
    if (home.isEmpty()) return -1;
    const QRegularExpression pattern(QStringLiteral("^python3(\\d+)\\.dll$"),
                                     QRegularExpression::CaseInsensitiveOption);
    const QDir directory(home);
    for (const QString& file : directory.entryList({QStringLiteral("python3*.dll")}, QDir::Files)) {
        const auto match = pattern.match(file);
        if (match.hasMatch()) return match.captured(1).toInt();
    }
    return -1;
}

QString pythonExecutableAt(const QString& home) {
    const QDir directory(home);
    for (const QString& name : {QStringLiteral("python.exe"), QStringLiteral("python3.exe")}) {
        const QString candidate = directory.absoluteFilePath(name);
        if (QFileInfo(candidate).isFile()) return candidate;
    }
    return {};
}

// worker 直接运行 python.exe，因此除标准库地标外还必须有可执行解释器。
bool hasRuntime(const QString& home) {
    return pythonMinorVersion(home) >= 12
        && QDir(home).exists(QStringLiteral("Lib/os.py"))
        && !pythonExecutableAt(home).isEmpty();
}

QString registryPythonHome() {
    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    QString best;
    int bestMinor = 11;
    for (const HKEY root : roots) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, L"Software\\Python\\PythonCore", 0, KEY_READ, &key)
            != ERROR_SUCCESS)
            continue;
        wchar_t name[64];
        for (DWORD index = 0;; ++index) {
            DWORD length = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr)
                != ERROR_SUCCESS)
                break;
            const QString version = QString::fromWCharArray(name);
            const auto parts = version.split(QLatin1Char('.'));
            if (parts.size() < 2 || parts[0] != QLatin1String("3")) continue;
            bool ok = false;
            const int minor = parts[1].split(QLatin1Char('-')).first().toInt(&ok);
            if (!ok || minor <= bestMinor) continue;
            wchar_t buffer[MAX_PATH] = {};
            DWORD size = sizeof(buffer);
            const QString subKey = QStringLiteral("Software\\Python\\PythonCore\\") + version
                                   + QStringLiteral("\\InstallPath");
            if (RegGetValueW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()), nullptr, RRF_RT_REG_SZ,
                             nullptr, buffer, &size) == ERROR_SUCCESS && buffer[0] != L'\0') {
                best = QString::fromWCharArray(buffer);
                bestMinor = minor;
            }
        }
        RegCloseKey(key);
    }
    return best;
}

QString pyLauncherHome() {
    QProcess process;
    process.start(QStringLiteral("py"), {QStringLiteral("-3"), QStringLiteral("-c"),
        QStringLiteral("import sys; print(sys.version_info[1]); print(sys.prefix)")});
    if (!process.waitForFinished(3000) || process.exitCode() != 0) return {};
    const auto lines = QString::fromUtf8(process.readAllStandardOutput())
                           .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return {};
    bool ok = false;
    const int minor = lines[0].trimmed().toInt(&ok);
    return ok && minor >= 12 ? lines[1].trimmed() : QString();
}

QString pathPythonHome() {
    for (const char* executable : {"python", "python3"}) {
        const QString path = QStandardPaths::findExecutable(QLatin1String(executable));
        if (!path.isEmpty()) return QFileInfo(path).absolutePath();
    }
    return {};
}

QJsonObject readPluginJson(const QDir& directory) {
    QFile file(directory.absoluteFilePath(QStringLiteral("plugin.json")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
} // namespace

xuanhub::PluginMetadata PyBridgePlugin::metadata() const {
    return {QStringLiteral("pybridge"), QStringLiteral("PyBridge"),
            QStringLiteral("0.2.1"), QStringLiteral("XuanHub")};
}

bool PyBridgePlugin::init(xuanhub::BarPluginContext* ctx) {
    m_ctx = *ctx;
    if (!pyChecked()) {
        setPyChecked();
        if (qEnvironmentVariableIsSet("XUANHUB_DISABLE_PYTHON")) {
            qWarning() << "[PyBridge] XUANHUB_DISABLE_PYTHON set, python force-disabled";
        } else {
            const QString exeDir = QCoreApplication::applicationDirPath();
            struct Candidate { QString home; const char* source; };
            const Candidate candidates[] = {
                {exeDir, "bundled"},
                {qEnvironmentVariable("PYTHONHOME"), "env"},
                {QStringLiteral(XUANHUB_PYTHON_HOME), "fallback"},
                {registryPythonHome(), "registry"},
                {pyLauncherHome(), "py-launcher"},
                {pathPythonHome(), "PATH"},
            };
            for (const auto& candidate : candidates) {
                if (!hasRuntime(candidate.home)) continue;
                setPyAvailable(true);
                setPyExecutable(pythonExecutableAt(candidate.home));
                qDebug() << "[PyBridge] Python worker runtime" << candidate.source << "->"
                         << pyExecutable();
                break;
            }
        }
        if (!pyAvailable())
            qWarning() << "[PyBridge] no Python 3.12+ runtime found; python sub-plugins disabled";
    }

    m_pythonExe = pyExecutable();
    m_workerScript = QDir(QCoreApplication::applicationDirPath())
                         .absoluteFilePath(QStringLiteral("plugins_py/bridge_worker.py"));
    if (pyAvailable() && !QFileInfo(m_workerScript).isFile()) {
        qWarning() << "[PyBridge] bridge worker is missing:" << m_workerScript;
        setPyAvailable(false);
        m_pythonExe.clear();
    }

    if (!pyAvailable() && !pyNotified() && m_ctx.eventBus) {
        setPyNotified();
        m_ctx.eventBus->publish(QStringLiteral("app.notify"), {
            {QStringLiteral("title"), QStringLiteral("XuanHub")},
            {QStringLiteral("message"), QCoreApplication::translate(
                "plugin.pybridge.notifications",
                "未找到可用的 Python 3.12+ 运行时，Python 插件已停用。请安装 Python，或使用 Bundle 便携包。")},
        });
    }
    return true;
}

QObject* PyBridgePlugin::viewModel() { return nullptr; }

QUrl PyBridgePlugin::qmlUrl() const { return {}; }

void PyBridgePlugin::shutdown() {
    qDebug() << "[PyBridge] shutdown:" << m_vms.size() << "isolated worker VM(s)";
    qDeleteAll(m_vms);
    m_vms.clear();
    m_dirs.clear();
    m_pythonExe.clear();
    m_workerScript.clear();
}

QStringList PyBridgePlugin::subPluginIds() {
    QStringList ids;
    m_dirs.clear();
    if (!pyAvailable() || m_pythonExe.isEmpty()) return ids;
    const QDir base(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins_py"));
    if (!base.exists()) {
        qDebug() << "[PyBridge] plugins_py not found:" << base.path();
        return ids;
    }
    for (const QString& sub : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir directory(base.filePath(sub));
        const QJsonObject object = readPluginJson(directory);
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            qWarning() << "[PyBridge] skip" << directory.path() << ": plugin.json missing id";
            continue;
        }
        if (!directory.exists(QStringLiteral("main.py"))) {
            qWarning() << "[PyBridge] skip" << id << ": main.py is missing";
            continue;
        }
        if (m_dirs.contains(id)) {
            qWarning() << "[PyBridge] duplicate sub-plugin id" << id << ":"
                       << directory.path() << "vs" << m_dirs.value(id) << "-> keep first";
            continue;
        }
        ids << id;
        m_dirs.insert(id, directory.absolutePath());
        qDebug() << "[PyBridge] found sub-plugin:" << id << "@" << directory.path();
    }
    return ids;
}

xuanhub::SubPluginInfo PyBridgePlugin::createSubPlugin(const QString& id, const QVariantMap& cfg) {
    xuanhub::SubPluginInfo info;
    if (m_vms.contains(id)) {
        qWarning() << "[PyBridge] createSubPlugin: already running" << id;
        return info;
    }
    const QString dirPath = m_dirs.value(id);
    if (dirPath.isEmpty()) {
        qWarning() << "[PyBridge] createSubPlugin: unknown id" << id;
        return info;
    }
    const QDir directory(dirPath);
    const QJsonObject object = readPluginJson(directory);
    QString viewQml = object.value(QStringLiteral("qml")).toString();
    if (!viewQml.isEmpty() && !directory.exists(viewQml)) {
        qWarning() << "[PyBridge]" << id << ": declared qml missing:" << viewQml;
        viewQml.clear();
    }
    if (viewQml.isEmpty()) {
        auto qmls = directory.entryList({QStringLiteral("*.qml")}, QDir::Files);
        const QString settingsQml = object.value(QStringLiteral("settingsQml")).toString();
        if (!settingsQml.isEmpty()) qmls.removeAll(QFileInfo(settingsQml).fileName());
        if (qmls.isEmpty()) {
            qWarning() << "[PyBridge] skip" << id << ": no bar-view QML";
            return info;
        }
        viewQml = qmls.first();
    }

    auto* viewModel = new PyWorkerViewModel(id, dirPath, cfg, m_pythonExe,
                                            m_workerScript, m_ctx.eventBus,
                                            m_ctx.eventScope, this);
    if (!viewModel->start()) {
        qWarning() << "[PyBridge]" << id << ": cannot start isolated worker";
        delete viewModel;
        return info;
    }
    m_vms.insert(id, viewModel);
    info.meta = {id,
                 object.value(QStringLiteral("name")).toString(id),
                 object.value(QStringLiteral("version")).toString(),
                 object.value(QStringLiteral("author")).toString()};
    info.viewModel = viewModel;
    info.qmlFilePath = viewQml;
    info.dirPath = dirPath;
    return info;
}

bool PyBridgePlugin::destroySubPlugin(const QString& id) {
    QObject* viewModel = m_vms.take(id);
    if (!viewModel) return false;
    delete viewModel;
    qDebug() << "[PyBridge] destroyed sub-plugin:" << id;
    return true;
}
