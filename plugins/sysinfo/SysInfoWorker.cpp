// SysInfoWorker.cpp
#include "SysInfoWorker.h"

#include <QDebug>

// winsock2 必须在 windows.h 之前（否则 win.h 抢先引入 winsock.h 冲突），
// WIN32_LEAN_AND_MEAN 仅减少 windows.h 的展开量，不改变与 winsock2 的先后依赖。
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
// DXGI/D3D11 显存查询（GPU 利用率 PDH 取不到时的回落路径）
#include <d3d11.h>
#include <dxgi1_4.h>   // IDXGIAdapter3（QueryVideoMemoryInfo 自此引入）

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

// ── 内部采集器：封装 PDH 查询句柄与 DXGI 适配器 ──────────────────────
// 单独成结构体便于 RAII（worker 析构时自动释放），且头文件不必暴露 Win32 头
struct SysInfoCollector {
    // CPU：单计数器查询，两次 collect 之间 PdhGetFormattedCounterValue 求增量
    PDH_HQUERY cpuQuery = nullptr;
    PDH_HCOUNTER cpuCounter = nullptr;

    // GPU 引擎利用率：单个通配实例计数器（PdhAddEnglishCounterW 支持通配路径，
    // 读取时用 PdhGetFormattedCounterArrayW 展开全部实例逐个过滤求和）
    PDH_HQUERY gpuQuery = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    bool gpuPdhAvailable = false;            // Win10<1809 或无 GPU 时为 false → 走 DXGI 显存回落

    // DXGI 显存回落：首适配器的 IDXGIAdapter3（lazy 初始化，采集失败不重试到死）
    IDXGIFactory1* dxgiFactory = nullptr;
    IDXGIAdapter3* dxgiAdapter = nullptr;

    ~SysInfoCollector() {
        if (cpuQuery) PdhCloseQuery(cpuQuery);
        if (gpuQuery) PdhCloseQuery(gpuQuery);
        if (dxgiAdapter) dxgiAdapter->Release();
        if (dxgiFactory) dxgiFactory->Release();
    }
};

SysInfoWorker::SysInfoWorker(QObject* parent) : QObject(parent) {}

SysInfoWorker::~SysInfoWorker() = default;   // unique_ptr 析构 Collector（释放 PDH/DXGI 句柄）

