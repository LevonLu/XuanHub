// PyProxyViewModel.h — Python 子插件的通用代理 ViewModel（M6.3）
// 每个 Python 子插件一个实例：QTimer 持 GIL「拉」main.py 的 update() 填 values 属性袋；
// QML 交互经 call() 转发给 main.py 的 invoke(name, args)。
// 生命周期归 PyBridgePlugin（host shutdown 时统一销毁）。
// 注意：pybridge 目标定义了 QT_NO_KEYWORDS，本文件用 Q_SIGNALS/Q_EMIT 显式写法。
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <nanobind/nanobind.h>
namespace nb = nanobind;

class PyProxyViewModel : public QObject {
    Q_OBJECT
    // 属性袋：python update() 返回的 dict 原样放入，QML 用 plugin.values.xxx 绑定
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
public:
    PyProxyViewModel(QString id, QString dirPath, QVariantMap cfg, QObject* parent = nullptr);
    ~PyProxyViewModel() override;

    QVariantMap values() const { return m_values; }

    // 按文件路径导入 main.py → init(cfg) → 立即 tick 一次 → 启动 QTimer。
    // 脚本语法错误 / 缺少 update() 时返回 false（已记日志）。
    bool load();

    // QML 交互入口 → main.py 的 invoke(name, args)；成功后立即刷新一次 values
    Q_INVOKABLE QVariant call(const QString& name, const QVariantList& args);

Q_SIGNALS:
    void valuesChanged();

private:
    void tick();   // QTimer 触发：持 GIL 调 update()，dict 并入 values 后 emit

    QString m_id;
    QString m_dirPath;
    QString m_modName;      // sys.modules 键：xuanhub_<id>（全局唯一）
    QVariantMap m_cfg;
    QVariantMap m_values;
    QTimer m_timer;
    nb::object m_module;    // 已导入的 main.py 模块对象；一切访问必须持 GIL
};
