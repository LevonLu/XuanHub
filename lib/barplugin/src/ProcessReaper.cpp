// ProcessReaper.cpp — 常驻 barplugin.dll 中的子进程异步回收器
#include "barplugin/ProcessReaper.h"

#include <QCoreApplication>
#include <QPointer>
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
        // worker 可能在宽限期内自行退出，finished 会先 deleteLater。宽限定时器若
        // 捕获裸指针，随后会对已释放 QProcess 调 state()，多 worker 重载时稳定触发
        // Qt6Core 访问冲突。QPointer 让两级定时器都能安全识别对象已销毁。
        const QPointer<QProcess> guarded(process);
        const auto terminate = [guarded] {
            if (!guarded) return;
            if (guarded->state() != QProcess::NotRunning) guarded->terminate();
            QTimer::singleShot(1000, guarded, [guarded] {
                if (guarded && guarded->state() != QProcess::NotRunning) guarded->kill();
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
