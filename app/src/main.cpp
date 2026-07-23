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
#include <QQuickStyle>

#include <windows.h>

#include "BarWindow.h"
#include "ConfigHotReloader.h"
#include "ConfigService.h"
#include "PluginManager.h"
#include "PluginListModel.h"
#include "SettingsController.h"
#include "ThemeManager.h"
#include "ThemeRegistry.h"
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
    explicit TaskbarMonitor(QPointer<TrayIcon> tray, QPointer<BarWindow> window)
        : m_tray(tray), m_window(window) {}
    bool nativeEventFilter(const QByteArray&, void* msg, qintptr*) override {
        static const UINT s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        auto* m = static_cast<MSG*>(msg);
        if (m->message == s_taskbarCreated) {
            // explorer 重启：托盘图标丢失需重建；taskbar 模式下我们的子窗口
            // 随任务栏销毁，需重建平台窗口重新嵌入（非 taskbar 模式内部忽略）
            if (m_tray) m_tray->recreate();
            if (m_window) m_window->recreateForTaskbar();
            return true;
        }
        return false;
    }
private:
    QPointer<TrayIcon> m_tray;
    QPointer<BarWindow> m_window;
};

// ── main ──────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // QSystemTrayIcon 需要 QApplication（QGuiApplication 无 Widgets 支持）
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("XuanHub"));
    app.setOrganizationName(QStringLiteral("XuanHub"));
    // BarWindow 是 Qt::Tool，lastWindowClosed 机制会把它排除在"可见顶层窗口"之外，
    // 于是设置窗口（唯一普通顶层窗口）被 close 隐藏后 Qt 误判"已无窗口"→ 默认退出整个进程。
    // 玄枢的生命周期由托盘 "退出" 显式控制，关闭这条隐式退出路径。
    app.setQuitOnLastWindowClosed(false);

    // 统一 QtQuick.Controls 样式为 Basic（纯 QML 实现，最稳，与 SettingsWindow 一致）。
    // 进程级全局设置：Bar 引擎内的插件 Window 也会走 Basic，避免走系统默认样式。
    // 必须在任何 Controls 控件实例化之前调用——此处紧随 QApplication 构造，最早。
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QString exeDir = QCoreApplication::applicationDirPath();

    // 便携标记：exeDir 下存在 portable 文件时，强制所有运行时数据（配置、日志、锁）
    // 留在 exeDir，不触碰 %APPDATA% 与 %TEMP%
    const bool isPortable = QFile::exists(QDir(exeDir).absoluteFilePath(QStringLiteral("portable")));

    // 配置目录：便携模式用 exeDir；常规模式用 %APPDATA%/XuanHub，不可用时退回 exeDir
    QString configDir;
    if (isPortable) {
        configDir = exeDir;
    } else {
        const QString appData = qEnvironmentVariable("APPDATA");
        if (!appData.isEmpty())
            configDir = QDir(appData).absoluteFilePath(QStringLiteral("XuanHub"));
        if (configDir.isEmpty() || !QDir().mkpath(configDir))
            configDir = exeDir;
    }

    // 单实例守卫：必须在日志初始化之前——
    // 失败实例不能以 Truncate 方式截断正在运行实例的日志
    QLockFile instLock(QDir(configDir).absoluteFilePath(QStringLiteral("XuanHub.lock")));
    if (!instLock.tryLock(100)) {
        OutputDebugStringW(L"[XuanHub] another instance is already running, exit\n");
        return 0;
    }

    // 日志写到可写的配置目录（exeDir 在 Program Files 安装场景下不可写）
    g_logFile.setFileName(QDir(configDir).absoluteFilePath(QStringLiteral("xuanhub.log")));
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    qInstallMessageHandler(logToFile);

    qDebug() << "=== XuanHub start ===" << "configDir =" << configDir;

    // 栈顺序保证析构安全（构造顺序 = 声明顺序，析构逆序）
    xuanhub::EventBus bus;
    ConfigService config;
    config.load(exeDir, configDir);

    // 主题包注册表：扫描 themes/*/theme.json，解析校验后缓存所有已安装主题包
    ThemeRegistry registry;
    registry.reload(QDir(exeDir).absoluteFilePath(QStringLiteral("themes")));

    ThemeManager theme(&bus, &registry, config.themeId(), config.theme());

    // 托盘（先于插件加载创建：插件加载期间可能经 EventBus "app.notify"
    // 请求气泡提示——如 pybridge 未检测到 Python 环境，订阅必须先于发布）
    TrayIcon tray(&theme, &bus, &config);
    tray.show();

    PluginManager pm(&bus, configDir);
    pm.loadFrom(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins")),
                config.pluginEntries());

    PluginListModel model;
    model.setPlugins(pm.loaded());

    BarWindow window(&config, &model, &theme);
    window.create();
    window.setupBar();
    window.show();

    // 配置热重载（M7）：文件监听自动触发 + 托盘 "重新加载配置" 手动触发
    ConfigHotReloader reloader(&config, &theme, &registry, &pm, &model, &window, exeDir, configDir);
    QObject::connect(&tray, &TrayIcon::reloadRequested,
                     &reloader, &ConfigHotReloader::reload);

    // 设置窗口（M8）：托盘 "设置..." 打开（懒建，关闭=隐藏不销毁）
    SettingsController settingsCtl(&config, &theme, &registry, exeDir);
    QObject::connect(&tray, &TrayIcon::settingsRequested,
                     &settingsCtl, &SettingsController::open);

    // 调试入口：--settings 启动即打开设置窗口（开发/验收用，免去托盘点击）
    if (QCoreApplication::arguments().contains(QStringLiteral("--settings")))
        settingsCtl.open();

    auto* monitor = new TaskbarMonitor(&tray, &window);
    QAbstractEventDispatcher::instance()->installNativeEventFilter(monitor);

    const int code = app.exec();

    qDebug() << "=== XuanHub exit code=" << code;
    return code;
}
