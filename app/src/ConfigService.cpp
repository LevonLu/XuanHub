// ConfigService.cpp
#include "ConfigService.h"
#include "SecretStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QDebug>

namespace {
// 用量中心的厂商凭据位于 vendors 数组中，不能只按顶层字段名解密。
// secretId 是设置模型生成的随机稳定标识；重排厂商不会把 A 的凭据给 B。
void hydrateUsageHubVendorSecrets(PluginConfigEntry* entry, const QJsonObject& config,
                                  const SecretStore& secrets) {
    if (!entry || entry->id != QLatin1String("py_usage_hub")) return;
    const QJsonArray rawVendors = config.value(QStringLiteral("vendors")).toArray();
    QVariantList vendors = entry->config.value(QStringLiteral("vendors")).toList();
    for (qsizetype index = 0; index < rawVendors.size() && index < vendors.size(); ++index) {
        const QJsonObject rawVendor = rawVendors.at(index).toObject();
        const QString token = rawVendor.value(QStringLiteral("secretId")).toString();
        const QString reference = SecretStore::referenceId(
            rawVendor.value(QStringLiteral("apiKey")));
        if (reference.isEmpty()) continue;

        QVariantMap vendor = vendors.at(index).toMap();
        const QString expected = SecretStore::idForPluginField(
            entry->id, QStringLiteral("vendors.%1.apiKey").arg(token));
        if (token.isEmpty() || reference != expected) {
            qWarning() << "[Config] invalid usage-hub vendor secret reference at" << index;
            vendor.remove(QStringLiteral("apiKey"));
        } else if (const auto plaintext = secrets.read(reference)) {
            vendor.insert(QStringLiteral("apiKey"), *plaintext);
        } else {
            // 解密失败时绝不把引用对象交给 Python worker，避免被当作字符串凭据使用。
            vendor.remove(QStringLiteral("apiKey"));
        }
        vendors[index] = vendor;
    }
    entry->config.insert(QStringLiteral("vendors"), vendors);
}
} // namespace

ConfigService::ConfigService(QObject* parent) : QObject(parent) {}

