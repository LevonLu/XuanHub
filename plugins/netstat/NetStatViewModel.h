// NetStatViewModel.h — 网络速率数据模型（GUI 线程）
#pragma once
#include <QObject>
#include <QString>

class NetStatViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString upRate   READ upRate   NOTIFY upRateChanged)
    Q_PROPERTY(QString downRate READ downRate NOTIFY downRateChanged)

public:
    explicit NetStatViewModel(QObject* parent = nullptr);

    QString upRate()   const { return m_upRate; }
    QString downRate() const { return m_downRate; }

public slots:
    void onRatesChanged(quint64 upBps, quint64 downBps);

signals:
    void upRateChanged();
    void downRateChanged();

private:
    static QString formatRate(quint64 bytesPerSec);

    QString m_upRate;
    QString m_downRate;
};