void SysInfoWorker::start(int intervalMs) {
    if (m_timer) stop();       // 允许重复 start：先清理旧定时器与旧采集器
    m_collector = std::make_unique<SysInfoCollector>();

    // ── 初始化 CPU 计数器 ──
    // \Processor(_Total)\% Processor Time：所有逻辑核心平均占用（与任务管理器底部总数一致）
    if (PdhOpenQuery(nullptr, 0, &m_collector->cpuQuery) == ERROR_SUCCESS) {
        // 必须用 PdhAddEnglishCounterW：PDH 计数器名按系统 locale 本地化，
        // PdhAddCounter 拿英文路径在非英文 Windows 上返回 PDH_CSTATUS_NO_COUNTER，
        // CPU 项会永久失效；English 变体接受英文路径、与 locale 无关（Vista+）
        if (PdhAddEnglishCounterW(m_collector->cpuQuery,
                                  L"\\Processor(_Total)\\% Processor Time",
                                  0, &m_collector->cpuCounter) != ERROR_SUCCESS) {
            m_collector->cpuCounter = nullptr;
        }
    } else {
        m_collector->cpuQuery = nullptr;
        m_collector->cpuCounter = nullptr;
    }

    // ── 初始化 GPU 计数器（通配实例）──
    // \GPU Engine(*)\Utilization Percentage 的实例名形如 pid_xxx_luid_0x..._phys_N_engtype_3D_eng_N
    // 单个通配计数器加入查询，读取时 PdhGetFormattedCounterArrayW 展开；
    // 实例名 token（pid_ / engtype_3D）由 GPU 计数器提供方生成，与 locale 无关
    if (PdhOpenQuery(nullptr, 0, &m_collector->gpuQuery) == ERROR_SUCCESS) {
        m_collector->gpuPdhAvailable =
            PdhAddEnglishCounterW(m_collector->gpuQuery,
                                  L"\\GPU Engine(*)\\Utilization Percentage",
                                  0, &m_collector->gpuCounter) == ERROR_SUCCESS;
        if (!m_collector->gpuPdhAvailable)
            m_collector->gpuCounter = nullptr;
    }
    if (!m_collector->gpuPdhAvailable) {
        // PDH 不可用（Win10<1809 或无 GPU）→ 标记走 DXGI 显存回落
        qWarning() << "[SysInfo] GPU engine PDH unavailable, fallback to DXGI VRAM usage";
    }

    // ── 初始化 DXGI 显存回落路径（lazy，失败不报错到死）──
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&m_collector->dxgiFactory)))) {
        // 取首个非软件适配器（跳过 Microsoft Basic Render Driver 等软件模拟）
        IDXGIAdapter1* a1 = nullptr;
        for (UINT i = 0; m_collector->dxgiFactory->EnumAdapters1(i, &a1) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            a1->GetDesc1(&desc);
            // 0x1414（Microsoft WARP）/ 0x00000000（软件）跳过；有 DedicatedVideoMemory 的才算真 GPU
            if (desc.VendorId != 0x1414 && desc.VendorId != 0 &&
                desc.DedicatedVideoMemory > 0) {
                if (SUCCEEDED(a1->QueryInterface(IID_PPV_ARGS(&m_collector->dxgiAdapter)))) {
                    a1->Release();
                    break;
                }
            }
            a1->Release();
        }
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &SysInfoWorker::tick);
    m_first = true;
    tick();          // 采集第一帧基准（PDH 与网络差值都需要两帧才有值，首帧只 collect 不 emit）
    m_timer->start();
}

void SysInfoWorker::stop() {
    if (m_timer) { m_timer->stop(); delete m_timer; m_timer = nullptr; }
    // 采集器随 worker 析构释放，这里 reset 提前释放（stop 后不再 tick）
    m_collector.reset();
}

void SysInfoWorker::refresh() {
    if (m_collector) tick();
}