bool ConfigService::load(const QString& exeDir, const QString& configDir) {
    // 解析顺序：exeDir（便携/开发模式优先）→ configDir（%APPDATA%/XuanHub）
    // 首次播种：exeDir 优先（便携分发），不可写时退回 configDir
    QString path = QDir(exeDir).absoluteFilePath(QStringLiteral("config.json"));
    if (!QFile::exists(path)) {
        if (!configDir.isEmpty()) {
            const QString userPath = QDir(configDir).absoluteFilePath(QStringLiteral("config.json"));
            if (QFile::exists(userPath)) {
                path = userPath;  // 已有用户配置，直接用
            } else {
                // 文件曾成功加载过（运行中被删/改名）：不播种，保留当前设置——
                // 热重载期重新播种会把 bar 整体翻成默认主题/模式（实测踩过）
                if (m_everLoadedFromFile) {
                    qWarning() << "[Config] config.json missing at" << path
                               << "-> keep current settings (loaded before)";
                    return false;
                }
                // 初次运行：尝试播种到 exeDir（便携友好），不行再播种到 configDir
                const QString seedPath = QDir(exeDir).absoluteFilePath(QStringLiteral("config.json"));
                QFile::copy(QStringLiteral(":/qt/qml/XuanHub/resources/default_config.json"),
                            seedPath);
                if (QFile::exists(seedPath)) {
                    QFile::setPermissions(seedPath,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                    path = seedPath;
                    qDebug() << "[Config] seeded default config to" << seedPath;
                } else {
                    QDir().mkpath(configDir);
                    if (QFile::copy(QStringLiteral(":/qt/qml/XuanHub/resources/default_config.json"),
                                    userPath)) {
                        QFile::setPermissions(userPath,
                            QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                        qDebug() << "[Config] seeded default config to" << userPath;
                    }
                    path = userPath;
                }
            }
        }
        // configDir 为空时：path 保持 exeDir/config.json，load 会打日志退出（预期行为）
    }

    // 记录实际生效路径（即使稍后读取失败）：热重载监视器盯的就是它
    m_activePath = path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Config] no config.json at" << path << "-> keep current settings";
        return false;
    }
    QJsonParseError perr{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        qWarning() << "[Config] parse error in" << path << ":" << perr.errorString()
                   << "at offset" << perr.offset << "-> keep current settings";
        return false;
    }
    if (!doc.isObject()) {
        qWarning() << "[Config] root must be a JSON object in" << path
                   << "-> keep current settings";
        return false;
    }
    const auto root = doc.object();

    // 先校验 plugins id 唯一性；不能在已修改成员状态后才拒绝，否则调用方会以为
    // reload 失败、实际 bar 字段却已部分更新，形成难追踪的半生效状态。
    QSet<QString> pluginIds;
    const auto pluginArray = root.value("plugins").toArray();
    for (const auto& pv : pluginArray) {
        if (!pv.isObject()) continue;
        const QString id = pv.toObject().value("id").toString();
        if (!id.isEmpty() && pluginIds.contains(id)) {
            qWarning() << "[Config] duplicate plugin id:" << id
                       << "-> keep current settings";
            return false;
        }
        if (!id.isEmpty()) pluginIds.insert(id);
    }
    m_raw = root;  // M8：留存原始文档，save() 只改已知键（未知键如 bar.monitor 不丢）

    // M7 热重载：先留旧值，加载完成后仅对变化项 emit 对应 NOTIFY
    const int oldHeight = m_barHeight;
    const QString oldEdge = m_barEdge;
    const qreal oldOpacity = m_barOpacity;
    const QString oldTheme = m_theme;
    const QString oldThemeId = m_themeId;
    const QString oldBarMode = m_barMode;
    const QString oldLanguage = m_language;

    const auto bar = root.value("bar").toObject();
    // height：钳到与设置窗口校验一致的范围（24–200），防手写越界（负值/超大）
    // 直接用于 AppBar reserve 会压坏工作区
    if (bar.contains("height")) {
        const int h = bar.value("height").toInt(m_barHeight);
        const int clamped = qBound(24, h, 200);
        if (clamped != h)
            qWarning() << "[Config] height out of range:" << h << "-> clamp" << clamped;
        m_barHeight = clamped;
    }
    if (bar.contains("edge"))   m_barEdge   = bar.value("edge").toString(m_barEdge);
    // opacity：0.0-1.0，越界/非法回落 1.0（不透明）。下限 0.2 防用户调成看不见
    if (bar.contains("opacity")) {
        const qreal op = bar.value("opacity").toDouble(m_barOpacity);
        if (op >= 0.2 && op <= 1.0) m_barOpacity = op;
        else qWarning() << "[Config] invalid opacity:" << op << "-> keep" << m_barOpacity;
    }
    // theme：语义是模式（dark/light）。兼容旧值：非 dark/light 告警回落 dark
    if (bar.contains("theme")) {
        const QString t = bar.value("theme").toString(m_theme);
        if (t == QStringLiteral("dark") || t == QStringLiteral("light")) {
            m_theme = t;
        } else {
            qWarning() << "[Config] invalid theme mode:" << t << "-> dark";
            m_theme = QStringLiteral("dark");
        }
    }
    // themeId：主题包 id（缺省回落内置默认）
    if (bar.contains("themeId")) m_themeId = bar.value("themeId").toString(m_themeId);
    if (m_barEdge != QStringLiteral("top") && m_barEdge != QStringLiteral("bottom")) {
        qWarning() << "[Config] invalid edge:" << m_barEdge << "-> top";
        m_barEdge = QStringLiteral("top");
    }
    // mode：工作模式（appbar 占工作区 / taskbar 嵌入任务栏），非法值回落 appbar
    if (bar.contains("mode")) {
        const QString m = bar.value("mode").toString(m_barMode);
        if (m == QStringLiteral("appbar") || m == QStringLiteral("taskbar")) {
            m_barMode = m;
        } else {
            qWarning() << "[Config] invalid bar mode:" << m << "-> appbar";
            m_barMode = QStringLiteral("appbar");
        }
    }
    // taskbarOffsetX：taskbar 模式下距任务栏左缘的偏移（逻辑像素，可为负）
    if (bar.contains("taskbarOffsetX"))
        m_taskbarOffsetX = bar.value("taskbarOffsetX").toInt(m_taskbarOffsetX);

    // ui.language：默认跟随系统；非法值回退 system，避免语言包路径被任意输入污染。
    const QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    if (ui.contains(QStringLiteral("language"))) {
        const QString language = ui.value(QStringLiteral("language")).toString();
        if (language == QStringLiteral("system") || language == QStringLiteral("zh-CN")
            || language == QStringLiteral("en-US")) {
            m_language = language;
        } else {
            qWarning() << "[Config] invalid language:" << language << "-> system";
            m_language = QStringLiteral("system");
        }
    } else {
        m_language = QStringLiteral("system");
    }

    m_pluginEntries.clear();
    const auto arr = pluginArray;
    const SecretStore secrets(QFileInfo(m_activePath).absolutePath());
    for (const auto& pv : arr) {
        const auto o = pv.toObject();
        PluginConfigEntry e;
        e.id = o.value("id").toString();
        const QJsonValue configVersion = o.value(QStringLiteral("configVersion"));
        if (!configVersion.isUndefined()) {
            if (configVersion.isDouble()
                && configVersion.toDouble() == configVersion.toInt()
                && configVersion.toInt() >= 1) {
                e.configVersion = configVersion.toInt();
            } else {
                qWarning() << "[Config] invalid configVersion for" << e.id
                           << "-> use 1";
            }
        }
        e.position = o.value("position").toString();
        e.order = qMax(0, o.value("order").toInt(0));   // 负 order 无意义，防手写越界
        // 插件宽度（像素，0=跟随内容自适应）；钳到合理范围防手写越界撑爆状态栏
        e.width = qBound(0, o.value("width").toInt(0), 800);
        const QJsonObject configObject = o.value("config").toObject();
        e.config = configObject.toVariantMap();
        // 密钥在 config.json 中只保存为 DPAPI 密文库引用；运行时才在内存中注入
        // 明文，且引用必须严格匹配当前插件和字段，防止插件跨域读取别人的密钥。
        for (auto it = configObject.begin(); it != configObject.end(); ++it) {
            const QString reference = SecretStore::referenceId(it.value());
            if (reference.isEmpty()) continue;
            const QString expected = SecretStore::idForPluginField(e.id, it.key());
            if (reference != expected) {
                qWarning() << "[Config] invalid secret reference for" << e.id << it.key();
                e.config.remove(it.key());
                continue;
            }
            const auto plaintext = secrets.read(reference);
            if (plaintext) e.config.insert(it.key(), *plaintext);
            else e.config.remove(it.key());
        }
        hydrateUsageHubVendorSecrets(&e, configObject, secrets);
        if (!e.id.isEmpty()) m_pluginEntries.append(e);
    }

    qDebug() << "[Config] loaded" << path
             << "| height=" << m_barHeight << "edge=" << m_barEdge
             << "opacity=" << m_barOpacity
             << "themeId=" << m_themeId << "theme=" << m_theme
             << "barMode=" << m_barMode
             << "language=" << m_language
             << "| plugins=" << m_pluginEntries.size();

    if (m_barHeight != oldHeight) emit barHeightChanged();
    if (m_barEdge != oldEdge) emit barEdgeChanged();
    if (m_barOpacity != oldOpacity) emit barOpacityChanged();
    if (m_theme != oldTheme) emit themeChanged();
    if (m_themeId != oldThemeId) emit themeIdChanged();
    if (m_barMode != oldBarMode) emit barModeChanged();
    if (m_language != oldLanguage) emit languageChanged();
    // 已成功从真实文件加载：此后文件缺失时不再播种默认配置（见上方缺失分支）
    m_everLoadedFromFile = true;
    return true;
}

bool ConfigService::save(const QJsonObject& root) {
    if (m_activePath.isEmpty()) {
        qWarning() << "[Config] save failed: no active path";
        return false;
    }
    // QSaveFile（临时文件 + rename 原子替换）：热重载 watcher 不会读到半截 JSON
    QSaveFile f(m_activePath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[Config] save failed: cannot open" << m_activePath << "-" << f.errorString();
        return false;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size()) {
        qWarning() << "[Config] save write failed:" << m_activePath << "-" << f.errorString();
        return false;
    }
    if (!f.commit()) {
        qWarning() << "[Config] save commit failed:" << m_activePath << "-" << f.errorString();
        return false;
    }
    m_raw = root;
    qDebug() << "[Config] saved" << m_activePath;
    return true;
}
