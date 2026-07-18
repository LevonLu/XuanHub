// NetStatViewModel.h — 网络速率数据模型（GUI 线程）
// 显示定制（每行前缀/颜色）由 NetStatPlugin::init 从 config.json 的 config 段读入，
// 经构造函数注入；运行期不变 → CONSTANT 属性。
#pragma once
#include <QObject>
#include <QString>

class NetStatViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString upRate   READ upRate   NOTIFY upRateChanged)
    Q_PROPERTY(QString downRate READ downRate NOTIFY downRateChanged)
    Q_PROPERTY(QString downPrefix READ downPrefix CONSTANT)
    Q_PROPERTY(QString upPrefix   READ upPrefix   CONSTANT)
    // 空串 = 跟随主题（QML 回落 themeManager.colors.success/danger）
    Q_PROPERTY(QString downColor  READ downColor  CONSTANT)
    Q_PROPERTY(QString upColor    READ upColor    CONSTANT)

public:
    explicit NetStatViewModel(const QString& downPrefix = QStringLiteral("↓ "),
                              const QString& upPrefix   = QStringLiteral("↑ "),
                              const QString& downColor  = QString(),
                              const QString& upColor    = QString(),
                              QObject* parent = nullptr);

    QString upRate()   const { return m_upRate; }
    QString downRate() const { return m_downRate; }
    QString downPrefix() const { return m_downPrefix; }
    QString upPrefix()   const { return m_upPrefix; }
    QString downColor()  const { return m_downColor; }
    QString upColor()    const { return m_upColor; }

public slots:
    void onRatesChanged(quint64 upBps, quint64 downBps);

signals:
    void upRateChanged();
    void downRateChanged();

private:
    static QString formatRate(quint64 bytesPerSec);

    QString m_downPrefix;
    QString m_upPrefix;
    QString m_downColor;
    QString m_upColor;
    QString m_upRate;
    QString m_downRate;
};
