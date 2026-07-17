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
    QString path = QDir(exeDir).absoluteFilePath(QStringLiteral("config.json"));
    if (!QFile::exists(path) && !configDir.isEmpty()) {
        const QString userPath = QDir(configDir).absoluteFilePath(QStringLiteral("config.json"));
        if (!QFile::exists(userPath)) {
            // 首次运行：落地一份默认配置供用户编辑
            //（qrc 拷贝会继承只读属性，需显式加回写权限）
            QDir().mkpath(configDir);
            if (QFile::copy(QStringLiteral(":/qt/qml/XuanHub/resources/default_config.json"),
                            userPath)) {
                QFile::setPermissions(userPath,
                    QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                    QFileDevice::ReadGroup | QFileDevice::ReadOther);
                qDebug() << "[Config] seeded default config to" << userPath;
            }
        }
        path = userPath;
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
    const QString oldTheme = m_theme;

    const auto bar = root.value("bar").toObject();
    if (bar.contains("height")) m_barHeight = bar.value("height").toInt(m_barHeight);
    if (bar.contains("edge"))   m_barEdge   = bar.value("edge").toString(m_barEdge);
    if (bar.contains("theme"))  m_theme     = bar.value("theme").toString(m_theme);
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
             << "| height=" << m_barHeight << "edge=" << m_barEdge << "theme=" << m_theme
             << "| plugins=" << m_pluginEntries.size();

    if (m_barHeight != oldHeight) emit barHeightChanged();
    if (m_barEdge != oldEdge) emit barEdgeChanged();
    if (m_theme != oldTheme) emit themeChanged();
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
