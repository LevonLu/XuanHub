// PyBridgePlugin.cpp — Python 子插件发现、运行时探测与隔离 worker 工厂
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "PyBridgePlugin.h"
#include "PyWorkerViewModel.h"
#include "PythonDependencyDoctor.h"
#include "PythonDependencySpec.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include "barplugin/EventBus.h"
#include "barplugin/PluginPathValidator.h"

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

struct PythonManifestResult {
    bool ok = false;
    QJsonObject object;
    QString id;
    QString error;
};

QVariantMap privateEnvironmentPlan(const QString& id, const QString& pluginDir,
                                   const QString& configDir,
                                   const PythonDependencySpec& spec) {
    QVariantMap plan{
        {QStringLiteral("available"), false},
        {QStringLiteral("requiresConfirmation"), true},
        {QStringLiteral("mode"), spec.environmentMode},
    };
    if (spec.environmentMode != QLatin1String("private")) return plan;
    if (spec.wheelDirectory.isEmpty()) {
        plan.insert(QStringLiteral("reason"),
                    QStringLiteral("private environment requires an offline wheel directory"));
        return plan;
    }
    const QString pluginCanonical = QDir::fromNativeSeparators(
        QFileInfo(pluginDir).canonicalFilePath());
    const QFileInfo wheelInfo(QDir(pluginDir).absoluteFilePath(spec.wheelDirectory));
    const QString wheelCanonical = QDir::fromNativeSeparators(
        wheelInfo.canonicalFilePath());
    const QString prefix = pluginCanonical + QLatin1Char('/');
    if (pluginCanonical.isEmpty() || wheelCanonical.isEmpty() || !wheelInfo.isDir()
        || (!wheelCanonical.startsWith(prefix, Qt::CaseInsensitive)
            && wheelCanonical.compare(pluginCanonical, Qt::CaseInsensitive) != 0)) {
        plan.insert(QStringLiteral("reason"),
                    QStringLiteral("offline wheel directory is missing or outside plugin root"));
        return plan;
    }
    const QDir wheelDir(wheelCanonical);
    const QStringList wheels = wheelDir.entryList({QStringLiteral("*.whl")}, QDir::Files);
    if (wheels.isEmpty()) {
        plan.insert(QStringLiteral("reason"),
                    QStringLiteral("offline wheel directory contains no .whl files"));
        return plan;
    }
    QStringList requirements;
    QStringList packages;
    for (const PythonPackageRequirement& package : spec.packages) {
        packages.append(package.name);
        QString constraint = package.versionConstraint;
        constraint.remove(QLatin1Char(' '));
        requirements.append(package.versionConstraint == QLatin1String("*")
            ? package.name : package.name + constraint);
    }
    if (requirements.isEmpty()) {
        plan.insert(QStringLiteral("reason"),
                    QStringLiteral("private environment declares no packages"));
        return plan;
    }
    const QString environment = QDir(configDir).absoluteFilePath(
        QStringLiteral("python-envs/") + id);
    plan.insert(QStringLiteral("available"), true);
    plan.insert(QStringLiteral("packages"), packages);
    plan.insert(QStringLiteral("wheelCount"), wheels.size());
    plan.insert(QStringLiteral("wheelDirectory"), spec.wheelDirectory);
    plan.insert(QStringLiteral("_environment"), QDir::cleanPath(environment));
    plan.insert(QStringLiteral("_wheels"), wheelCanonical);
    plan.insert(QStringLiteral("_requirements"), requirements);
    return plan;
}

