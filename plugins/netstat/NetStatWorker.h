// NetStatWorker.h — 后台采集线程（QThread + moveToThread）
// 用 GetIfTable2 采集网络接口累计字节，按时差计算上下行速率。
#pragma once
#include <QObject>
#include <QTimer>
#include <cstdint>

class NetStatWorker : public QObject {
    Q_OBJECT
public:
    explicit NetStatWorker(QObject* parent = nullptr);

public slots:
    void start(int intervalMs = 1000);
    void stop();

signals:
    void ratesChanged(quint64 upBytesPerSec, quint64 downBytesPerSec);

private:
    void tick();

    QTimer* m_timer = nullptr;
    quint64 m_lastIn = 0;
    quint64 m_lastOut = 0;
    bool m_first = true;
};
