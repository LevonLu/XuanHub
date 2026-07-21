// ConfigService.cpp
#include "ConfigService.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

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
                // 初次运行：尝试播种到 exeDir（便携友好），不行再播种到 configDir
                const QString seedPath = QDir(exeDir).absoluteFilePath(QStringLiteral("config.json"));
                QFile::copy(QStringLiteral(":/qt/qml/XuanHub/resources/default_config.json"),
                            seedPath);
                if (QFile::exists(seedPath)) {
                    QFile::setPermissions(seedPath,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                        QFileDevice::ReadGroup | QFileDevice::ReadOther);
                    path = seedPath;
                    qDebug() << "[Config] seeded default config to" << seedPath;
                } else {
                    QDir().mkpath(configDir);
                    if (QFile::copy(QStringLiteral(":/qt/qml/XuanHub/resources/default_config.json"),
                                    userPath)) {
                        QFile::setPermissions(userPath,
                            QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ReadGroup | QFileDevice::ReadOther);
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
    const auto root = doc.object();
    m_raw = root;  // M8：留存原始文档，save() 只改已知键（未知键如 bar.monitor 不丢）

    // M7 热重载：先留旧值，加载完成后仅对变化项 emit 对应 NOTIFY
    const int oldHeight = m_barHeight;
    const QString oldEdge = m_barEdge;
    const qreal oldOpacity = m_barOpacity;
    const QString oldTheme = m_theme;
    const QString oldThemeId = m_themeId;

    const auto bar = root.value("bar").toObject();
    if (bar.contains("height")) m_barHeight = bar.value("height").toInt(m_barHeight);
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

    m_pluginEntries.clear();
    const auto arr = root.value("plugins").toArray();
    for (const auto& pv : arr) {
        const auto o = pv.toObject();
        PluginConfigEntry e;
        e.id = o.value("id").toString();
        e.position = o.value("position").toString();
        e.order = o.value("order").toInt(0);
        e.config = o.value("config").toObject().toVariantMap();
        if (!e.id.isEmpty()) m_pluginEntries.append(e);
    }

    qDebug() << "[Config] loaded" << path
             << "| height=" << m_barHeight << "edge=" << m_barEdge
             << "opacity=" << m_barOpacity
             << "themeId=" << m_themeId << "mode=" << m_theme
             << "| plugins=" << m_pluginEntries.size();

    if (m_barHeight != oldHeight) emit barHeightChanged();
    if (m_barEdge != oldEdge) emit barEdgeChanged();
    if (m_barOpacity != oldOpacity) emit barOpacityChanged();
    if (m_theme != oldTheme) emit themeChanged();
    if (m_themeId != oldThemeId) emit themeIdChanged();
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
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qWarning() << "[Config] save commit failed:" << m_activePath << "-" << f.errorString();
        return false;
    }
    m_raw = root;
    qDebug() << "[Config] saved" << m_activePath;
    return true;
}