bool readPythonActions(const QJsonObject& object,
                       QVector<xuanhub::PluginAction>* output,
                       QString* error) {
    const QJsonValue actionsValue = object.value(QStringLiteral("actions"));
    if (actionsValue.isUndefined()) return true;
    if (!actionsValue.isArray()) {
        *error = QStringLiteral("actions must be an array");
        return false;
    }
    const QRegularExpression idPattern(QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));
    const QSet<QString> allowedPlacements = {
        QStringLiteral("quickMenu"), QStringLiteral("tray"),
        QStringLiteral("pluginMenu")};
    QSet<QString> ids;
    for (const QJsonValue& value : actionsValue.toArray()) {
        if (!value.isObject()) {
            *error = QStringLiteral("action must be an object");
            return false;
        }
        const QJsonObject actionObject = value.toObject();
        xuanhub::PluginAction action;
        action.id = actionObject.value(QStringLiteral("id")).toString().trimmed();
        action.label = actionObject.value(QStringLiteral("label")).toString().trimmed();
        if (!idPattern.match(action.id).hasMatch() || action.label.isEmpty()
            || ids.contains(action.id)) {
            *error = QStringLiteral("action has invalid or duplicate id/label");
            return false;
        }
        ids.insert(action.id);
        const QJsonValue placements = actionObject.value(QStringLiteral("placements"));
        if (!placements.isArray() || placements.toArray().isEmpty()) {
            *error = QStringLiteral("action '%1' requires placements").arg(action.id);
            return false;
        }
        QSet<QString> seenPlacements;
        for (const QJsonValue& placementValue : placements.toArray()) {
            const QString placement = placementValue.toString();
            if (!placementValue.isString() || !allowedPlacements.contains(placement)
                || seenPlacements.contains(placement)) {
                *error = QStringLiteral("action '%1' has invalid placement").arg(action.id);
                return false;
            }
            seenPlacements.insert(placement);
            action.placements.append(placement);
        }
        for (const QString& key : {QStringLiteral("icon"),
                                   QStringLiteral("defaultShortcut")}) {
            const QJsonValue field = actionObject.value(key);
            if (!field.isUndefined() && !field.isString()) {
                *error = QStringLiteral("action '%1' field '%2' must be a string")
                             .arg(action.id, key);
                return false;
            }
        }
        action.icon = actionObject.value(QStringLiteral("icon")).toString().trimmed();
        action.defaultShortcut = actionObject.value(
            QStringLiteral("defaultShortcut")).toString().trimmed();
        const QJsonValue checkable = actionObject.value(QStringLiteral("checkable"));
        if (!checkable.isUndefined() && !checkable.isBool()) {
            *error = QStringLiteral("action '%1' checkable must be boolean").arg(action.id);
            return false;
        }
        action.checkable = checkable.toBool(false);
        output->append(action);
    }
    return true;
}

bool readPythonLayout(const QJsonObject& object,
                      xuanhub::PluginLayoutPreference* output,
                      QString* error) {
    const QJsonValue value = object.value(QStringLiteral("layout"));
    if (value.isUndefined()) return true;
    if (!value.isObject()) {
        *error = QStringLiteral("layout must be an object");
        return false;
    }
    const QJsonObject layout = value.toObject();
    output->declared = true;
    const QJsonValue region = layout.value(QStringLiteral("region"));
    const QJsonValue overflow = layout.value(QStringLiteral("overflow"));
    if ((!region.isUndefined() && !region.isString())
        || (!overflow.isUndefined() && !overflow.isString())) {
        *error = QStringLiteral("layout region/overflow must be strings");
        return false;
    }
    output->region = region.toString().trimmed();
    output->overflow = overflow.toString(QStringLiteral("clip")).trimmed();
    if (!output->region.isEmpty()
        && output->region != QStringLiteral("left")
        && output->region != QStringLiteral("center")
        && output->region != QStringLiteral("right")) {
        *error = QStringLiteral("layout region is invalid");
        return false;
    }
    if (output->overflow != QStringLiteral("clip")
        && output->overflow != QStringLiteral("hide")
        && output->overflow != QStringLiteral("menu")) {
        *error = QStringLiteral("layout overflow is invalid");
        return false;
    }
    auto readInteger = [&layout, error](const QString& key, int fallback,
                                       int minimum, int maximum, int* target) {
        const QJsonValue field = layout.value(key);
        if (field.isUndefined()) {
            *target = fallback;
            return true;
        }
        if (!field.isDouble() || field.toDouble() != field.toInt()
            || field.toInt() < minimum || field.toInt() > maximum) {
            *error = QStringLiteral("layout field '%1' is invalid").arg(key);
            return false;
        }
        *target = field.toInt();
        return true;
    };
    if (!readInteger(QStringLiteral("minWidth"), 0, 0, 800, &output->minWidth)
        || !readInteger(QStringLiteral("preferredWidth"), 0, 0, 800,
                        &output->preferredWidth)
        || !readInteger(QStringLiteral("maxWidth"), 0, 0, 800, &output->maxWidth)
        || !readInteger(QStringLiteral("flex"), 0, 0, 100, &output->flex)
        || !readInteger(QStringLiteral("priority"), 50, 0, 100, &output->priority)) {
        return false;
    }
    if (output->preferredWidth > 0 && output->preferredWidth < output->minWidth) {
        *error = QStringLiteral("layout preferredWidth is smaller than minWidth");
        return false;
    }
    const int lowerBound = qMax(output->minWidth, output->preferredWidth);
    if (output->maxWidth > 0 && output->maxWidth < lowerBound) {
        *error = QStringLiteral("layout maxWidth is smaller than min/preferred width");
        return false;
    }
    return true;
}

