// ProcessReaper.cpp — 常驻 barplugin.dll 中的子进程异步回收器
#include "barplugin/ProcessReaper.h"

#include <QCoreApplication>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVector>

#include <algorithm>

namespace xuanhub {
namespace {
class ProcessReaper final : public QObject {
public:
    explicit ProcessReaper(QObject* parent) : QObject(parent) {}

    ~ProcessReaper() override { drain(1000); }

    void drain(int waitMs) {
        // QCoreApplication 析构时事件循环已经不再可靠，不能把仍在宽限期内的
        // QProcess 留给 QObject 的隐式子对象清理：finished 可能在成员析构后
        // 回调本对象。这里同步收口，保证 DLL/进程退出都没有悬空回调。
        const QVector<QPointer<QProcess>> processes = m_processes;
        m_processes.clear();
        for (const QPointer<QProcess>& guarded : processes) {
            QProcess* process = guarded.data();
            if (!process) continue;
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished(qMax(0, waitMs));
            }
            process->setParent(nullptr);
            delete process;
        }
    }

    void adopt(QProcess* process, int graceMs) {
        if (!process || std::any_of(m_processes.cbegin(), m_processes.cend(),
                                    [process](const auto& guarded) {
            return guarded.data() == process;
        })) return;
        // 调用方须先断开其 DLL 内的接收者；这里只建立 barplugin.dll 自身的
        // 回收回调，后续事件不会再跳回可卸载模块。
        process->setParent(this);
        m_processes.append(QPointer<QProcess>(process));
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int, QProcess::ExitStatus) {
            m_processes.removeIf([process](const auto& guarded) {
                return !guarded || guarded.data() == process;
            });
            process->deleteLater();
        });
        connect(process, &QObject::destroyed, this, [this, process] {
            m_processes.removeIf([process](const auto& guarded) {
                return !guarded || guarded.data() == process;
            });
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
    QVector<QPointer<QProcess>> m_processes;
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

void drainReapedProcesses(int waitMs) {
    if (auto* reaper = instance()) reaper->drain(qMax(0, waitMs));
}

} // namespace xuanhub
