// LauncherPlugin.cpp
#include "LauncherPlugin.h"
#include "LauncherViewModel.h"

#include <QVariantMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

using namespace xuanhub;

static const PluginMetadata kMeta = {
    QStringLiteral("launcher"),
    QStringLiteral("Launcher"),
    QStringLiteral("0.1.0"),
    QStringLiteral("XuanHub"),
};

PluginMetadata LauncherPlugin::metadata() const { return kMeta; }

bool LauncherPlugin::init(BarPluginContext* ctx) {
    m_vm = new LauncherViewModel();

    const auto cfg = ctx ? ctx->pluginConfig : QVariantMap();
    const auto arr = cfg.value(QStringLiteral("items")).toJsonArray();
    QList<LauncherViewModel::Entry> entries;
    for (const auto& v : arr) {
        const auto o = v.toObject();
        entries.append({
            o.value(QStringLiteral("name")).toString(),
            o.value(QStringLiteral("program")).toString(),
            o.value(QStringLiteral("icon")).toString(),
        });
    }
    m_vm->setEntries(entries);
    return true;
}

QObject* LauncherPlugin::viewModel() { return m_vm; }

QUrl LauncherPlugin::qmlUrl() const { return QUrl(QStringLiteral("Launcher.qml")); }

void LauncherPlugin::shutdown() {
    if (m_vm) { delete m_vm; m_vm = nullptr; }
}