PythonManifestResult readPluginJson(const QDir& directory) {
    PythonManifestResult result;
    QFile file(directory.absoluteFilePath(QStringLiteral("plugin.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("cannot open plugin.json");
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("invalid plugin.json: %1").arg(parseError.errorString());
        return result;
    }
    result.object = document.object();
    const QRegularExpression idPattern(QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));
    result.id = result.object.value(QStringLiteral("id")).toString().trimmed();
    if (!idPattern.match(result.id).hasMatch()) {
        result.error = QStringLiteral("invalid or missing id");
        return result;
    }
    const QString name = result.object.value(QStringLiteral("name")).toString().trimmed();
    const QString version = result.object.value(QStringLiteral("version")).toString().trimmed();
    const QRegularExpression semverPattern(QStringLiteral(
        R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$)"));
    if (name.isEmpty()) {
        result.error = QStringLiteral("missing name");
        return result;
    }
    const auto versionMatch = semverPattern.match(version);
    bool majorOk = false;
    bool minorOk = false;
    bool patchOk = false;
    versionMatch.captured(1).toInt(&majorOk);
    versionMatch.captured(2).toInt(&minorOk);
    versionMatch.captured(3).toInt(&patchOk);
    bool prereleaseOk = true;
    for (const QString& identifier : versionMatch.captured(4).split(
             QLatin1Char('.'), Qt::SkipEmptyParts)) {
        bool numeric = false;
        identifier.toULongLong(&numeric);
        if (numeric && identifier.size() > 1
            && identifier.startsWith(QLatin1Char('0'))) {
            prereleaseOk = false;
            break;
        }
    }
    if (!versionMatch.hasMatch() || !majorOk || !minorOk || !patchOk
        || !prereleaseOk) {
        result.error = QStringLiteral("invalid or missing semantic version");
        return result;
    }
    const QJsonValue apiVersion = result.object.value(QStringLiteral("apiVersion"));
    if (!apiVersion.isUndefined()
        && (!apiVersion.isDouble() || apiVersion.toDouble() != apiVersion.toInt()
            || (apiVersion.toInt() != 1 && apiVersion.toInt() != 2))) {
        result.error = QStringLiteral("unsupported apiVersion");
        return result;
    }
    const QJsonValue configVersion = result.object.value(QStringLiteral("configVersion"));
    if (!configVersion.isUndefined()
        && (!configVersion.isDouble()
            || configVersion.toDouble() != configVersion.toInt()
            || configVersion.toInt() < 1)) {
        result.error = QStringLiteral("configVersion must be a positive integer");
        return result;
    }
    const QRegularExpression declarationPattern(
        QStringLiteral(R"(^[A-Za-z0-9._:*-]{1,128}$)"));
    for (const QString& key : {QStringLiteral("capabilities"),
                               QStringLiteral("permissions")}) {
        const QJsonValue declaration = result.object.value(key);
        if (declaration.isUndefined()) continue;
        if (!declaration.isArray()) {
            result.error = QStringLiteral("%1 must be an array of strings").arg(key);
            return result;
        }
        QSet<QString> seen;
        for (const QJsonValue& value : declaration.toArray()) {
            const QString item = value.toString();
            const bool validWildcard = !item.contains(QLatin1Char('*'))
                || (key == QLatin1String("permissions")
                    && item.count(QLatin1Char('*')) == 1
                    && item.endsWith(QStringLiteral(".*")));
            if (!value.isString() || !declarationPattern.match(item).hasMatch()
                || !validWildcard
                || seen.contains(item)) {
                result.error = QStringLiteral("%1 contains an invalid or duplicate value")
                                   .arg(key);
                return result;
            }
            seen.insert(item);
        }
    }
    QVector<xuanhub::PluginAction> actions;
    if (!readPythonActions(result.object, &actions, &result.error)) return result;
    xuanhub::PluginLayoutPreference layout;
    if (!readPythonLayout(result.object, &layout, &result.error)) return result;
    PythonDependencySpec dependencies;
    if (!readPythonDependencySpec(result.object, &dependencies, &result.error)) return result;

    QString pathError;
    if (!xuanhub::validatePluginFilePath(directory.absolutePath(),
                                         QStringLiteral("backend"),
                                         QStringLiteral("main.py"), &pathError)) {
        result.error = pathError;
        return result;
    }
    const QString qml = result.object.value(QStringLiteral("qml")).toString().trimmed();
    if (!xuanhub::validatePluginFilePath(directory.absolutePath(),
                                         QStringLiteral("qml"), qml, &pathError)) {
        result.error = pathError;
        return result;
    }
    const QJsonValue settingsValue = result.object.value(QStringLiteral("settingsQml"));
    if (!settingsValue.isUndefined() && !settingsValue.isString()) {
        result.error = QStringLiteral("settingsQml must be a string");
        return result;
    }
    const QString settingsQml = settingsValue.toString().trimmed();
    if (!settingsQml.isEmpty()
        && !xuanhub::validatePluginFilePath(directory.absolutePath(),
                                            QStringLiteral("settingsQml"),
                                            settingsQml, &pathError)) {
        result.error = pathError;
        return result;
    }
    result.ok = true;
    return result;
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
    m_doctorScript = QDir(QCoreApplication::applicationDirPath())
                         .absoluteFilePath(QStringLiteral("plugins_py/dependency_doctor.py"));
    m_installerScript = QDir(QCoreApplication::applicationDirPath())
                            .absoluteFilePath(QStringLiteral("plugins_py/environment_installer.py"));
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
    const QList<QProcess*> installers = m_dependencyInstallers.values();
    m_dependencyInstallers.clear();
    for (QProcess* installer : installers) {
        if (!installer) continue;
        disconnect(installer, nullptr, this, nullptr);
        installer->terminate();
        if (!installer->waitForFinished(1000)) {
            installer->kill();
            installer->waitForFinished(1000);
        }
    }
    qDeleteAll(installers);
    qDeleteAll(m_vms);
    m_vms.clear();
    m_dirs.clear();
    m_manifests.clear();
    m_configVersions.clear();
    m_subPluginErrors.clear();
    m_dependencyPlans.clear();
    m_pythonExe.clear();
    m_workerScript.clear();
    m_doctorScript.clear();
    m_installerScript.clear();
}

QStringList PyBridgePlugin::subPluginIds() {
    QStringList ids;
    m_dirs.clear();
    m_manifests.clear();
    m_configVersions.clear();
    m_subPluginErrors.clear();
    m_dependencyPlans.clear();
    if (!pyAvailable() || m_pythonExe.isEmpty()) return ids;
    const QDir base(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins_py"));
    if (!base.exists()) {
        qDebug() << "[PyBridge] plugins_py not found:" << base.path();
        return ids;
    }
    for (const QString& sub : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir directory(base.filePath(sub));
        const PythonManifestResult manifest = readPluginJson(directory);
        if (!manifest.ok) {
            qWarning() << "[PyBridge] manifest rejected:" << directory.path()
                       << "->" << manifest.error;
            continue;
        }
        const QString id = manifest.id;
        if (m_dirs.contains(id)) {
            qWarning() << "[PyBridge] duplicate sub-plugin id" << id << ":"
                       << directory.path() << "vs" << m_dirs.value(id) << "-> keep first";
            continue;
        }
        ids << id;
        m_dirs.insert(id, directory.absolutePath());
        m_manifests.insert(id, manifest.object);
        PythonDependencySpec dependencies;
        QString dependencyError;
        if (readPythonDependencySpec(manifest.object, &dependencies, &dependencyError)
            && dependencies.environmentMode == QLatin1String("private")) {
            m_dependencyPlans.insert(id, privateEnvironmentPlan(
                id, directory.absolutePath(), m_ctx.configDir, dependencies));
        }
        qDebug() << "[PyBridge] found sub-plugin:" << id << "@" << directory.path();
    }
    return ids;
}

xuanhub::SubPluginInfo PyBridgePlugin::createSubPlugin(const QString& id, const QVariantMap& cfg) {
    xuanhub::SubPluginInfo info;
    m_subPluginErrors.remove(id);
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
    // 发现后到启用前 plugin.json 仍可能被替换，因此启动 worker 前再次完整预检。
    const PythonManifestResult manifest = readPluginJson(directory);
    if (!manifest.ok || manifest.id != id) {
        m_subPluginErrors.insert(id, manifest.error.isEmpty()
            ? QStringLiteral("Python plugin manifest identity changed") : manifest.error);
        qWarning() << "[PyBridge] manifest changed or rejected before start:" << id
                   << "->" << manifest.error;
        return info;
    }
    const QJsonObject object = manifest.object;
    const QString viewQml = object.value(QStringLiteral("qml")).toString().trimmed();

    PythonDependencySpec dependencies;
    QString dependencyParseError;
    if (!readPythonDependencySpec(object, &dependencies, &dependencyParseError)) {
        m_subPluginErrors.insert(id, dependencyParseError);
        return info;
    }
    QString runtimeExecutable = m_pythonExe;
    if (dependencies.environmentMode == QLatin1String("private")) {
        const QVariantMap plan = m_dependencyPlans.value(id);
        const QString environment = plan.value(QStringLiteral("_environment")).toString();
        const QString privatePython = QDir(environment).absoluteFilePath(
            QStringLiteral("Scripts/python.exe"));
        if (environment.isEmpty() || !QFileInfo(privatePython).isFile()) {
            const QString reason = plan.value(QStringLiteral("reason")).toString();
            m_subPluginErrors.insert(id, reason.isEmpty()
                ? QStringLiteral("private Python environment is not prepared; confirm offline installation")
                : reason);
            return info;
        }
        runtimeExecutable = privatePython;
    }
    if (dependencies.declared) {
        const PythonDependencyReport report = PythonDependencyDoctor::run(
            runtimeExecutable, m_doctorScript, dependencies);
        if (!report.ok) {
            const QString summary = report.summary().isEmpty()
                ? QStringLiteral("Python dependency doctor failed") : report.summary();
            m_subPluginErrors.insert(id, summary);
            QStringList codes;
            for (const QVariant& value : report.issues) {
                const QString code = value.toMap().value(QStringLiteral("code")).toString();
                if (!code.isEmpty()) codes.append(code);
            }
            qWarning() << "[PyBridge] dependency doctor rejected" << id
                       << "codes=" << codes << "->" << summary;
            return info;
        }
        qDebug() << "[PyBridge] dependency doctor passed" << id
                 << report.runtime.value(QStringLiteral("version")).toString()
                 << report.runtime.value(QStringLiteral("machine")).toString();
    }

    const int currentConfigVersion = qMax(1, m_configVersions.value(id, 1));
    const int targetConfigVersion = qMax(
        1, object.value(QStringLiteral("configVersion")).toInt(1));
    auto* viewModel = new PyWorkerViewModel(id, dirPath, cfg, runtimeExecutable,
                                            m_workerScript, m_ctx.eventBus,
                                            m_ctx.eventScope, this);
    viewModel->setConfigVersions(currentConfigVersion, targetConfigVersion);
    // Worker 要到核心按子插件身份绑定 HostServices 后再启动，避免插件 init()
    // 抢先发服务请求时仍只有 pybridge 的宿主身份。
    m_vms.insert(id, viewModel);
    info.meta = {id,
                 object.value(QStringLiteral("name")).toString(id),
                 object.value(QStringLiteral("version")).toString(),
                 object.value(QStringLiteral("author")).toString()};
    info.viewModel = viewModel;
    info.qmlFilePath = viewQml;
    info.dirPath = dirPath;
    for (const QJsonValue& value : object.value(QStringLiteral("capabilities")).toArray())
        if (value.isString()) info.capabilities.append(value.toString());
    for (const QJsonValue& value : object.value(QStringLiteral("permissions")).toArray())
        if (value.isString()) info.permissions.append(value.toString());
    QString actionError;
    if (!readPythonActions(object, &info.actions, &actionError)) {
        qWarning() << "[PyBridge] action manifest changed before start:" << id
                   << "->" << actionError;
        delete viewModel;
        m_vms.remove(id);
        return {};
    }
    QString layoutError;
    if (!readPythonLayout(object, &info.layout, &layoutError)) {
        qWarning() << "[PyBridge] layout manifest changed before start:" << id
                   << "->" << layoutError;
        delete viewModel;
        m_vms.remove(id);
        return {};
    }
    return info;
}

bool PyBridgePlugin::destroySubPlugin(const QString& id) {
    QObject* viewModel = m_vms.take(id);
    if (!viewModel) return false;
    delete viewModel;
    m_configVersions.remove(id);
    qDebug() << "[PyBridge] destroyed sub-plugin:" << id;
    return true;
}

QString PyBridgePlugin::subPluginLoadError(const QString& id) const {
    return m_subPluginErrors.value(id);
}

QVariantMap PyBridgePlugin::subPluginDependencyPlan(const QString& id) const {
    QVariantMap plan = m_dependencyPlans.value(id);
    plan.remove(QStringLiteral("_environment"));
    plan.remove(QStringLiteral("_wheels"));
    plan.remove(QStringLiteral("_requirements"));
    plan.insert(QStringLiteral("installing"), m_dependencyInstallers.contains(id));
    return plan;
}

bool PyBridgePlugin::installSubPluginDependencies(const QString& id) {
    if (m_dependencyInstallers.contains(id)) return false;
    const QVariantMap plan = m_dependencyPlans.value(id);
    if (!plan.value(QStringLiteral("available")).toBool()
        || !plan.value(QStringLiteral("requiresConfirmation")).toBool()
        || !QFileInfo(m_installerScript).isFile()) {
        return false;
    }

    const QString environment = plan.value(QStringLiteral("_environment")).toString();
    const QString configRoot = QDir(m_ctx.configDir).absolutePath();
    const QString environmentPath = QDir::fromNativeSeparators(
        QFileInfo(environment).absoluteFilePath());
    const QString expectedPrefix = QDir::fromNativeSeparators(
        QDir(configRoot).absoluteFilePath(QStringLiteral("python-envs")))
        + QLatin1Char('/');
    if (!environmentPath.startsWith(expectedPrefix, Qt::CaseInsensitive)) {
        qWarning() << "[PyBridge] refused dependency environment outside config root:" << id;
        return false;
    }

    auto* process = new QProcess(this);
    process->setProgram(m_pythonExe);
    process->setArguments({QStringLiteral("-B"), m_installerScript});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    m_dependencyInstallers.insert(id, process);
    connect(process, &QProcess::errorOccurred, this,
            [this, id, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_dependencyInstallers.value(id) != process)
            return;
        m_dependencyInstallers.remove(id);
        const QString message = QStringLiteral("dependency installer failed to start: %1")
                                    .arg(process->errorString());
        m_subPluginErrors.insert(id, message);
        Q_EMIT subPluginDependencyInstallFinished(id, false, message);
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, id, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_dependencyInstallers.value(id) != process) return;
        m_dependencyInstallers.remove(id);
        const QByteArray output = process->readAllStandardOutput();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
        const QJsonObject result = document.isObject() ? document.object() : QJsonObject{};
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0
            && result.value(QStringLiteral("ok")).toBool();
        QString message;
        if (!success) {
            message = result.value(QStringLiteral("error")).toString();
            if (message.isEmpty())
                message = parseError.error == QJsonParseError::NoError
                    ? QStringLiteral("offline dependency installation failed")
                    : QStringLiteral("dependency installer returned invalid JSON");
            m_subPluginErrors.insert(id, message);
        } else {
            m_subPluginErrors.remove(id);
        }
        Q_EMIT subPluginDependencyInstallFinished(id, success, message);
        process->deleteLater();
    });
    process->start();
    if (!process->waitForStarted(3000)) return false;
    const QJsonObject input{
        {QStringLiteral("environment"), environmentPath},
        {QStringLiteral("wheels"), plan.value(QStringLiteral("_wheels")).toString()},
        {QStringLiteral("requirements"), QJsonArray::fromStringList(
            plan.value(QStringLiteral("_requirements")).toStringList())},
    };
    process->write(QJsonDocument(input).toJson(QJsonDocument::Compact));
    process->closeWriteChannel();
    return true;
}

bool PyBridgePlugin::canApplySubPluginConfig(const QString& id) const {
    const auto* viewModel = qobject_cast<PyWorkerViewModel*>(m_vms.value(id));
    return viewModel && viewModel->supportsHotConfig();
}

bool PyBridgePlugin::applySubPluginConfig(const QString& id,
                                          const QVariantMap& config) {
    auto* viewModel = qobject_cast<PyWorkerViewModel*>(m_vms.value(id));
    return viewModel && viewModel->applyConfig(config);
}

xuanhub::HostedPluginConfigValidationResult
PyBridgePlugin::validateSubPluginConfig(const QString& id,
                                        const QVariantMap& config,
                                        int timeoutMs) {
    auto* viewModel = qobject_cast<PyWorkerViewModel*>(m_vms.value(id));
    return viewModel ? viewModel->validateConfig(config, timeoutMs)
                     : xuanhub::HostedPluginConfigValidationResult{};
}

void PyBridgePlugin::setSubPluginConfigVersion(const QString& id,
                                                int currentVersion) {
    if (!id.isEmpty()) m_configVersions.insert(id, qMax(1, currentVersion));
}

bool PyBridgePlugin::bindSubPluginHostServices(
    const QString& id, xuanhub::IHostServices* services) {
    auto* viewModel = qobject_cast<PyWorkerViewModel*>(m_vms.value(id));
    if (!viewModel || !services) return false;
    viewModel->setHostServices(services);
    if (viewModel->start()) return true;
    qWarning() << "[PyBridge]" << id << ": cannot start isolated worker";
    return false;
}

bool PyBridgePlugin::bindSubPluginEventScope(
    const QString& id, xuanhub::PluginEventScope* eventScope) {
    auto* viewModel = qobject_cast<PyWorkerViewModel*>(m_vms.value(id));
    if (!viewModel || !eventScope) return false;
    viewModel->setEventScope(eventScope);
    return true;
}
