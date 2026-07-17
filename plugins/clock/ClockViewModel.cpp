// ClockViewModel.cpp
#include "ClockViewModel.h"
#include <QTimer>
#include <QDateTime>

ClockViewModel::ClockViewModel(QObject* parent)
    : QObject(parent), m_timer(new QTimer(this)) {
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ClockViewModel::refresh);
}

ClockViewModel::~ClockViewModel() = default;

void ClockViewModel::setFormat(const QString& f) {
    m_format = f;
    refresh();
}

void ClockViewModel::start() {
    refresh();
    m_timer->start();
}

void ClockViewModel::stop() { m_timer->stop(); }

void ClockViewModel::refresh() {
    const QString t = QDateTime::currentDateTime().toString(m_format);
    if (t != m_timeText) {
        m_timeText = t;
        emit timeTextChanged();
    }
}
