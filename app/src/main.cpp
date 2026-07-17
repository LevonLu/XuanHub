// main.cpp — 玄枢 / XuanHub 入口
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QLockFile>
#include <QAbstractNativeEventFilter>
#include <QAbstractEventDispatcher>
#include <QPointer>

#include <windows.h>

#include "BarWindow.h"
#include "ConfigHotReloader.h"
#include "ConfigService.h"
#include "PluginManager.h"
#include "PluginListModel.h"
#include "SettingsController.h"
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

    // 配置目录：%APPDATA%/XuanHub（plan §2 决策）；不可用时退回 exeDir
    QString configDir;
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        configDir = QDir(appData).absoluteFilePath(QStringLiteral("XuanHub"));
    if (configDir.isEmpty() || !QDir().mkpath(configDir))
        configDir = exeDir;

    // 单实例守卫：必须在日志初始化之前——
    // 失败实例不能以 Truncate 方式截断正在运行实例的日志
    QLockFile instLock(QDir::temp().absoluteFilePath(QStringLiteral("XuanHub.lock")));
    if (!instLock.tryLock(100)) {
        OutputDebugStringW(L"[XuanHub] another instance is already running, exit\n");
        return 0;
    }

    // 日志写到可写的配置目录（exeDir 在 Program Files 安装场景下不可写）
    g_logFile.setFileName(QDir(configDir).absoluteFilePath(QStringLiteral("xuanhub.log")));
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    qInstallMessageHandler(logToFile);

    qDebug() << "=== XuanHub start ===" << "configDir =" << configDir;

    // 栈顺序保证析构安全
    xuanhub::EventBus bus;
    ConfigService config;
    config.load(exeDir, configDir);

    ThemeManager theme(&bus, config.theme());

    PluginManager pm(&bus, configDir);
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

    // 配置热重载（M7）：文件监听自动触发 + 托盘 "Reload Config" 手动触发
    ConfigHotReloader reloader(&config, &theme, &pm, &model, &window, exeDir, configDir);
    QObject::connect(&tray, &TrayIcon::reloadRequested,
                     &reloader, &ConfigHotReloader::reload);

    // 设置窗口（M8）：托盘 "Settings..." 打开（懒建，关闭=隐藏不销毁）
    SettingsController settingsCtl(&config, &theme, exeDir);
    QObject::connect(&tray, &TrayIcon::settingsRequested,
                     &settingsCtl, &SettingsController::open);

    auto* monitor = new TaskbarMonitor(&tray);
    QAbstractEventDispatcher::instance()->installNativeEventFilter(monitor);

    const int code = app.exec();

    qDebug() << "=== XuanHub exit code=" << code;
    return code;
}
