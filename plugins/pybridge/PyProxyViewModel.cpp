// PyProxyViewModel.cpp
#include "PyProxyViewModel.h"

#include <QDebug>
#include <QRegularExpression>

namespace {

// QVariant → py::object（config / invoke 参数方向；未知类型退化为字符串）
py::object variantToPy(const QVariant& v) {
    switch (v.typeId()) {
    case QMetaType::Bool:      return py::bool_(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:  return py::int_(v.toLongLong());
    case QMetaType::UInt:
    case QMetaType::ULongLong: return py::int_(static_cast<qlonglong>(v.toULongLong()));
    case QMetaType::Double:    return py::float_(v.toDouble());
    case QMetaType::QString:   return py::str(v.toString().toStdString());
    case QMetaType::QStringList: {
        py::list l;
        for (const auto& s : v.toStringList()) l.append(py::str(s.toStdString()));
        return std::move(l);
    }
    case QMetaType::QVariantList: {
        py::list l;
        for (const auto& e : v.toList()) l.append(variantToPy(e));
        return std::move(l);
    }
    case QMetaType::QVariantMap: {
        py::dict d;
        const auto m = v.toMap();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            d[it.key().toStdString().c_str()] = variantToPy(it.value());
        return std::move(d);
    }
    default: return py::str(v.toString().toStdString());
    }
}

// py::handle → QVariant（update() 返回值方向）
// 注意 bool 必须先于 int 判断（Python 里 bool 是 int 的子类）
QVariant pyToVariant(py::handle h) {
    if (h.is_none())                   return {};
    if (py::isinstance<py::bool_>(h))  return h.cast<bool>();
    if (py::isinstance<py::int_>(h))   return QVariant::fromValue(h.cast<qlonglong>());
    if (py::isinstance<py::float_>(h)) return h.cast<double>();
    if (py::isinstance<py::str>(h))    return QString::fromStdString(h.cast<std::string>());
    if (py::isinstance<py::dict>(h)) {
        QVariantMap m;
        for (auto item : py::cast<py::dict>(h))
            m.insert(QString::fromStdString(py::str(item.first).cast<std::string>()),
                     pyToVariant(item.second));
        return m;
    }
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) {
        QVariantList l;
        for (auto item : py::cast<py::sequence>(h)) l.append(pyToVariant(item));
        return l;
    }
    return QString::fromStdString(py::str(h).cast<std::string>());
}

} // namespace

PyProxyViewModel::PyProxyViewModel(QString id, QString dirPath, QVariantMap cfg, QObject* parent)
    : QObject(parent), m_id(std::move(id)), m_dirPath(std::move(dirPath)), m_cfg(std::move(cfg)) {}

PyProxyViewModel::~PyProxyViewModel() {
    m_timer.stop();
    if (m_module) {
        py::gil_scoped_acquire gil;
        // 从 sys.modules 摘掉：热重载重新 import 才能拿到磁盘上的新代码；
        // 解释器状态异常时静默 —— 析构路径绝不抛异常
        try {
            py::module_::import("sys").attr("modules").attr("pop")(m_modName.toStdString(), py::none());
        } catch (const py::error_already_set&) {
        }
        m_module = py::object();
    }
}

bool PyProxyViewModel::load() {
    const QString filePath = m_dirPath + QStringLiteral("/main.py");
    m_modName = QStringLiteral("xuanhub_") + m_id;
    m_modName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    bool hasUpdate = false;
    try {
        py::gil_scoped_acquire gil;
        // 按文件路径导入（子插件目录不在 sys.path 上；模块名全局唯一防冲突）
        py::object util = py::module_::import("importlib.util");
        py::object spec = util.attr("spec_from_file_location")(m_modName.toStdString(), filePath.toStdString());
        m_module = util.attr("module_from_spec")(spec);
        py::module_::import("sys").attr("modules")[m_modName.toStdString().c_str()] = m_module;
        spec.attr("loader").attr("exec_module")(m_module);
        // 子插件目录挂到 sys.path 末尾，方便 main.py 导入同目录辅助模块
        py::module_::import("sys").attr("path").attr("append")(m_dirPath.toStdString());
        if (py::hasattr(m_module, "init"))
            m_module.attr("init")(variantToPy(m_cfg));
        hasUpdate = py::hasattr(m_module, "update");
    } catch (const py::error_already_set& e) {
        py::gil_scoped_acquire gil;
        qWarning() << "[PyBridge]" << m_id << "load failed:" << e.what();
        return false;
    }
    if (!hasUpdate) {
        qWarning() << "[PyBridge]" << m_id << ": main.py 缺少 update()，跳过";
        return false;
    }
    tick();  // 立即拉一次，不等首个 tick 周期
    int interval = m_cfg.value(QStringLiteral("intervalMs")).toInt();
    if (interval <= 0) interval = 1000;
    connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
    m_timer.start(interval);
    return true;
}

void PyProxyViewModel::tick() {
    if (!m_module) return;
    try {
        py::gil_scoped_acquire gil;
        const QVariantMap m = pyToVariant(m_module.attr("update")()).toMap();
        if (m != m_values) {
            m_values = m;
            Q_EMIT valuesChanged();
        }
    } catch (const py::error_already_set& e) {
        py::gil_scoped_acquire gil;
        // 吞掉异常：UI 保留旧值，traceback 进日志（plan M6.3 验收项）
        qWarning() << "[PyBridge]" << m_id << "update() error:" << e.what();
    }
}

QVariant PyProxyViewModel::call(const QString& name, const QVariantList& args) {
    QVariant ret;
    if (!m_module) return ret;
    bool invoked = false;
    try {
        py::gil_scoped_acquire gil;
        if (py::hasattr(m_module, "invoke")) {
            py::list pyArgs;
            for (const auto& a : args) pyArgs.append(variantToPy(a));
            ret = pyToVariant(m_module.attr("invoke")(name.toStdString(), pyArgs));
            invoked = true;
        }
    } catch (const py::error_already_set& e) {
        py::gil_scoped_acquire gil;
        qWarning() << "[PyBridge]" << m_id << "invoke() error:" << e.what();
    }
    if (invoked) tick();  // 交互后立即刷新，不等下个 tick（M6.4）
    return ret;
}
