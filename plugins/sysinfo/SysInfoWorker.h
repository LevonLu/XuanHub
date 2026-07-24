// SysInfoWorker.h — 系统信息后台采集线程（QThread + moveToThread）
// 单 Worker 统一采集 5 项指标，各自独立、互不阻塞：
//   - CPU：PDH（Performance Data Helper）计数器 \Processor(_Total)\% Processor Time，
//     与任务管理器同源，官方稳定；差值型采集（两次 collect 求增量）。
//   - 内存：GlobalMemoryStatusEx 的 dwMemoryLoad（直接给 0-100 百分比，瞬时值）。
//   - GPU：优先 PDH \GPU Engine(*)\Utilization Percentage（Win10 1809+，过滤非 3D 引擎
//     避免重复计数，取所有物理引擎节点之和）；PDH 取不到时回落 DXGI 显存占用百分比
//     （IDXGIAdapter3::QueryVideoMemoryInfo，CurrentUsage/LocalBudget）。两者都失败 → -1（UI 显示 ---）。
//   - 网络（下行/上行速率）：GetIfTable2 累计字节差值，按真实间隔归一为每秒字节数；
//     迁自原 netstat 插件，保留首帧基准/接口过滤/回绕保护/速率归一四要点。
//
// 线程范式严格遵循 netstat：worker 只算数 + emit metricsChanged，绝不碰 QML/ViewModel；
// 跨线程信号由 ViewModel（GUI 线程）的 AutoConnection 排队接收。
#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

#include <memory>
#include <vector>
#include <cstdint>

// Win32 采集类型前向声明（.cpp 内部用，头里只放指针以减编译依赖）
struct SysInfoCollector;

class SysInfoWorker : public QObject {
    Q_OBJECT
public:
    explicit SysInfoWorker(QObject* parent = nullptr);
    ~SysInfoWorker() override;

public slots:
    void start(int intervalMs = 1000);
    void stop();

signals:
    // cpu/mem/gpu 均为 0.0-100.0；<0 表示该项本次未能采集（UI 保留旧值或显示 ---）
    // netDownBps/netUpBps 为每秒字节数（quint64，无失败语义，恒 >=0）
    void metricsChanged(double cpu, double mem, double gpu,
                        quint64 netDownBps, quint64 netUpBps);

private:
    void tick();

    QTimer* m_timer = nullptr;
    QElapsedTimer m_elapsed;   // 真实间隔计时（网络速率归一用，吸收定时器抖动）
    bool m_first = true;       // 首帧只记基准不 emit（PDH 与网络差值都需要基准）
    // 网络采集基准（迁自 netstat）
    quint64 m_lastIn = 0;
    quint64 m_lastOut = 0;
    std::unique_ptr<SysInfoCollector> m_collector;
};
