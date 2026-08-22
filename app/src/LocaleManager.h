// LocaleManager.h — 解析语言配置、加载 JSON 翻译并驱动运行时重翻译
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QVariantMap>

#include "JsonTranslator.h"

class QQmlEngine;
namespace xuanhub { class EventBus; }

class LocaleManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString requestedLanguage READ requestedLanguage NOTIFY requestedLanguageChanged)
    Q_PROPERTY(QString activeLocale READ activeLocale NOTIFY localeChanged)
public:
    explicit LocaleManager(xuanhub::EventBus* bus, QString exeDir,
                           QObject* parent = nullptr);
    ~LocaleManager() override;

    QString requestedLanguage() const { return m_requestedLanguage; }
    QString activeLocale() const { return m_activeLocale; }

    static QString normalizeRequested(const QString& requested);
    static QString resolveActive(const QString& requested,
                                 const QString& systemLocale = {});

    void registerEngine(QQmlEngine* engine);

public slots:
    void setRequestedLanguage(const QString& requested);
    void reloadCatalogs();

signals:
    void requestedLanguageChanged();
    void localeChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onLocaleRequest(QVariantMap payload = {});

private:
    void apply(bool forceReload);
    void publishLocale();

    xuanhub::EventBus* m_bus = nullptr;
    QString m_exeDir;
    QString m_requestedLanguage;
    QString m_activeLocale;
    JsonTranslator m_translator;
    QVector<QPointer<QQmlEngine>> m_engines;
};
