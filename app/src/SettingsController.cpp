// SettingsController.cpp
#include "SettingsController.h"
#include "SettingsViewModel.h"
#include "ThemeManager.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeySequence>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTest>

SettingsController::SettingsController(ConfigService* config, ThemeManager* theme,
                                       ThemeRegistry* registry,
                                       const QString& exeDir, QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_registry(registry),
      m_exeDir(exeDir),
      m_uitest(QCoreApplication::arguments().contains(QStringLiteral("--uitest"))) {}

SettingsController::~SettingsController() {
    delete m_window.data();   // QWindow 无 QObject 父对象，手动释放
}

void SettingsController::open() {
    if (!m_window) {
        // Basic 样式：自绘色板随主题（进程内仅设置窗口用 Controls，不影响 bar 的 QML）
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        m_vm = new SettingsViewModel(m_config, m_registry, m_theme, m_exeDir, this);
        auto* w = new QQuickView();
        w->setTitle(QStringLiteral("玄枢设置 XuanHub Settings"));
        // 无边框：去掉系统标题栏，改由 QML 自绘标题栏（配色跟随主题）。
        // 系统 Windows 标题栏不受 Qt 主题控制，无法改色，故用 Frameless + 自绘方案。
        w->setFlag(Qt::FramelessWindowHint);
        // 透明窗口背景：让 QML 圆角外的区域能透出桌面（隐式启用 WA_TranslucentBackground）。
        // 代价：失去系统阴影，改由 QML DropShadow 补偿。
        w->setColor(QColor(Qt::transparent));
        w->setResizeMode(QQuickView::SizeRootObjectToView);
        // 左侧导航 + 卡片流布局（插件页为 master-detail 两栏，M10）；
        // 加宽到 960：sysinfo 自绘设置页每行含 metric 下拉+前缀+颜色+预览+↑↓✕ 多个控件，
        // 780 下右侧内容区被挤窄导致上移下移按钮被截断。
        w->resize(960, 680);
        w->rootContext()->setContextProperty(QStringLiteral("settingsVM"), m_vm);
        w->rootContext()->setContextProperty(QStringLiteral("themeManager"), m_theme);
        w->rootContext()->setContextProperty(QStringLiteral("settingsCtl"), this);
        // --uitest：从 <exeDir>/uitest.txt 读验收脚本（每行一条：click:x,y / kp:文本，
        // 空行与 # 开头忽略），注入 context property 由 QML Timer 逐条回放
        if (m_uitest) {
            QStringList cmds;
            QFile f(QDir(m_exeDir).absoluteFilePath(QStringLiteral("uitest.txt")));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!f.atEnd()) {
                    const QString line = QString::fromUtf8(f.readLine()).trimmed();
                    if (!line.isEmpty() && !line.startsWith(QStringLiteral("#")))
                        cmds.append(line);
                }
            }
            w->rootContext()->setContextProperty(QStringLiteral("uiTestCommands"), cmds);
        }
        w->setSource(QUrl(QStringLiteral("qrc:/qt/qml/XuanHub/qml/SettingsWindow.qml")));
        if (w->status() != QQuickView::Ready) {
            for (const auto& e : w->errors())
                qWarning() << "[Settings] QML error:" << e.toString();
        }
        // 标题栏关闭按钮 = 隐藏（QWindow::close 默认行为即 hide），窗口不销毁
        m_window = w;
    }
    m_vm->reloadFromConfig();
    m_vm->reloadThemePacks();   // 打开时刷新主题包下拉（用户可能新装/卸载了包）
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

namespace {
// 深度优先找首个「可见且文本匹配」的 item；匹配属性依次为
// text / placeholderText / displayText / currentText（覆盖 Text、TextField、
// Button、CheckBox、TabButton、ComboBox、弹层 ItemDelegate 等）。
// exact=true 全等匹配，false 包含匹配。命中信息写入 hitInfo（类名.属性名）。
QQuickItem* findByText(QQuickItem* root, const QString& needle, bool exact, QString* hitInfo) {
    if (!root) return nullptr;
    if (root->isVisible()) {
        static const char* kProps[] = {"text", "placeholderText", "displayText", "currentText"};
        for (const char* p : kProps) {
            const QVariant v = root->property(p);
            if (!v.isValid()) continue;
            const QString s = v.toString();
            if (s.isEmpty()) continue;
            if ((exact && s == needle) || (!exact && s.contains(needle))) {
                if (hitInfo)
                    *hitInfo = QString::fromUtf8(root->metaObject()->className())
                               + QStringLiteral(".") + p;
                return root;
            }
        }
    }
    const auto children = root->childItems();
    for (QQuickItem* ch : children) {
        if (QQuickItem* r = findByText(ch, needle, exact, hitInfo)) return r;
    }
    return nullptr;
}
} // namespace

void SettingsController::devUiEvent(const QString& spec) {
    if (!m_uitest || !m_window) return;   // 非 --uitest 模式一律拒绝
    const int colon = spec.indexOf(QStringLiteral(":"));
    if (colon < 0) return;
    const QString op = spec.left(colon);
    const QString arg = spec.mid(colon + 1);
    if (op == QStringLiteral("click")) {
        const auto pts = arg.split(QStringLiteral(","));
        if (pts.size() != 2) return;
        const QPoint local(pts[0].toInt(), pts[1].toInt());
        QCursor::setPos(m_window->mapToGlobal(local));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, local, 10);
        qDebug() << "[uitest] click" << local;
    } else if (op == QStringLiteral("fc")) {
        // find-and-click：按可见文本定位（先全等后包含），点其中心——不猜坐标
        QString info;
        QQuickItem* hit = findByText(m_window->contentItem(), arg, true, &info);
        if (!hit) hit = findByText(m_window->contentItem(), arg, false, &info);
        if (!hit) {
            qDebug() << "[uitest] fc miss:" << arg;
            return;
        }
        const QPointF c = hit->mapToItem(nullptr, QPointF(hit->width() / 2, hit->height() / 2));
        QCursor::setPos(m_window->mapToGlobal(c.toPoint()));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, c.toPoint(), 10);
        qDebug() << "[uitest] fc" << arg << "->" << info << c;
    } else if (op == QStringLiteral("kp")) {
        const int key = (arg == QStringLiteral("enter"))
                            ? int(Qt::Key_Return)
                            : QKeySequence(arg).isEmpty() ? 0 : QKeySequence(arg)[0].key();
        if (key == 0) return;
        QTest::keyClick(m_window, static_cast<Qt::Key>(key), Qt::NoModifier, 10);
        qDebug() << "[uitest] key" << arg;
    }
}
