// LocaleManager.cpp — 语言状态、系统语言跟随和 QML 引擎重翻译
#include "LocaleManager.h"

#include "barplugin/EventBus.h"

#include <QCoreApplication>
#include <QEvent>
#include <QLocale>
#include <QQmlEngine>

#include <utility>

LocaleManager::LocaleManager(xuanhub::EventBus* bus, QString exeDir, QObject* parent)
    : QObject(parent), m_bus(bus), m_exeDir(std::move(exeDir)), m_translator(this) {
    QCoreApplication::installTranslator(&m_translator);
    if (qApp) qApp->installEventFilter(this);
    if (m_bus) {
        m_bus->subscribe(QStringLiteral("app.locale.request"), this,
                         "onLocaleRequest(QVariantMap)");
    }
    setRequestedLanguage(QStringLiteral("system"));
}

LocaleManager::~LocaleManager() {
    if (qApp) qApp->removeEventFilter(this);
    QCoreApplication::removeTranslator(&m_translator);
}

QString LocaleManager::normalizeRequested(const QString& requested) {
    const QString value = requested.trimmed();
    if (value.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("zh-CN");
    if (value.compare(QStringLiteral("en-US"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("en-US");
    return QStringLiteral("system");
}

QString LocaleManager::resolveActive(const QString& requested,
                                     const QString& systemLocale) {
    const QString normalized = normalizeRequested(requested);
    if (normalized != QStringLiteral("system")) return normalized;
    const QString system = systemLocale.isEmpty() ? QLocale::system().bcp47Name()
                                                   : systemLocale;
    return system.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)
               ? QStringLiteral("zh-CN") : QStringLiteral("en-US");
}

void LocaleManager::registerEngine(QQmlEngine* engine) {
    if (!engine) return;
    for (const auto& existing : std::as_const(m_engines)) {
        if (existing == engine) return;
    }
    m_engines.append(engine);
}

void LocaleManager::setRequestedLanguage(const QString& requested) {
    const QString normalized = normalizeRequested(requested);
    if (normalized == m_requestedLanguage) return;
    m_requestedLanguage = normalized;
    emit requestedLanguageChanged();
    apply(false);
}

void LocaleManager::reloadCatalogs() { apply(true); }

void LocaleManager::apply(bool forceReload) {
    const QString resolved = resolveActive(m_requestedLanguage);
    if (!forceReload && resolved == m_activeLocale) return;

    m_activeLocale = resolved;
    m_translator.loadLocale(m_activeLocale, m_exeDir);
    for (auto it = m_engines.begin(); it != m_engines.end();) {
        if (!*it) {
            it = m_engines.erase(it);
            continue;
        }
        (*it)->retranslate();
        ++it;
    }
    emit localeChanged();
    publishLocale();
}

bool LocaleManager::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qApp && event && event->type() == QEvent::LocaleChange
        && m_requestedLanguage == QStringLiteral("system")) {
        apply(false);
    }
    return QObject::eventFilter(watched, event);
}

void LocaleManager::onLocaleRequest(QVariantMap) { publishLocale(); }

void LocaleManager::publishLocale() {
    if (!m_bus) return;
    const QVariantMap state{
        {QStringLiteral("locale"), m_activeLocale},
        {QStringLiteral("requestedLanguage"), m_requestedLanguage}
    };
    m_bus->setState(QStringLiteral("app.locale"), state);
    // V1 兼容广播保留一个周期；新代码订阅 retained 的 app.locale。
    m_bus->publish(QStringLiteral("app.locale.changed"), state);
}
