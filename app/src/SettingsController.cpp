// SettingsController.cpp
#include "SettingsController.h"
#include "SettingsViewModel.h"
#include "ThemeManager.h"
#include "LocaleManager.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QKeySequence>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QTest>

SettingsController::SettingsController(ConfigService* config, ThemeManager* theme,
                                       ThemeRegistry* registry,
                                       LocaleManager* locale, const QString& exeDir,
                                       PluginListModel* barPlugins,
                                       PluginManager* pluginManager,
                                       QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_registry(registry),
      m_locale(locale), m_exeDir(exeDir), m_barPlugins(barPlugins),
      m_pluginManager(pluginManager),
      m_uitest(QCoreApplication::arguments().contains(QStringLiteral("--uitest"))) {}

SettingsController::~SettingsController() {
    delete m_window.data();   // QWindow 无 QObject 父对象，手动释放
}

void SettingsController::open() {
    if (!m_window) {
        // Basic 样式已在 main() 创建任何 QML 引擎前设置；此处重复调用不仅无效，
        // 还会在每次首次打开设置窗口时产生“必须在加载 QML 前调用”的误导性警告。

        m_vm = new SettingsViewModel(m_config, m_registry, m_theme, m_locale,
                                     m_exeDir, m_barPlugins, m_pluginManager, this);
        auto* w = new QQuickView();
        w->setTitle(QCoreApplication::translate(
            "xuanhub.settings", "玄枢设置 XuanHub Settings"));
        // 设置窗口是独立创建的 QQuickView。Windows 对无边框窗口不会始终可靠地
        // 继承 QApplication 的默认图标，因此在创建原生 HWND 前显式设置一次。
        w->setIcon(QGuiApplication::windowIcon());
        // 无边框：去掉系统标题栏，改由 QML 自绘标题栏（配色跟随主题）。
        // 系统 Windows 标题栏不受 Qt 主题控制，无法改色，故用 Frameless + 自绘方案。
        w->setFlag(Qt::FramelessWindowHint);
        // 透明窗口背景：让 QML 圆角外的区域能透出桌面（隐式启用 WA_TranslucentBackground）。
        // 代价：失去系统阴影，改由 QML DropShadow 补偿。
        w->setColor(QColor(Qt::transparent));
        w->setResizeMode(QQuickView::SizeRootObjectToView);
        // Fluent 2 设置页的插件主从布局需要留出搜索列表、详情分组与指标编辑行。
        // 1080px 逻辑宽度在 100%/150% DPI 都能避免 SysInfo 的操作按钮被截断；
        // 表单在更窄空间会自动退为单列，不依赖固定像素排版。
        w->resize(1080, 720);
        w->rootContext()->setContextProperty(QStringLiteral("settingsVM"), m_vm);
        w->rootContext()->setContextProperty(QStringLiteral("themeManager"), m_theme);
        w->rootContext()->setContextProperty(QStringLiteral("settingsCtl"), this);
        if (m_locale) {
            w->rootContext()->setContextProperty(QStringLiteral("i18n"), m_locale);
            m_locale->registerEngine(w->engine());
            connect(m_locale, &LocaleManager::localeChanged, w, [w] {
                w->setTitle(QCoreApplication::translate(
                    "xuanhub.settings", "玄枢设置 XuanHub Settings"));
            });
        }
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

void SettingsController::openForPlugin(QVariantMap payload) {
    const QString pluginId = payload.value(QStringLiteral("pluginId")).toString();
    open();
    if (pluginId.isEmpty() || !m_window || !m_window->rootObject()) return;

    // QML 只接收宿主服务附加的稳定 id；插件不传行号，也不能借排序变化把
    // 请求导向另一个插件。窗口和模型均已由 open() 同步初始化，可直接选中。
    if (!QMetaObject::invokeMethod(m_window->rootObject(), "selectPlugin",
                                   Qt::DirectConnection,
                                   Q_ARG(QVariant, QVariant(pluginId)))) {
        qWarning() << "[Settings] selectPlugin entry missing for" << pluginId;
    }
}

QString SettingsController::choosePluginPackage() {
    return QFileDialog::getOpenFileName(
        nullptr,
        QCoreApplication::translate("xuanhub.settings", "选择玄枢插件包"),
        {},
        QCoreApplication::translate(
            "xuanhub.settings", "玄枢插件包 (*.xuanplugin)"));
}

QString SettingsController::choosePluginDiagnostics(const QString& pluginId) {
    const QString safeId = pluginId.isEmpty() ? QStringLiteral("plugin") : pluginId;
    return QFileDialog::getSaveFileName(
        nullptr,
        QCoreApplication::translate("xuanhub.settings", "导出插件诊断"),
        QStringLiteral("%1-diagnostics.json").arg(safeId),
        QCoreApplication::translate(
            "xuanhub.settings", "玄枢诊断包 (*.json)"));
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
