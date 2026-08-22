// DemoPlugin.h — 不依赖宿主源码的第三方 SDK V2 最小示例
#pragma once

#include "barplugin/IBarPlugin2.h"

#include <QObject>

class DemoPlugin final : public QObject, public xuanhub::IBarPlugin2 {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2)

public:
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(
        const xuanhub::PluginContext2& context) override;
    xuanhub::PluginConfigValidationResult validateConfig(
        const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(
        const QVariantMap& config) override;
    QObject* viewModel() override { return this; }
    QUrl qmlUrl() const override { return QUrl(QStringLiteral("Demo.qml")); }
    void shutdown() override {}

    QString text() const { return m_text; }

Q_SIGNALS:
    void textChanged();

private:
    QString m_text = QStringLiteral("SDK V2");
};
