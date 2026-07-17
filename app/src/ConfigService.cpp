// ConfigService.cpp
#include "ConfigService.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

ConfigService::ConfigService(QObject* parent) : QObject(parent) {}

bool ConfigService::load(const QString& exeDir) {
    const QString path = QDir(exeDir).absoluteFilePath(QStringLiteral("config.json"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Config] no config.json at" << path << "-> using defaults";
        return false;
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto root = doc.object();

    const auto bar = root.value("bar").toObject();
    if (bar.contains("height")) m_barHeight = bar.value("height").toInt(m_barHeight);
    if (bar.contains("edge"))   m_barEdge   = bar.value("edge").toString(m_barEdge);
    if (bar.contains("theme"))  m_theme     = bar.value("theme").toString(m_theme);

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
    return true;
}
