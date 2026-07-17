// NetStatWorker.cpp
#include "NetStatWorker.h"

// Winsock types before iphlpapi / netioapi
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#pragma comment(lib, "iphlpapi.lib")

NetStatWorker::NetStatWorker(QObject* parent) : QObject(parent) {}

void NetStatWorker::start(int intervalMs) {
    if (m_timer) stop();       // 允许重复 start：先清理旧定时器
    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &NetStatWorker::tick);
    m_first = true;
    tick();          // 采集第一帧基准
    m_timer->start();
}

void NetStatWorker::stop() {
    if (m_timer) { m_timer->stop(); delete m_timer; m_timer = nullptr; }
}

void NetStatWorker::tick() {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return;

    quint64 in = 0, out = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        if (table->Table[i].Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (table->Table[i].OperStatus != IfOperStatusUp) continue;
        in  += table->Table[i].InOctets;
        out += table->Table[i].OutOctets;
    }
    FreeMibTable(table);

    if (m_first) {
        m_lastIn  = in;
        m_lastOut = out;
        m_first   = false;
        m_elapsed.start();
        return;
    }

    const qint64 elapsedMs = m_elapsed.restart();
    // 网卡下线/VPN 断开时其计数从总和中消失，in/out 倒退；
    // quint64 直接相减会回绕成天文数字，此时重置基准、跳过本帧
    if (in < m_lastIn || out < m_lastOut || elapsedMs <= 0) {
        m_lastIn  = in;
        m_lastOut = out;
        return;
    }

    const quint64 dIn  = in  - m_lastIn;
    const quint64 dOut = out - m_lastOut;
    m_lastIn  = in;
    m_lastOut = out;

    // 按真实间隔归一为每秒字节数（intervalMs 可配，且定时器有抖动）
    const quint64 upBps   = dOut * 1000ULL / quint64(elapsedMs);
    const quint64 downBps = dIn  * 1000ULL / quint64(elapsedMs);
    emit ratesChanged(upBps, downBps);
}
