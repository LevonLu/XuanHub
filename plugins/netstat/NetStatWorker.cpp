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
        return;
    }
    quint64 dIn  = in  - m_lastIn;
    quint64 dOut = out - m_lastOut;
    m_lastIn  = in;
    m_lastOut = out;
    emit ratesChanged(dOut, dIn);   // 注意顺序：up(dOut), down(dIn)
}
