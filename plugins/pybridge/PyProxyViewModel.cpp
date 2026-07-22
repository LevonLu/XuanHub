// PyProxyViewModel.cpp
#include "PyProxyViewModel.h"

#include <QDebug>
#include <QRegularExpression>

#include <nanobind/stl/string.h>   // std::string 双向 cast

namespace {

// QVariant → nb::object（config / invoke 参数方向；未知类型退化为字符串）
nb::object variantToPy(const QVariant& v) {
    switch (v.typeId()) {
    case QMetaType::Bool:      return nb::bool_(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:  return nb::int_(v.toLongLong());
    case QMetaType::UInt:
    case QMetaType::ULongLong: return nb::int_(static_cast<qlonglong>(v.toULongLong()));
    case QMetaType::Double:    return nb::float_(v.toDouble());
    case QMetaType::QString:   return nb::str(v.toString().toStdString().c_str());
    case QMetaType::QStringList: {
        nb::list l;
        for (const auto& s : v.toStringList()) l.append(nb::str(s.toStdString().c_str()));
        return std::move(l);
    }
    case QMetaType::QVariantList: {
        nb::list l;
        for (const auto& e : v.toList()) l.append(variantToPy(e));
        return std::move(l);
    }
    case QMetaType::QVariantMap: {
        nb::dict d;
        const auto m = v.toMap();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            d[it.key().toStdString().c_str()] = variantToPy(it.value());
        return std::move(d);
    }
    default: return nb::str(v.toString().toStdString().c_str());
    }
}

// nb::handle → QVariant（update() 返回值方向）
// 注意 bool 必须先于 int 判断（Python 里 bool 是 int 的子类）
QVariant pyToVariant(nb::handle h) {
    if (h.is_none())                   return {};
    if (nb::isinstance<nb::bool_>(h))  return nb::cast<bool>(h);
    if (nb::isinstance<nb::int_>(h))   return QVariant::fromValue(nb::cast<qlonglong>(h));
    if (nb::isinstance<nb::float_>(h)) return nb::cast<double>(h);
    if (nb::isinstance<nb::str>(h))    return QString::fromStdString(nb::cast<std::string>(h));
    if (nb::isinstance<nb::dict>(h)) {
        QVariantMap m;
        for (auto item : nb::cast<nb::dict>(h))
            m.insert(QString::fromStdString(nb::cast<std::string>(nb::str(item.first))),
                     pyToVariant(item.second));
        return m;
    }
    if (nb::isinstance<nb::list>(h) || nb::isinstance<nb::tuple>(h)) {
        QVariantList l;
        for (auto item : nb::cast<nb::sequence>(h)) l.append(pyToVariant(item));
        return l;
    }
    return QString::fromStdString(nb::cast<std::string>(nb::str(h)));
}

} // namespace

PyProxyViewModel::PyProxyViewModel(QString id, QString dirPath, QVariantMap cfg, QObject* parent)
    : QObject(parent), m_id(std::move(id)), m_dirPath(std::move(dirPath)), m_cfg(std::move(cfg)) {}

PyProxyViewModel::~PyProxyViewModel() {
    m_timer.stop();
    if (m_module.is_valid()) {
        nb::gil_scoped_acquire gil;
        // 可选 stop() 约定：通知模块停掉后台线程（网络轮询等）。
        // 热重载/卸载旧模块前不调的话，旧线程会一直泄漏运行
        try {
            if (nb::hasattr(m_module, "stop"))
                m_module.attr("stop")();
        } catch (const nb::python_error& e) {
            qWarning() << "[PyBridge]" << m_id << "stop() error:" << e.what();
        }
        // 从 sys.modules 摘掉：热重载重新 import 才能拿到磁盘上的新代码；
        // 解释器状态异常时静默 —— 析构路径绝不抛异常
        try {
            nb::module_::import_("sys").attr("modules").attr("pop")(
                m_modName.toStdString().c_str(), nb::none());
        } catch (const nb::python_error&) {
        }
        m_module = nb::object();
    }
}

bool PyProxyViewModel::load() {
    const QString filePath = m_dirPath + QStringLiteral("/main.py");
    m_modName = QStringLiteral("xuanhub_") + m_id;
    m_modName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    bool hasUpdate = false;
    try {
        nb::gil_scoped_acquire gil;
        // 按文件路径导入（子插件目录不在 sys.path 上；模块名全局唯一防冲突）
        nb::object util = nb::module_::import_("importlib.util");
        nb::object spec = util.attr("spec_from_file_location")(m_modName.toStdString().c_str(),
                                                               filePath.toStdString().c_str());
        m_module = util.attr("module_from_spec")(spec);
        nb::module_::import_("sys").attr("modules")[m_modName.toStdString().c_str()] = m_module;
        // 子插件目录挂到 sys.path 末尾（必须在 exec_module 之前，使 main.py 可导入同目录子包）
        nb::module_::import_("sys").attr("path").attr("append")(m_dirPath.toStdString().c_str());
        spec.attr("loader").attr("exec_module")(m_module);
        if (nb::hasattr(m_module, "init"))
            m_module.attr("init")(variantToPy(m_cfg));
        hasUpdate = nb::hasattr(m_module, "update");
    } catch (const nb::python_error& e) {
        // nanobind 的 python_error::what() 自行获取 GIL，无需在此重新 acquire
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
    if (!m_module.is_valid()) return;
    try {
        nb::gil_scoped_acquire gil;
        const QVariantMap m = pyToVariant(m_module.attr("update")()).toMap();
        if (m != m_values) {
            m_values = m;
            Q_EMIT valuesChanged();
        }
    } catch (const nb::python_error& e) {
        // 吞掉异常：UI 保留旧值，traceback 进日志（plan M6.3 验收项）
        qWarning() << "[PyBridge]" << m_id << "update() error:" << e.what();
    }
}

QVariant PyProxyViewModel::call(const QString& name, const QVariantList& args) {
    QVariant ret;
    if (!m_module.is_valid()) return ret;
    bool invoked = false;
    try {
        nb::gil_scoped_acquire gil;
        if (nb::hasattr(m_module, "invoke")) {
            nb::list pyArgs;
            for (const auto& a : args) pyArgs.append(variantToPy(a));
            ret = pyToVariant(m_module.attr("invoke")(name.toStdString().c_str(), pyArgs));
            invoked = true;
        }
    } catch (const nb::python_error& e) {
        qWarning() << "[PyBridge]" << m_id << "invoke() error:" << e.what();
    }
    if (invoked) tick();  // 交互后立即刷新，不等下个 tick（M6.4）
    return ret;
}
