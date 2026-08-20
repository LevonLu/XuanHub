// ProcessReaper.cpp — 常驻 barplugin.dll 中的子进程异步回收器
#include "barplugin/ProcessReaper.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSet>
#include <QTimer>

namespace xuanhub {
namespace {
class ProcessReaper final : public QObject {
public:
    explicit ProcessReaper(QObject* parent) : QObject(parent) {}

    void adopt(QProcess* process, int graceMs) {
        if (!process || m_processes.contains(process)) return;
        // 断掉原插件的所有信号连接；此对象位于 barplugin.dll，后续回调不会跳回已卸载 DLL。
        process->disconnect();
        process->setParent(this);
        m_processes.insert(process);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int, QProcess::ExitStatus) {
            m_processes.remove(process);
            process->deleteLater();
        });
        const auto terminate = [process] {
            if (process->state() != QProcess::NotRunning) process->terminate();
            QTimer::singleShot(1000, process, [process] {
                if (process->state() != QProcess::NotRunning) process->kill();
            });
        };
        if (graceMs > 0) QTimer::singleShot(graceMs, this, terminate);
        else terminate();
    }

private:
    QSet<QProcess*> m_processes;
};

ProcessReaper* instance() {
    static ProcessReaper* reaper = nullptr;
    if (!reaper && QCoreApplication::instance())
        reaper = new ProcessReaper(QCoreApplication::instance());
    return reaper;
}
} // namespace

void reapProcess(QProcess* process, int graceMs) {
    if (auto* reaper = instance()) reaper->adopt(process, qMax(0, graceMs));
    else if (process) process->kill();
}

} // namespace xuanhub
