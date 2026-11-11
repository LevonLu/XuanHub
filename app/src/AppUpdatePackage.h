// AppUpdatePackage.h — 在后台验证 ZIP 完整文件集合并解压到新的暂存目录
#pragma once
#include <QString>
#include <atomic>

class AppUpdatePackage final {
public:
    static bool extract(const QString& archive, const QString& stage,
                        const QString& version, const std::atomic_bool& cancelled,
                        QString* error);
};
