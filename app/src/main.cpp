// main.cpp — 玄枢 / XuanHub 入口
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QAbstractNativeEventFilter>
#include <QAbstractEventDispatcher>
#include <QPointer>

#include <windows.h>

#include "BarWindow.h"
#include "ConfigService.h"
#include "PluginManager.h"
#include "PluginListModel.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "barplugin/EventBus.h"

// ── 日志系统 ──────────────────────────────────────────────
static QFile g_logFile;

static void logToFile(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (g_logFile.isOpen()) {
        QTextStream s(&g_logFile);
        const QString now = QDateTime::currentDateTime()
                                .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        const char* level = "D";
        switch (type) {
            case QtDebugMsg:    level = "D"; break;
            case QtInfoMsg:     level = "I"; break;
            case QtWarningMsg:  level = "W"; break;
            case QtCriticalMsg: level = "C"; break;
            case QtFatalMsg:    level = "F"; break;
        }
        s << "[" << now << "] [" << level << "] " << msg << "\n";
        s.flush();
    }
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(msg.utf16()));
}

// ── TaskbarCreated 守卫 ──────────────────────────────────
class TaskbarMonitor : public QAbstractNativeEventFilter {
public:
    explicit TaskbarMonitor(QPointer<TrayIcon> tray) : m_tray(tray) {}
    bool nativeEventFilter(const QByteArray&, void* msg, qintptr*) override {
        static const UINT s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        auto* m = static_cast<MSG*>(msg);
        if (m->message == s_taskbarCreated && m_tray) {
            m_tray->recreate();
            return true;
        }
        return false;
    }
private:
    QPointer<TrayIcon> m_tray;
};

// ── main ──────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // QSystemTrayIcon 需要 QApplication（QGuiApplication 无 Widgets 支持）
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("XuanHub"));
    app.setOrganizationName(QStringLiteral("XuanHub"));

    const QString exeDir = QCoreApplication::applicationDirPath();
    g_logFile.setFileName(QDir(exeDir).absoluteFilePath(QStringLiteral("xuanhub.log")));
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    qInstallMessageHandler(logToFile);

    qDebug() << "=== XuanHub start ===";

    // 栈顺序保证析构安全
    xuanhub::EventBus bus;
    ConfigService config;
    config.load(exeDir);

    ThemeManager theme(&bus, config.theme());

    PluginManager pm(&bus, exeDir);
    pm.loadFrom(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins")),
                config.pluginEntries());

    PluginListModel model;
    model.setPlugins(pm.loaded());

    BarWindow window(&config, &model, &theme);
    window.create();
    window.setupBar();
    window.show();

    // 托盘
    TrayIcon tray(&theme);
    tray.show();

    auto* monitor = new TaskbarMonitor(&tray);
    QAbstractEventDispatcher::instance()->installNativeEventFilter(monitor);

    const int code = app.exec();

    qDebug() << "=== XuanHub exit code=" << code;
    return code;
}