void SysInfoWorker::tick() {
    // ── 内存占用率：GlobalMemoryStatusEx，瞬时值，无需差值 ──
    double memPct = -1.0;
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
        memPct = static_cast<double>(mem.dwMemoryLoad);   // 已是 0-100

    // ── CPU 占用率：PDH 差值型 ──
    double cpuPct = -1.0;
    if (m_collector && m_collector->cpuQuery && m_collector->cpuCounter) {
        // 每帧都 collect（首帧建立基准，后续帧 collect 后才能 format）
        if (PdhCollectQueryData(m_collector->cpuQuery) == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE fmt{};
            // PDH_FMT_DOUBLE + NOCAP100 遇到尚未有两帧时返回 PDH_CALC_NEGATIVE_DENOMINATOR，
            // 此时 format 失败 → 本帧 CPU 留 -1，下一帧就有差值了
            if (PdhGetFormattedCounterValue(m_collector->cpuCounter,
                                            PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                            nullptr, &fmt) == ERROR_SUCCESS) {
                cpuPct = fmt.doubleValue;
                if (cpuPct < 0.0) cpuPct = 0.0;
                if (cpuPct > 100.0) cpuPct = 100.0;
            }
        }
    }

    // ── GPU 占用率：优先 PDH 引擎利用率，回落 DXGI 显存 ──
    double gpuPct = -1.0;
    if (m_collector && m_collector->gpuPdhAvailable) {
        // 与 CPU 共用同一轮 collect 逻辑：gpuQuery 独立 collect
        if (PdhCollectQueryData(m_collector->gpuQuery) == ERROR_SUCCESS) {
            // 通配计数器用 PdhGetFormattedCounterArrayW 取全部实例；
            // 两次调用：先取所需缓冲区大小，再取数据
            DWORD bufSize = 0, itemCount = 0;
            PDH_STATUS st = PdhGetFormattedCounterArrayW(
                m_collector->gpuCounter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                &bufSize, &itemCount, nullptr);
            if (st == PDH_MORE_DATA && bufSize > 0) {
                std::vector<unsigned char> buf(bufSize);
                auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
                st = PdhGetFormattedCounterArrayW(
                    m_collector->gpuCounter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                    &bufSize, &itemCount, items);
                if (st == ERROR_SUCCESS) {
                    double sum = 0.0;
                    bool any = false;
                    for (DWORD i = 0; i < itemCount; ++i) {
                        const std::wstring name = items[i].szName;
                        // 仅留物理 3D 引擎节点；含 "engtype_3D" 且不含 "pid_"
                        // （排除进程级实例——任务管理器底部 GPU% 是所有进程的物理引擎总和；
                        // 同时过滤 VideoDecode/Encode/Copy 等非 3D 引擎避免双计）
                        if (name.find(L"engtype_3D") == std::wstring::npos ||
                            name.find(L"pid_") != std::wstring::npos)
                            continue;
                        if (items[i].FmtValue.CStatus == ERROR_SUCCESS) {
                            sum += items[i].FmtValue.doubleValue;
                            any = true;
                        }
                    }
                    if (any) {
                        // 各引擎节点的 utilization 是 0-100（任务管理器语义），
                        // 取所有 3D 引擎之和并钳到 100（多 GPU/多引擎叠加极少超 100，钳制保险）
                        gpuPct = sum;
                        if (gpuPct < 0.0) gpuPct = 0.0;
                        if (gpuPct > 100.0) gpuPct = 100.0;
                    }
                }
            }
        }
    }
    // PDH 不可用或本帧失败 → 尝试 DXGI 显存占用（粗略近似 GPU 忙碌度，不精确但聊胜于无）
    if (gpuPct < 0.0 && m_collector && m_collector->dxgiAdapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(m_collector->dxgiAdapter->QueryVideoMemoryInfo(
                0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) && info.Budget > 0) {
            gpuPct = static_cast<double>(info.CurrentUsage) * 100.0
                     / static_cast<double>(info.Budget);
            if (gpuPct > 100.0) gpuPct = 100.0;
        }
    }

    // ── 网络上下行速率：GetIfTable2 累计字节差值（迁自 netstat，四要点原样保留）──
    quint64 netDownBps = 0, netUpBps = 0;
    quint64 in = 0, out = 0;
    {
        PMIB_IF_TABLE2 table = nullptr;
        if (GetIfTable2(&table) == NO_ERROR && table) {
            for (ULONG i = 0; i < table->NumEntries; ++i) {
                // 跳过回环口与离线口，只统计在线物理/虚拟网卡
                if (table->Table[i].Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                if (table->Table[i].OperStatus != IfOperStatusUp) continue;
                in  += table->Table[i].InOctets;   // 下行（接收）
                out += table->Table[i].OutOctets;   // 上行（发送）
            }
            FreeMibTable(table);
        }
    }

    // 首帧：PDH 与网络都需要两帧才有差值，只记基准不 emit
    if (m_first) {
        m_lastIn  = in;
        m_lastOut = out;
        m_first   = false;
        m_elapsed.start();
        return;
    }

    const qint64 elapsedMs = m_elapsed.restart();
    // 网络速率归一：按真实间隔归为每秒字节数（intervalMs 可配，且定时器有抖动）
    // 回绕保护：网卡下线/VPN 断开时其计数从总和消失，in/out 倒退；
    // quint64 直接相减会回绕成天文数字，此时重置基准、跳过本帧（速率留 0）
    if (in >= m_lastIn && out >= m_lastOut && elapsedMs > 0) {
        const quint64 dIn  = in  - m_lastIn;
        const quint64 dOut = out - m_lastOut;
        netDownBps = dIn  * 1000ULL / quint64(elapsedMs);
        netUpBps   = dOut * 1000ULL / quint64(elapsedMs);
    }
    m_lastIn  = in;
    m_lastOut = out;

    emit metricsChanged(cpuPct, memPct, gpuPct, netDownBps, netUpBps);
}
